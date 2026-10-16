# Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
# PolyForm Noncommercial License 1.0.0: not for sale, and this notice stays in every copy.
# Turns the package build_gv.bat makes (signed with RetroArch's temporary key, publisher CN=libretro) into
# "RetroArch for Game Vault" signed with Game Vault's key (publisher CN=GamerDad), so it updates the installed app.
#   resign_gv.ps1 <version>   ->  dist\RetroArch-for-GameVault_<version>_x64.appx
param([string]$Ver)
$ErrorActionPreference = "Stop"
$sdk = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64"
$root = $PSScriptRoot
$pfx = Join-Path $root "..\GameVault\cert\GameVault.pfx"
$pw = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "..\GameVault\pfx_password.ps1")
$src = Join-Path $root "pkg\msvc-uwp\AppPackages\RetroArch-msvcUWP\RetroArch-msvcUWP_${Ver}_x64_Test\RetroArch-msvcUWP_${Ver}_x64.appx"
if (-not (Test-Path -LiteralPath $src)) { throw "no package $src" }
$work = Join-Path $env:TEMP "retroarch_resign"; Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
$dir = Join-Path $work "unpacked"
& "$sdk\makeappx.exe" unpack /o /p $src /d $dir | Out-Null
Remove-Item "$dir\AppxSignature.p7x", "$dir\AppxBlockMap.xml", "$dir\[Content_Types].xml" -ErrorAction SilentlyContinue
Remove-Item "$dir\AppxMetadata" -Recurse -ErrorAction SilentlyContinue
$man = Join-Path $dir "AppxManifest.xml"
(Get-Content $man -Raw) -replace 'Publisher="CN=libretro"', 'Publisher="CN=GamerDad"' | Set-Content $man -NoNewline -Encoding UTF8
$dist = Join-Path $root "dist"; New-Item -ItemType Directory -Force $dist | Out-Null
$out = Join-Path $dist "RetroArch-for-GameVault_${Ver}_x64.appx"
& "$sdk\makeappx.exe" pack /o /d $dir /p $out | Out-Null
& "$sdk\signtool.exe" sign /fd SHA256 /a /f $pfx /p $pw $out | Out-Null
"package: $out"
