/* Copyright (c) 2026 Skitzelles - part of RetroArch for Game Vault (GPL-3.0, as RetroArch).
 * Three things Game Vault sends in the link with a game (included into uwp_main.cpp):
 *   style=crt|lcd|sharp|none  Screen style (Game Vault Options > Play): one of the shaders that come in this
 *                             package (assets\gv-shaders, libretro/slang-shaders), or none; no value = the
 *                             player's own setting stays
 *   seats=2,0,1,3             Who's playing?: player 1 is XInput pad 2, player 2 pad 0... (input_player<N>_joypad_index)
 *                             for this game; the next game without seats gets the usual order back. Never online.
 *   kidend=<unix time>        Kid mode: the day's play time ends then. A message 5 minutes and 1 minute before;
 *                             at the end the game is saved where it was (the resume save) and RetroArch closes, back
 *                             to Game Vault, which says the time is over. The resume save stays for next time. */

#include "../gfx/video_shader_parse.h"

static std::string s_gv_style;     /* style= of the last link */
static std::string s_gv_seats;     /* seats= */
static long long s_gv_kidend;      /* kidend=, 0 = none */
static bool s_gv_resume_now;       /* ResumeTick: save now (the play time is over) */
static bool s_gv_keep_resume;      /* Uninitialize: keep the resume save (closed by Kid mode, not by the player) */

static void GvPlayReset(void)
{
   s_gv_style.clear();
   s_gv_seats.clear();
   s_gv_kidend = 0;
}

/* a value of the link; true when it was one of these */
static bool GvPlayArg(const winrt::hstring &name, const winrt::hstring &value)
{
   std::string v = winrt::to_string(value);
   if (name == L"style")
   {
      if (v == "crt" || v == "lcd" || v == "sharp" || v == "none")
         s_gv_style = v;
      return true;
   }
   if (name == L"seats")
   {
      /* digits 0-3 and commas only, at most four pads */
      bool ok = !v.empty() && v.size() <= 7;
      for (char c : v)
         ok = ok && ((c >= '0' && c <= '3') || c == ',');
      s_gv_seats = ok ? v : std::string();
      return true;
   }
   if (name == L"kidend")
   {
      s_gv_kidend = atoll(v.c_str());
      return true;
   }
   return false;
}

static std::string GvSeatsFile(void) { return LocalStateDir() + "\\config\\gv-seats.txt"; }

static void GvApplySeats(void)
{
   settings_t *settings = config_get_ptr();
#ifdef HAVE_NETWORKING
   if (netplay_driver_ctl(RARCH_NETPLAY_CTL_IS_ENABLED, NULL))
      return;   /* online: netplay seats the players itself */
#endif
   WIN32_FILE_ATTRIBUTE_DATA d;
   bool ours = GetFileAttributesExFromAppW(winrt::to_hstring(GvSeatsFile()).c_str(), GetFileExInfoStandard, &d) != 0;
   if (s_gv_seats.empty())
   {
      if (!ours)
         return;
      /* the last game had seats: the usual order again, unless the player changed it since */
      std::string was = ReadSmallFile(GvSeatsFile());
      for (unsigned i = 0; i < 4; i++)
      {
         unsigned set = (unsigned)(i * 2 < was.size() ? was[i * 2] - '0' : i);
         if (settings->uints.input_joypad_index[i] == set)
            settings->uints.input_joypad_index[i] = i;
      }
      DeleteFileFromAppW(winrt::to_hstring(GvSeatsFile()).c_str());
      RARCH_LOG("[Game Vault] Seats: the usual controller order.\n");
      return;
   }
   std::string order;
   for (unsigned i = 0, at = 0; i < 4 && at < s_gv_seats.size(); i++, at += 2)
   {
      unsigned pad = (unsigned)(s_gv_seats[at] - '0');
      settings->uints.input_joypad_index[i] = pad;
      order += (char)('0' + pad);
      order += ',';
   }
   if (FILE *f = fopen(GvSeatsFile().c_str(), "wb"))
   {
      fwrite(order.data(), 1, order.size(), f);
      fclose(f);
   }
   RARCH_LOG("[Game Vault] Seats: player 1-4 on pads %s.\n", s_gv_seats.c_str());
}

static void GvApplyStyle(void)
{
   if (s_gv_style.empty())
      return;
   settings_t *settings = config_get_ptr();
   if (s_gv_style == "none")
   {
      if (video_shader_apply_shader(settings, RARCH_SHADER_NONE, NULL, false))
         RARCH_LOG("[Game Vault] Screen style: none.\n");
      return;
   }
   std::string preset = winrt::to_string(winrt::Windows::ApplicationModel::Package::Current()
         .InstalledLocation().Path()) + "\\assets\\gv-shaders\\" + s_gv_style + ".slangp";
   configuration_set_bool(settings, settings->bools.video_shader_enable, true);
   if (video_shader_apply_shader(settings, RARCH_SHADER_SLANG, preset.c_str(), false))
      RARCH_LOG("[Game Vault] Screen style: %s.\n", s_gv_style.c_str());
   else
      RARCH_LOG("[Game Vault] Screen style: %s could not be used with this video driver.\n", s_gv_style.c_str());
}

static void GvKidMessage(const char *text)
{
   runloop_msg_queue_push(text, strlen(text), 2, 8 * 60, true, NULL, MESSAGE_QUEUE_ICON_DEFAULT,
         MESSAGE_QUEUE_CATEGORY_INFO);
   RARCH_LOG("[Game Vault] Kid mode: %s\n", text);
}

static void GvPlayTick(void)
{
   static std::string game;
   static ULONGLONG started = 0, endingSince = 0;
   static bool applied = false, warned5 = false, warned1 = false, ending = false;
   const char *content = path_get(RARCH_PATH_CONTENT);
   content_state_t *p_content = content_state_get_ptr();
   bool running = content && *content && (p_content->flags & CONTENT_ST_FLAG_IS_INITED);
   ULONGLONG now = GetTickCount64();
   if (!running)
   {
      game.clear();
      return;
   }
   if (game != content)   /* a new game: its seats and its look, once it runs */
   {
      game     = content;
      started  = now;
      applied  = false;
      warned5  = warned1 = ending = false;
      GvApplySeats();
   }
   if (!applied && now - started > 1500)
   {
      applied = true;
      GvApplyStyle();
   }
   if (!s_gv_kidend)
      return;
   long long left = s_gv_kidend - (long long)time(NULL);
   if (!warned5 && left <= 5 * 60 && left > 60)
   {
      warned5 = true;
      GvKidMessage("5 minutes of play time left today.");
   }
   if (!warned1 && left <= 60 && left > 0)
   {
      warned1 = true;
      GvKidMessage("1 minute of play time left. Find a good place to stop!");
   }
   if (left > 0)
      return;
   if (!ending)
   {
      ending         = true;
      endingSince    = now;
      s_gv_resume_now = true;   /* ResumeTick saves where the game is now */
      GvKidMessage("Play time is over for today. Saving your game...");
      return;
   }
   /* the save is written (ResumeTick clears the flag once it started one; a few seconds for the file), or 10 s */
   if ((!s_gv_resume_now && now - endingSince > 4000) || now - endingSince > 10000)
   {
      s_gv_kidend      = 0;
      s_gv_keep_resume = true;
      RARCH_LOG("[Game Vault] Kid mode: play time over, closing.\n");
      CoreApplication::Exit();
   }
}

static bool GvKeepResume(void) { return s_gv_keep_resume; }
