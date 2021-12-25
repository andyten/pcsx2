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
#include "Frontend/InputSource.h"
#include "SDL.h"
#include <array>
#include <functional>
#include <mutex>
#include <vector>

class SettingsInterface;

class SDLInputSource final : public InputSource
{
public:
  SDLInputSource();
  ~SDLInputSource();

  bool Initialize(SettingsInterface& si) override;
  void Shutdown() override;

  /// Returns the path of the optional game controller database file.
  std::string GetGameControllerDBFileName() const;

  // Changing rumble strength.
  u32 GetVibrationMotorCount(u32 controller_index) override;
  void SetVibrationMotorStrength(u32 controller_index, const float* strengths, u32 num_motors) override;

  void PollEvents() override;

  std::optional<InputBindingKey> ParseKeyString(const std::string_view& device, const std::string_view& binding) override;
  std::string ConvertKeyToString(InputBindingKey key) override;

  bool ProcessSDLEvent(const SDL_Event* event);

private:
  enum : int
  {
    MAX_NUM_AXES = 7,
    MAX_NUM_BUTTONS = 16,
  };

  struct ControllerData
  {
    SDL_Haptic* haptic;
    SDL_GameController* game_controller;
    int haptic_left_right_effect;
    int joystick_id;
    int player_id;
    bool use_game_controller_rumble;
  };

  using ControllerDataVector = std::vector<ControllerData>;

  ControllerDataVector::iterator GetControllerDataForJoystickId(int id);
  ControllerDataVector::iterator GetControllerDataForPlayerId(int id);
  int GetFreePlayerId() const;

  bool OpenGameController(int index);
  bool CloseGameController(int joystick_index);
  bool HandleControllerAxisEvent(const SDL_ControllerAxisEvent* event);
  bool HandleControllerButtonEvent(const SDL_ControllerButtonEvent* event);

  ControllerDataVector m_controllers;

  bool m_sdl_subsystem_initialized = false;
};
