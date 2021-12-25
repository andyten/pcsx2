/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"
#include "Frontend/InputManager.h"
#include "Frontend/InputSource.h"
#include "PAD/Host/PAD.h"
#include "common/StringUtil.h"
#include <array>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

// ------------------------------------------------------------------------
// Constants
// ------------------------------------------------------------------------

enum : u32
{
	MAX_KEYS_PER_BINDING = 4,
	FIRST_EXTERNAL_INPUT_SOURCE = static_cast<u32>(InputSourceType::Mouse) + 1u,
	LAST_EXTERNAL_INPUT_SOURCE = static_cast<u32>(InputSourceType::Count),
};

// ------------------------------------------------------------------------
// Event Handler Type
// ------------------------------------------------------------------------
// This class acts as an adapter to convert from normalized values to
// binary values when the callback is a binary/button handler. That way
// you don't need to convert float->bool in your callbacks.

class InputEventHandler
{
public:
	InputEventHandler()
	{
		new (&u.button) InputButtonEventHandler;
		is_axis = false;
	}

	InputEventHandler(InputButtonEventHandler button)
	{
		new (&u.button) InputButtonEventHandler(std::move(button));
		is_axis = false;
	}

	InputEventHandler(InputAxisEventHandler axis)
	{
		new (&u.axis) InputAxisEventHandler(std::move(axis));
		is_axis = true;
	}

	InputEventHandler(const InputEventHandler& copy)
	{
		if (copy.is_axis)
			new (&u.axis) InputAxisEventHandler(copy.u.axis);
		else
			new (&u.button) InputButtonEventHandler(copy.u.button);
		is_axis = copy.is_axis;
	}

	InputEventHandler(InputEventHandler&& move)
	{
		if (move.is_axis)
			new (&u.axis) InputAxisEventHandler(std::move(move.u.axis));
		else
			new (&u.button) InputButtonEventHandler(std::move(move.u.button));
		is_axis = move.is_axis;
	}

	~InputEventHandler()
	{
		// call the right destructor... :D
		if (is_axis)
			u.axis.InputAxisEventHandler::~InputAxisEventHandler();
		else
			u.button.InputButtonEventHandler::~InputButtonEventHandler();
	}

	InputEventHandler& operator=(const InputEventHandler& copy)
	{
		InputEventHandler::~InputEventHandler();

		if (copy.is_axis)
			new (&u.axis) InputAxisEventHandler(copy.u.axis);
		else
			new (&u.button) InputButtonEventHandler(copy.u.button);
		is_axis = copy.is_axis;
		return *this;
	}

	InputEventHandler& operator=(InputEventHandler&& move)
	{
		InputEventHandler::~InputEventHandler();

		if (move.is_axis)
			new (&u.axis) InputAxisEventHandler(std::move(move.u.axis));
		else
			new (&u.button) InputButtonEventHandler(std::move(move.u.button));
		is_axis = move.is_axis;
		return *this;
	}

	__fi bool IsAxis() const { return is_axis; }

	__fi void Invoke(float value) const
	{
		if (is_axis)
			u.axis(value);
		else
			u.button(value > 0.0f);
	}

private:
	union HandlerUnion
	{
		// constructor/destructor needs to be declared
		HandlerUnion() {}
		~HandlerUnion() {}

		InputButtonEventHandler button;
		InputAxisEventHandler axis;
	} u;

	bool is_axis;
};

// ------------------------------------------------------------------------
// Binding Type
// ------------------------------------------------------------------------
// This class tracks both the keys which make it up (for chords), as well
// as the state of all buttons. For button callbacks, it's fired when
// all keys go active, and for axis callbacks, when all are active and
// the value changes.

struct InputBinding
{
	InputBindingKey keys[MAX_KEYS_PER_BINDING] = {};
	InputEventHandler handler;
	u8 num_keys = 0;
	u8 full_mask = 0;
	u8 current_mask = 0;
};

// ------------------------------------------------------------------------
// Forward Declarations (for static qualifier)
// ------------------------------------------------------------------------
namespace InputManager
{
	static std::optional<InputBindingKey> ParseHostKeyboardKey(
		const std::string_view& source, const std::string_view& sub_binding);
	static std::optional<InputBindingKey> ParseHostMouseKey(
		const std::string_view& source, const std::string_view& sub_binding);

	static std::vector<std::string_view> SplitChord(const std::string_view& binding);
	static bool SplitBinding(const std::string_view& binding, std::string_view* source, std::string_view* sub_binding);
	static void AddBindings(const std::vector<std::string>& bindings, const InputEventHandler& handler);

	static void AddHotkeyBindings(SettingsInterface& si);
	static void AddPadBindings(SettingsInterface& si, u32 pad, const char* default_type);

	static bool DoEventHook(InputBindingKey key, float value);
} // namespace InputManager

// ------------------------------------------------------------------------
// Local Variables
// ------------------------------------------------------------------------

// This is a multimap containing any binds related to the specified key.
using BindingMap = std::unordered_multimap<InputBindingKey, std::shared_ptr<InputBinding>, InputBindingKeyHash>;
static BindingMap s_binding_map;
static std::mutex s_binding_map_write_lock;

// Hooks/intercepting (for setting bindings)
static std::mutex m_event_intercept_mutex;
static InputInterceptHook::Callback m_event_intercept_callback;

// Input sources. Keyboard/mouse don't exist here.
static std::array<std::unique_ptr<InputSource>, static_cast<u32>(InputSourceType::Count)> s_input_sources;

// ------------------------------------------------------------------------
// Hotkeys
// ------------------------------------------------------------------------
static const HotkeyInfo* const s_hotkey_list[] = {g_vm_manager_hotkeys, g_host_hotkeys};

// ------------------------------------------------------------------------
// Binding Parsing
// ------------------------------------------------------------------------

std::vector<std::string_view> InputManager::SplitChord(const std::string_view& binding)
{
	std::vector<std::string_view> parts;

	// under an if for RVO
	if (!binding.empty())
	{
		std::string_view::size_type last = 0;
		std::string_view::size_type next;
		while ((next = binding.find('&', last)) != std::string_view::npos)
		{
			if (last != next)
			{
				std::string_view part(StringUtil::StripWhitespace(binding.substr(last, next - last)));
				if (!part.empty())
					parts.push_back(std::move(part));
			}
			last = next + 1;
		}
		if (last < (binding.size() - 1))
		{
			std::string_view part(StringUtil::StripWhitespace(binding.substr(last)));
			if (!part.empty())
				parts.push_back(std::move(part));
		}
	}

	return parts;
}

bool InputManager::SplitBinding(
	const std::string_view& binding, std::string_view* source, std::string_view* sub_binding)
{
	const std::string_view::size_type slash_pos = binding.find('/');
	if (slash_pos == std::string_view::npos)
	{
		Console.Warning("Malformed binding: '%*s'", static_cast<int>(binding.size()), binding.data());
		return false;
	}

	*source = std::string_view(binding).substr(0, slash_pos);
	*sub_binding = std::string_view(binding).substr(slash_pos + 1);
	return true;
}

std::optional<InputBindingKey> InputManager::ParseInputBindingKey(const std::string_view& binding)
{
	std::string_view source, sub_binding;
	if (!SplitBinding(binding, &source, &sub_binding))
		return std::nullopt;

	// lameee, string matching
	if (StringUtil::StartsWith(source, "Keyboard"))
	{
		return ParseHostKeyboardKey(source, sub_binding);
	}
	else if (StringUtil::StartsWith(source, "Mouse"))
	{
		return ParseHostMouseKey(source, sub_binding);
	}
	else
	{
		for (u32 i = FIRST_EXTERNAL_INPUT_SOURCE; i < LAST_EXTERNAL_INPUT_SOURCE; i++)
		{
			if (s_input_sources[i])
			{
				std::optional<InputBindingKey> key = s_input_sources[i]->ParseKeyString(source, sub_binding);
				if (key.has_value())
					return key;
			}
		}
	}

	return std::nullopt;
}

std::string InputManager::ConvertInputBindingKeyToString(InputBindingKey key)
{
	if (key.source_type == InputSourceType::Keyboard)
	{
		const std::optional<std::string> str(ConvertHostKeyboardCodeToString(key.data));
		if (str.has_value() && !str->empty())
			return StringUtil::StdStringFromFormat("Keyboard/%s", str->c_str());
	}
	else if (key.source_type == InputSourceType::Mouse)
	{
		if (key.source_subtype == InputSubclass::MouseButton)
			return StringUtil::StdStringFromFormat("Mouse%u/Button%u", key.source_index, key.data);
		else if (key.source_subtype == InputSubclass::MousePointer)
			return StringUtil::StdStringFromFormat("Mouse%u/Pointer%u", key.source_index, key.data);
		else if (key.source_subtype == InputSubclass::MouseWheel)
			return StringUtil::StdStringFromFormat(
				"Mouse%u/Wheel%u%c", key.source_index, key.data, key.negative ? '-' : '+');
	}
	else if (key.source_type < InputSourceType::Count && s_input_sources[static_cast<u32>(key.source_type)])
	{
		return s_input_sources[static_cast<u32>(key.source_type)]->ConvertKeyToString(key);
	}

	return {};
}

std::string InputManager::ConvertInputBindingKeysToString(const InputBindingKey* keys, size_t num_keys)
{
	std::stringstream ss;
	bool first = true;

	for (size_t i = 0; i < num_keys; i++)
	{
		const std::string keystr(ConvertInputBindingKeyToString(keys[i]));
		if (keystr.empty())
			return std::string();

		if (i > 0)
			ss << " & ";

		ss << keystr;
	}

	return ss.str();
}

void InputManager::AddBindings(const std::vector<std::string>& bindings, const InputEventHandler& handler)
{
	for (const std::string& binding : bindings)
	{
		std::shared_ptr<InputBinding> ibinding;
		const std::vector<std::string_view> chord_bindings(SplitChord(binding));

		for (const std::string_view& chord_binding : chord_bindings)
		{
			std::optional<InputBindingKey> key = ParseInputBindingKey(chord_binding);
			if (!key.has_value())
			{
				Console.WriteLn("Invalid binding: '%s'", binding.c_str());
				ibinding.reset();
				break;
			}

			if (!ibinding)
			{
				ibinding = std::make_shared<InputBinding>();
				ibinding->handler = handler;
			}

			if (ibinding->num_keys == MAX_KEYS_PER_BINDING)
			{
				Console.WriteLn("Too many chord parts, max is %u (%s)", MAX_KEYS_PER_BINDING, binding.c_str());
				ibinding.reset();
				break;
			}

			ibinding->keys[ibinding->num_keys] = key.value();
			ibinding->full_mask |= (static_cast<u8>(1) << ibinding->num_keys);
			ibinding->num_keys++;
		}

		if (!ibinding)
			continue;

		// plop it in the input map for all the keys
		for (u32 i = 0; i < ibinding->num_keys; i++)
			s_binding_map.emplace(ibinding->keys[i].MaskDirection(), ibinding);
	}
}

// ------------------------------------------------------------------------
// Key Decoders
// ------------------------------------------------------------------------

InputBindingKey InputManager::MakeHostKeyboardKey(s32 key_code)
{
	InputBindingKey key = {};
	key.source_type = InputSourceType::Keyboard;
	key.data = static_cast<u32>(key_code);
	return key;
}

InputBindingKey InputManager::MakeHostMouseButtonKey(s32 button_index)
{
	InputBindingKey key = {};
	key.source_type = InputSourceType::Mouse;
	key.source_subtype = InputSubclass::MouseButton;
	key.data = static_cast<u32>(button_index);
	return key;
}

InputBindingKey InputManager::MakeHostMouseWheelKey(s32 axis_index)
{
	InputBindingKey key = {};
	key.source_type = InputSourceType::Mouse;
	key.source_subtype = InputSubclass::MouseWheel;
	key.data = static_cast<u32>(axis_index);
	return key;
}

// ------------------------------------------------------------------------
// Bind Encoders
// ------------------------------------------------------------------------

static std::array<const char*, static_cast<u32>(InputSourceType::Count)> s_input_class_names = {{
	"Keyboard",
	"Mouse",
#ifdef SDL_BUILD
	"SDL",
#endif
}};

InputSource* InputManager::GetInputSourceInterface(InputSourceType type)
{
	return s_input_sources[static_cast<u32>(type)].get();
}

const char* InputManager::InputSourceToString(InputSourceType clazz)
{
	return s_input_class_names[static_cast<u32>(clazz)];
}

std::optional<InputSourceType> InputManager::ParseInputSourceString(const std::string_view& str)
{
	for (u32 i = 0; i < static_cast<u32>(InputSourceType::Count); i++)
	{
		if (str == s_input_class_names[i])
			return static_cast<InputSourceType>(i);
	}

	return std::nullopt;
}

std::optional<InputBindingKey> InputManager::ParseHostKeyboardKey(
	const std::string_view& source, const std::string_view& sub_binding)
{
	if (source != "Keyboard")
		return std::nullopt;

	const std::optional<s32> code = ConvertHostKeyboardStringToCode(sub_binding);
	if (!code.has_value())
		return std::nullopt;

	InputBindingKey key = {};
	key.source_type = InputSourceType::Keyboard;
	key.data = static_cast<u32>(code.value());
	return key;
}

std::optional<InputBindingKey> InputManager::ParseHostMouseKey(
	const std::string_view& source, const std::string_view& sub_binding)
{
	if (source != "Mouse")
		return std::nullopt;

	InputBindingKey key = {};
	key.source_type = InputSourceType::Mouse;

	if (StringUtil::StartsWith(sub_binding, "Button"))
	{
		const std::optional<s32> button_number = StringUtil::FromChars<s32>(sub_binding.substr(6));
		if (!button_number.has_value() || button_number.value() < 0)
			return std::nullopt;

		key.source_subtype = InputSubclass::MouseButton;
		key.data = static_cast<u32>(button_number.value());
	}
	else
	{
		return std::nullopt;
	}

	return key;
}

// ------------------------------------------------------------------------
// Binding Enumeration
// ------------------------------------------------------------------------

std::vector<const HotkeyInfo*> InputManager::GetHotkeyList()
{
	std::vector<const HotkeyInfo*> ret;
	for (const HotkeyInfo* hotkey_list : s_hotkey_list)
	{
		for (const HotkeyInfo* hotkey = hotkey_list; hotkey->name != nullptr; hotkey++)
			ret.push_back(hotkey);
	}
	std::sort(ret.begin(), ret.end(), [](const HotkeyInfo* left, const HotkeyInfo* right)
	{
		// category -> display name
		if (const int res = StringUtil::Strcasecmp(left->category, right->category); res != 0)
			return (res < 0);
		return (StringUtil::Strcasecmp(left->display_name, right->display_name) < 0);
	});
	return ret;
}

void InputManager::AddHotkeyBindings(SettingsInterface& si)
{
	for (const HotkeyInfo* hotkey_list : s_hotkey_list)
	{
		for (const HotkeyInfo* hotkey = hotkey_list; hotkey->name != nullptr; hotkey++)
		{
			const std::vector<std::string> bindings(si.GetStringList("Hotkeys", hotkey->name));
			if (bindings.empty())
				continue;

			AddBindings(bindings, InputButtonEventHandler{hotkey->handler});
		}
	}
}

void InputManager::AddPadBindings(SettingsInterface& si, u32 pad_index, const char* default_type)
{
	const std::string section(StringUtil::StdStringFromFormat("Pad%u", pad_index + 1));
	const std::string type(si.GetStringValue(section.c_str(), "Type", default_type));
	if (type.empty() || type == "None")
		return;

	const std::vector<std::string> bind_names = PAD::GetControllerBinds(type);
	if (bind_names.empty())
		return;

	for (u32 bind_index = 0; bind_index < static_cast<u32>(bind_names.size()); bind_index++)
	{
		const std::string& bind_name = bind_names[bind_index];
		const std::vector<std::string> bindings(si.GetStringList(section.c_str(), bind_name.c_str()));
		if (bindings.empty())
			continue;

		// we use axes for all pad bindings to simplify things, and because they are pressure sensitive
		AddBindings(bindings, InputAxisEventHandler{[pad_index, bind_index, bind_names](float value) {
			PAD::SetControllerState(pad_index, bind_index, value);
		}});
	}
}

// ------------------------------------------------------------------------
// Event Handling
// ------------------------------------------------------------------------

bool InputManager::HasAnyBindingsForKey(InputBindingKey key)
{
	std::unique_lock lock(s_binding_map_write_lock);
	return (s_binding_map.find(key.MaskDirection()) != s_binding_map.end());
}

bool InputManager::InvokeEvents(InputBindingKey key, float value)
{
	if (DoEventHook(key, value))
		return true;

	// find all the bindings associated with this key
	const InputBindingKey masked_key = key.MaskDirection();
	const auto range = s_binding_map.equal_range(masked_key);
	if (range.first == s_binding_map.end())
		return false;

	for (auto it = range.first; it != range.second; ++it)
	{
		InputBinding* binding = it->second.get();

		// find the key which matches us
		for (u32 i = 0; i < binding->num_keys; i++)
		{
			if (binding->keys[i].MaskDirection() != masked_key)
				continue;

			const u8 bit = static_cast<u8>(1) << i;
			const bool negative = binding->keys[i].negative;
			const bool prev_state = (binding->current_mask & bit) != 0;
			const bool new_state = (negative ? (value < 0.0f) : (value > 0.0f));

			// update state based on whether the whole chord was activated
			const u8 new_mask = (new_state ? (binding->current_mask | bit) : (binding->current_mask & ~bit));
			const bool prev_full_state = (binding->current_mask == binding->full_mask);
			const bool new_full_state = (new_mask == binding->full_mask);
			binding->current_mask = new_mask;

			// invert if we're negative, since the handler expects 0..1
			const float value_to_pass = (negative ? ((value < 0.0f) ? -value : 0.0f) : (value > 0.0f) ? value : 0.0f);

			// axes are fired regardless of a state change, unless they're zero
			// for buttons, we can use the state of the last chord key, because it'll be 1 on press,
			// and 0 on release (when the full state changes).
			if (prev_full_state != new_full_state || (binding->handler.IsAxis() && value_to_pass > 0.0f))
			{
				binding->handler.Invoke(value_to_pass);
			}

			// bail out, since we shouldn't have the same key twice in the chord
			break;
		}
	}

	return true;
}

// ------------------------------------------------------------------------
// Hooks/Event Intercepting
// ------------------------------------------------------------------------

void InputManager::SetHook(InputInterceptHook::Callback callback)
{
	std::unique_lock<std::mutex> lock(m_event_intercept_mutex);
	pxAssert(!m_event_intercept_callback);
	m_event_intercept_callback = std::move(callback);
}

void InputManager::RemoveHook()
{
	std::unique_lock<std::mutex> lock(m_event_intercept_mutex);
	if (m_event_intercept_callback)
		m_event_intercept_callback = {};
}

bool InputManager::HasHook()
{
	std::unique_lock<std::mutex> lock(m_event_intercept_mutex);
	return (bool)m_event_intercept_callback;
}

bool InputManager::DoEventHook(InputBindingKey key, float value)
{
	std::unique_lock<std::mutex> lock(m_event_intercept_mutex);
	if (!m_event_intercept_callback)
		return false;

	const InputInterceptHook::CallbackResult action = m_event_intercept_callback(key, value);
	if (action == InputInterceptHook::CallbackResult::StopMonitoring)
		m_event_intercept_callback = {};

	return true;
}

// ------------------------------------------------------------------------
// Binding Updater
// ------------------------------------------------------------------------

// TODO(Stenzek): Find a better place for this. Maybe in PAD?
static constexpr std::array<const char*, InputManager::MAX_PAD_NUMBER> s_default_pad_types = {{
	"DualShock2", // Pad 1
	"None" // Pad 2
}};

void InputManager::ReloadBindings(SettingsInterface& si)
{
	std::unique_lock lock(s_binding_map_write_lock);

	s_binding_map.clear();

	AddHotkeyBindings(si);

	for (u32 pad = 0; pad < MAX_PAD_NUMBER; pad++)
		AddPadBindings(si, pad, s_default_pad_types[pad]);
}

// ------------------------------------------------------------------------
// Source Management
// ------------------------------------------------------------------------

void InputManager::CloseSources()
{
	for (u32 i = FIRST_EXTERNAL_INPUT_SOURCE; i < LAST_EXTERNAL_INPUT_SOURCE; i++)
	{
		if (s_input_sources[i])
		{
			s_input_sources[i]->Shutdown();
			s_input_sources[i].reset();
		}
	}
}

void InputManager::PollSources()
{
	for (u32 i = FIRST_EXTERNAL_INPUT_SOURCE; i < LAST_EXTERNAL_INPUT_SOURCE; i++)
	{
		if (s_input_sources[i])
			s_input_sources[i]->PollEvents();
	}
}

template <typename T>
static void UpdateInputSourceState(SettingsInterface& si, InputSourceType type, bool default_state)
{
	const bool old_state = (s_input_sources[static_cast<u32>(type)] != nullptr);
	const bool new_state = si.GetBoolValue("InputSources", InputManager::InputSourceToString(type), default_state);
	if (old_state == new_state)
		return;

	if (new_state)
	{
		std::unique_ptr<InputSource> source = std::make_unique<T>();
		if (!source->Initialize(si))
		{
			Console.Error("(InputManager) Source '%s' failed to initialize.", InputManager::InputSourceToString(type));
			return;
		}

		s_input_sources[static_cast<u32>(type)] = std::move(source);
	}
	else
	{
		s_input_sources[static_cast<u32>(type)]->Shutdown();
		s_input_sources[static_cast<u32>(type)].reset();
	}
}

#ifdef SDL_BUILD
#include "Frontend/SDLInputSource.h"
#endif

void InputManager::ReloadSources(SettingsInterface& si)
{
#ifdef SDL_BUILD
	UpdateInputSourceState<SDLInputSource>(si, InputSourceType::SDL, true);
#endif
}
