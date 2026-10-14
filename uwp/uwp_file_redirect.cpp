/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like RetroArch.
 *
 * Emulator cores open game files with the C library (fopen and friends), which ends in CreateFileW.
 * In a UWP app that direct call only works where the app container itself has rights: on an NTFS drive
 * that was given "ALL APPLICATION PACKAGES" access. On a FAT32 or exFAT stick, or an NTFS one that was
 * not prepared, it fails with "access denied" although the user allowed file system access
 * (broadFileSystemAccess): that permission is honoured only by the *FromApp functions, which go through
 * the file broker when the direct call is refused and are otherwise the same call.
 *
 * So the import tables of the C runtime and of every core that is loaded are pointed at the FromApp
 * versions. They take the same arguments, and where the direct call is allowed they behave exactly as
 * before. Then any USB drive works for every core, as it already did for Game Vault and xemu.
 */
#include <windows.h>
#include <fileapifromapp.h>
#include <memoryapi.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <winrt/Windows.Storage.h>

extern "C" void RARCH_LOG_uwp_redirect(const char *what, unsigned count);

namespace {

/* "\\?\F:\..." (the long-path form some cores use) is refused by the broker; the plain path works */
static LPCWSTR Plain(LPCWSTR name)
{
   if (name && name[0] == L'\\' && name[1] == L'\\' && name[2] == L'?' && name[3] == L'\\' && name[4] &&
         name[5] == L':')
      return name + 4;
   return name;
}

DWORD WINAPI GetFileAttributesW_FromApp(LPCWSTR name)
{
   WIN32_FILE_ATTRIBUTE_DATA data;
   if (!GetFileAttributesExFromAppW(Plain(name), GetFileExInfoStandard, &data))
      return INVALID_FILE_ATTRIBUTES;
   return data.dwFileAttributes;
}

wchar_t *Widen(LPCSTR s)
{
   if (!s)
      return NULL;
   int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
   wchar_t *w = (wchar_t*)malloc(n * sizeof(wchar_t));
   if (w)
      MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
   return w;
}

/* CreateFileFromAppW, and when the file broker refuses the call's extras (FAT32 / exFAT sticks are opened
 * through the broker, which answers ERROR_INVALID_PARAMETER for an inheritable handle and for some flags; the
 * C runtime passes both, and fopen then fails with errno 22) the same open again without them. */
/* The first failed opens, with path and error, go to LocalState\uwp_redirect.log (for bug reports). */
static void Note(LPCWSTR what, LPCWSTR name, DWORD err)
{
   static LONG told;
   static wchar_t logPath[MAX_PATH + 64];
   if (!name || InterlockedIncrement(&told) > 50)
      return;
   if (!logPath[0])
   {
      try
      {
         std::wstring dir(winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path());
         wcsncpy_s(logPath, (dir + L"\\uwp_redirect.log").c_str(), _TRUNCATE);
      }
      catch (...)
      {
         return;
      }
   }
   HANDLE f = CreateFile2FromAppW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, NULL);
   if (f == INVALID_HANDLE_VALUE)
      return;
   char line[1200];
   int len = snprintf(line, sizeof(line), "%ls failed (error %lu): %ls\r\n", what, err, name);
   DWORD wrote;
   if (len > 0)
      WriteFile(f, line, (DWORD)len, &wrote, NULL);
   CloseHandle(f);
}

static HANDLE CreateFileW_FromApp_Try(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
      DWORD disposition, DWORD flags, HANDLE templ);

HANDLE WINAPI CreateFileW_FromApp(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
      DWORD disposition, DWORD flags, HANDLE templ)
{
   name     = Plain(name);
   HANDLE h = CreateFileW_FromApp_Try(name, access, share, sa, disposition, flags, templ);
   if (h == INVALID_HANDLE_VALUE)
   {
      DWORD err = GetLastError();
      wchar_t what[160];
      swprintf_s(what, L"CreateFileW access %08lx share %lx disposition %lu flags %08lx sa %d", access, share,
            disposition, flags, sa ? 1 : 0);
      Note(what, name, err);
      SetLastError(err);
   }
   return h;
}

static HANDLE CreateFileW_FromApp_Try(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
      DWORD disposition, DWORD flags, HANDLE templ)
{
   HANDLE h = CreateFileFromAppW(name, access, share, sa, disposition, flags, templ);
   if (h != INVALID_HANDLE_VALUE)
      return h;
   /* the broker refuses security attributes (an inheritable handle) with error 4252 or 87: try without */
   if (sa)
   {
      h = CreateFileFromAppW(name, access, share, NULL, disposition, flags, templ);
      if (h != INVALID_HANDLE_VALUE)
         return h;
   }
   if (GetLastError() != ERROR_INVALID_PARAMETER)
      return h;
   h = CreateFileFromAppW(name, access, share, NULL, disposition, flags, templ);
   if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_INVALID_PARAMETER)
      return h;
   /* attributes and the access-pattern hints only */
   DWORD plain = flags & (0xFFFF | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_BACKUP_SEMANTICS);
   return CreateFileFromAppW(name, access, share, NULL, disposition, plain, NULL);
}

HANDLE WINAPI CreateFile2_FromApp(LPCWSTR name, DWORD access, DWORD share, DWORD disposition,
      LPCREATEFILE2_EXTENDED_PARAMETERS ext)
{
   name     = Plain(name);
   HANDLE h = CreateFile2FromAppW(name, access, share, disposition, ext);
   if (h == INVALID_HANDLE_VALUE && ext && ext->lpSecurityAttributes)
   {
      /* the broker refuses security attributes: try without them */
      CREATEFILE2_EXTENDED_PARAMETERS nosa = *ext;
      nosa.lpSecurityAttributes = NULL;
      h = CreateFile2FromAppW(name, access, share, disposition, &nosa);
   }
   if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_INVALID_PARAMETER || !ext)
   {
      if (h == INVALID_HANDLE_VALUE)
      {
         DWORD err = GetLastError();
         Note(L"CreateFile2", name, err);
         SetLastError(err);
      }
      return h;
   }
   CREATEFILE2_EXTENDED_PARAMETERS plain = *ext;
   plain.lpSecurityAttributes = NULL;
   plain.hTemplateFile        = NULL;
   plain.dwSecurityQosFlags   = 0;
   plain.dwFileFlags         &= FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_RANDOM_ACCESS | FILE_FLAG_BACKUP_SEMANTICS;
   return CreateFile2FromAppW(name, access, share, disposition, &plain);
}

HANDLE WINAPI CreateFileA_FromApp(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
      DWORD disposition, DWORD flags, HANDLE templ)
{
   wchar_t *w = Widen(name);
   HANDLE h   = CreateFileW_FromApp(w, access, share, sa, disposition, flags, templ);
   DWORD err  = GetLastError();
   free(w);
   SetLastError(err);
   return h;
}

DWORD WINAPI GetFileAttributesA_FromApp(LPCSTR name)
{
   wchar_t *w = Widen(name);
   DWORD a    = GetFileAttributesW_FromApp(w);
   DWORD err  = GetLastError();
   free(w);
   SetLastError(err);
   return a;
}

BOOL WINAPI GetFileAttributesExW_FromApp(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info)
{
   return GetFileAttributesExFromAppW(Plain(name), level, info);
}

HANDLE WINAPI FindFirstFileExW_FromApp(LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data,
      FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags)
{
   return FindFirstFileExFromAppW(Plain(name), level, data, op, filter, flags);
}

HANDLE WINAPI FindFirstFileW_FromApp(LPCWSTR name, LPWIN32_FIND_DATAW data)
{
   return FindFirstFileExFromAppW(Plain(name), FindExInfoStandard, data, FindExSearchNameMatch, NULL, 0);
}

BOOL WINAPI SetFileAttributesW_FromApp(LPCWSTR name, DWORD attributes)
{
   return SetFileAttributesFromAppW(Plain(name), attributes);
}

/* Saving through a temporary file and renaming it over the old one (save files, states, memory cards, the
 * settings). MoveFileFromAppW never replaces, so an existing target is replaced with ReplaceFileFromAppW, or
 * deleted first when that is refused; another drive means copy and delete, as MOVEFILE_COPY_ALLOWED asks. */
BOOL WINAPI MoveFileExW_FromApp(LPCWSTR from, LPCWSTR to, DWORD flags)
{
   from = Plain(from);
   to   = Plain(to);
   if (!to)
      return DeleteFileFromAppW(from);
   if (MoveFileFromAppW(from, to))
      return TRUE;
   DWORD err = GetLastError();
   if ((flags & MOVEFILE_REPLACE_EXISTING) && (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS))
   {
      if (ReplaceFileFromAppW(to, from, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL))
         return TRUE;
      if (DeleteFileFromAppW(to) && MoveFileFromAppW(from, to))
         return TRUE;
      err = GetLastError();
   }
   else if ((flags & MOVEFILE_COPY_ALLOWED) && err == ERROR_NOT_SAME_DEVICE)
   {
      if (CopyFileFromAppW(from, to, !(flags & MOVEFILE_REPLACE_EXISTING)) && DeleteFileFromAppW(from))
         return TRUE;
      err = GetLastError();
   }
   SetLastError(err);
   return FALSE;
}

BOOL WINAPI MoveFileW_FromApp(LPCWSTR from, LPCWSTR to)
{
   return MoveFileExW_FromApp(from, to, MOVEFILE_COPY_ALLOWED);
}

BOOL WINAPI ReplaceFileW_FromApp(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags,
      LPVOID exclude, LPVOID reserved)
{
   return ReplaceFileFromAppW(Plain(replaced), Plain(replacement), Plain(backup), flags, exclude, reserved);
}

struct Redirect
{
   const char *name;
   void *to;
};

const Redirect kRedirects[] = {
   { "CreateFileW",          (void*)CreateFileW_FromApp },
   { "CreateFile2",          (void*)CreateFile2_FromApp },
   { "CreateFileA",          (void*)CreateFileA_FromApp },
   { "GetFileAttributesExW", (void*)GetFileAttributesExW_FromApp },
   { "GetFileAttributesW",   (void*)GetFileAttributesW_FromApp },
   { "GetFileAttributesA",   (void*)GetFileAttributesA_FromApp },
   { "FindFirstFileExW",     (void*)FindFirstFileExW_FromApp },
   { "FindFirstFileW",       (void*)FindFirstFileW_FromApp },
   { "CreateDirectoryW",     (void*)CreateDirectoryFromAppW },
   { "DeleteFileW",          (void*)DeleteFileFromAppW },
   { "RemoveDirectoryW",     (void*)RemoveDirectoryFromAppW },
   { "SetFileAttributesW",   (void*)SetFileAttributesW_FromApp },
   { "MoveFileExW",          (void*)MoveFileExW_FromApp },
   { "MoveFileW",            (void*)MoveFileW_FromApp },
   { "ReplaceFileW",         (void*)ReplaceFileW_FromApp },
};

} // namespace

/* Points the module's imports of the functions above at the FromApp versions. Returns how many. */
extern "C" unsigned uwp_redirect_file_apis(void *module)
{
   unsigned count = 0;
   BYTE *base     = (BYTE*)module;
   if (!base)
      return 0;
   IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
   if (dos->e_magic != IMAGE_DOS_SIGNATURE)
      return 0;
   IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
   IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
   if (!dir.VirtualAddress)
      return 0;
   for (IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress);
         imp->Name; imp++)
   {
      IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
      IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA*)(base +
            (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
      for (; names->u1.AddressOfData; names++, thunk++)
      {
         if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
            continue;
         const char *fn = (const char*)((IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData))->Name;
         for (const Redirect &r : kRedirects)
         {
            if (strcmp(fn, r.name) || thunk->u1.Function == (ULONG_PTR)r.to)
               continue;
            ULONG old;
            if (VirtualProtectFromApp(&thunk->u1.Function, sizeof(thunk->u1.Function),
                     PAGE_READWRITE, &old))
            {
               thunk->u1.Function = (ULONG_PTR)r.to;
               VirtualProtectFromApp(&thunk->u1.Function, sizeof(thunk->u1.Function), old, &old);
               count++;
            }
            break;
         }
      }
   }
   return count;
}

/* The C runtime (fopen, stat, opendir in every core) and RetroArch itself, once at start-up. */
extern "C" void uwp_redirect_file_apis_startup(void)
{
   static const wchar_t *const modules[] = {
      L"ucrtbase.dll", L"ucrtbased.dll", L"msvcrt.dll", NULL
   };
   unsigned n = uwp_redirect_file_apis(GetModuleHandleW(NULL));
   for (int i = 0; modules[i]; i++)
   {
      HMODULE m = GetModuleHandleW(modules[i]);
      if (m)
         n += uwp_redirect_file_apis(m);
   }
   RARCH_LOG_uwp_redirect("C runtime and RetroArch", n);
}
