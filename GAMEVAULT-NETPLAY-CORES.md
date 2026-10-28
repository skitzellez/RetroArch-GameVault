# Online cores in RetroArch for Game Vault

RetroArch for Game Vault 1.22.2.29 and newer carry extra builds of the cores below, in `cores
etplay\<commit>\`,
so that online rooms opened by RetroArch on PCs and other devices can be joined: both players need the same build of
a core. Each one is built with MinGW-w64 from the unchanged source of that core at that commit. For mGBA 7a12d6d the
version text is set to `0.11-212-7a12d6d`, the text libretro's own build shows. Each core keeps its own licence; its
source is at the link.

| Core | Commit | Source |
|---|---|---|
| snes9x | 5a40cd5 | https://github.com/libretro/snes9x/tree/5a40cd5 |
| snes9x | fae2fea | https://github.com/libretro/snes9x/tree/fae2fea |
| fceumm | 5cd4a43 | https://github.com/libretro/libretro-fceumm/tree/5cd4a43 |
| fceumm | ac0d00a | https://github.com/libretro/libretro-fceumm/tree/ac0d00a |
| nestopia | 5deada5 | https://github.com/libretro/nestopia/tree/5deada5 |
| nestopia | 06a8949 | https://github.com/libretro/nestopia/tree/06a8949 |
| gambatte | 6924c76 | https://github.com/libretro/gambatte-libretro/tree/6924c76 |
| gambatte | d9d6cd0 | https://github.com/libretro/gambatte-libretro/tree/d9d6cd0 |
| mgba | c758314 | https://github.com/libretro/mgba/tree/c758314 |
| mgba | 7a12d6d | https://github.com/libretro/mgba/tree/7a12d6d (CMake build: LIBMGBA_ONLY, BUILD_LIBRETRO) |
| gpsp | 74db5e5 | https://github.com/libretro/gpsp/tree/74db5e5 |
| gpsp | 5819380 | https://github.com/libretro/gpsp/tree/5819380 |
| genesis_plus_gx | 46a5521 | https://github.com/libretro/Genesis-Plus-GX/tree/46a5521 |
| genesis_plus_gx | aa74d79 | https://github.com/libretro/Genesis-Plus-GX/tree/aa74d79 |
| picodrive | 046e5ff | https://github.com/libretro/picodrive/tree/046e5ff |
| picodrive | 1890c29 | https://github.com/libretro/picodrive/tree/1890c29 |
| fbneo | 1a03a15 | https://github.com/libretro/FBNeo/tree/1a03a15 |
| stella | 7c4e3b2 | https://github.com/stella-emu/stella/tree/7c4e3b2 |
| stella | 2c3d702 | https://github.com/stella-emu/stella/tree/2c3d702 |
| stella | 9bbd209 | libretro buildbot build of https://github.com/stella-emu/stella/tree/9bbd209 (the default before 1.22.2.77) |
| prosystem | acae250 | https://github.com/libretro/prosystem-libretro/tree/acae250 |
| prosystem | 8a88014 | https://github.com/libretro/prosystem-libretro/tree/8a88014 |
| mednafen_pce_fast | 7e9b257 | https://github.com/libretro/beetle-pce-fast-libretro/tree/7e9b257 |
| handy | fca2392 | https://github.com/libretro/libretro-handy/tree/fca2392 |
| mednafen_ngp | 139fe34 | https://github.com/libretro/beetle-ngp-libretro/tree/139fe34 |
| mednafen_wswan | 2aeb47d | https://github.com/libretro/beetle-wswan-libretro/tree/2aeb47d |

## Default cores changed in 1.22.2.77

- Stella: the default `cores\stella_libretro.dll` is now the 2c3d702 build above (the 9bbd209 build sent a padded
  ROM for games on a network share); 9bbd209 moved to `cores\netplay\9bbd209\` for online rooms.
- Azahar: `cores\azahar_libretro.dll` is the official Azahar 2126.2 Windows x86-64 libretro build
  (`azahar-libretro-windows-x86_64-2126.2.zip`, https://github.com/azahar-emu/azahar/tree/2126.2, GPL-2.0), a security update.
- Everything else in 1.22.2.77 is the 1.22.2.51 package unchanged.
