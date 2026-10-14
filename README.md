<p align="center">
  <img src="https://raw.githubusercontent.com/skitzellez/xbox-game-vault/main/GameVault/art/logo-banner.png" alt="Game Vault" width="640">
</p>

<h1 align="center">RetroArch for Game Vault</h1>

<p align="center">
  <b>RetroArch for Xbox Series X|S in Dev Mode, built to work hand in hand with
  <a href="https://github.com/skitzellez/xbox-game-vault">Game Vault</a>.</b><br>
  Any USB stick, your saves on it, and discs straight from the console's drive.
</p>

<p align="center">
  <a href="https://github.com/skitzellez/RetroArch-GameVault/releases"><img alt="Latest release" src="https://img.shields.io/github/v/release/skitzellez/RetroArch-GameVault?include_prereleases&label=release&color=107C10"></a>
  <img alt="Platform: Xbox Dev Mode" src="https://img.shields.io/badge/platform-Xbox%20Dev%20Mode-107C10?logo=xbox&logoColor=white">
  <img alt="License: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-blue">
  <a href="https://www.patreon.com/Skitzelles"><img alt="Patreon" src="https://img.shields.io/badge/Patreon-Skitzelles-F96854?logo=patreon&logoColor=white"></a>
</p>

---

> [!IMPORTANT]
> This is an **unofficial build**. It is not made or supported by libretro or XboxEmulationHub, so please report
> problems with it **here**, not to them.

## ✨ What's different from the original

| | |
|---|---|
| 💽 **Any USB stick** | FAT32, exFAT or NTFS, exactly as it is. No formatting, no permission commands on a PC. |
| 💾 **Saves on your stick** | Started from Game Vault, it keeps BIOS files, saves, states and its log in the `Game Vault` folder on the stick, and moves over what it had on the console the first time. |
| 📀 **Discs from the drive** | PlayStation discs play straight from the Series X disc drive. |
| 🔁 **New game, no restart** | A game sent from Game Vault while RetroArch is still open loads right away. |
| 🤝 **Lives next to the original** | Its own name and link, so the original RetroArch app and its settings are left alone. |

## 🚀 Getting started

The easy way is the all-in-one setup from **[Game Vault](https://github.com/skitzellez/xbox-game-vault#installation)**.
It installs this build and copies over your settings, saves and BIOS files from the original RetroArch app.

By hand: download the `.appx` from [Releases](https://github.com/skitzellez/RetroArch-GameVault/releases), install
it through the Xbox **Device Portal**, then in **Dev Home** set its **App type** to **Game**.

## 🛠️ Source

Everything is on the **`gamevault`** branch, on top of
[XboxEmulationHub/RetroArch](https://github.com/XboxEmulationHub/RetroArch). The main changes:
`uwp/uwp_file_redirect.cpp` (any USB stick), `uwp/uwp_cdrom.cpp` (the disc drive) and `uwp/uwp_main.cpp` (the
Game Vault folder and launch link). Build with `build_gv.bat` (Visual Studio 2022 Build Tools).

## 📜 Credits and licence

- **[RetroArch](https://www.retroarch.com)** and the libretro cores by libretro and contributors.
- **[XboxEmulationHub](https://github.com/XboxEmulationHub)** for the Xbox build this is based on, SDL3-uwp and
  mesa-uwp.
- Changes for Game Vault by **[Skitzelles](https://github.com/skitzellez)**, written with an AI assistant (Claude).

GPL-3.0, like RetroArch. RetroArch's own README: [README-RetroArch.md](README-RetroArch.md).
