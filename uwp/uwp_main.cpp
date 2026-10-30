/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2018 - Krzysztof Haładyn
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Graphics.Display.h>
#include <winrt/Windows.Graphics.Display.Core.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.System.Profile.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <windows.h>
#include <gamingdeviceinformation.h>

#include <boolean.h>
#include <encodings/utf.h>
#include <string/stdstring.h>
#include <lists/string_list.h>
#include <queues/task_queue.h>
#include <retro_timers.h>
#include <file/file_path.h>
#include <sstream>
#include <iomanip>

#include "configuration.h"
#include "paths.h"

#include "uwp_main.h"
#include "../retroarch.h"
#ifdef HAVE_SMBCLIENT
#include "../libretro-common/vfs/vfs_implementation_smb.h"
extern "C" void retroarch_smb_init(void);
#endif
#include "../frontend/frontend.h"
#include "../input/input_keymaps.h"
#include "../verbosity.h"
#include "../content.h"
#include "../tasks/task_content.h"
#ifdef HAVE_NETWORKING
/* netplay.h itself brings in the socket headers, whose in_addr names clash with this file's own */
#include "../network/netplay/netplay_defines.h"
extern "C" bool netplay_driver_ctl(enum rarch_netplay_ctl_state state, void *data);
#endif
#include "uwp_func.h"
#include "uwp_async.h"
#include "../runloop.h"
#include "../disk_control_interface.h"
#include "../tasks/tasks_internal.h"
#include "../gfx/video_driver.h"

extern "C" void uwp_cdrom_watch(int on);
extern "C" int uwp_cdrom_media_event(void);

static void ResumeTick(void);
static void ResumeForget(void);
static void ResumeLeft(bool left);
static void ControlsTick(void);
static void TitleShotTick(void);
#ifdef GV_A3_FEATURES   /* gamerdad-a3: screen style, seats, Kid mode (gv_play.inc) */
static void GvPlayTick(void);     /* gv_play.inc: screen style, seats, Kid mode */
static bool GvKeepResume(void);
#endif

/* Game Vault: a game from the console's drive changes discs when the player swaps the disc - no menu. The
 * drive is watched (uwp_cdrom.cpp); the disc going opens the core's virtual lid, the next disc being ready
 * closes it again (at least 1.5 s later, so the game sees the lid open as on the real console). Runs on the
 * main loop's thread, as the core's disc control must. */
static void DiscSwapTick(void)
{
   static bool      watching  = false;
   static ULONGLONG insert_at = 0;
   const char *content = path_get(RARCH_PATH_CONTENT);
   bool from_drive     = content && strncmp(content, "cdrom://", 8) == 0;
   if (from_drive != watching)
   {
      RARCH_LOG("[Game Vault] Disc watch %s (content \"%s\").\n", from_drive ? "on" : "off", content ? content : "");
      uwp_cdrom_watch(from_drive ? 1 : 0);
      watching  = from_drive;
      insert_at = 0;
   }
   if (!from_drive)
      return;
   disk_control_interface_t *dc = &runloop_state_get_ptr()->system.disk_control;
   const int ev = uwp_cdrom_media_event();
   if (!disk_control_enabled(dc))
   {
      static bool told = false;
      if (ev && !told)
      {
         told = true;
         RARCH_LOG("[Game Vault] Disc swap seen, but this core has no disc control.\n");
      }
      return;
   }
   if (ev)
   {
      if (!disk_control_get_eject_state(dc))
      {
         disk_control_set_eject_state(dc, true, false);
         RARCH_LOG("[Game Vault] Disc taken out: virtual lid open.\n");
      }
      insert_at = ev == 2 ? GetTickCount64() + 1500 : 0;
   }
   if (insert_at && GetTickCount64() >= insert_at)
   {
      insert_at = 0;
      if (disk_control_get_eject_state(dc))
      {
         disk_control_set_eject_state(dc, false, false);
         RARCH_LOG("[Game Vault] New disc in the drive: virtual lid closed.\n");
      }
   }
}
#include <compat/strl.h>

using namespace RetroArchUWP;

using namespace winrt;
using namespace winrt::Windows::ApplicationModel;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;
using namespace winrt::Windows::UI::Input;
using namespace winrt::Windows::UI::ViewManagement;
using namespace winrt::Windows::Devices::Input;
using namespace winrt::Windows::System;
using namespace winrt::Windows::System::Profile;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Graphics::Display;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Storage;

char uwp_dir_install[DIR_MAX_LENGTH]  = { 0 };
char uwp_dir_data[DIR_MAX_LENGTH]     = { 0 };
char uwp_device_family[128]           = { 0 };
char win32_cpu_model_name[128]        = { 0 };

/* Some keys are unavailable in the VirtualKey enum (wtf) but the old-style
 * constants work */
const struct rarch_key_map rarch_key_map_uwp[] = {
   { (unsigned int)VirtualKey::Back, RETROK_BACKSPACE },
   { (unsigned int)VirtualKey::Tab, RETROK_TAB },
   { (unsigned int)VirtualKey::Clear, RETROK_CLEAR },
   { (unsigned int)VirtualKey::Enter, RETROK_RETURN },
   { (unsigned int)VirtualKey::Pause, RETROK_PAUSE },
   { (unsigned int)VirtualKey::Escape, RETROK_ESCAPE },
   { (unsigned int)VirtualKey::ModeChange, RETROK_MODE },
   { (unsigned int)VirtualKey::Space, RETROK_SPACE },
   { (unsigned int)VirtualKey::PageUp, RETROK_PAGEUP },
   { (unsigned int)VirtualKey::PageDown, RETROK_PAGEDOWN },
   { (unsigned int)VirtualKey::End, RETROK_END },
   { (unsigned int)VirtualKey::Home, RETROK_HOME },
   { (unsigned int)VirtualKey::Left, RETROK_LEFT },
   { (unsigned int)VirtualKey::Up, RETROK_UP },
   { (unsigned int)VirtualKey::Right, RETROK_RIGHT },
   { (unsigned int)VirtualKey::Down, RETROK_DOWN },
   { (unsigned int)VirtualKey::Print, RETROK_PRINT },
   { (unsigned int)VirtualKey::Insert, RETROK_INSERT },
   { (unsigned int)VirtualKey::Delete, RETROK_DELETE },
   { (unsigned int)VirtualKey::Help, RETROK_HELP },
   { (unsigned int)VirtualKey::Number0, RETROK_0 },
   { (unsigned int)VirtualKey::Number1, RETROK_1 },
   { (unsigned int)VirtualKey::Number2, RETROK_2 },
   { (unsigned int)VirtualKey::Number3, RETROK_3 },
   { (unsigned int)VirtualKey::Number4, RETROK_4 },
   { (unsigned int)VirtualKey::Number5, RETROK_5 },
   { (unsigned int)VirtualKey::Number6, RETROK_6 },
   { (unsigned int)VirtualKey::Number7, RETROK_7 },
   { (unsigned int)VirtualKey::Number8, RETROK_8 },
   { (unsigned int)VirtualKey::Number9, RETROK_9 },
   { (unsigned int)VirtualKey::A, RETROK_a },
   { (unsigned int)VirtualKey::B, RETROK_b },
   { (unsigned int)VirtualKey::C, RETROK_c },
   { (unsigned int)VirtualKey::D, RETROK_d },
   { (unsigned int)VirtualKey::E, RETROK_e },
   { (unsigned int)VirtualKey::F, RETROK_f },
   { (unsigned int)VirtualKey::G, RETROK_g },
   { (unsigned int)VirtualKey::H, RETROK_h },
   { (unsigned int)VirtualKey::I, RETROK_i },
   { (unsigned int)VirtualKey::J, RETROK_j },
   { (unsigned int)VirtualKey::K, RETROK_k },
   { (unsigned int)VirtualKey::L, RETROK_l },
   { (unsigned int)VirtualKey::M, RETROK_m },
   { (unsigned int)VirtualKey::N, RETROK_n },
   { (unsigned int)VirtualKey::O, RETROK_o },
   { (unsigned int)VirtualKey::P, RETROK_p },
   { (unsigned int)VirtualKey::Q, RETROK_q },
   { (unsigned int)VirtualKey::R, RETROK_r },
   { (unsigned int)VirtualKey::S, RETROK_s },
   { (unsigned int)VirtualKey::T, RETROK_t },
   { (unsigned int)VirtualKey::U, RETROK_u },
   { (unsigned int)VirtualKey::V, RETROK_v },
   { (unsigned int)VirtualKey::W, RETROK_w },
   { (unsigned int)VirtualKey::X, RETROK_x },
   { (unsigned int)VirtualKey::Y, RETROK_y },
   { (unsigned int)VirtualKey::Z, RETROK_z },
   { (unsigned int)VirtualKey::LeftWindows, RETROK_LSUPER },
   { (unsigned int)VirtualKey::RightWindows, RETROK_RSUPER },
   { (unsigned int)VirtualKey::Application, RETROK_MENU },
   { (unsigned int)VirtualKey::NumberPad0, RETROK_KP0 },
   { (unsigned int)VirtualKey::NumberPad1, RETROK_KP1 },
   { (unsigned int)VirtualKey::NumberPad2, RETROK_KP2 },
   { (unsigned int)VirtualKey::NumberPad3, RETROK_KP3 },
   { (unsigned int)VirtualKey::NumberPad4, RETROK_KP4 },
   { (unsigned int)VirtualKey::NumberPad5, RETROK_KP5 },
   { (unsigned int)VirtualKey::NumberPad6, RETROK_KP6 },
   { (unsigned int)VirtualKey::NumberPad7, RETROK_KP7 },
   { (unsigned int)VirtualKey::NumberPad8, RETROK_KP8 },
   { (unsigned int)VirtualKey::NumberPad9, RETROK_KP9 },
   { (unsigned int)VirtualKey::Multiply, RETROK_KP_MULTIPLY },
   { (unsigned int)VirtualKey::Add, RETROK_KP_PLUS },
   { (unsigned int)VirtualKey::Subtract, RETROK_KP_MINUS },
   { (unsigned int)VirtualKey::Decimal, RETROK_KP_PERIOD },
   { (unsigned int)VirtualKey::Divide, RETROK_KP_DIVIDE },
   { (unsigned int)VirtualKey::F1, RETROK_F1 },
   { (unsigned int)VirtualKey::F2, RETROK_F2 },
   { (unsigned int)VirtualKey::F3, RETROK_F3 },
   { (unsigned int)VirtualKey::F4, RETROK_F4 },
   { (unsigned int)VirtualKey::F5, RETROK_F5 },
   { (unsigned int)VirtualKey::F6, RETROK_F6 },
   { (unsigned int)VirtualKey::F7, RETROK_F7 },
   { (unsigned int)VirtualKey::F8, RETROK_F8 },
   { (unsigned int)VirtualKey::F9, RETROK_F9 },
   { (unsigned int)VirtualKey::F10, RETROK_F10 },
   { (unsigned int)VirtualKey::F11, RETROK_F11 },
   { (unsigned int)VirtualKey::F12, RETROK_F12 },
   { (unsigned int)VirtualKey::F13, RETROK_F13 },
   { (unsigned int)VirtualKey::F14, RETROK_F14 },
   { (unsigned int)VirtualKey::F15, RETROK_F15 },
   { (unsigned int)VirtualKey::NumberKeyLock, RETROK_NUMLOCK },
   { (unsigned int)VirtualKey::Scroll, RETROK_SCROLLOCK },
   { (unsigned int)VirtualKey::LeftShift, RETROK_LSHIFT },
   { (unsigned int)VirtualKey::RightShift, RETROK_RSHIFT },
   { (unsigned int)VirtualKey::LeftControl, RETROK_LCTRL },
   { (unsigned int)VirtualKey::RightControl, RETROK_RCTRL },
   { (unsigned int)VirtualKey::LeftMenu, RETROK_LALT },
   { (unsigned int)VirtualKey::RightMenu, RETROK_RALT },
   { (unsigned int)VirtualKey::CapitalLock, RETROK_CAPSLOCK },
   { VK_OEM_1, RETROK_SEMICOLON },
   { VK_OEM_PLUS, RETROK_EQUALS },
   { VK_OEM_COMMA, RETROK_COMMA },
   { VK_OEM_MINUS, RETROK_MINUS },
   { VK_OEM_PERIOD, RETROK_PERIOD },
   { VK_OEM_2, RETROK_SLASH },
   { VK_OEM_3, RETROK_BACKQUOTE },
   { VK_OEM_4, RETROK_LEFTBRACKET },
   { VK_OEM_5, RETROK_BACKSLASH },
   { VK_OEM_6, RETROK_RIGHTBRACKET },
   { VK_OEM_7, RETROK_QUOTE },
   { 0, RETROK_UNKNOWN }
};

#define MAX_TOUCH 16
struct input_pointer
{
   int id;
   int16_t x;
   int16_t y;
   int16_t full_x;
   int16_t full_y;
   bool isInContact;
};

struct uwp_input_state_t
{
   struct input_pointer touch[MAX_TOUCH]; /* int alignment */
   unsigned touch_count;
   int16_t mouse_screen_x;
   int16_t mouse_screen_y;
   int16_t mouse_rel_x;
   int16_t mouse_rel_y;
   int16_t mouse_wheel_left;
   int16_t mouse_wheel_up;
   bool mouse_left;
   bool mouse_right;
   bool mouse_middle;
   bool mouse_button4;
   bool mouse_button5;
};

struct uwp_input_state_t uwp_current_input, uwp_next_input;

/* Taken from DirectX UWP samples - on Xbox, everything is scaled 200%,
 * so getting the DPI calculation correct is crucial */
static inline float ConvertDipsToPixels(float dips, float dpi)
{
   static const float dips_per_inch = 96.0f;
   return floorf(dips * dpi / dips_per_inch + 0.5f);
}

/* The main function is only used to initialize our IFrameworkView class. */
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
   winrt::init_apartment(winrt::apartment_type::multi_threaded);

   DWORD dwAttrib;
   char vfs_cache_dir[MAX_PATH];
   hstring local_folder = ApplicationData::Current().LocalFolder().Path();
   hstring install_dir  = Package::Current().InstalledLocation().Path() + L"\\";
   hstring data_dir     = local_folder + L"\\";

   /* Delete VFS cache dir, we do this because this allows a far more
    * concise implementation than manually implementing a function to do this
    * This may be a little slower but shouldn't really matter as the cache dir
    * should never have more than a few items */
   hstring vfs_dir     = local_folder + L"\\VFSCACHE";

   wcstombs(uwp_dir_install, install_dir.c_str(), sizeof(uwp_dir_install));
   wcstombs(uwp_dir_data,    data_dir.c_str(),    sizeof(uwp_dir_data));
   wcstombs(vfs_cache_dir,   vfs_dir.c_str(),     sizeof(vfs_cache_dir));

   dwAttrib = GetFileAttributesA(vfs_cache_dir);
   if ((dwAttrib != INVALID_FILE_ATTRIBUTES) && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
   {
      try
      {
         auto vfsdir = StorageFolder::GetFolderFromPathAsync(vfs_dir).get();
         vfsdir.DeleteAsync().get();
      }
      catch (winrt::hresult_error const&)
      {
         /* Ignore errors when deleting VFS cache */
      }
   }

   wcstombs(uwp_device_family,
         AnalyticsInfo::VersionInfo().DeviceFamily().c_str(),
         sizeof(uwp_device_family));

   RARCH_LOG("[UWP] Data dir: %ls\n", data_dir.c_str());
   RARCH_LOG("[UWP] Install dir: %ls\n", install_dir.c_str());

   auto direct3DApplicationSource = winrt::make<Direct3DApplicationSource>();
   CoreApplication::Run(direct3DApplicationSource);
   return 0;
}

IFrameworkView Direct3DApplicationSource::CreateView()
{
   return winrt::make<App>();
}

App* App::m_instance = nullptr;

App::App() :
   m_initialized(false),
   m_windowClosed(false),
   m_windowVisible(true),
   m_windowFocused(true),
   m_windowResized(false),
   m_launchOnExitShutdown(false)
{
   m_instance = this;
}

/* The first method called when the IFrameworkView is being created. */
extern "C" void uwp_redirect_file_apis_startup(void);
extern "C" void RARCH_LOG_uwp_redirect(const char *what, unsigned count)
{
   RARCH_LOG("[UWP] File access through the broker (any USB drive): %s, %u imports.\n", what, count);
}

void App::Initialize(CoreApplicationView const& applicationView)
{
   uwp_redirect_file_apis_startup();

   /* Register event handlers for app lifecycle. This example
    * includes Activated, so that we can make the CoreWindow active and start
    * rendering on the window. */
   m_activatedToken = applicationView.Activated({ this, &App::OnActivated });
   m_suspendingToken = CoreApplication::Suspending({ this, &App::OnSuspending });
   m_resumingToken = CoreApplication::Resuming({ this, &App::OnResuming });
   m_enteredBackgroundToken = CoreApplication::EnteredBackground({ this, &App::OnEnteredBackground });
}

/* Called when the CoreWindow object is created (or re-created). */
void App::SetWindow(CoreWindow const& window)
{
   m_sizeChangedToken = window.SizeChanged({ this, &App::OnWindowSizeChanged });
   m_visibilityChangedToken = window.VisibilityChanged({ this, &App::OnVisibilityChanged });
   m_activatedWindowToken = window.Activated({ this, &App::OnWindowActivated });
   m_closedToken = window.Closed({ this, &App::OnWindowClosed });
   m_pointerPressedToken = window.PointerPressed({ this, &App::OnPointer });
   m_pointerReleasedToken = window.PointerReleased({ this, &App::OnPointer });
   m_pointerMovedToken = window.PointerMoved({ this, &App::OnPointer });
   m_pointerWheelChangedToken = window.PointerWheelChanged({ this, &App::OnPointer });
   m_acceleratorKeyToken = window.Dispatcher().AcceleratorKeyActivated({ this, &App::OnAcceleratorKey });

   DisplayInformation currentDisplayInformation = DisplayInformation::GetForCurrentView();

   m_dpiChangedToken = currentDisplayInformation.DpiChanged({ this, &App::OnDpiChanged });
   m_displayContentsInvalidatedToken = DisplayInformation::DisplayContentsInvalidated({ this, &App::OnDisplayContentsInvalidated });
   m_orientationChangedToken = currentDisplayInformation.OrientationChanged({ this, &App::OnOrientationChanged });

   m_backRequestedToken = SystemNavigationManager::GetForCurrentView().BackRequested({ this, &App::OnBackRequested });
}

/* Initializes scene resources, or loads a previously saved app state. */
void App::Load(hstring const& entryPoint)
{
   auto catalog = PackageCatalog::OpenForCurrentPackage();
   m_packageInstallingToken = catalog.PackageInstalling({ this, &App::OnPackageInstalling });
}

/* This method is called after the window becomes active. */
void App::Run()
{
   bool x = false;
   if (!m_initialized)
   {
      RARCH_WARN("[UWP] Initialization failed, so not running.\n");
      return;
   }

   for (;;)
   {
      int ret;
      CoreWindow::GetForCurrentThread().Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);

      ret = runloop_iterate();

      task_queue_check();

      DiscSwapTick();

      ResumeTick();

      ControlsTick();

      TitleShotTick();

#ifdef GV_A3_FEATURES   /* gamerdad-a3: screen style, seats, Kid mode (gv_play.inc) */
      GvPlayTick();
#endif

      if (!x)
      {
         /* HACK: I have no idea why is this necessary but
          * it is required to get proper scaling on Xbox *
          * Perhaps PreferredLaunchViewSize is broken and
          * we need to wait until the app starts to call TryResizeView */
         m_windowResized = true;
         x               = true;
      }

      if (ret == -1)
         break;
   }
}

/* Required for IFrameworkView.
 * Terminate events do not cause Uninitialize to be called.
 * It will be called if your IFrameworkView
 * class is torn down while the app is in the foreground. */
void App::Uninitialize()
{
#ifdef GV_A3_FEATURES   /* gamerdad-a3: screen style, seats, Kid mode (gv_play.inc) */
   if (!GvKeepResume())   /* Kid mode closed it: the game picks up there next time */
      ResumeForget();   /* quit the normal way: nothing to take the player back to */
#else
   ResumeForget();   /* quit the normal way: nothing to take the player back to */
#endif
   main_exit(NULL);

   /* If this instance of RetroArch was started from another app/frontend
    * and the frontend passed "launchOnExit" parameter:
    * 1. launch the app specified in "launchOnExit", most likely the
    *    same app that started RetroArch
    * 2. RetroArch goes to background and RunAsyncAndCatchErrors doesn't
    *    return, because the target app is immediately started.
    * 3. Explicitly exit in App::OnEnteredBackground if
    *    m_launchOnExitShutdown is set. Otherwise, RetroArch doesn't
    *    properly shutdown.
    */
   if (!m_launchOnExit.empty())
   {
      try
      {
         /* Launch the target app */
         m_launchOnExitShutdown = true;
         auto ret = RunAsyncAndCatchErrors<bool>([this]() {
               return Launcher::LaunchUriAsync(Uri(m_launchOnExit));
               }, false);
      }
      catch (winrt::hresult_error const&)
      {
      }
   }
}

/* Game Vault keeps BIOS files, save files and states on the games USB drive, in "<drive>\Game Vault", and
 * sends that folder as "home" with every game. The first time, what RetroArch had in its own storage is moved
 * there (a file already on the drive is left alone), and RetroArch's system, save and state folders are pointed
 * at it. A game started without "home" uses RetroArch's own folders again, so a stick that is no longer plugged
 * in is never looked for. */
static std::string s_home;
/* Game Vault's Online tab: a game started to host or join a netplay room. The link's cmd carries RetroArch's own
 * --host, or --connect <address> --port <n> [--mitm-session <id>], and --nick <name>. "relay" (nyc, madrid,
 * saopaulo, singapore) makes a hosted room go through that RetroArch relay server: a console cannot open a port on
 * the player's router. */
static std::string s_relay;
/* Game Vault: a game on a PC's shared folder or a NAS comes as "smb://<server>/<share>/<path>" and is read straight
 * from there (RetroArch's own SMB client, libretro-common/vfs/vfs_implementation_smb.c) instead of being copied to
 * the console first. The sign-in comes as "netcred": a one-time file in the folder only Game Vault and its
 * emulators can open, one line "server<tab>share<tab>user<tab>password" (as for xemu). It is read once, written
 * over and deleted, and kept in memory only: never in retroarch.cfg, whose copies go to the games drive. */
static std::string s_netcred_file;
static struct
{
   bool set;
   char server[256], share[256], user[256], password[256];
} s_net;

static void ReadNetCred(void)
{
   if (s_netcred_file.empty())
      return;
   std::wstring file = winrt::to_hstring(s_netcred_file).c_str();
   s_netcred_file.clear();
   /* only Game Vault's own one-time files: the file is deleted after reading, so a link from any other app must
    * not be able to name some other file */
   {
      std::wstring lower = file;
      for (auto &c : lower)
         c = towlower(c);
      size_t name = lower.find_last_of(L'\\');
      if (lower.find(L"..") != std::wstring::npos || lower.find(L'/') != std::wstring::npos ||
          name == std::wstring::npos || name < 6 || lower.compare(name - 6, 6, L"\\vault") ||
          lower.compare(name + 1, 8, L"netcred-") || lower.size() < 4 ||
          lower.compare(lower.size() - 4, 4, L".txt") ||
          lower.find(L"\\publishers\\") == std::wstring::npos)
      {
         RARCH_WARN("[Game Vault] Network sign-in file refused (not Game Vault's).\n");
         return;
      }
   }
   HANDLE h = CreateFile2FromAppW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
   if (h == INVALID_HANDLE_VALUE)
   {
      RARCH_WARN("[Game Vault] Network sign-in file could not be opened.\n");
      return;
   }
   char text[1100] = {0};
   DWORD got = 0;
   ReadFile(h, text, sizeof(text) - 1, &got, NULL);
   CloseHandle(h);
   h = CreateFile2FromAppW(file.c_str(), GENERIC_WRITE, 0, OPEN_EXISTING, NULL);
   if (h != INVALID_HANDLE_VALUE)
   {
      std::string blank(got, ' ');
      DWORD n;
      WriteFile(h, blank.data(), got, &n, NULL);
      CloseHandle(h);
   }
   DeleteFileFromAppW(file.c_str());
   char *field[4] = {s_net.server, s_net.share, s_net.user, s_net.password};
   const char *p = text;
   memset(&s_net, 0, sizeof(s_net));
   for (int i = 0; i < 4; i++)
   {
      const char *stop = i < 3 ? "\t" : "\r\n";
      size_t n = strcspn(p, stop);
      memcpy(field[i], p, n > 255 ? 255 : n);
      p += n;
      if (*p)
         p++;
   }
   SecureZeroMemory(text, sizeof(text));
   s_net.set = s_net.server[0] && s_net.share[0];
   if (s_net.set)
      RARCH_LOG("[Game Vault] Network games from %s, share %s.\n", s_net.server, s_net.share);
}

extern "C" void uwp_gamevault_smb_override(struct smb_settings *cfg)
{
   if (!s_net.set)
      return;
   cfg->server_address = s_net.server;
   cfg->share          = s_net.share;
   cfg->username       = s_net.user;
   cfg->password       = s_net.password;
   cfg->subdir         = "";
}
/* The frame-rate display: Game Vault sends "fps=1" while it is on. It is turned off again
 * only when it was Game Vault that turned it on (LocalState\gamevault_fps.on), so a choice made in RetroArch's
 * own menu is left alone. */
static bool s_fps;

/* "true" / "false" for video_fps_show, or empty when nothing changes */
/* Game Vault: a hosted room of a SNES game for 3 or more players ("multitap=1") plugs Snes9x's Multitap into port 2,
 * so players 3, 4 and 5 can join; the room's other players get the same from the host. Only for those games: the
 * Multitap breaks some others (Tetris & Dr. Mario), so the next start without it plugs the usual pad back in.
 * "" = leave port 2 alone, else the device for input_libretro_device_p2. */
static bool s_multitap;
static std::string MultitapChange()
{
   std::wstring mark = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) +
         L"\\gamevault_multitap.on";
   WIN32_FILE_ATTRIBUTE_DATA d;
   bool marked = GetFileAttributesExFromAppW(mark.c_str(), GetFileExInfoStandard, &d) != 0;
   if (s_multitap && !marked)
   {
      HANDLE h = CreateFile2FromAppW(mark.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, NULL);
      if (h != INVALID_HANDLE_VALUE)
         CloseHandle(h);
   }
   else if (!s_multitap && marked)
   {
      DeleteFileFromAppW(mark.c_str());
      return "1";     /* RETRO_DEVICE_JOYPAD */
   }
   return s_multitap ? "257" : std::string();   /* RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0): Snes9x "Multitap" */
}

static std::string FpsChange()
{
   std::wstring mark = std::wstring(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()) +
         L"\\gamevault_fps.on";
   WIN32_FILE_ATTRIBUTE_DATA d;
   bool marked = GetFileAttributesExFromAppW(mark.c_str(), GetFileExInfoStandard, &d) != 0;
   if (s_fps && !marked)
   {
      HANDLE h = CreateFile2FromAppW(mark.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, NULL);
      if (h != INVALID_HANDLE_VALUE)
         CloseHandle(h);
   }
   else if (!s_fps && marked)
   {
      DeleteFileFromAppW(mark.c_str());
      return "false";
   }
   return s_fps ? "true" : std::string();
}
static const char *const kHomeFolders[3][3] = {
   /* setting, folder in "home", RetroArch's own folder */
   { "system_directory",    "bios",   "system" },
   { "savefile_directory",  "saves",  "saves" },
   { "savestate_directory", "states", "states" },
};

static std::string LocalStateDir()
{
   return winrt::to_string(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
}

static bool UnderLocalState(const std::string &path)
{
   std::string own = LocalStateDir();
   return !path.empty() && _strnicmp(path.c_str(), own.c_str(), own.size()) == 0;
}

static void MoveTreeToHome(const std::wstring &from, const std::wstring &to)
{
   WIN32_FIND_DATAW fd;
   HANDLE h = FindFirstFileExFromAppW((from + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
         NULL, 0);
   if (h == INVALID_HANDLE_VALUE)
      return;
   CreateDirectoryFromAppW(to.c_str(), NULL);
   do
   {
      std::wstring name = fd.cFileName;
      if (name == L"." || name == L"..")
         continue;
      std::wstring src = from + L"\\" + name, dst = to + L"\\" + name;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      {
         MoveTreeToHome(src, dst);
         RemoveDirectoryFromAppW(src.c_str());
      }
      else if (CopyFileFromAppW(src.c_str(), dst.c_str(), TRUE))
         DeleteFileFromAppW(src.c_str());
   } while (FindNextFileW(h, &fd));
   FindClose(h);
}

/* The folder a setting names: "default" (or nothing) is RetroArch's own folder in LocalState. */
static std::string FolderOf(const std::string &value, const char *own)
{
   if (value.empty() || value == "default")
      return LocalStateDir() + "\\" + own;
   if (value[0] == '~')
      return LocalStateDir() + value.substr(1);
   return value;
}

/* A setting's value in a .cfg file, without the quotes. */
static std::string CfgValue(const std::string &text, const char *key)
{
   size_t len = strlen(key), pos = 0;
   while ((pos = text.find(key, pos)) != std::string::npos)
   {
      bool line_start = pos == 0 || text[pos - 1] == '\n';
      size_t eq       = text.find_first_not_of(" \t", pos + len);
      pos            += len;
      if (!line_start || eq == std::string::npos || text[eq] != '=')
         continue;
      size_t q1 = text.find('"', eq), nl = text.find('\n', eq);
      if (q1 == std::string::npos || q1 > nl)
         return std::string();
      size_t q2 = text.find('"', q1 + 1);
      return q2 == std::string::npos ? std::string() : text.substr(q1 + 1, q2 - q1 - 1);
   }
   return std::string();
}

static std::string ReadSmallFile(const std::string &path)
{
   std::string text;
   HANDLE h = CreateFile2FromAppW(winrt::to_hstring(path).c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
         NULL);
   if (h == INVALID_HANDLE_VALUE)
      return text;
   char buf[16384];
   DWORD n;
   while (ReadFile(h, buf, sizeof(buf), &n, NULL) && n)
      text.append(buf, n);
   CloseHandle(h);
   return text;
}

#ifdef GV_A3_FEATURES   /* gamerdad-a3: screen style, seats, Kid mode (gv_play.inc) */
#include "gv_play.inc"   /* Game Vault: screen style, Who's playing? seats, Kid mode's play time */
#endif

/* Game Vault "never lose a run": while a game plays, a save state is written in the background every 2 minutes
 * (content_save_state_quiet, no message on screen) into "<home>\resume", two files in turn, so the last one that
 * was finished is always whole. "resume\retroarch.txt" then names the game, the core and that state. RetroArch
 * deletes them when it quits the normal way or the game is closed; so when Game Vault finds the file while
 * RetroArch is not running, the emulator stopped (a crash, or the console closed it), and Game Vault starts the
 * game again with "resume=1" in the link: the state is loaded a moment after the game has started.
 * Not during online play (both sides must stay the same), and not for cores without save states. */
static bool s_resume_load;   /* resume=1 in the link: load the resume state once the game runs */

static std::string ResumeDir(void)
{
   return s_home + "\\resume";
}

static void ResumeForget(void)
{
   if (s_home.empty())
      return;
   for (const char *name : { "\\retroarch.txt", "\\retroarch-1.state", "\\retroarch-2.state", "\\retroarch.left" })
      DeleteFileFromAppW(winrt::to_hstring(ResumeDir() + name).c_str());
}

/* "resume\retroarch.left" while RetroArch is suspended (the player went to Home or the guide; Quit from Home and
 * the console closing it for memory both come after this). A crash during play never passes here, so Game Vault
 * tells the two apart: no marker = it stopped by itself (back at once); marker = the player left it (asks). */
static void ResumeLeft(bool left)
{
   if (s_home.empty())
      return;
   std::wstring file = winrt::to_hstring(ResumeDir() + "\\retroarch.left").c_str();
   if (!left)
   {
      DeleteFileFromAppW(file.c_str());
      return;
   }
   WIN32_FILE_ATTRIBUTE_DATA d;
   if (!GetFileAttributesExFromAppW(winrt::to_hstring(ResumeDir() + "\\retroarch.txt").c_str(),
            GetFileExInfoStandard, &d))
      return;   /* no game to go back to */
   HANDLE h = CreateFile2FromAppW(file.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, NULL);
   if (h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
}

static void ResumeTick(void)
{
   static std::string game;          /* the game the resume files are for */
   static ULONGLONG started = 0, saved = 0;
   static int slot = 0;              /* the file being written (1 or 2), 0 = none */
   static bool told = false;
   if (s_home.empty())
      return;
   const char *content = path_get(RARCH_PATH_CONTENT);
   content_state_t *p_content = content_state_get_ptr();
   bool running = content && *content && (p_content->flags & CONTENT_ST_FLAG_IS_INITED);
   ULONGLONG now = GetTickCount64();
   if (!running || game != content)
   {
      /* the game was closed or another one started; or a new start that is not a resume, whose old files
       * would point Game Vault at a game left long ago */
      if (!game.empty() || (running && !s_resume_load))
         ResumeForget();
      game    = running ? content : "";
      started = now;
      saved   = 0;
      slot    = 0;
      return;
   }
   if (s_resume_load && now - started > 1500)
   {
      s_resume_load = false;
      std::string info = ReadSmallFile(ResumeDir() + "\\retroarch.txt"), want = "\nstate=", state;
      size_t at = ("\n" + info).find(want);
      if (at != std::string::npos)
      {
         size_t end = info.find_first_of("\r\n", at + want.size() - 1);
         state = info.substr(at + want.size() - 1, end == std::string::npos ? std::string::npos : end - (at + want.size() - 1));
      }
      bool ours = state == ResumeDir() + "\\retroarch-1.state" || state == ResumeDir() + "\\retroarch-2.state";
      if (ours && info.find("game=" + game + "\n") != std::string::npos && content_load_state(state.c_str(), false, false))
         RARCH_LOG("[Game Vault] Resume: back where the game was (%s).\n", state.c_str());
      else
         RARCH_LOG("[Game Vault] Resume: no resume state for this game.\n");
   }
   /* the state being written is finished: from now on it is the one to go back to */
   if (slot && !content_save_state_in_progress(NULL))
   {
      std::string state = ResumeDir() + "\\retroarch-" + std::to_string(slot) + ".state";
      /* only a file the save task wrote to the end just now: the queue drops a save while another one is busy,
       * and a failed write leaves the old file (or none). This compared the file's time with the clock before,
       * which a drive's own times broke (player 186a: every save COMPLETED, every one taken as "not written",
       * so Game Vault never had a resume point) */
      if (state != content_last_saved_state)
      {
         RARCH_LOG("[Game Vault] Resume: the save was not written; the last one stays.\n");
         slot = 0;
         return;
      }
      std::string text  = "game=" + game + "\ncore=" + std::string(path_get(RARCH_PATH_CORE)) + "\nstate=" + state +
            "\nsaved=" + std::to_string((long long)time(NULL)) + "\n";
      std::string info  = ResumeDir() + "\\retroarch.txt", part = info + ".new";
      HANDLE h = CreateFile2FromAppW(winrt::to_hstring(part).c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, NULL);
      if (h != INVALID_HANDLE_VALUE)
      {
         DWORD n;
         WriteFile(h, text.data(), (DWORD)text.size(), &n, NULL);
         CloseHandle(h);
         DeleteFileFromAppW(winrt::to_hstring(info).c_str());
         MoveFileFromAppW(winrt::to_hstring(part).c_str(), winrt::to_hstring(info).c_str());
      }
      slot = 0;
   }
#ifdef GV_A3_FEATURES   /* gamerdad-a3: screen style, seats, Kid mode (gv_play.inc) */
   if (slot || (!s_gv_resume_now && (now - started < 60000 || (saved && now - saved < 120000))))
      return;
   s_gv_resume_now = false;   /* Kid mode asked for this save (gv_play.inc) */
#else
   if (slot || now - started < 60000 || (saved && now - saved < 120000))
      return;
#endif
   saved = now;   /* also when this time is skipped: the next try is 2 minutes on */
   runloop_state_t *runloop_st = runloop_state_get_ptr();
   if (runloop_st->flags & (RUNLOOP_FLAG_PAUSED | RUNLOOP_FLAG_IDLE))
      return;
#ifdef HAVE_NETWORKING
   if (netplay_driver_ctl(RARCH_NETPLAY_CTL_IS_ENABLED, NULL))
      return;
#endif
   /* a very big state (FinalBurn Neo's Cave CV1000 games: 150 MB) stopped the game right after it was written,
    * every time (player 9446, DoDonPachi SaiDaiOuJou); RetroArch started on its own, with no resume saves, played
    * it fine. Such games get no resume point. Looked at once per game: the core counts its state for this. */
   static std::string sized;
   static bool too_big = false;
   if (sized != game)
   {
      size_t size = core_serialize_size();
      sized       = game;
      too_big     = size > 100u * 1024 * 1024;
      if (too_big)
         RARCH_LOG("[Game Vault] Resume: this game's save state is too big (%u MB); no resume point.\n",
               (unsigned)(size >> 20));
   }
   if (too_big)
      return;
   CreateDirectoryFromAppW(winrt::to_hstring(ResumeDir()).c_str(), NULL);
   /* the other file than the one retroarch.txt names now */
   std::string info = ReadSmallFile(ResumeDir() + "\\retroarch.txt");
   int next = info.find("retroarch-1.state") != std::string::npos ? 2 : 1;
   std::string state = ResumeDir() + "\\retroarch-" + std::to_string(next) + ".state";
   content_last_saved_state[0] = '\0';
   if (content_save_state_quiet(state.c_str()))
      slot = next;
   else if (!told)
   {
      told = true;
      RARCH_LOG("[Game Vault] Resume: this core has no save states; no resume point.\n");
   }
}

/* Game Vault shows what each button of the Xbox pad does in a game (its details, "Controls"): the names the core
 * itself gives its buttons ("Cross", "C", "Z"...), after the player's own remapping. Written a few seconds after a
 * game starts, as "<home>\controls\<game file name>.txt" and "<home>\controls\core <core name>.txt" (for games of
 * that core not played yet), one "<Xbox button>=<what it does>" a line. */
static void ControlsTick(void)
{
   static std::string done;      /* the game written for */
   static ULONGLONG since = 0;
   if (s_home.empty())
      return;
   const char *content = path_get(RARCH_PATH_CONTENT);
   content_state_t *p_content = content_state_get_ptr();
   if (!content || !*content || !(p_content->flags & CONTENT_ST_FLAG_IS_INITED))
   {
      done.clear();
      since = 0;
      return;
   }
   if (done == content)
      return;
   ULONGLONG now = GetTickCount64();
   if (!since)
      since = now;
   if (now - since < 4000)   /* the core says its buttons as the game starts */
      return;
   done  = content;
   since = 0;
   /* the RetroPad button each Xbox button is, in RetroArch's numbering (B is the bottom one, Xbox A) */
   static const struct { unsigned id; const char *xbox; } pad[] = {
      { RETRO_DEVICE_ID_JOYPAD_B, "A" }, { RETRO_DEVICE_ID_JOYPAD_A, "B" }, { RETRO_DEVICE_ID_JOYPAD_Y, "X" },
      { RETRO_DEVICE_ID_JOYPAD_X, "Y" }, { RETRO_DEVICE_ID_JOYPAD_L, "LB" }, { RETRO_DEVICE_ID_JOYPAD_R, "RB" },
      { RETRO_DEVICE_ID_JOYPAD_L2, "LT" }, { RETRO_DEVICE_ID_JOYPAD_R2, "RT" }, { RETRO_DEVICE_ID_JOYPAD_L3, "L3" },
      { RETRO_DEVICE_ID_JOYPAD_R3, "R3" }, { RETRO_DEVICE_ID_JOYPAD_UP, "D-pad" },
      { RETRO_DEVICE_ID_JOYPAD_SELECT, "View" }, { RETRO_DEVICE_ID_JOYPAD_START, "Menu" },
      { RARCH_ANALOG_LEFT_X_PLUS, "Left stick" }, { RARCH_ANALOG_RIGHT_X_PLUS, "Right stick" } };
   rarch_system_info_t *sys = &runloop_state_get_ptr()->system;
   settings_t *settings     = config_get_ptr();
   std::string text;
   for (const auto &b : pad)
   {
      unsigned target = b.id;
      if (b.id < RARCH_FIRST_CUSTOM_BIND)   /* the player's remap: what this button sends now */
      {
         target = settings->uints.input_remap_ids[0][b.id];
         if (target >= RARCH_FIRST_CUSTOM_BIND)
            continue;   /* turned off, or sent to a stick */
      }
      const char *what = sys->input_desc_btn[0][target];
      if (!what || !*what)
         continue;
      std::string name = what;
      if (b.id == RETRO_DEVICE_ID_JOYPAD_UP)
      {
         /* the four directions as one line: "D-Pad Up" -> "D-Pad" */
         size_t sp = name.rfind(' ');
         if (sp != std::string::npos && sp > 0)
            name.resize(sp);
      }
      for (char &c : name)   /* one line each */
         if (c == '\n' || c == '\r' || c == '=')
            c = ' ';
      text += std::string(b.xbox) + "=" + name + "\n";
   }
   if (text.empty())
      return;
   std::string dir = s_home + "\\controls";
   CreateDirectoryFromAppW(winrt::to_hstring(dir).c_str(), NULL);
   std::string game = path_basename(content), core = path_basename(path_get(RARCH_PATH_CORE));
   if (game.rfind('.') != std::string::npos)
      game.resize(game.rfind('.'));
   if (core.rfind('.') != std::string::npos)
      core.resize(core.rfind('.'));
   for (const std::string &file : { dir + "\\" + game + ".txt", dir + "\\core " + core + ".txt" })
   {
      if (core.empty() && file != dir + "\\" + game + ".txt")
         continue;
      HANDLE h = CreateFile2FromAppW(winrt::to_hstring(file).c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, NULL);
      if (h == INVALID_HANDLE_VALUE)
         continue;
      DWORD n;
      WriteFile(h, text.data(), (DWORD)text.size(), &n, NULL);
      CloseHandle(h);
   }
   RARCH_LOG("[Game Vault] Controls written for %s.\n", game.c_str());
}

/* Game Vault: box art for a game nobody has a box for (a ROM hack, a homebrew, a translation). Game Vault adds
 * "titleshot=1" to the link when the game has no picture; then the screen is kept the moment the player first
 * presses Start in the first 5 minutes: that is almost always the title screen ("PRESS START"). It goes to
 * "<home>\box art\title screens\<game file name>.png", which Game Vault shows as the game's picture until the
 * player gives it a better one. Once per game start; the picture is taken before the press reaches the game. */
static bool s_titleshot;   /* titleshot=1 in the link */

static void TitleShotTick(void)
{
   static std::string game;
   static ULONGLONG started = 0;
   static bool held = true, done = false;
   if (!s_titleshot || s_home.empty())
      return;
   const char *content = path_get(RARCH_PATH_CONTENT);
   content_state_t *p_content = content_state_get_ptr();
   if (!content || !*content || !(p_content->flags & CONTENT_ST_FLAG_IS_INITED))
      return;
   ULONGLONG now = GetTickCount64();
   if (game != content)
   {
      game    = content;
      started = now;
      held    = true;   /* a Start still held from Game Vault's own screen does not count */
      done    = false;
   }
   if (done || now - started > 5 * 60 * 1000)
      return;
   bool start = false;
   try
   {
      for (auto const &pad : winrt::Windows::Gaming::Input::Gamepad::Gamepads())
         if ((pad.GetCurrentReading().Buttons & winrt::Windows::Gaming::Input::GamepadButtons::Menu) ==
               winrt::Windows::Gaming::Input::GamepadButtons::Menu)
            start = true;
   }
   catch (...)
   {
   }
   if (!start)
   {
      held = false;
      return;
   }
   if (held || now - started < 3000)
      return;
   done = true;
   std::string dir = s_home + "\\box art", name = path_basename(content);
   if (name.rfind('.') != std::string::npos)
      name.resize(name.rfind('.'));
   CreateDirectoryFromAppW(winrt::to_hstring(dir).c_str(), NULL);
   dir += "\\title screens";
   CreateDirectoryFromAppW(winrt::to_hstring(dir).c_str(), NULL);
   std::string base = dir + "\\" + name;
#ifdef HAVE_SCREENSHOTS
   /* "savestate" style: silent, written as <base>.png */
   if (take_screenshot(NULL, base.c_str(), true, video_driver_cached_frame_is_hw_render(), false, true))
      RARCH_LOG("[Game Vault] Title screen kept as box art for %s.\n", name.c_str());
   else
#endif
      RARCH_LOG("[Game Vault] Title screen could not be kept for %s.\n", name.c_str());
}

/* The three folders to use for this game: in "home", or RetroArch's own when the last game had set "home".
 * The current values come from the running settings, or before start-up from retroarch.cfg. Returns false
 * when nothing has to change. */
static bool HomeFolders(const std::string current[3], std::string want[3])
{
   bool change = false;
   for (int i = 0; i < 3; i++)
   {
      if (s_home.empty())
         want[i] = current[i].find("\\Game Vault\\") != std::string::npos ? "default" : current[i];
      else
      {
         want[i]         = s_home + "\\" + kHomeFolders[i][1];
         std::string own = FolderOf(current[i], kHomeFolders[i][2]);
         CreateDirectoryFromAppW(winrt::to_hstring(want[i]).c_str(), NULL);
         /* only RetroArch's own storage is emptied, never another drive's Game Vault folder */
         if (UnderLocalState(own))
            MoveTreeToHome(winrt::to_hstring(own).c_str(), winrt::to_hstring(want[i]).c_str());
      }
      change |= want[i] != current[i];
   }
   return change;
}

/* Game Vault: a copy of RetroArch's settings outside its own storage. The console deletes an app's storage when
 * the app is removed (a failed or queued update can do that), and with it every setting and controller choice.
 * So retroarch.cfg is also kept in the publisher's shared folder (games\retroarch: outlives a reinstall) and on
 * the games drive (<home>\settings\RetroArch retroarch.cfg: outlives a reset console), and put back by itself
 * when RetroArch starts without one. Every start from Game Vault refreshes the copies. */
static void SettingsBackup(void)
{
   std::wstring own = winrt::to_hstring(LocalStateDir() + "\\retroarch.cfg").c_str(), shared, drive;
   try
   {
      std::wstring dir = std::wstring(winrt::Windows::Storage::ApplicationData::Current()
            .GetPublisherCacheFolder(L"games").Path()) + L"\\retroarch";
      CreateDirectoryFromAppW(dir.c_str(), NULL);
      shared = dir + L"\\retroarch.cfg";
   }
   catch (...)
   {
   }
   if (!s_home.empty())
   {
      std::wstring dir = winrt::to_hstring(s_home + "\\settings").c_str();
      CreateDirectoryFromAppW(dir.c_str(), NULL);
      drive = dir + L"\\RetroArch retroarch.cfg";
   }
   WIN32_FILE_ATTRIBUTE_DATA d;
   if (!GetFileAttributesExFromAppW(own.c_str(), GetFileExInfoStandard, &d))
   {
      /* a reinstall (the shared copy) or a reset console (the drive's copy) */
      if ((!shared.empty() && CopyFileFromAppW(shared.c_str(), own.c_str(), FALSE)) ||
          (!drive.empty() && CopyFileFromAppW(drive.c_str(), own.c_str(), FALSE)))
         RARCH_LOG("[Game Vault] Settings put back from the backup.\n");
      return;
   }
   if (!shared.empty())
      CopyFileFromAppW(own.c_str(), shared.c_str(), FALSE);
   if (!drive.empty())
      CopyFileFromAppW(own.c_str(), drive.c_str(), FALSE);
}

/* Game Vault: settings every player gets without the PC setup, set once each (config\gv-defaults.txt remembers
 * them; a player who changes one afterwards keeps their choice).
 * HD: SwanStation (PS1) draws with the console's GPU at 8x with PGXP (no wobbly polygons) and true colour; these
 * came only from the PC setup's SwanStation.opt before, so players without it played at native resolution.
 * Loading: Dreamcast discs are read faster than the real drive did; the PS1 skips its BIOS intro.
 * Rumble on by default wherever a core has it. Some cores ship with it off: SwanStation (PS1) needs
 * its rumble switch and the DualShock pad type (a digital pad has no motors), Mupen64Plus-Next (N64) puts a
 * Memory Pak in the controller instead of a Rumble Pak, mGBA leaves Game Boy Player rumble off. The DualShock
 * starts in digital mode, so games made for the digital pad still work. */
static void GvDefaults(void)
{
   struct Default
   {
      const char *file; /* under config\ */
      const char *key;
      const char *value;
   };
   static const Default kDefaults[] = {
      {"SwanStation\\SwanStation.opt", "swanstation_GPU_Renderer", "D3D11"},
      {"SwanStation\\SwanStation.opt", "swanstation_GPU_ResolutionScale", "8"},
      {"SwanStation\\SwanStation.opt", "swanstation_GPU_TrueColor", "true"},
      {"SwanStation\\SwanStation.opt", "swanstation_GPU_ScaledDithering", "true"},
      {"SwanStation\\SwanStation.opt", "swanstation_GPU_PGXPEnable", "true"},
      {"SwanStation\\SwanStation.opt", "swanstation_GPU_PGXPCulling", "true"},
      {"SwanStation\\SwanStation.opt", "swanstation_GPU_PGXPTextureCorrection", "true"},
      /* shorter start: the BIOS intro is skipped. The disc keeps its real speed: 4x faster reads and seeks froze
       * Driver 2 when its loading finished (games may wait on the drive's real timing) */
      {"SwanStation\\SwanStation.opt", "swanstation_BIOS_PatchFastBoot", "true"},
      {"Flycast\\Flycast.opt", "reicast_gdrom_fast_loading", "enabled"}, /* Dreamcast GD-ROM at full speed */
      {"SwanStation\\SwanStation.opt", "swanstation_Controller_EnableRumble", "true"},
      {"SwanStation\\SwanStation.opt", "swanstation_Controller1_AnalogDPadInDigitalMode", "true"},
      {"remaps\\SwanStation\\SwanStation.rmp", "input_libretro_device_p1", "261"}, /* Analog Controller (DualShock) */
      {"PCSX-ReARMed\\PCSX-ReARMed.opt", "pcsx_rearmed_vibration", "enabled"},
      {"Mupen64Plus-Next\\Mupen64Plus-Next.opt", "mupen64plus-pak1", "rumble"},
      {"mGBA\\mGBA.opt", "mgba_force_gbp", "ON"},
      /* Azahar (3DS) on Auto asks for Vulkan, which this build has not got: "Failed to load content" */
      {"Azahar\\Azahar.opt", "citra_graphics_api", "OpenGL"},
   };
   const std::string config = LocalStateDir() + "\\config";
   const std::string done_file = config + "\\gv-defaults.txt";
   std::string done;
   if (FILE *f = fopen(done_file.c_str(), "rb"))
   {
      char buf[4096];
      size_t n = fread(buf, 1, sizeof(buf), f);
      done.assign(buf, n);
      fclose(f);
   }
   std::string newly;
   for (const Default &d : kDefaults)
   {
      const std::string id = std::string(d.file) + "|" + d.key;
      if (done.find(id + "\n") != std::string::npos)
         continue;
      const std::string path = config + "\\" + d.file;
      CreateDirectoryFromAppW(winrt::to_hstring(path.substr(0, path.rfind('\\'))).c_str(), NULL);
      if (path.find("\\remaps\\") != std::string::npos)
         CreateDirectoryFromAppW(winrt::to_hstring(config + "\\remaps").c_str(), NULL);
      /* the file without the key's old line, then the key with our value */
      std::string text, line;
      if (FILE *f = fopen(path.c_str(), "rb"))
      {
         char buf[8192];
         size_t n;
         while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            text.append(buf, n);
         fclose(f);
      }
      std::string out;
      for (size_t at = 0; at < text.size();)
      {
         size_t end = text.find('\n', at);
         end        = end == std::string::npos ? text.size() : end + 1;
         line       = text.substr(at, end - at);
         if (line.compare(0, strlen(d.key), d.key) != 0 ||
               (line.size() > strlen(d.key) && line[strlen(d.key)] != ' ' && line[strlen(d.key)] != '='))
            out += line;
         at = end;
      }
      if (!out.empty() && out.back() != '\n')
         out += "\n";
      out += std::string(d.key) + " = \"" + d.value + "\"\n";
      if (FILE *f = fopen(path.c_str(), "wb"))
      {
         fwrite(out.data(), 1, out.size(), f);
         fclose(f);
         newly += id + "\n";
         RARCH_LOG("[Game Vault] Default: %s = \"%s\" (%s).\n", d.key, d.value, d.file);
      }
   }
   if (!newly.empty())
      if (FILE *f = fopen(done_file.c_str(), "ab"))
      {
         fwrite(newly.data(), 1, newly.size(), f);
         fclose(f);
      }
}

/* Game Vault: a PlayStation game played online starts with PCSX-ReARMed (SwanStation's states do not keep two
 * machines in step), and every player must run the same BIOS, or the games drift apart: one player's own BIOS file
 * against another's built-in one is enough. So an online start of PCSX-ReARMed always uses its built-in BIOS (HLE).
 * PCSX-ReARMed plays nothing else in Game Vault, so the setting stays. */
static void NetplayCoreOptions(const std::vector<std::string> &args)
{
   bool online = false, pcsx = false;
   for (size_t i = 0; i < args.size(); i++)
   {
      online |= args[i] == "--host" || args[i] == "--connect";
      pcsx   |= args[i] == "-L" && i + 1 < args.size() && args[i + 1].find("pcsx_rearmed") != std::string::npos;
   }
   if (!online || !pcsx)
      return;
   const std::string dir  = LocalStateDir() + "\\config\\PCSX-ReARMed";
   const std::string path = dir + "\\PCSX-ReARMed.opt";
   CreateDirectoryFromAppW(winrt::to_hstring(LocalStateDir() + "\\config").c_str(), NULL);
   CreateDirectoryFromAppW(winrt::to_hstring(dir).c_str(), NULL);
   std::string text = ReadSmallFile(path), out;
   for (size_t at = 0; at < text.size();)
   {
      size_t end       = text.find('\n', at);
      end              = end == std::string::npos ? text.size() : end + 1;
      std::string line = text.substr(at, end - at);
      if (line.compare(0, 17, "pcsx_rearmed_bios") != 0)
         out += line;
      at = end;
   }
   if (!out.empty() && out.back() != '\n')
      out += "\n";
   out += "pcsx_rearmed_bios = \"HLE\"\n";
   if (FILE *f = fopen(path.c_str(), "wb"))
   {
      fwrite(out.data(), 1, out.size(), f);
      fclose(f);
      RARCH_LOG("[Game Vault] Online PlayStation: built-in BIOS\n");
   }
}

/* Before start-up: the folders go in a small extra config, given to RetroArch as --appendconfig. */
static void HomeBeforeStart(std::vector<std::string> *args)
{
   SettingsBackup();
   GvDefaults();
   std::string cfg = ReadSmallFile(LocalStateDir() + "\\retroarch.cfg"), current[3], want[3];
   for (int i = 0; i < 3; i++)
      current[i] = CfgValue(cfg, kHomeFolders[i][0]);
   std::string extra = LocalStateDir() + "\\gamevault_home.cfg", text, fps = FpsChange();
   if (HomeFolders(current, want))
      for (int i = 0; i < 3; i++)
         text += std::string(kHomeFolders[i][0]) + " = \"" + want[i] + "\"\n";
   if (!fps.empty())
      text += "video_fps_show = \"" + fps + "\"\n";
   if (!s_relay.empty())   /* a room hosted from Game Vault's Online tab */
      text += "netplay_use_mitm_server = \"true\"\nnetplay_mitm_server = \"" + s_relay + "\"\n"
              "netplay_public_announce = \"true\"\n";
   std::string multitap = MultitapChange();
   if (!multitap.empty())
      text += "input_libretro_device_p2 = \"" + multitap + "\"\n";
   NetplayCoreOptions(*args);
   /* The menu's pictures and fonts (assets\ozone: the selection bar, the icons) come with this package. A console
    * never had them in RetroArch's own folder, so the menu showed no selection at all. The package's copy is used
    * whenever the folder set in the settings lacks them (its path changes with each update, hence every start).
    * The XMB (monochrome theme) and Material UI menus come along too: switched to XMB, a player saw bare text. */
   {
      std::string packaged = winrt::to_string(winrt::Windows::ApplicationModel::Package::Current()
            .InstalledLocation().Path()) + "\\assets";
      std::string set      = FolderOf(CfgValue(cfg, "assets_directory"), "assets");
      static const char *const kMenuMarks[] = {
         "\\ozone\\png\\cursor_border.png", "\\xmb\\monochrome\\png\\cursor.png", "\\glui\\add.png" };
      WIN32_FILE_ATTRIBUTE_DATA d;
      bool setHasAll = true;
      for (const char *mark : kMenuMarks)
         if (!GetFileAttributesExFromAppW(winrt::to_hstring(set + mark).c_str(), GetFileExInfoStandard, &d))
            setHasAll = false;
      if (!setHasAll &&
          GetFileAttributesExFromAppW(winrt::to_hstring(packaged + kMenuMarks[1]).c_str(),
               GetFileExInfoStandard, &d))
      {
         text += "assets_directory = \"" + packaged + "\"\n";
         RARCH_LOG("[Game Vault] Menu pictures from the package: %s\n", packaged.c_str());
      }
   }
   /* Continue anywhere (Game Vault's vaultlink.inc): closing a game writes a save state of where it was
    * ("<home>\states\<core>\<game>.state.auto"), which Game Vault for Android can carry on from */
   if (!s_home.empty())
      text += "savestate_auto_save = \"true\"\n";
   /* RetroArch's log of each session goes to the drive too (logs\retroarch.log), where a player can pick
    * it up for a problem report */
   if (!s_home.empty())
      text += "log_to_file = \"true\"\nlog_to_file_timestamp = \"false\"\nlog_verbosity = \"true\"\n"
              "frontend_log_level = \"1\"\nlibretro_log_level = \"1\"\nlog_dir = \"" + s_home + "\\logs\"\n";
   if (text.empty())
      return;
   HANDLE h = CreateFile2FromAppW(winrt::to_hstring(extra).c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, NULL);
   if (h == INVALID_HANDLE_VALUE)
      return;
   DWORD n;
   WriteFile(h, text.data(), (DWORD)text.size(), &n, NULL);
   CloseHandle(h);
   if (args->empty())
      args->push_back("retroarch");
   args->insert(args->begin() + 1, extra);
   args->insert(args->begin() + 1, "--appendconfig");
}

/* While running (a new game sent to an open RetroArch): straight into the settings. */
static void HomeWhileRunning(void)
{
   settings_t *settings = config_get_ptr();
   std::string fps      = FpsChange();
   if (!fps.empty())
      settings->bools.video_fps_show = fps == "true";
   std::string current[3], want[3];
   current[0] = settings->paths.directory_system;
   current[1] = dir_get_ptr(RARCH_DIR_SAVEFILE);
   current[2] = dir_get_ptr(RARCH_DIR_SAVESTATE);
   if (!HomeFolders(current, want))
      return;
   for (int i = 0; i < 3; i++)
      if (want[i] == "default")
         want[i] = FolderOf("", kHomeFolders[i][2]);
   strlcpy(settings->paths.directory_system, want[0].c_str(), sizeof(settings->paths.directory_system));
   dir_set(RARCH_DIR_SAVEFILE, want[1].c_str());
   dir_set(RARCH_DIR_SAVESTATE, want[2].c_str());
   RARCH_LOG("[UWP] Game Vault folders: %s, %s, %s\n", want[0].c_str(), want[1].c_str(), want[2].c_str());
}

/* Application lifecycle event handlers. */

void App::OnActivated(CoreApplicationView const& applicationView, IActivatedEventArgs const& args)
{
   int ret;
   int argc = 0;
   std::vector<char*> argv;
   /* using std::string as temp buf instead of char* array
    * to avoid manual char allocations */
   std::vector<std::string> argvTmp;
   ParseProtocolArgs(args, &argc, &argv, &argvTmp);

   /* Start only if not already initialized. */
   if (m_initialized)
   {
      /* Already running (the player left with the Xbox button instead of
       * quitting) and a launcher sends a new game: load it instead of
       * ignoring it, so the launcher never has to ask the player to quit
       * RetroArch first. "retroarch -L <core> <content>" */
      const char *core    = NULL;
      const char *content = NULL;
      for (size_t i = 0; i + 1 < argvTmp.size(); i++)
      {
         if (argvTmp[i] == "-L")
         {
            core = argvTmp[i + 1].c_str();
            for (size_t j = i + 2; j < argvTmp.size(); j++)
            {
               if (argvTmp[j].empty() || argvTmp[j][0] == '-')
                  continue;
               content = argvTmp[j].c_str();
               break;
            }
            break;
         }
      }
      if (core && content)
      {
         HomeWhileRunning();
#ifdef HAVE_NETWORKING
         /* Game Vault's Online tab (see s_relay): what RetroArch's own lobby does before it loads a room's game.
          * A game without --host / --connect ends any room the last one had. */
         {
            std::string connect, port, session, nick;
            bool host = false;
            for (size_t i = 0; i < argvTmp.size(); i++)
            {
               const std::string &a = argvTmp[i];
               const char *next     = i + 1 < argvTmp.size() ? argvTmp[i + 1].c_str() : "";
               if (a == "--host")
                  host = true;
               else if (a == "--connect")
                  connect = next;
               else if (a == "--port")
                  port = next;
               else if (a == "--mitm-session")
                  session = next;
               else if (a == "--nick")
                  nick = next;
            }
            settings_t *settings = config_get_ptr();
            command_event(CMD_EVENT_NETPLAY_DEINIT, NULL);
            netplay_driver_ctl(RARCH_NETPLAY_CTL_DISABLE, NULL);
            if (!nick.empty())
               configuration_set_string(settings, settings->paths.username, nick.c_str());
            NetplayCoreOptions(argvTmp);   /* before the game loads: the core reads its options then */
            std::string multitap = MultitapChange();   /* before the game loads: the core takes port 2's device then */
            if (!multitap.empty())
            {
               configuration_set_uint(settings, settings->uints.input_libretro_device[1], unsigned(atoi(multitap.c_str())));
               RARCH_LOG("[Game Vault] Port 2: %s\n", s_multitap ? "Multitap" : "pad");
            }
            if (host)
            {
               if (!s_relay.empty())
               {
                  configuration_set_bool(settings, settings->bools.netplay_use_mitm_server, true);
                  configuration_set_string(settings, settings->arrays.netplay_mitm_server, s_relay.c_str());
                  configuration_set_bool(settings, settings->bools.netplay_public_announce, true);
               }
               netplay_driver_ctl(RARCH_NETPLAY_CTL_ENABLE_SERVER, NULL);
               RARCH_LOG("[Game Vault] Hosting a room (relay %s)\n", s_relay.empty() ? "none" : s_relay.c_str());
            }
            else if (!connect.empty())
            {
               std::string hostname = connect + "|" + (port.empty() ? "55435" : port);
               if (!session.empty())
                  hostname += "|" + session;
               netplay_driver_ctl(RARCH_NETPLAY_CTL_ENABLE_CLIENT, NULL);
               command_event(CMD_EVENT_NETPLAY_INIT_DIRECT_DEFERRED, (void*)hostname.c_str());
               RARCH_LOG("[Game Vault] Joining a room at %s\n", hostname.c_str());
            }
         }
#endif
         if (strncmp(content, "smb://", 6) == 0)
            retroarch_smb_init();   /* the sign-in of the share this game is on */
         content_ctx_info_t content_info = {0};
         RARCH_LOG("[UWP] New game while running: %s with %s\n", content, core);
         task_push_load_content_with_new_core_from_companion_ui(
               core, content, NULL, NULL, NULL, &content_info, NULL, NULL);
      }
      return;
   }

   /* Game Vault: on Xbox the video is rebuilt once after start-up (the display size below); RetroArch cannot show
    * a picture without that rebuild, and a hardware-rendered core that is already running does not survive it
    * (SwanStation: "Switching to software renderer", PS1 without HD). So a game sent with the start is held back
    * and loaded once the video is ready, the way a game sent to an open RetroArch is. Online rooms (--host /
    * --connect) still start the old way. */
   std::string held_core, held_content;
   if (is_running_on_xbox())
   {
      bool online = false;
      for (size_t i = 0; i < argvTmp.size(); i++)
         online |= argvTmp[i] == "--host" || argvTmp[i] == "--connect";
      for (size_t i = 0; !online && i + 2 < argvTmp.size(); i++)
      {
         if (argvTmp[i] != "-L" || argvTmp[i + 2].empty() || argvTmp[i + 2][0] == '-')
            continue;
         held_core    = argvTmp[i + 1];
         held_content = argvTmp[i + 2];
         /* RetroArch starts with the rest of the arguments (--appendconfig ...) */
         std::vector<char*> rest;
         for (size_t j = 0; j < argv.size(); j++)
            if (j < i || j > i + 2)
               rest.push_back(argv[j]);
         argv = rest;
         argc = (int)argv.size() - 1;   /* argv ends with a null pointer */
         break;
      }
   }

   if ((ret = rarch_main(argc, argv.data(), NULL)) != 0)
   {
      RARCH_ERR("[UWP] Init failed.\n");
      CoreApplication::Exit();
      return;
   }
   m_initialized = true;

   /* Game Vault: the memory this app may use. Dev Home starts it with 1 GB on App type "App" (about 5 GB on
    * "Game"); bigger systems then run out and the console closes RetroArch, which looks like "it opens and closes".
    * The setting cannot be changed from inside an app, so Game Vault is told on the way back and says how. */
   {
      uint64_t limit = winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
      RARCH_LOG("[Game Vault] Memory limit %u MB (%s).\n", (unsigned)(limit >> 20),
            limit < (2048ull << 20) ? "App type App" : "App type Game");
      if (limit < (2048ull << 20) && m_launchOnExit == L"gamevault:")
         m_launchOnExit = L"gamevault:?from=lowmem-retroarch";
   }

   if (is_running_on_xbox())
   {
      bool reset                 = false;
      int width                  = uwp_get_width();
      int height                 = uwp_get_height();
      /* Reset driver to D3D11 if set to OpenGL on boot as cores can
       * just set to 'gl' when needed and there is no good reason to
       * use 'gl' for the menus
       * Do not change the default driver if the content is already
       * initialized through arguments, as this would crash RA for
       * cores that use only ANGLE */
      settings_t *settings       = config_get_ptr();
      content_state_t *p_content = content_state_get_ptr();
      char *currentdriver        = settings->arrays.video_driver;
      if (     strcmpi(currentdriver, "gl") == 0
            && !p_content->flags & CONTENT_ST_FLAG_IS_INITED)
      {
         /* Set driver to default */
         configuration_set_string(settings,
               settings->arrays.video_driver,
               config_get_default_video());
         reset = true; /* Reset needed */
      }
      if (     (settings->uints.video_fullscreen_x != width)
            || (settings->uints.video_fullscreen_y != height))
      {
         /* Get width and height from display again */
         configuration_set_int(settings,
               settings->uints.video_fullscreen_x,
               width);
         configuration_set_int(settings,
               settings->uints.video_fullscreen_y,
               height);
         reset = true; /* Reset needed */
      }
      if (reset) /* Restart driver */
         command_event(CMD_EVENT_REINIT, NULL);
   }

   if (!held_content.empty())   /* the game held back above, now that the video is ready */
   {
      if (strncmp(held_content.c_str(), "smb://", 6) == 0)
         retroarch_smb_init();
      content_ctx_info_t content_info = {0};
      RARCH_LOG("[Game Vault] Game loaded after the video was ready: %s with %s\n", held_content.c_str(),
            held_core.c_str());
      task_push_load_content_with_new_core_from_companion_ui(
            held_core.c_str(), held_content.c_str(), NULL, NULL, NULL, &content_info, NULL, NULL);
   }

   /* Run() won't start until the CoreWindow is activated. */
   CoreWindow::GetForCurrentThread().Activate();
}

void App::OnSuspending(IInspectable const& sender, SuspendingEventArgs const& args)
{
   /* This function will ensure that configs are saved in case the app
    * is sent to background or terminated for saving configs on quit
    * for saving configs on quit now configs will be saved in
    * `retroarch_main_quit` at `retroarch.c`
    * If this function is called because of app closed by quit,
    * the below code must be ignored

   /* Save app state asynchronously after requesting a deferral.
    * Holding a deferral indicates that the application is busy
    * performing suspending operations. Be aware that a deferral may
    * not be held indefinitely. After about five seconds, the app will
    * be forced to exit. */
   SuspendingDeferral deferral = args.SuspendingOperation().GetDeferral();
   ResumeLeft(true);   /* Game Vault: the player left the game (see ResumeLeft) */

   std::thread([deferral]()
         {
         /* TODO: Maybe creating a save state here would be a good idea? */
         settings_t* settings     = config_get_ptr();
         bool config_save_on_exit = settings->bools.config_save_on_exit;

         if (config_save_on_exit)
         {
         if (!path_is_empty(RARCH_PATH_CONFIG))
         {
         const char* config_path = path_get(RARCH_PATH_CONFIG);
         bool path_exists        = config_path && *config_path;

         if (path_exists)
         {
         if (config_save_file(config_path))
         {
         RARCH_LOG("[UWP] %s \"%s\".\n",
               msg_hash_to_str(MSG_SAVED_NEW_CONFIG_TO),
               config_path);
         }
         else
         {
            RARCH_ERR("[UWP] %s \"%s\".\n",
                  msg_hash_to_str(MSG_FAILED_SAVING_CONFIG_TO),
                  config_path);
         }
         }

         }
         }

         deferral.Complete();
         }).detach();
}

void App::OnResuming(IInspectable const& sender, IInspectable const& args)
{
   ResumeLeft(false);   /* back in the game */
   /* Restore any data or state that was unloaded on suspend. By default, data
    * and state are persisted when resuming from suspend. Note that this event
    * does not occur if the app was previously terminated.
    */
}

void App::OnEnteredBackground(IInspectable const& sender, EnteredBackgroundEventArgs const& args)
{
   /* RetroArch entered background because another app/frontend
    * was launched on exit, so properly quit */
   if (m_launchOnExitShutdown)
      CoreApplication::Exit();
}

void App::OnBackRequested(IInspectable const& sender, BackRequestedEventArgs const& args)
{
   /* Prevent the B controller button on Xbox One from quitting the app */
   args.Handled(true);
}

/* Window event handlers. */

void App::OnWindowSizeChanged(CoreWindow const& sender, WindowSizeChangedEventArgs const& args)
{
   m_windowResized = true;
}

void App::OnVisibilityChanged(CoreWindow const& sender, VisibilityChangedEventArgs const& args)
{
   m_windowVisible = args.Visible();
}

void App::OnWindowActivated(CoreWindow const& sender, WindowActivatedEventArgs const& args)
{
   m_windowFocused = args.WindowActivationState() != CoreWindowActivationState::Deactivated;
   if (!m_windowFocused)
   {
      /* When deactivating the window, send key up events for anything still held down */
      for (const rarch_key_map& it : rarch_key_map_uwp)
      {
         if ((sender.GetKeyState((VirtualKey)it.sym) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down)
            input_keyboard_event(false, it.rk, 0, 0, RETRO_DEVICE_KEYBOARD);
      }
   }
}

void App::OnAcceleratorKey(CoreDispatcher const& sender, AcceleratorKeyEventArgs const& args)
{
   if (args.EventType() != CoreAcceleratorKeyEventType::KeyDown && args.EventType() != CoreAcceleratorKeyEventType::KeyUp &&
       args.EventType() != CoreAcceleratorKeyEventType::SystemKeyDown && args.EventType() != CoreAcceleratorKeyEventType::SystemKeyUp)
      return;

   /* Unlike CoreWindow::KeyDown/KeyUp events, this callback gets called for all keys including
    * F10, Alt and any keys pressed in addition to while Alt is being held down. */
   bool down = !args.KeyStatus().IsKeyReleased;
   bool extended = args.KeyStatus().IsExtendedKey;
   VirtualKey vkey = args.VirtualKey();
   CoreWindow window = CoreWindow::GetForCurrentThread();

   /* Some keys we need to specify via the extended flag */
   unsigned keycode = RETROK_UNKNOWN;
   if (vkey == VirtualKey::Enter)
      keycode = (extended ? RETROK_KP_ENTER : RETROK_RETURN);
   else if (vkey == VirtualKey::Control)
      keycode = (extended ? RETROK_RCTRL : RETROK_LCTRL);
   else if (vkey == VirtualKey::Menu)
      keycode = (extended ? RETROK_RALT : RETROK_LALT);
   else if (vkey == VirtualKey::Shift)
   {
      /* Shift is just sent generic, we need to query for and remember left or right */
      bool left_down = ((window.GetKeyState(VirtualKey::LeftShift) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down);
      bool right_down = ((window.GetKeyState(VirtualKey::RightShift) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down);
      static bool had_left_down, had_right_down;
      if (down != had_left_down && down == left_down)
      {
         had_left_down = down;
         keycode = RETROK_LSHIFT;
      }
      else if (down != had_right_down && down == right_down)
      {
         had_right_down = down;
         keycode = RETROK_RSHIFT;
      }
   }
   else
      keycode = input_keymaps_translate_keysym_to_rk((unsigned)vkey);

   if (keycode == RETROK_UNKNOWN)
      return;

   uint16_t mod = 0;
   if ((window.GetKeyState(VirtualKey::Shift) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down)
      mod |= RETROKMOD_SHIFT;
   if ((window.GetKeyState(VirtualKey::Control) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down)
      mod |= RETROKMOD_CTRL;
   if ((window.GetKeyState(VirtualKey::Menu) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down)
      mod |= RETROKMOD_ALT;
   if ((window.GetKeyState(VirtualKey::CapitalLock) & CoreVirtualKeyStates::Locked) == CoreVirtualKeyStates::Locked)
      mod |= RETROKMOD_CAPSLOCK;
   if ((window.GetKeyState(VirtualKey::Scroll) & CoreVirtualKeyStates::Locked) == CoreVirtualKeyStates::Locked)
      mod |= RETROKMOD_SCROLLOCK;
   if ((window.GetKeyState(VirtualKey::NumberKeyLock) & CoreVirtualKeyStates::Locked) == CoreVirtualKeyStates::Locked)
      mod |= RETROKMOD_NUMLOCK;
   if ((window.GetKeyState(VirtualKey::LeftWindows) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down ||
         (window.GetKeyState(VirtualKey::RightWindows) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down)
      mod |= RETROKMOD_META;

   input_keyboard_event(down, keycode, 0, mod, RETRO_DEVICE_KEYBOARD);
}

void App::OnPointer(CoreWindow const& sender, PointerEventArgs const& args)
{
   float dpi = DisplayInformation::GetForCurrentView().LogicalDpi();

   if (args.CurrentPoint().PointerDevice().PointerDeviceType() == PointerDeviceType::Mouse)
   {
      uwp_next_input.mouse_left     = args.CurrentPoint().Properties().IsLeftButtonPressed();
      uwp_next_input.mouse_middle   = args.CurrentPoint().Properties().IsMiddleButtonPressed();
      uwp_next_input.mouse_right    = args.CurrentPoint().Properties().IsRightButtonPressed();
      uwp_next_input.mouse_button4  = args.CurrentPoint().Properties().IsXButton1Pressed();
      uwp_next_input.mouse_button5  = args.CurrentPoint().Properties().IsXButton2Pressed();
      uwp_next_input.mouse_screen_x = ConvertDipsToPixels(args.CurrentPoint().Position().X, dpi);
      uwp_next_input.mouse_screen_y = ConvertDipsToPixels(args.CurrentPoint().Position().Y, dpi);
      uwp_next_input.mouse_rel_x    = uwp_next_input.mouse_screen_x - uwp_current_input.mouse_screen_x;
      uwp_next_input.mouse_rel_y    = uwp_next_input.mouse_screen_y - uwp_current_input.mouse_screen_y;
      if (args.CurrentPoint().Properties().IsHorizontalMouseWheel())
         uwp_next_input.mouse_wheel_left += args.CurrentPoint().Properties().MouseWheelDelta();
      else
         uwp_next_input.mouse_wheel_up += args.CurrentPoint().Properties().MouseWheelDelta();
   }
   else
   {
      struct video_viewport vp;
      unsigned i, free_index = MAX_TOUCH; bool found = false;
      int id = args.CurrentPoint().PointerId();

      for (i = 0; i < uwp_next_input.touch_count; i++)
      {
         if (!uwp_next_input.touch[i].isInContact && free_index == MAX_TOUCH)
            free_index = i;
         if (uwp_next_input.touch[i].id == id)
         {
            found = true;
            break;
         }
      }

      if (!found)
      {
         if (free_index >= 0 && free_index < uwp_next_input.touch_count)
            i = free_index;
         else if (uwp_next_input.touch_count + 1 < MAX_TOUCH)
            i = ++uwp_next_input.touch_count;
         else
            return;
      }

      uwp_next_input.touch[i].id = id;

      /* convert from event coordinates to core and screen coordinates */
      vp.x           = 0;
      vp.y           = 0;
      vp.width       = 0;
      vp.height      = 0;
      vp.full_width  = 0;
      vp.full_height = 0;

      video_driver_translate_coord_viewport_wrap(
            &vp,
            ConvertDipsToPixels(args.CurrentPoint().Position().X, dpi),
            ConvertDipsToPixels(args.CurrentPoint().Position().Y, dpi),
            &uwp_next_input.touch[i].x,
            &uwp_next_input.touch[i].y,
            &uwp_next_input.touch[i].full_x,
            &uwp_next_input.touch[i].full_y);

      uwp_next_input.touch[i].isInContact = args.CurrentPoint().IsInContact();

   }
}

void App::OnWindowClosed(CoreWindow const& sender, CoreWindowEventArgs const& args)
{
   m_windowClosed = true;
}

/* DisplayInformation event handlers. */

void App::OnDpiChanged(DisplayInformation const& sender, IInspectable const& args)
{
   m_windowResized = true;
}

void App::OnOrientationChanged(DisplayInformation const& sender, IInspectable const& args)
{
   m_windowResized = true;
}

void App::OnDisplayContentsInvalidated(DisplayInformation const& sender, IInspectable const& args)
{
   /* Probably can be ignored? */
}

void App::OnPackageInstalling(PackageCatalog const& sender, PackageInstallingEventArgs const& args)
{
   /* TODO: This doesn't seem to work even though it's exactly the same as in sample app and it works there */
   if (args.IsComplete())
   {
      char msg[512];
      size_t _len = snprintf(msg, sizeof(msg),
            "Package \"%ls\" installed, a restart may be necessary",
            args.Package().DisplayName().c_str());
      runloop_msg_queue_push(msg, _len, 1, 5 * 60, false, NULL,
            MESSAGE_QUEUE_ICON_DEFAULT, MESSAGE_QUEUE_CATEGORY_INFO);
   }
}

void App::ParseProtocolArgs(IActivatedEventArgs const& args, int *argc, std::vector<char*> *argv, std::vector<std::string> *argvTmp)
{
   argvTmp->clear();
   argv->clear();

   /* If the app is activated using protocol,
    * it is expected to be in this format:
    * "retroarch:?cmd=<RetroArch CLI arguments>&launchOnExit=<app to launch on exit>"
    * For example:
    * retroarch:?cmd=retroarch -L cores\core_libretro.dll "c:\mypath\path with spaces\game.rom"&launchOnExit=LaunchApp:
    * "cmd" and "launchOnExit" are optional. If none specified,
    * it will normally launch into menu
    */
   if (args.Kind() == ActivationKind::Protocol)
   {
      unsigned i;
      std::string home, relay;
      s_resume_load = false;   /* resume=1 below, for this start only */
      s_titleshot   = false;   /* titleshot=1 too */
#ifdef GV_A3_FEATURES   /* gamerdad-a3: screen style, seats, Kid mode (gv_play.inc) */
      GvPlayReset();             /* style=, seats=, kidend= (gv_play.inc) */
#endif
      bool fps = false, multitap = false;
      ProtocolActivatedEventArgs protocolArgs = args.as<ProtocolActivatedEventArgs>();
      WwwFormUrlDecoder query = protocolArgs.Uri().QueryParsed();

      for (i = 0; i < query.Size(); i++)
      {
         IWwwFormUrlDecoderEntry arg = query.GetAt(i);

         /* Parse RetroArch command line string */
         /* This allows a frotend to quit RetroArch, which in turn allows it
          * to launch a different game. */
         if (arg.Name() == L"forceExit")
            CoreApplication::Exit();
         else if (arg.Name() == L"cmd")
         {
            /* UTF-8, as RetroArch's paths are: a plain wide-to-narrow copy broke game names with letters such as
             * é, ü or ™ when started from a link (the menu opened them fine) */
            std::string strValue = winrt::to_string(arg.Value());
            std::istringstream iss(strValue);
            std::string s;

            /* Set escape character to NULL char to preserve backslashes in
             * paths which are inside quotes, they get stripped by default */
            while (iss >> std::quoted(s, '"', (char)0))
               argvTmp->push_back(s);
         }
         /* If RetroArch UWP app is started using protocol
          * with argument "launchOnExit", this gives an option
          * to launch another app on RA exit,
          * making it easy to integrate RA with other UWP frontends */
         else if (arg.Name() == L"launchOnExit")
            m_launchOnExit = arg.Value();
         /* Game Vault's folder on the games drive (see HomeFolders) */
         else if (arg.Name() == L"home")
            home = winrt::to_string(arg.Value());
         else if (arg.Name() == L"fps")
            fps = arg.Value() == L"1";
         else if (arg.Name() == L"netcred")
            s_netcred_file = winrt::to_string(arg.Value());
         else if (arg.Name() == L"relay")
            relay = winrt::to_string(arg.Value());
         else if (arg.Name() == L"multitap")
            multitap = arg.Value() == L"1";
         else if (arg.Name() == L"resume")
            s_resume_load = arg.Value() == L"1";
         else if (arg.Name() == L"titleshot")
            s_titleshot = arg.Value() == L"1";
#ifdef GV_A3_FEATURES   /* gamerdad-a3: screen style, seats, Kid mode (gv_play.inc) */
         else
            GvPlayArg(arg.Name(), arg.Value());
#endif
      }
      ReadNetCred();
      s_home = home;
      s_fps  = fps;
      /* only for this start; a server name is lower-case letters only (it goes into a settings file) */
      bool letters = relay.size() <= 32;
      for (char c : relay)
         letters = letters && c >= 'a' && c <= 'z';
      s_relay = letters ? relay : std::string();
      s_multitap = multitap;
   }

   if (!m_initialized)
      HomeBeforeStart(argvTmp);

   if (!m_initialized)
   {
      int i;
      (*argc) = argvTmp->size();
      /* Convert to char* array compatible with argv */
      for (i = 0; i < argvTmp->size(); i++)
         argv->push_back((char*)(argvTmp->at(i)).c_str());
      argv->push_back(nullptr);
   }
}

/* Implement UWP equivalents of various win32_* functions */
extern "C" {

   bool is_running_on_xbox(void)
   {
      hstring device_family = AnalyticsInfo::VersionInfo().DeviceFamily();
      return (device_family == L"Windows.Xbox");
   }

   bool win32_has_focus(void *data)
   {
      return App::GetInstance()->IsWindowFocused();
   }

   bool win32_set_video_mode(void *data, unsigned width, unsigned height, bool fullscreen)
   {
      if (App::GetInstance()->IsInitialized())
      {
         if (fullscreen !=
               ApplicationView::GetForCurrentView().IsFullScreenMode())
         {
            if (fullscreen)
               ApplicationView::GetForCurrentView().TryEnterFullScreenMode();
            else
               ApplicationView::GetForCurrentView().ExitFullScreenMode();
         }
         ApplicationView::GetForCurrentView().TryResizeView(winrt::Windows::Foundation::Size(static_cast<float>(width), static_cast<float>(height)));
      }
      else
      {
         /* In case the window is not activated yet,
          * TryResizeView will fail and we have to set the
          * initial parameters instead
          * Note that these are preserved after restarting the app
          * and used for the UWP splash screen size (!), so they
          * should be set only during init and not changed afterwards */
         ApplicationView::PreferredLaunchViewSize(winrt::Windows::Foundation::Size(static_cast<float>(width), static_cast<float>(height)));
         ApplicationView::PreferredLaunchWindowingMode(fullscreen ? ApplicationViewWindowingMode::FullScreen : ApplicationViewWindowingMode::PreferredLaunchViewSize);
      }

      /* Setting the window size may sometimes fail "because UWP"
       * (i.e. we are on device with no windows, or Windows sandbox decides the window can't be that small)
       * so in case resizing fails we just send a resized event back to RetroArch with old size
       * (and report success because otherwise it bails out hard about failing
       * to set video mode) */
      App::GetInstance()->SetWindowResized();
      return true;
   }

   void win32_show_cursor(void *data, bool state)
   {
      CoreWindow::GetForCurrentThread().PointerCursor(state ? CoreCursor(CoreCursorType::Arrow, 0) : nullptr);
   }

   bool win32_get_client_rect(RECT* rect)
   {
      auto bounds = ApplicationView::GetForCurrentView().VisibleBounds();
      rect->top    = static_cast<LONG>(bounds.Y);
      rect->left   = static_cast<LONG>(bounds.X);
      rect->bottom = static_cast<LONG>(bounds.Y + bounds.Height);
      rect->right  = static_cast<LONG>(bounds.X + bounds.Width);

      return true;
   }

   void win32_check_window(void *data,
         bool *quit, bool *resize, unsigned *width, unsigned *height)
   {
      static bool is_xbox     = is_running_on_xbox();
      *quit                   = App::GetInstance()->IsWindowClosed();
      if (is_xbox)
      {
         settings_t* settings = config_get_ptr();
         *width               = settings->uints.video_fullscreen_x  != 0 ? settings->uints.video_fullscreen_x : uwp_get_width();
         *height              = settings->uints.video_fullscreen_y  != 0 ? settings->uints.video_fullscreen_y : uwp_get_height();
         return;
      }

      *resize = App::GetInstance()->CheckWindowResized();
      if (*resize)
      {
         float dpi = DisplayInformation::GetForCurrentView().LogicalDpi();
         *width    = ConvertDipsToPixels(CoreWindow::GetForCurrentThread().Bounds().Width, dpi);
         *height   = ConvertDipsToPixels(CoreWindow::GetForCurrentThread().Bounds().Height, dpi);
      }
   }

   void* uwp_get_corewindow(void)
   {
      return winrt::get_abi(CoreWindow::GetForCurrentThread());
   }

   int current_height = -1;

   int uwp_get_height(void)
   {
       if (current_height != -1)
           return current_height;

      /* This function must be performed within UI thread,
       * otherwise it will cause a crash in specific cases
       * https://github.com/libretro/RetroArch/issues/13491 */
      float surface_scale    = 0;
      int ret                = -1;
      volatile bool finished = false;
      CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(
            CoreDispatcherPriority::Normal,
            DispatchedHandler([&surface_scale, &ret, &finished]()
               {
               if (is_running_on_xbox())
               {
               auto hdi = winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();
               if (hdi)
               ret = hdi.GetCurrentDisplayMode().ResolutionHeightInRawPixels();
               }

               if (ret == -1)
               {
               const LONG32 resolution_scale = static_cast<LONG32>(DisplayInformation::GetForCurrentView().ResolutionScale());
               surface_scale                 = static_cast<float>(resolution_scale) / 100.0f;
               ret                           = static_cast<LONG32>(
                     CoreWindow::GetForCurrentThread().Bounds().Height
                     * surface_scale);
               }
               finished = true;
               }));
      auto corewindow = CoreWindow::GetForCurrentThread();
      while (!finished)
      {
         if (corewindow)
            corewindow.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
      }
      current_height = ret;
      return ret;
   }

   int current_width = -1;

   int uwp_get_width(void)
   {
       if (current_width != -1)
           return current_width;

      /* This function must be performed within UI thread,
       * otherwise it will cause a crash in specific cases
       * https://github.com/libretro/RetroArch/issues/13491 */
      float surface_scale    = 0;
      int returnValue        = -1;
      volatile bool finished = false;
      CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(
            CoreDispatcherPriority::Normal,
            DispatchedHandler([&surface_scale, &returnValue, &finished]()
               {
               if (is_running_on_xbox())
               {
               auto hdi = winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();
               if (hdi)
               returnValue = hdi.GetCurrentDisplayMode().ResolutionWidthInRawPixels();
               }

               if(returnValue == -1)
               {
               const LONG32 resolution_scale = static_cast<LONG32>(DisplayInformation::GetForCurrentView().ResolutionScale());
               surface_scale = static_cast<float>(resolution_scale) / 100.0f;
               returnValue   = static_cast<LONG32>(
                     CoreWindow::GetForCurrentThread().Bounds().Width
                     * surface_scale);
               }
               finished = true;
               }));
      auto corewindow = CoreWindow::GetForCurrentThread();
      while (!finished)
      {
         if (corewindow)
            corewindow.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
      }
      current_width = returnValue;
      return returnValue;
   }

   float uwp_get_refresh_rate(void)
   {
      float ret              = 0.0f;
      volatile bool finished = false;
      CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(
            CoreDispatcherPriority::Normal,
            DispatchedHandler([&ret, &finished]()
               {
               if (is_running_on_xbox())
               {
                  auto hdi = winrt::Windows::Graphics::Display::Core::HdmiDisplayInformation::GetForCurrentView();
                  if (hdi)
                     ret = static_cast<float>(hdi.GetCurrentDisplayMode().RefreshRate());
               }
               finished = true;
               }));
      auto corewindow = CoreWindow::GetForCurrentThread();
      while (!finished)
      {
         if (corewindow)
            corewindow.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
      }
      return ret;
   }

   float uwp_get_dpi(void)
   {
      float ret              = 0.0f;
      volatile bool finished = false;
      CoreApplication::MainView().CoreWindow().Dispatcher().RunAsync(
            CoreDispatcherPriority::Normal,
            DispatchedHandler([&ret, &finished]()
               {
               ret     = DisplayInformation::GetForCurrentView().RawDpiX();
               finished = true;
               }));
      auto corewindow = CoreWindow::GetForCurrentThread();
      while (!finished)
      {
         if (corewindow)
            corewindow.Dispatcher().ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
      }
      return ret;
   }

   void uwp_fill_installed_core_packages(struct string_list *list)
   {
      for (auto package : Package::Current().Dependencies())
      {
         if (package.IsOptional())
         {
            string_list_elem_attr attr{};
            std::wstring path = std::wstring(package.InstalledLocation().Path().c_str()) + L"\\cores";
            string_list_append(list, utf16_to_utf8_string_alloc(path.c_str()), attr);
         }
      }
   }

   void uwp_input_next_frame(void *data)
   {
      uwp_current_input                = uwp_next_input;
      uwp_next_input.mouse_rel_x       = 0;
      uwp_next_input.mouse_rel_y       = 0;
      uwp_next_input.mouse_wheel_up   %= WHEEL_DELTA;
      uwp_next_input.mouse_wheel_left %= WHEEL_DELTA;
   }

   bool uwp_keyboard_pressed(unsigned key)
   {
      VirtualKey sym = (VirtualKey)rarch_keysym_lut[(enum retro_key)key];

      if (sym == VirtualKey::None)
         return false;

      CoreWindow window = CoreWindow::GetForCurrentThread();

      /* At times CoreWindow will return NULL while running Dolphin core
       * Dolphin core runs on its own CPU thread separate from the UI-thread and so we must do a check for this. */
      if (window)
         return (window.GetKeyState(sym) & CoreVirtualKeyStates::Down) == CoreVirtualKeyStates::Down;
      return false;
   }

   int16_t uwp_mouse_state(unsigned port, unsigned id, bool screen)
   {
      int16_t state = 0;

      switch (id)
      {
         case RETRO_DEVICE_ID_MOUSE_X:
            return screen
               ? uwp_current_input.mouse_screen_x
               : uwp_current_input.mouse_rel_x;
         case RETRO_DEVICE_ID_MOUSE_Y:
            return screen
               ? uwp_current_input.mouse_screen_y
               : uwp_current_input.mouse_rel_y;
         case RETRO_DEVICE_ID_MOUSE_LEFT:
            return uwp_current_input.mouse_left;
         case RETRO_DEVICE_ID_MOUSE_RIGHT:
            return uwp_current_input.mouse_right;
         case RETRO_DEVICE_ID_MOUSE_WHEELUP:
            return uwp_current_input.mouse_wheel_up > WHEEL_DELTA;
         case RETRO_DEVICE_ID_MOUSE_WHEELDOWN:
            return uwp_current_input.mouse_wheel_up < -WHEEL_DELTA;
         case RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELUP:
            return uwp_current_input.mouse_wheel_left > WHEEL_DELTA;
         case RETRO_DEVICE_ID_MOUSE_HORIZ_WHEELDOWN:
            return uwp_current_input.mouse_wheel_left < -WHEEL_DELTA;
         case RETRO_DEVICE_ID_MOUSE_MIDDLE:
            return uwp_current_input.mouse_middle;
         case RETRO_DEVICE_ID_MOUSE_BUTTON_4:
            return uwp_current_input.mouse_button4;
         case RETRO_DEVICE_ID_MOUSE_BUTTON_5:
            return uwp_current_input.mouse_button5;
      }

      return 0;
   }

   int16_t uwp_pointer_state(unsigned idx, unsigned id, bool screen)
   {
      switch (id)
      {
         case RETRO_DEVICE_ID_POINTER_X:
            return screen
               ? uwp_current_input.touch[idx].full_x
               : uwp_current_input.touch[idx].x;
         case RETRO_DEVICE_ID_POINTER_Y:
            return screen
               ? uwp_current_input.touch[idx].full_y
               : uwp_current_input.touch[idx].y;
         case RETRO_DEVICE_ID_POINTER_PRESSED:
            return uwp_current_input.touch[idx].isInContact;
         case RETRO_DEVICE_ID_POINTER_COUNT:
            return uwp_current_input.touch_count;
         default:
            break;
      }

      return 0;
   }

   void uwp_open_broadfilesystemaccess_settings(void)
   {
      Launcher::LaunchUriAsync(Uri(L"ms-settings:privacy-broadfilesystemaccess"));
   }

   enum retro_language uwp_get_language(void)
   {
      size_t _len;
      char lang_iso[16];
      auto lang                 = winrt::Windows::System::UserProfile::GlobalizationPreferences::Languages().GetAt(0);
      struct string_list  split = {0};
      char lang_bcp[16]         = {0};

      wcstombs(lang_bcp, lang.c_str(), sizeof(lang_bcp));

      /* Trying to convert BCP 47 language codes to ISO 639 ones */
      string_list_initialize(&split);
      string_split_noalloc(&split, lang_bcp, "-");

      _len = strlcpy(lang_iso, split.elems[0].data, sizeof(lang_iso));

      if (split.size >= 2)
      {
         _len += strlcpy_lit(lang_iso + _len, "_", sizeof(lang_iso) - _len);
         strlcpy(lang_iso       + _len,
               split.elems[split.size >= 3 ? 2 : 1].data,
               sizeof(lang_iso) - _len);
      }
      string_list_deinitialize(&split);
      return retroarch_get_language_from_iso(lang_iso);
   }

   const char* uwp_get_cpu_model_name(void)
   {
      if (is_running_on_xbox())
      {
         /* Use Gaming Device Information API for specific Xbox model */
         GAMING_DEVICE_MODEL_INFORMATION deviceInfo = {};
         HRESULT hr = GetGamingDeviceModelInformation(&deviceInfo);

         if (SUCCEEDED(hr))
         {
            switch (deviceInfo.deviceId)
            {
               case GAMING_DEVICE_DEVICE_ID_XBOX_ONE:
                  return "Xbox One CPU";
               case GAMING_DEVICE_DEVICE_ID_XBOX_ONE_S:
                  return "Xbox One S CPU";
               case GAMING_DEVICE_DEVICE_ID_XBOX_ONE_X:
                  return "Xbox One X CPU";
               case GAMING_DEVICE_DEVICE_ID_XBOX_ONE_X_DEVKIT:
                  return "Xbox One X DevKit CPU";
               case GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_S:
                  return "Xbox Series S CPU";
               case GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_X:
                  return "Xbox Series X CPU";
               case GAMING_DEVICE_DEVICE_ID_XBOX_SERIES_X_DEVKIT:
                  return "Xbox Series X DevKit CPU";
               default:
                  return "Xbox CPU (Unknown Model)";
            }
         }
         return "Xbox CPU";
      }
      else
      {
         hstring cpu_id;
         hstring cpu_name;

         /* GUID_DEVICE_PROCESSOR: {97FADB10-4E33-40AE-359C-8BEF029DBDD0} */
         hstring if_filter = L"System.Devices.InterfaceClassGuid:=\"{97FADB10-4E33-40AE-359C-8BEF029DBDD0}\"";

         /* Enumerate all CPU DeviceInterfaces, and get DeviceInstanceID of the first one. */
         try
         {
            auto collection = DeviceInformation::FindAllAsync(if_filter).get();
            if (collection.Size() > 0)
            {
               auto props = collection.GetAt(0).Properties();
               auto val = props.Lookup(L"System.Devices.DeviceInstanceID");
               if (val)
                  cpu_id = winrt::unbox_value<hstring>(val);
            }
         }
         catch (winrt::hresult_error const&)
         {
            cpu_id = hstring{};
         }

         if (!cpu_id.empty())
         {
            hstring dev_filter = L"System.Devices.DeviceInstanceID:=\"" + cpu_id + L"\"";

            /* Get the Device with the same ID as the DeviceInterface
             * Then get the name (description) of that Device
             * We have to do this because the DeviceInterface we get doesn't have a proper description. */
            try
            {
               auto collection = DeviceInformation::FindAllAsync(dev_filter, {}, DeviceInformationKind::Device).get();
               if (collection.Size() > 0)
                  cpu_name = collection.GetAt(0).Name();
            }
            catch (winrt::hresult_error const&)
            {
               cpu_name = hstring{};
            }
         }

         if (!cpu_name.empty())
         {
            wcstombs(win32_cpu_model_name, cpu_name.c_str(), sizeof(win32_cpu_model_name));
            return win32_cpu_model_name;
         }
      }
      return "Unknown";
   }
}
