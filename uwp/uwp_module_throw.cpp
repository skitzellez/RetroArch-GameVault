/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like RetroArch.
 *
 * C++ throw for this module. The runtime's own _CxxThrowException asks Windows (RtlPcToFileHeader) which module
 * the thrown type lives in; for code Windows did not map itself that answer is 0 and the throw crashes. This copy
 * names this module directly, which is the same answer when Windows did map it.
 */
#include <windows.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

extern "C" __declspec(noreturn) void __stdcall _CxxThrowException(void *object, _ThrowInfo *info) noexcept(false)
{
   const DWORD cpp_exception = 0xE06D7363; /* 'msc' */
   ULONG_PTR magic           = 0x19930520; /* EH_MAGIC_NUMBER1 */
   if (info && (*reinterpret_cast<const DWORD*>(static_cast<const void*>(info)) & 0x8)) /* TI_IsPure */
      magic = 0x01994000;                  /* EH_PURE_MAGIC_NUMBER1 */
   const ULONG_PTR params[4] = { magic, reinterpret_cast<ULONG_PTR>(object),
      reinterpret_cast<ULONG_PTR>(info), reinterpret_cast<ULONG_PTR>(&__ImageBase) };
   RaiseException(cpp_exception, EXCEPTION_NONCONTINUABLE, 4, params);
   __assume(0);
}
