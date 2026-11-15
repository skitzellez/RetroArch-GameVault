/* Copyright (c) 2026 Skitzelles - part of RetroArch for Game Vault (GPL-3.0, as RetroArch).
 * Three things Game Vault sends in the link with a game (included into uwp_main.cpp):
 *   style=crt|tv|pro|curve|lcd|sharp|none  Screen style (Game Vault Options): one of the shaders that come in this
 *                             package (assets\gv-shaders, libretro/slang-shaders), or none; no value = the
 *                             player's own setting stays
 *   frame=tv|arcade|handheld  Screen frame: a picture around the game (assets\gv-frames), in the black bars beside it.
 *                             The picture is chosen by the game's real shape, so a vertical arcade game gets a tall
 *                             opening and a widescreen game gets no frame. No value = no frame of ours.
 *   seats=2,0,1,3            Who's playing?: player 1 is XInput pad 2, player 2 pad 0... (input_player<N>_joypad_index)
 *                             for this game; the next game without seats gets the usual order back. Never online.
 *   kidend=<unix time>        Kid mode: the day's play time ends then. A message 5 minutes and 1 minute before;
 *                             at the end the game is saved where it was (the resume save) and RetroArch closes, back
 *                             to Game Vault, which says the time is over. The resume save stays for next time. */

#include <math.h>
#include <string/stdstring.h>
#include "../command.h"
#include "../gfx/video_driver.h"
#include "../gfx/video_shader_parse.h"

static std::string s_gv_style;     /* style= of the last link */
/* fsr=1: Sharper picture (FSR), sent for 3D systems only (assets\gv-shaders\fsr.slangp) */
static bool s_gv_fsr;
/* fsrs=<percent>: Game Vault's FSR strength (10..100), 0 = the preset's own sharpness */
static int s_gv_fsr_pct;
static std::string s_gv_frame;     /* frame= */
static bool s_gv_integer;          /* integer=1: Pixel perfect (whole-number scaling), for this game only */
static std::string s_gv_seats;     /* seats= */
static long long s_gv_kidend;      /* kidend=, 0 = none */
static bool s_gv_resume_now;       /* ResumeTick: save now (the play time is over) */
static bool s_gv_keep_resume;      /* Uninitialize: keep the resume save (closed by Kid mode, not by the player) */

static void GvPlayReset(void)
{
   s_gv_style.clear();
   s_gv_fsr = false;
   s_gv_fsr_pct = 0;
   s_gv_frame.clear();
   s_gv_integer = false;
   s_gv_seats.clear();
   s_gv_kidend = 0;
}

/* a value of the link; true when it was one of these */
static bool GvPlayArg(const winrt::hstring &name, const winrt::hstring &value)
{
   std::string v = winrt::to_string(value);
   if (name == L"style")
   {
      if (v == "crt" || v == "tv" || v == "pro" || v == "curve" || v == "lcd" || v == "sharp" || v == "none")   /* "fsr" only through fsr=1 */
         s_gv_style = v;
      return true;
   }
   if (name == L"fsr")
   {
      s_gv_fsr = v == "1";
      return true;
   }
   if (name == L"fsrs")
   {
      int pct = atoi(v.c_str());
      s_gv_fsr_pct = pct > 0 && pct <= 100 ? pct : 0;
      return true;
   }
   if (name == L"integer")
   {
      s_gv_integer = v == "1";
      return true;
   }
   if (name == L"frame")
   {
      if (v == "tv" || v == "arcade" || v == "handheld")
         s_gv_frame = v;
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
   /* Sharper picture (FSR) wins over "none" (a 3D system's Auto style); a style the player picked (CRT on
    * every system) stays */
   if (s_gv_fsr && (s_gv_style.empty() || s_gv_style == "none"))
      s_gv_style = "fsr";
   if (s_gv_style.empty())
      return;
   settings_t *settings = config_get_ptr();
   if (s_gv_style == "none")
   {
      if (video_shader_apply_shader(settings, RARCH_SHADER_NONE, NULL, false))
         RARCH_LOG("[Game Vault] Screen style: none.\n");
      return;
   }
   /* Cores that draw with OpenGL (N64 Mupen64Plus-Next, PS1 Beetle HW) run on RetroArch's "gl" driver, which takes
    * GLSL shaders, not slang: FSR comes as fsr.glslp for them (the other styles exist only as slang) */
   const bool glsl  = s_gv_style == "fsr" && string_is_equal(video_driver_get_ident(), "gl");
   const char *ext  = glsl ? ".glslp" : ".slangp";
   std::string preset = winrt::to_string(winrt::Windows::ApplicationModel::Package::Current()
         .InstalledLocation().Path()) + "\\assets\\gv-shaders\\" + s_gv_style + ext;
   if (s_gv_style == "fsr" && s_gv_fsr_pct)
   {
      /* Game Vault's FSR strength: a preset in LocalState that points at ours and changes only its Sharpness
       * (percent, the same number; the shader gives RCAS (100 - %) / 50). Quick Menu > Sharpness changes it live. */
      std::string own = LocalStateDir() + "\\config\\gv-fsr" + ext;
      if (FILE *f = fopen(own.c_str(), "w"))
      {
         fprintf(f, "#reference \"%s\"\nGV_SHARPNESS = \"%d.0\"\n", preset.c_str(), s_gv_fsr_pct);
         fclose(f);
         preset = own;
         RARCH_LOG("[Game Vault] FSR strength %d%%.\n", s_gv_fsr_pct);
      }
   }
   configuration_set_bool(settings, settings->bools.video_shader_enable, true);
   if (video_shader_apply_shader(settings, glsl ? RARCH_SHADER_GLSL : RARCH_SHADER_SLANG, preset.c_str(), false))
      RARCH_LOG("[Game Vault] Screen style: %s.\n", s_gv_style.c_str());
   else
      RARCH_LOG("[Game Vault] Screen style: %s could not be used with this video driver.\n", s_gv_style.c_str());
}

/* Screen frame. RetroArch keeps overlay settings in its config, so the player's own are written down the first time
 * a frame goes on (config\gv-frame.txt) and put back when a game starts with no frame. */
static std::string GvFrameFile(void) { return LocalStateDir() + "\\config\\gv-frame.txt"; }

static void GvFrameOverlaySettings(settings_t *s, bool to_file, FILE *f, const std::string *from)
{
   struct { bool *v; } b[] = {{&s->bools.input_overlay_enable}, {&s->bools.input_overlay_hide_when_gamepad_connected},
      {&s->bools.input_overlay_auto_scale}, {&s->bools.input_overlay_auto_rotate}};
   struct { float *v; } n[] = {{&s->floats.input_overlay_opacity}, {&s->floats.input_overlay_scale_landscape},
      {&s->floats.input_overlay_aspect_adjust_landscape}, {&s->floats.input_overlay_x_separation_landscape},
      {&s->floats.input_overlay_y_separation_landscape}, {&s->floats.input_overlay_x_offset_landscape},
      {&s->floats.input_overlay_y_offset_landscape}};
   size_t at = 0;
   auto line = [&](std::string &out) {
      size_t end = from->find('\n', at);
      out = from->substr(at, end == std::string::npos ? std::string::npos : end - at);
      at  = end == std::string::npos ? from->size() : end + 1;
   };
   std::string v;
   for (auto &x : b)
   {
      if (to_file)
         fprintf(f, "%d\n", *x.v ? 1 : 0);
      else
      {
         line(v);
         *x.v = v == "1";
      }
   }
   for (auto &x : n)
   {
      if (to_file)
         fprintf(f, "%.6f\n", *x.v);
      else
      {
         line(v);
         *x.v = (float)atof(v.c_str());
      }
   }
   unsigned *inputs = &s->uints.input_overlay_show_inputs;
   if (to_file)
      fprintf(f, "%u\n%s\n", *inputs, s->paths.path_overlay);
   else
   {
      line(v);
      *inputs = (unsigned)atoi(v.c_str());
      line(v);
      strlcpy(s->paths.path_overlay, v.c_str(), sizeof(s->paths.path_overlay));
   }
}

/* the picture for a game of this shape: "43" and so on, or empty when none fits (widescreen, a dual-screen layout).
 * A small difference only leaves a thin black edge or hides a thin edge, as a TV's bezel does. */
static std::string GvFrameShape(float aspect)
{
   static const struct { const char *name; float a; } kShapes[] = {{"43", 4.0f / 3.0f}, {"34", 3.0f / 4.0f},
      {"109", 10.0f / 9.0f}, {"32", 3.0f / 2.0f}};
   std::string best;
   float off = 0.06f;
   for (auto &s : kShapes)
   {
      float d = fabsf(aspect / s.a - 1.0f);
      if (d < off)
      {
         off  = d;
         best = s.name;
      }
   }
   return best;
}

/* Pixel perfect (Game Vault Options > Look & sound): RetroArch's integer scaling for the game. The player's own
 * value is kept in config\gv-integer.txt while ours is on and put back when Game Vault no longer asks for it. */
static std::string GvIntegerFile(void) { return LocalStateDir() + "\\config\\gv-integer.txt"; }

static void GvApplyInteger(void)
{
   settings_t *settings = config_get_ptr();
   WIN32_FILE_ATTRIBUTE_DATA d;
   bool ours = GetFileAttributesExFromAppW(winrt::to_hstring(GvIntegerFile()).c_str(), GetFileExInfoStandard, &d) != 0;
   if (s_gv_integer)
   {
      if (!ours)
         if (FILE *f = fopen(GvIntegerFile().c_str(), "wb"))
         {
            fputs(settings->bools.video_scale_integer ? "1" : "0", f);
            fclose(f);
         }
      if (!settings->bools.video_scale_integer)
      {
         configuration_set_bool(settings, settings->bools.video_scale_integer, true);
         command_event(CMD_EVENT_VIDEO_APPLY_STATE_CHANGES, NULL);
      }
      RARCH_LOG("[Game Vault] Pixel perfect: on.\n");
      return;
   }
   if (!ours)
      return;
   bool was = ReadSmallFile(GvIntegerFile()).rfind("1", 0) == 0;
   DeleteFileFromAppW(winrt::to_hstring(GvIntegerFile()).c_str());
   if (settings->bools.video_scale_integer != was)
   {
      configuration_set_bool(settings, settings->bools.video_scale_integer, was);
      command_event(CMD_EVENT_VIDEO_APPLY_STATE_CHANGES, NULL);
   }
   RARCH_LOG("[Game Vault] Pixel perfect: off, the player's own setting back.\n");
}

static void GvApplyFrame(void)
{
   settings_t *settings = config_get_ptr();
   WIN32_FILE_ATTRIBUTE_DATA d;
   bool ours = GetFileAttributesExFromAppW(winrt::to_hstring(GvFrameFile()).c_str(), GetFileExInfoStandard, &d) != 0;
   float aspect = video_driver_get_aspect_ratio();
   /* a frame is drawn for the picture filling the screen's height; with Pixel perfect the picture is smaller and
    * would sit inside the frame's window with a gap, so no frame then */
   std::string shape = s_gv_frame.empty() || s_gv_integer ? std::string() : GvFrameShape(aspect);
   if (shape.empty())
   {
      if (!ours)
         return;
      std::string was = ReadSmallFile(GvFrameFile());
      GvFrameOverlaySettings(settings, false, NULL, &was);
      DeleteFileFromAppW(winrt::to_hstring(GvFrameFile()).c_str());
      command_event(CMD_EVENT_OVERLAY_UNLOAD, NULL);
      if (settings->bools.input_overlay_enable)
         command_event(CMD_EVENT_OVERLAY_INIT, NULL);
      RARCH_LOG("[Game Vault] Screen frame: none (game shape %.3f), overlay settings back.\n", aspect);
      return;
   }
   if (!ours)
      if (FILE *f = fopen(GvFrameFile().c_str(), "wb"))
      {
         GvFrameOverlaySettings(settings, true, f, NULL);
         fclose(f);
      }
   std::string cfg = winrt::to_string(winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path()) +
         "\\assets\\gv-frames\\" + s_gv_frame + "-" + shape + ".cfg";
   strlcpy(settings->paths.path_overlay, cfg.c_str(), sizeof(settings->paths.path_overlay));
   settings->bools.input_overlay_enable                      = true;
   settings->bools.input_overlay_hide_when_gamepad_connected = false;
   settings->bools.input_overlay_auto_scale                  = false;
   settings->bools.input_overlay_auto_rotate                 = false;
   settings->floats.input_overlay_opacity                    = 1.0f;
   settings->floats.input_overlay_scale_landscape            = 1.0f;
   settings->floats.input_overlay_aspect_adjust_landscape    = 0.0f;
   settings->floats.input_overlay_x_separation_landscape     = 0.0f;
   settings->floats.input_overlay_y_separation_landscape     = 0.0f;
   settings->floats.input_overlay_x_offset_landscape         = 0.0f;
   settings->floats.input_overlay_y_offset_landscape         = 0.0f;
   settings->uints.input_overlay_show_inputs                 = 0;
   command_event(CMD_EVENT_OVERLAY_INIT, NULL);
   RARCH_LOG("[Game Vault] Screen frame: %s-%s (game shape %.3f).\n", s_gv_frame.c_str(), shape.c_str(), aspect);
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
      GvApplyInteger();
      GvApplyFrame();
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
