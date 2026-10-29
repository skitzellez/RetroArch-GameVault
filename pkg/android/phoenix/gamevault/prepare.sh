#!/bin/sh
# Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
# GPL-3.0-or-later, like RetroArch.
# Fills the Game Vault build's third-party parts (not in git): the cores of cores.txt from the libretro buildbot
# into jniLibs/arm64-v8a/lib<name>_libretro_android.so, and RetroArch's menu art, controller profiles, touch
# overlays, core info and filters from the official RetroArch APK into assets/ (no shaders or database: size), and
# the system files some cores need (Dolphin, PPSSPP, LRPS2, blueMSX) into res/raw/gv_sys_<name>.zip.
# Usage: prepare.sh [download cache folder]   then: gradlew assembleGamevaultRelease
set -e
here=$(cd "$(dirname "$0")" && pwd)
cache=${1:-$here/.cache}
ra_version=1.22.2
mkdir -p "$cache/cores-zip" "$here/jniLibs/arm64-v8a" "$here/assets"

grep -v '^#' "$here/cores.txt" | while read -r c; do
  [ -n "$c" ] || continue
  z="$cache/cores-zip/$c.zip"
  [ -s "$z" ] || curl -sfL -o "$z" "https://buildbot.libretro.com/nightly/android/latest/arm64-v8a/${c}_libretro_android.so.zip"
  unzip -p "$z" "${c}_libretro_android.so" > "$here/jniLibs/arm64-v8a/lib${c}_libretro_android.so"
done

apk="$cache/official/RetroArch_aarch64.apk"
mkdir -p "$cache/official"
[ -s "$apk" ] || curl -sfL -o "$apk" "https://buildbot.libretro.com/stable/$ra_version/android/RetroArch_aarch64.apk"
x="$cache/official/x"
[ -d "$x/assets/autoconfig" ] || unzip -oq "$apk" -d "$x"
rm -rf "$here/assets"; mkdir -p "$here/assets"
cd "$here"
for d in assets autoconfig filters info overlays; do cp -r "$x/assets/$d" assets/; done
rm -rf assets/overlays/borders assets/overlays/effects
mkdir -p res/raw
for s in Dolphin PPSSPP blueMSX; do
  z="$cache/system/$s.zip"
  [ -s "$z" ] || { mkdir -p "$cache/system"; curl -sfL -o "$z" "https://buildbot.libretro.com/assets/system/$s.zip"; }
  cp "$z" "res/raw/gv_sys_$(echo "$s" | tr 'A-Z' 'a-z').zip"
done
# ARMSX2 (PlayStation 2): its resources from its source at the pinned commit, and the free OpenPS2BIOS (MIT) as
# bios/ - unpacked into RetroArch's system folder as pcsx2/, where the core looks (pcsx2/armsx2-pack marks it done)
armsx2_commit=90126bd
a="$cache/armsx2"
[ -d "$a/bin/resources" ] || { rm -rf "$a"; git clone -q --filter=blob:none --no-checkout https://github.com/ARMSX2/ARMSX2.git "$a" &&
  git -C "$a" sparse-checkout set bin/resources && git -C "$a" checkout -q "$armsx2_commit"; }
b="$cache/OpenPS2BIOS-0.2.bin"
[ -s "$b" ] || curl -sfL -o "$b" https://github.com/TamaribaEmu/OpenPS2BIOS/releases/download/v0.2/OpenPS2BIOS-0.2.bin
echo "1451bade21b8b162d1092faee567dc4620037cede367792c2e258ec43273c72a  $b" | sha256sum -c - >/dev/null
p="$cache/armsx2-pack"
rm -rf "$p"; mkdir -p "$p/pcsx2/bios" "$p/pcsx2/armsx2-pack"
cp -r "$a/bin/resources" "$p/pcsx2/resources"
cp "$b" "$p/pcsx2/bios/OpenPS2BIOS-0.2.bin"
echo "ARMSX2 resources $armsx2_commit + OpenPS2BIOS 0.2 (MIT)" > "$p/pcsx2/armsx2-pack/README.txt"
cp "$a/armsx2_libretro.info" assets/info/   # newer than the official APK's core info
rm -f jniLibs/arm64-v8a/libpcsx2_libretro_android.so res/raw/gv_sys_lrps2.zip res/raw/gv_sys_armsx2.zip
(cd "$p" && ${PYTHON:-python3} -c 'import shutil,sys; shutil.make_archive(sys.argv[1], "zip", ".", "pcsx2")' "$here/res/raw/gv_sys_armsx2")
# LINK PLAY: our own builds, the very ones the Xbox Game Vault hosts link rooms with (rooms only open between equal
# builds): Gambatte Link (skitzellez/Gambatte-GameVault, tag gv-link-6f367fe) and gpSP 5819380, each built with the
# NDK at that commit (ndk-build APP_ABI=arm64-v8a in libgambatte/libretro/jni and in jni/). LINK_CORES: the folder
# with both sources (default: the Xbox build's deps/netcores/src).
lc=${LINK_CORES:-$here/../../../../../deps/netcores/src}
cp "$lc/gambatte-link/libgambatte/libretro/libs/arm64-v8a/libretro.so" jniLibs/arm64-v8a/libgambatte_link_libretro_android.so
cp "$lc/gpsp/libs/arm64-v8a/libretro.so" jniLibs/arm64-v8a/libgpsp_libretro_android.so
sed 's/^display_name = .*/display_name = "Nintendo - Game Boy \/ Color (Gambatte Link)"/; s/^corename = .*/corename = "Gambatte Link"/' assets/info/gambatte_libretro.info > assets/info/gambatte_link_libretro.info
ls jniLibs/arm64-v8a | wc -l
du -sh jniLibs assets res
