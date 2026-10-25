<!-- Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault -->
# Cores inside RetroArch for Game Vault (Android)

RetroArch for Game Vault for Android carries these libretro cores unchanged, as built by the libretro buildbot
(`https://buildbot.libretro.com/nightly/android/latest/arm64-v8a/`). Each one keeps its own licence; the source of each
is at the address below. The menu art, controller profiles, touch overlays and core info come from the official
RetroArch 1.22.2 APK; the system files for Dolphin, PPSSPP and blueMSX come from
`https://buildbot.libretro.com/assets/system/`. ARMSX2's resources (game database, shaders, fonts) come from its
source (`bin/resources`, GPLv3), with the free PS2 BIOS OpenPS2BIOS 0.2 (MIT,
https://github.com/TamaribaEmu/OpenPS2BIOS) so PlayStation 2 games start without a BIOS of the player's own;
a BIOS the player adds to `system/pcsx2/bios` is used instead. RetroArch for Game Vault is free and never sold.

| Core | File | Licence | Source |
|---|---|---|---|
| ARMSX2 (PlayStation 2, ARM64 JIT) | `armsx2_libretro_android.so` | GPLv3 | https://github.com/ARMSX2/ARMSX2 |
| Dolphin | `dolphin_libretro_android.so` | GPLv2+ | https://github.com/libretro/dolphin |
| Citra | `citra_libretro_android.so` | GPLv2+ | https://github.com/libretro/citra |
| PCSX-ReARMed | `pcsx_rearmed_libretro_android.so` | GPLv2 | https://github.com/libretro/pcsx_rearmed |
| PPSSPP | `ppsspp_libretro_android.so` | GPLv2 | https://github.com/hrydgard/ppsspp |
| Flycast | `flycast_libretro_android.so` | GPLv2 | https://github.com/flyinghead/flycast |
| Beetle Saturn | `mednafen_saturn_libretro_android.so` | GPLv2 | https://github.com/libretro/beetle-saturn-libretro |
| YabaSanshiro | `yabasanshiro_libretro_android.so` | GPLv2 | https://github.com/libretro/yabause (branch yabasanshiro) |
| Genesis Plus GX | `genesis_plus_gx_libretro_android.so` | Non-commercial | https://github.com/libretro/Genesis-Plus-GX |
| melonDS DS | `melondsds_libretro_android.so` | GPLv3+ | https://github.com/JesseTG/melonds-ds |
| Mupen64Plus-Next | `mupen64plus_next_gles3_libretro_android.so` | GPLv2 | https://github.com/libretro/mupen64plus-libretro-nx |
| mGBA | `mgba_libretro_android.so` | MPLv2.0 | https://github.com/libretro/mgba |
| Snes9x | `snes9x_libretro_android.so` | Non-commercial | https://github.com/libretro/snes9x |
| Mesen | `mesen_libretro_android.so` | GPLv3 | https://github.com/libretro/Mesen |
| FCEUmm (NES online: the build Xbox and PC rooms use) | `fceumm_libretro_android.so` | GPLv2 | https://github.com/libretro/libretro-fceumm |
| Gambatte | `gambatte_libretro_android.so` | GPLv2 | https://github.com/libretro/gambatte-libretro |
| Beetle VB | `mednafen_vb_libretro_android.so` | GPLv2 | https://github.com/libretro/beetle-vb-libretro |
| PokeMini | `pokemini_libretro_android.so` | GPLv3 | https://github.com/libretro/PokeMini |
| PicoDrive | `picodrive_libretro_android.so` | MAME | https://github.com/libretro/picodrive |
| Beetle PCE Fast | `mednafen_pce_fast_libretro_android.so` | GPLv2 | https://github.com/libretro/beetle-pce-fast-libretro |
| FinalBurn Neo | `fbneo_libretro_android.so` | Non-commercial | https://github.com/libretro/FBNeo |
| Beetle NeoPop | `mednafen_ngp_libretro_android.so` | GPLv2 | https://github.com/libretro/beetle-ngp-libretro |
| Beetle WonderSwan | `mednafen_wswan_libretro_android.so` | GPLv2 | https://github.com/libretro/beetle-wswan-libretro |
| Stella | `stella_libretro_android.so` | GPLv2 | https://github.com/stella-emu/stella |
| a5200 | `a5200_libretro_android.so` | GPLv2 | https://github.com/libretro/a5200 |
| ProSystem | `prosystem_libretro_android.so` | GPLv2 | https://github.com/libretro/prosystem-libretro |
| Handy | `handy_libretro_android.so` | Zlib | https://github.com/libretro/libretro-handy |
| Virtual Jaguar | `virtualjaguar_libretro_android.so` | GPLv3 | https://github.com/libretro/virtualjaguar-libretro |
| Hatari | `hatari_libretro_android.so` | GPLv2 | https://github.com/libretro/hatari |
| Gearcoleco | `gearcoleco_libretro_android.so` | GPLv3 | https://github.com/drhelius/Gearcoleco |
| FreeIntv | `freeintv_libretro_android.so` | GPLv3 | https://github.com/libretro/FreeIntv |
| vecx | `vecx_libretro_android.so` | GPLv3 | https://github.com/libretro/libretro-vecx |
| FreeChaF | `freechaf_libretro_android.so` | GPLv3 | https://github.com/libretro/FreeChaF |
| Potator | `potator_libretro_android.so` | Public Domain | https://github.com/libretro/potator |
| blueMSX | `bluemsx_libretro_android.so` | GPLv2 | https://github.com/libretro/blueMSX-libretro |
| VICE x64sc | `vice_x64sc_libretro_android.so` | GPLv2 | https://github.com/libretro/vice-libretro |
| PUAE | `puae_libretro_android.so` | GPLv2 | https://github.com/libretro/libretro-uae |
| Fuse | `fuse_libretro_android.so` | GPLv3 | https://github.com/libretro/fuse-libretro |
| TIC-80 | `tic80_libretro_android.so` | MIT | https://github.com/nesbox/TIC-80 |
| uzem | `uzem_libretro_android.so` | MIT | https://github.com/libretro/libretro-uzem |
| GW | `gw_libretro_android.so` | zlib | https://github.com/libretro/gw-libretro |
| DOSBox Pure | `dosbox_pure_libretro_android.so` | GPLv2 | https://github.com/schellingb/dosbox-pure |
