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
#include "Frontend/SDLInputSource.h"
#include "Frontend/InputManager.h"
#include "HostSettings.h"
#include "common/Assertions.h"
#include "common/StringUtil.h"
#include "common/Console.h"
#include <cmath>

static constexpr char* s_sdl_axis_names[] = {
	"LeftX", // SDL_CONTROLLER_AXIS_LEFTX
	"LeftY", // SDL_CONTROLLER_AXIS_LEFTY
	"RightX", // SDL_CONTROLLER_AXIS_RIGHTX
	"RightY", // SDL_CONTROLLER_AXIS_RIGHTY
	"LeftTrigger", // SDL_CONTROLLER_AXIS_TRIGGERLEFT
	"RightTrigger", // SDL_CONTROLLER_AXIS_TRIGGERRIGHT
};

static constexpr char* s_sdl_button_names[] = {
	"A", // SDL_CONTROLLER_BUTTON_A
	"B", // SDL_CONTROLLER_BUTTON_B
	"X", // SDL_CONTROLLER_BUTTON_X
	"Y", // SDL_CONTROLLER_BUTTON_Y
	"Back", // SDL_CONTROLLER_BUTTON_BACK
	"Guide", // SDL_CONTROLLER_BUTTON_GUIDE
	"Start", // SDL_CONTROLLER_BUTTON_START
	"LeftStick", // SDL_CONTROLLER_BUTTON_LEFTSTICK
	"RightStick", // SDL_CONTROLLER_BUTTON_RIGHTSTICK
	"LeftShoulder", // SDL_CONTROLLER_BUTTON_LEFTSHOULDER
	"RightShoulder", // SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
	"DPadUp", // SDL_CONTROLLER_BUTTON_DPAD_UP
	"DPadDown", // SDL_CONTROLLER_BUTTON_DPAD_DOWN
	"DPadLeft", // SDL_CONTROLLER_BUTTON_DPAD_LEFT
	"DPadRight", // SDL_CONTROLLER_BUTTON_DPAD_RIGHT
	"Misc1", // SDL_CONTROLLER_BUTTON_MISC1
	"Paddle1", // SDL_CONTROLLER_BUTTON_PADDLE1
	"Paddle2", // SDL_CONTROLLER_BUTTON_PADDLE2
	"Paddle3", // SDL_CONTROLLER_BUTTON_PADDLE3
	"Paddle4", // SDL_CONTROLLER_BUTTON_PADDLE4
	"Touchpad", // SDL_CONTROLLER_BUTTON_TOUCHPAD
};

SDLInputSource::SDLInputSource() = default;

SDLInputSource::~SDLInputSource() { pxAssert(m_controllers.empty()); }

bool SDLInputSource::Initialize(SettingsInterface& si)
{
	const std::string gcdb_file_name = GetGameControllerDBFileName();
	if (!gcdb_file_name.empty())
	{
		Console.WriteLn("Loading game controller mappings from '%s'", gcdb_file_name.c_str());
		if (SDL_GameControllerAddMappingsFromFile(gcdb_file_name.c_str()) < 0)
		{
			Console.Error(
				"SDL_GameControllerAddMappingsFromFile(%s) failed: %s", gcdb_file_name.c_str(), SDL_GetError());
		}
	}

	const bool ds4_rumble_enabled = si.GetBoolValue("InputSources", "SDLControllerEnhancedMode", false);
	if (ds4_rumble_enabled)
	{
		Console.WriteLn("Enabling PS4/PS5 enhanced mode.");
#if SDL_VERSION_ATLEAST(2, 0, 9)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "true");
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "true");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 16)
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "true");
		SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "true");
#endif
	}

	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) < 0)
	{
		Console.Error("SDL_InitSubSystem(SDL_INIT_JOYSTICK |SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC) failed");
		return false;
	}

	// we should open the controllers as the connected events come in, so no need to do any more here
	m_sdl_subsystem_initialized = true;
	return true;
}

void SDLInputSource::Shutdown()
{
	while (!m_controllers.empty())
		CloseGameController(m_controllers.begin()->joystick_id);

	if (m_sdl_subsystem_initialized)
	{
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC);
		m_sdl_subsystem_initialized = false;
	}
}

std::string SDLInputSource::GetGameControllerDBFileName() const
{
#if 0
  // prefer the userdir copy
  std::string filename(m_host_interface->GetUserDirectoryRelativePath("gamecontrollerdb.txt"));
  if (FileSystem::FileExists(filename.c_str()))
    return filename;

  filename =
    m_host_interface->GetProgramDirectoryRelativePath("database" FS_OSPATH_SEPARATOR_STR "gamecontrollerdb.txt");
  if (FileSystem::FileExists(filename.c_str()))
    return filename;

#endif
	return {};
}

void SDLInputSource::PollEvents()
{
	for (;;)
	{
		SDL_Event ev;
		if (SDL_PollEvent(&ev))
			ProcessSDLEvent(&ev);
		else
			break;
	}
}

std::optional<InputBindingKey> SDLInputSource::ParseKeyString(
	const std::string_view& device, const std::string_view& binding)
{
	if (!StringUtil::StartsWith(device, "SDL-") || binding.empty())
		return std::nullopt;

	const std::optional<s32> player_id = StringUtil::FromChars<s32>(device.substr(4));
	if (!player_id.has_value() || player_id.value() < 0)
		return std::nullopt;

	InputBindingKey key = {};
	key.source_type = InputSourceType::SDL;
	key.source_index = static_cast<u32>(player_id.value());

	const char direction = binding[0];
	if (direction == '+' || direction == '-')
	{
		// likely an axis
		const std::string_view axis_name(binding.substr(1));
		for (u32 i = 0; i < std::size(s_sdl_axis_names); i++)
		{
			if (axis_name == s_sdl_axis_names[i])
			{
				// found an axis!
				key.source_subtype = InputSubclass::ControllerAxis;
				key.data = i;
				key.negative = (direction == '-');
				return key;
			}
		}
	}
	else
	{
		// must be a button
		for (u32 i = 0; i < std::size(s_sdl_button_names); i++)
		{
			if (binding == s_sdl_button_names[i])
			{
				key.source_subtype = InputSubclass::ControllerButton;
				key.data = i;
				return key;
			}
		}
	}

	// unknown axis/button
	return std::nullopt;
}

std::string SDLInputSource::ConvertKeyToString(InputBindingKey key)
{
	std::string ret;

	if (key.source_type == InputSourceType::SDL)
	{
		if (key.source_subtype == InputSubclass::ControllerAxis && key.data < std::size(s_sdl_axis_names))
		{
			ret = StringUtil::StdStringFromFormat(
				"SDL-%u/%c%s", key.source_index, key.negative ? '-' : '+', s_sdl_axis_names[key.data]);
		}
		else if (key.source_subtype == InputSubclass::ControllerButton && key.data < std::size(s_sdl_button_names))
		{
			ret = StringUtil::StdStringFromFormat("SDL-%u/%s", key.source_index, s_sdl_button_names[key.data]);
		}
	}

	return ret;
}

bool SDLInputSource::ProcessSDLEvent(const SDL_Event* event)
{
	switch (event->type)
	{
		case SDL_CONTROLLERDEVICEADDED:
		{
			Console.WriteLn("(SDLInputSource) Controller %d inserted", event->cdevice.which);
			OpenGameController(event->cdevice.which);
			return true;
		}

		case SDL_CONTROLLERDEVICEREMOVED:
		{
			Console.WriteLn("(SDLInputSource) Controller %d removed", event->cdevice.which);
			CloseGameController(event->cdevice.which);
			return true;
		}

		case SDL_CONTROLLERAXISMOTION:
			return HandleControllerAxisEvent(&event->caxis);

		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			return HandleControllerButtonEvent(&event->cbutton);

		default:
			return false;
	}
}

SDLInputSource::ControllerDataVector::iterator SDLInputSource::GetControllerDataForJoystickId(int id)
{
	return std::find_if(
		m_controllers.begin(), m_controllers.end(), [id](const ControllerData& cd) { return cd.joystick_id == id; });
}

SDLInputSource::ControllerDataVector::iterator SDLInputSource::GetControllerDataForPlayerId(int id)
{
	return std::find_if(
		m_controllers.begin(), m_controllers.end(), [id](const ControllerData& cd) { return cd.player_id == id; });
}

int SDLInputSource::GetFreePlayerId() const
{
	for (int player_id = 0;; player_id++)
	{
		size_t i;
		for (i = 0; i < m_controllers.size(); i++)
		{
			if (m_controllers[i].player_id == player_id)
				break;
		}
		if (i == m_controllers.size())
			return player_id;
	}

	return 0;
}

bool SDLInputSource::OpenGameController(int index)
{
	SDL_GameController* gcontroller = SDL_GameControllerOpen(index);
	SDL_Joystick* joystick = gcontroller ? SDL_GameControllerGetJoystick(gcontroller) : nullptr;
	if (!gcontroller || !joystick)
	{
		Console.Error("(SDLInputSource) Failed to open controller %d", index);
		if (gcontroller)
			SDL_GameControllerClose(gcontroller);

		return false;
	}

	int joystick_id = SDL_JoystickInstanceID(joystick);
#if SDL_VERSION_ATLEAST(2, 0, 9)
	int player_id = SDL_GameControllerGetPlayerIndex(gcontroller);
#else
	int player_id = -1;
#endif
	if (player_id < 0 || GetControllerDataForPlayerId(player_id) != m_controllers.end())
	{
		const int free_player_id = GetFreePlayerId();
		Console.Warning("(SDLInputSource) Controller %d (joystick %d) returned player ID %d, which is invalid or in "
						"use. Using ID %d instead.",
			index, joystick_id, player_id, free_player_id);
		player_id = free_player_id;
	}

	Console.WriteLn("(SDLInputSource) Opened controller %d (instance id %d, player id %d): %s", index, joystick_id,
		player_id, SDL_GameControllerName(gcontroller));

	ControllerData cd = {};
	cd.player_id = player_id;
	cd.joystick_id = joystick_id;
	cd.haptic_left_right_effect = -1;
	cd.game_controller = gcontroller;

#if SDL_VERSION_ATLEAST(2, 0, 9)
	cd.use_game_controller_rumble = (SDL_GameControllerRumble(gcontroller, 0, 0, 0) == 0);
#else
	cd.use_game_controller_rumble = false;
#endif

	if (cd.use_game_controller_rumble)
	{
		Console.WriteLn(
			"(SDLInputSource) Rumble is supported on '%s' via gamecontroller", SDL_GameControllerName(gcontroller));
	}
	else
	{
		SDL_Haptic* haptic = SDL_HapticOpenFromJoystick(joystick);
		if (haptic)
		{
			SDL_HapticEffect ef = {};
			ef.leftright.type = SDL_HAPTIC_LEFTRIGHT;
			ef.leftright.length = 1000;

			int ef_id = SDL_HapticNewEffect(haptic, &ef);
			if (ef_id >= 0)
			{
				cd.haptic = haptic;
				cd.haptic_left_right_effect = ef_id;
			}
			else
			{
				Console.Error("(SDLInputSource) Failed to create haptic left/right effect: %s", SDL_GetError());
				if (SDL_HapticRumbleSupported(haptic) && SDL_HapticRumbleInit(haptic) != 0)
				{
					cd.haptic = haptic;
				}
				else
				{
					Console.Error("(SDLInputSource) No haptic rumble supported: %s", SDL_GetError());
					SDL_HapticClose(haptic);
				}
			}
		}

		if (cd.haptic)
			Console.WriteLn(
				"(SDLInputSource) Rumble is supported on '%s' via haptic", SDL_GameControllerName(gcontroller));
	}

	if (!cd.haptic && !cd.use_game_controller_rumble)
		Console.Warning("(SDLInputSource) Rumble is not supported on '%s'", SDL_GameControllerName(gcontroller));

	m_controllers.push_back(std::move(cd));
	return true;
}

bool SDLInputSource::CloseGameController(int joystick_index)
{
	auto it = GetControllerDataForJoystickId(joystick_index);
	if (it == m_controllers.end())
		return false;

	if (it->haptic)
		SDL_HapticClose(static_cast<SDL_Haptic*>(it->haptic));

	SDL_GameControllerClose(static_cast<SDL_GameController*>(it->game_controller));
	m_controllers.erase(it);
	return true;
}

bool SDLInputSource::HandleControllerAxisEvent(const SDL_ControllerAxisEvent* ev)
{
	auto it = GetControllerDataForJoystickId(ev->which);
	if (it == m_controllers.end())
		return false;

	const InputBindingKey key(MakeGenericControllerAxisKey(InputSourceType::SDL, it->player_id, ev->axis));
	const float value = static_cast<float>(ev->value) / (ev->value < 0 ? 32768.0f : 32767.0f);
	return InputManager::InvokeEvents(key, value);
}

bool SDLInputSource::HandleControllerButtonEvent(const SDL_ControllerButtonEvent* ev)
{
	auto it = GetControllerDataForJoystickId(ev->which);
	if (it == m_controllers.end())
		return false;

	const InputBindingKey key(MakeGenericControllerButtonKey(InputSourceType::SDL, it->player_id, ev->button));
	return InputManager::InvokeEvents(key, (ev->state == SDL_PRESSED) ? 1.0f : 0.0f);
}

u32 SDLInputSource::GetVibrationMotorCount(u32 controller_index)
{
	auto it = GetControllerDataForPlayerId(controller_index);
	if (it == m_controllers.end())
		return 0;

	return (it->use_game_controller_rumble ? 2 : ((it->haptic_left_right_effect >= 0) ? 2 : (it->haptic ? 1 : 0)));
}

void SDLInputSource::SetVibrationMotorStrength(u32 controller_index, const float* strengths, u32 num_motors)
{
	auto it = GetControllerDataForPlayerId(controller_index);
	if (it == m_controllers.end())
		return;

	// we'll update before this duration is elapsed
	static constexpr u32 DURATION = 65535; // SDL_MAX_RUMBLE_DURATION_MS

#if SDL_VERSION_ATLEAST(2, 0, 9)
	if (it->use_game_controller_rumble)
	{
		const u16 large = static_cast<u16>(strengths[0] * 65535.0f);
		const u16 small = static_cast<u16>(strengths[1] * 65535.0f);
		SDL_GameControllerRumble(it->game_controller, large, small, DURATION);
		return;
	}
#endif

	SDL_Haptic* haptic = static_cast<SDL_Haptic*>(it->haptic);
	if (it->haptic_left_right_effect >= 0 && num_motors > 1)
	{
		if (strengths[0] > 0.0f || strengths[1] > 0.0f)
		{
			SDL_HapticEffect ef;
			ef.type = SDL_HAPTIC_LEFTRIGHT;
			ef.leftright.large_magnitude = static_cast<u16>(strengths[0] * 65535.0f);
			ef.leftright.small_magnitude = static_cast<u16>(strengths[1] * 65535.0f);
			ef.leftright.length = DURATION;
			SDL_HapticUpdateEffect(haptic, it->haptic_left_right_effect, &ef);
			SDL_HapticRunEffect(haptic, it->haptic_left_right_effect, SDL_HAPTIC_INFINITY);
		}
		else
		{
			SDL_HapticStopEffect(haptic, it->haptic_left_right_effect);
		}
	}
	else
	{
		float max_strength = 0.0f;
		for (u32 i = 0; i < num_motors; i++)
			max_strength = std::max(max_strength, strengths[i]);

		if (max_strength > 0.0f)
			SDL_HapticRumblePlay(haptic, max_strength, DURATION);
		else
			SDL_HapticRumbleStop(haptic);
	}
}
