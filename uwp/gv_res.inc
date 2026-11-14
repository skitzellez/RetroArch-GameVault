/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0 like the rest of RetroArch; this notice stays in every copy.
 *
 * Game Vault: the picture size of 3D games, set once in Game Vault (Options > Look & sound > Resolution) instead of in
 * each core's own options (players asked 11-14: "the video options 2x 3x seem to be missing"). The link carries
 * "res=1|2|3|4" (1 original, 2 HD 720p, 3 Full HD 1080p, 4 4K) and "n64wide=0|1"; older Game Vault sends "n64res=1|2|4"
 * (N64 only). Written into the core's options file before the game loads; a link without them leaves the files alone
 * (the player's own choice in RetroArch's menu stays). Included by uwp_main.cpp. */

static std::string s_gvres, s_n64res, s_n64wide;   /* res= (PS1, 3DS, 3DO); n64res= / n64wide= (N64, its own setting) */

/* rewrites "key = value" lines of one core options file (config\<dir>\<file>), keeping every other line */
static void GvSetCoreOptions(const char *dir_name, const char *file_name,
      const std::vector<std::pair<std::string, std::string>> &set)
{
   if (set.empty())
      return;
   const std::string dir  = LocalStateDir() + "\\config\\" + dir_name;
   const std::string path = dir + "\\" + file_name;
   CreateDirectoryFromAppW(winrt::to_hstring(LocalStateDir() + "\\config").c_str(), NULL);
   CreateDirectoryFromAppW(winrt::to_hstring(dir).c_str(), NULL);
   std::string text = ReadSmallFile(path), out;
   for (size_t at = 0; at < text.size();)
   {
      size_t end       = text.find('\n', at);
      end              = end == std::string::npos ? text.size() : end + 1;
      std::string line = text.substr(at, end - at);
      bool ours        = false;
      for (const auto &kv : set)
         ours |= line.compare(0, kv.first.size(), kv.first) == 0 && line.size() > kv.first.size() &&
               (line[kv.first.size()] == ' ' || line[kv.first.size()] == '=');
      if (!ours)
         out += line;
      at = end;
   }
   if (!out.empty() && out.back() != '\n')
      out += "\n";
   for (const auto &kv : set)
      out += kv.first + " = \"" + kv.second + "\"\n";
   if (FILE *f = fopen(path.c_str(), "wb"))
   {
      fwrite(out.data(), 1, out.size(), f);
      fclose(f);
   }
}

static void GvResolutionOptions(const std::vector<std::string> &args)
{
   if (s_gvres.empty() && s_n64res.empty() && s_n64wide.empty())
      return;
   std::string core;
   for (size_t i = 0; i + 1 < args.size(); i++)
      if (args[i] == "-L")
         core = args[i + 1];
   for (auto &c : core)
      c = (char)tolower((unsigned char)c);
   const int r = s_gvres.empty() ? 0 : atoi(s_gvres.c_str());   /* 0: size not sent */
   auto pick   = [r](const char *a, const char *b, const char *c, const char *d) { return r == 1 ? a : r == 2 ? b : r == 3 ? c : d; };
   std::vector<std::pair<std::string, std::string>> set;

   if (core.find("mupen64plus") != std::string::npos)
   {
      if (!s_n64res.empty())   /* 1 = the core's own 640x480, 2 = 1280x960 (1920x1080 wide), 4 = 2880x2160 (3840x2160 wide) */
      {
         const bool two = s_n64res == "2", four = s_n64res == "4";
         set.push_back({"mupen64plus-EnableNativeResFactor", "0"});   /* else it would win over the sizes */
         set.push_back({"mupen64plus-43screensize", four ? "2880x2160" : two ? "1280x960" : "640x480"});
         set.push_back({"mupen64plus-169screensize", four ? "3840x2160" : two ? "1920x1080" : "960x540"});
      }
      if (!s_n64wide.empty())   /* GLideN64's "16:9 adjusted": the 3D view is widened, not stretched */
         set.push_back({"mupen64plus-aspect", s_n64wide == "1" ? "16:9 adjusted" : "4:3"});
      GvSetCoreOptions("Mupen64Plus-Next", "Mupen64Plus-Next.opt", set);
   }
   else if (!r)
      return;
   else if (core.find("swanstation") != std::string::npos)   /* PS1: 240p x scale (9x is about 4K) */
   {
      set.push_back({"swanstation_GPU_ResolutionScale", pick("1", "3", "5", "9")});
      GvSetCoreOptions("SwanStation", "SwanStation.opt", set);
   }
   else if (core.find("mednafen_psx_hw") != std::string::npos)
   {
      set.push_back({"beetle_psx_hw_internal_resolution", pick("1x(native)", "2x", "4x", "8x")});
      GvSetCoreOptions("Beetle PSX HW", "Beetle PSX HW.opt", set);
   }
   else if (core.find("azahar") != std::string::npos)   /* 3DS: 400x240 x factor */
   {
      set.push_back({"citra_resolution_factor", pick("1", "3", "4", "9")});
      GvSetCoreOptions("Azahar", "Azahar.opt", set);
   }
   else if (core.find("opera") != std::string::npos)   /* 3DO: only a 2x mode */
   {
      set.push_back({"opera_high_resolution", r >= 2 ? "enabled" : "disabled"});
      GvSetCoreOptions("Opera", "Opera.opt", set);
   }
   if (!set.empty())
      RARCH_LOG("[Game Vault] Resolution %d for %s%s\n", r, core.c_str(),
            s_n64wide.empty() ? "" : (s_n64wide == "1" ? ", N64 wide" : ", N64 4:3"));
}

/* the link's res= / n64wide= (and the older n64res=); true when the argument was one of them */
static bool GvResolutionArg(const winrt::hstring &name, const winrt::hstring &value)
{
   if (name == L"res" && (value == L"1" || value == L"2" || value == L"3" || value == L"4"))
      s_gvres = winrt::to_string(value);
   else if (name == L"n64res" && (value == L"1" || value == L"2" || value == L"4"))
      s_n64res = winrt::to_string(value);
   else if (name == L"n64wide" && (value == L"0" || value == L"1"))
      s_n64wide = winrt::to_string(value);
   else
      return false;
   return true;
}
