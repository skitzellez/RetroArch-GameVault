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
for s in Dolphin PPSSPP LRPS2 blueMSX; do
  z="$cache/system/$s.zip"
  [ -s "$z" ] || { mkdir -p "$cache/system"; curl -sfL -o "$z" "https://buildbot.libretro.com/assets/system/$s.zip"; }
  cp "$z" "res/raw/gv_sys_$(echo "$s" | tr 'A-Z' 'a-z').zip"
done
ls jniLibs/arm64-v8a | wc -l
du -sh jniLibs assets res
