/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0 like the rest of RetroArch; this notice stays in every copy.
 *
 * Game Vault: RetroAchievements without typing anything in RetroArch. The player signs in once in Game Vault
 * (Options > Achievements); Game Vault keeps the RetroAchievements login token (never the password) and, at each
 * start, hands it over as "cheevos": a one-time file in the folder only Game Vault and its emulators can open, one
 * line "user<tab>token<tab>hardcore(0/1)<tab>enable(0/1)". It is read once, written over and deleted, and kept in
 * memory only. While it is in use, retroarch.cfg keeps the player's OWN values for the achievement settings (the
 * file is copied to the games drive), so the token never lands in a file. Included by uwp_main.cpp. */

static std::string s_cheevos_file;
static struct
{
   bool set, applied;
   bool hardcore, enable;
   char user[128], token[128];
   /* the player's own values, written to retroarch.cfg instead of ours */
   bool own_enable, own_hardcore;
   char own_user[128], own_token[128], own_password[128];
} s_gvch;

static void ReadCheevosCred(void)
{
   if (s_cheevos_file.empty())
      return;
   std::wstring file = winrt::to_hstring(s_cheevos_file).c_str();
   s_cheevos_file.clear();
   /* only Game Vault's own one-time files (see ReadNetCred) */
   {
      std::wstring lower = file;
      for (auto &c : lower)
         c = towlower(c);
      size_t name = lower.find_last_of(L'\\');
      if (lower.find(L"..") != std::wstring::npos || lower.find(L'/') != std::wstring::npos ||
          name == std::wstring::npos || name < 6 || lower.compare(name - 6, 6, L"\\vault") ||
          lower.compare(name + 1, 8, L"cheevos-") || lower.size() < 4 ||
          lower.compare(lower.size() - 4, 4, L".txt") ||
          lower.find(L"\\publishers\\") == std::wstring::npos)
      {
         RARCH_WARN("[Game Vault] Achievements sign-in file refused (not Game Vault's).\n");
         return;
      }
   }
   HANDLE h = CreateFile2FromAppW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
   if (h == INVALID_HANDLE_VALUE)
   {
      RARCH_WARN("[Game Vault] Achievements sign-in file could not be opened.\n");
      return;
   }
   char text[600] = {0};
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

   char field[4][128];
   memset(field, 0, sizeof(field));
   const char *p = text;
   for (int i = 0; i < 4 && *p; i++)
   {
      const char *stop = i < 3 ? "\t\r\n" : "\r\n";
      size_t n = strcspn(p, stop);
      memcpy(field[i], p, n > 127 ? 127 : n);
      p += n;
      if (*p == '\t')
         p++;
      else
         break;
   }
   SecureZeroMemory(text, sizeof(text));
   bool ok = field[0][0] && field[1][0];
   if (!ok)
   {
      SecureZeroMemory(field, sizeof(field));
      return;
   }
   strlcpy(s_gvch.user, field[0], sizeof(s_gvch.user));
   strlcpy(s_gvch.token, field[1], sizeof(s_gvch.token));
   s_gvch.hardcore = field[2][0] == '1';
   s_gvch.enable   = field[3][0] != '0';
   SecureZeroMemory(field, sizeof(field));
   s_gvch.set = true;
   RARCH_LOG("[Game Vault] Achievements: signed in by Game Vault (%s mode).\n",
         s_gvch.hardcore ? "hardcore" : "softcore");
}

/* a new start from Game Vault: forget the last one's account (the new link may have none: the player signed out),
 * and give the running settings the player's own values back */
static void GvCheevosReset(void)
{
   if (s_gvch.applied)
   {
      settings_t *settings = config_get_ptr();
      if (settings)
      {
         settings->bools.cheevos_enable               = s_gvch.own_enable;
         settings->bools.cheevos_hardcore_mode_enable = s_gvch.own_hardcore;
         strlcpy(settings->arrays.cheevos_username, s_gvch.own_user, sizeof(settings->arrays.cheevos_username));
         strlcpy(settings->arrays.cheevos_token, s_gvch.own_token, sizeof(settings->arrays.cheevos_token));
         strlcpy(settings->arrays.cheevos_password, s_gvch.own_password, sizeof(settings->arrays.cheevos_password));
      }
   }
   SecureZeroMemory(&s_gvch, sizeof(s_gvch));
}

/* cheevos.c, at the start of loading a game: put Game Vault's account into the running settings */
extern "C" void uwp_gamevault_cheevos_apply(settings_t *settings)
{
   if (!s_gvch.set || !settings)
      return;
   if (!s_gvch.applied)
   {
      s_gvch.own_enable   = settings->bools.cheevos_enable;
      s_gvch.own_hardcore = settings->bools.cheevos_hardcore_mode_enable;
      strlcpy(s_gvch.own_user, settings->arrays.cheevos_username, sizeof(s_gvch.own_user));
      strlcpy(s_gvch.own_token, settings->arrays.cheevos_token, sizeof(s_gvch.own_token));
      strlcpy(s_gvch.own_password, settings->arrays.cheevos_password, sizeof(s_gvch.own_password));
      s_gvch.applied = true;
   }
   settings->bools.cheevos_enable               = s_gvch.enable;
   settings->bools.cheevos_hardcore_mode_enable = s_gvch.hardcore;
   strlcpy(settings->arrays.cheevos_username, s_gvch.user, sizeof(settings->arrays.cheevos_username));
   /* a token RetroArch got itself for this same account (login callback) is as good */
   if (strcmp(settings->arrays.cheevos_token, s_gvch.token))
      strlcpy(settings->arrays.cheevos_token, s_gvch.token, sizeof(settings->arrays.cheevos_token));
   settings->arrays.cheevos_password[0] = '\0';
}

/* configuration.c, while writing retroarch.cfg: the player's own value for an achievement setting we changed, or
 * NULL to write the running value */
extern "C" const char *uwp_gamevault_cheevos_saved_string(const char *ident)
{
   if (!s_gvch.applied || !ident)
      return NULL;
   if (!strcmp(ident, "cheevos_username"))
      return s_gvch.own_user;
   if (!strcmp(ident, "cheevos_token"))
      return s_gvch.own_token;
   if (!strcmp(ident, "cheevos_password"))
      return s_gvch.own_password;
   return NULL;
}

/* the same for the two switches: 0/1, or -1 to write the running value */
extern "C" int uwp_gamevault_cheevos_saved_bool(const char *ident)
{
   if (!s_gvch.applied || !ident)
      return -1;
   if (!strcmp(ident, "cheevos_enable"))
      return s_gvch.own_enable ? 1 : 0;
   if (!strcmp(ident, "cheevos_hardcore_mode_enable"))
      return s_gvch.own_hardcore ? 1 : 0;
   return -1;
}
