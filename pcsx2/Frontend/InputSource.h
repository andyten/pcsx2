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

#pragma once

#include <optional>
#include <string_view>

#include "common/Pcsx2Defs.h"
#include "Frontend/InputManager.h"

class SettingsInterface;

class InputSource
{
public:
	InputSource();
	virtual ~InputSource();

	virtual bool Initialize(SettingsInterface& si) = 0;
	virtual void Shutdown() = 0;

	virtual void PollEvents() = 0;

	virtual u32 GetVibrationMotorCount(u32 index) = 0;
	virtual void SetVibrationMotorStrength(u32 index, const float* strengths, u32 num_motors) = 0;

	virtual std::optional<InputBindingKey> ParseKeyString(
		const std::string_view& device, const std::string_view& binding) = 0;
	virtual std::string ConvertKeyToString(InputBindingKey key) = 0;

	/// Creates a key for a generic controller axis event.
	static InputBindingKey MakeGenericControllerAxisKey(InputSourceType clazz, u32 controller_index, s32 axis_index);

	/// Creates a key for a generic controller button event.
	static InputBindingKey MakeGenericControllerButtonKey(InputSourceType clazz, u32 controller_index, s32 button_index);

	/// Parses a generic controller key string.
	static std::optional<InputBindingKey> ParseGenericControllerKey(
		InputSourceType clazz, const std::string_view& source, const std::string_view& sub_binding);

	/// Converts a generic controller key to a string.
	static std::string ConvertGenericControllerKeyToString(InputBindingKey key);
};
