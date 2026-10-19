/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like RetroArch.
 *
 * Racing wheels on the Xbox. XInput on UWP only reports gamepads, so a wheel (Logitech G920/G923 Xbox,
 * Thrustmaster, Fanatec, MOZA R3/ESX ...) was invisible to RetroArch. Windows.Gaming.Input.RacingWheel reads
 * every Xbox wheel in the same units; here a wheel is turned into an Xbox pad reading for the XInput driver, so
 * every core and the autoconfig see a normal controller:
 *   wheel -> left stick X, throttle -> right trigger, brake -> left trigger, clutch -> right stick down,
 *   A/B/X/Y, Menu/View, d-pad as on a pad, gear paddles -> RB (up) / LB (down), handbrake -> X.
 * A wheel takes the first XInput slot no real pad is in.
 */
#include <windows.h>
#include <Xinput.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

#include "uwp_wheel.h"
#include "../verbosity.h"

using namespace winrt::Windows::Gaming::Input;

namespace
{
   std::mutex s_lock;
   std::vector<RacingWheel> s_wheels;
   std::atomic<bool> s_started{false};

   void Added(const RacingWheel &w)
   {
      std::lock_guard<std::mutex> lk(s_lock);
      if (std::find(s_wheels.begin(), s_wheels.end(), w) != s_wheels.end())
         return;
      s_wheels.push_back(w);
      unsigned vid = 0, pid = 0;
      try
      {
         if (RawGameController raw = RawGameController::FromGameController(w))
         {
            vid = raw.HardwareVendorId();
            pid = raw.HardwareProductId();
         }
      }
      catch (...) {}
      RARCH_LOG("[Game Vault] Racing wheel connected (%04X:%04X), used as an Xbox pad.\n", vid, pid);
   }

   void Removed(const RacingWheel &w)
   {
      std::lock_guard<std::mutex> lk(s_lock);
      auto it = std::find(s_wheels.begin(), s_wheels.end(), w);
      if (it == s_wheels.end())
         return;
      s_wheels.erase(it);
      RARCH_LOG("[Game Vault] Racing wheel disconnected.\n");
   }

   SHORT Stick(double v) { return (SHORT)std::clamp(v * 32767.0, -32767.0, 32767.0); }
   BYTE Trigger(double v) { return (BYTE)std::clamp(v * 255.0, 0.0, 255.0); }
}

extern "C" void uwp_wheel_init(void)
{
   if (s_started.exchange(true))
      return;
   try
   {
      RacingWheel::RacingWheelAdded([](auto const &, RacingWheel const &w) { Added(w); });
      RacingWheel::RacingWheelRemoved([](auto const &, RacingWheel const &w) { Removed(w); });
      for (RacingWheel const &w : RacingWheel::RacingWheels())
         Added(w);
   }
   catch (...)
   {
      RARCH_WARN("[Game Vault] Racing wheel support could not start.\n");
   }
}

extern "C" bool uwp_wheel_get_state(unsigned n, XINPUT_STATE *out)
{
   RacingWheel w{nullptr};
   {
      std::lock_guard<std::mutex> lk(s_lock);
      if (n >= s_wheels.size())
         return false;
      w = s_wheels[n];
   }

   RacingWheelReading r;
   UINavigationReading nav{};
   bool have_nav = false;
   try
   {
      r = w.GetCurrentReading();
      if (UINavigationController ui = UINavigationController::FromGameController(w))
      {
         nav = ui.GetCurrentReading();
         have_nav = true;
      }
   }
   catch (...)
   {
      return false;
   }

   const auto has = [&r](RacingWheelButtons b) { return (r.Buttons & b) == b; };
   const auto nav_has = [&](RequiredUINavigationButtons b) { return have_nav && (nav.RequiredButtons & b) == b; };
   const auto nav_opt = [&](OptionalUINavigationButtons b) { return have_nav && (nav.OptionalButtons & b) == b; };

   WORD buttons = 0;
   if (has(RacingWheelButtons::DPadUp) || nav_has(RequiredUINavigationButtons::Up))       buttons |= XINPUT_GAMEPAD_DPAD_UP;
   if (has(RacingWheelButtons::DPadDown) || nav_has(RequiredUINavigationButtons::Down))   buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
   if (has(RacingWheelButtons::DPadLeft) || nav_has(RequiredUINavigationButtons::Left))   buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
   if (has(RacingWheelButtons::DPadRight) || nav_has(RequiredUINavigationButtons::Right)) buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
   if (nav_has(RequiredUINavigationButtons::Accept))                                      buttons |= XINPUT_GAMEPAD_A;
   if (nav_has(RequiredUINavigationButtons::Cancel))                                      buttons |= XINPUT_GAMEPAD_B;
   if (nav_opt(OptionalUINavigationButtons::Context1) || r.Handbrake > 0.5)       buttons |= XINPUT_GAMEPAD_X;
   if (nav_opt(OptionalUINavigationButtons::Context2))                            buttons |= XINPUT_GAMEPAD_Y;
   if (nav_has(RequiredUINavigationButtons::Menu))                                        buttons |= XINPUT_GAMEPAD_START;
   if (nav_has(RequiredUINavigationButtons::View))                                        buttons |= XINPUT_GAMEPAD_BACK;
   if (has(RacingWheelButtons::NextGear) || nav_opt(OptionalUINavigationButtons::PageRight))
      buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
   if (has(RacingWheelButtons::PreviousGear) || nav_opt(OptionalUINavigationButtons::PageLeft))
      buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
   if (has(RacingWheelButtons::Button1))                                          buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
   if (has(RacingWheelButtons::Button2))                                          buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;

   ZeroMemory(out, sizeof(*out));
   out->dwPacketNumber        = (DWORD)GetTickCount();
   out->Gamepad.wButtons      = buttons;
   out->Gamepad.sThumbLX      = Stick(r.Wheel);
   out->Gamepad.bRightTrigger = Trigger(r.Throttle);
   out->Gamepad.bLeftTrigger  = Trigger(r.Brake);
   out->Gamepad.sThumbRY      = Stick(-r.Clutch);
   return true;
}
