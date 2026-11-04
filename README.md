<p align="center">
  <img src="https://raw.githubusercontent.com/skitzellez/xbox-game-vault/main/GameVault/art/logo-banner.png" alt="Game Vault" width="640">
</p>

<h1 align="center">RetroArch for Game Vault</h1>

<p align="center">
  <b>RetroArch for Xbox Series X|S in Dev Mode, built to work hand in hand with
  <a href="https://github.com/skitzellez/xbox-game-vault">Game Vault</a>.</b><br>
  Any USB stick, your saves on it, and discs straight from the console's drive.
</p>

<!-- gv-buttons:start -->
<p align="center">
  <a href="https://github.com/skitzellez/RetroArch-GameVault/releases/latest"><img alt="DOWNLOAD RetroArch" src="https://img.shields.io/badge/DOWNLOAD-RetroArch-107C10?style=for-the-badge&logo=xbox&logoColor=white"></a>
  &nbsp;
  <a href="https://github.com/skitzellez/xbox-game-vault"><img alt="Get Game Vault" src="https://img.shields.io/badge/GET-Game%20Vault-107C10?style=for-the-badge&logo=xbox&logoColor=white"></a>
  &nbsp;
  <a href="https://discord.gg/RmhhWgguvw"><img alt="Join the Game Vault Discord" src="https://img.shields.io/badge/JOIN-Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white"></a>
  &nbsp;
  <a href="https://www.patreon.com/Skitzelles"><img alt="Support on Patreon" src="https://img.shields.io/badge/SUPPORT-Patreon-F96854?style=for-the-badge&logo=patreon&logoColor=white"></a>
</p>
<!-- gv-buttons:end -->

<p align="center">
  <a href="https://github.com/skitzellez/RetroArch-GameVault/releases"><img alt="Latest release" src="https://img.shields.io/github/v/release/skitzellez/RetroArch-GameVault?include_prereleases&label=release&color=107C10"></a>
  <a href="https://discord.gg/RmhhWgguvw"><img alt="Discord" src="https://img.shields.io/badge/chat-Discord-5865F2?logo=discord&logoColor=white"></a>
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

<!-- gv-family:start -->
## 🎮 The Game Vault family

Game Vault is one launcher plus emulators built to work with it. **Install [Game Vault](https://github.com/skitzellez/xbox-game-vault) and it sets these up for you**; press **RT** in Game Vault to update them all.

**Apps**

| Project | What it is | Runs on |
|---|---|---|
| [Game Vault](https://github.com/skitzellez/xbox-game-vault) | The launcher: plug in a USB drive, pick a game, play. Sets up everything below by itself. | Xbox Series X&#124;S |
| [Game Vault for Android](https://github.com/skitzellez/game-vault-android) | The same game library for phones, tablets and Android TV (beta). | Android |
| [Game Vault for iPhone](https://github.com/skitzellez/game-vault-ios) | The Game Vault look on iPhone (early test). | iPhone |

**Emulators built for Game Vault**

| Project | What it is | Runs on |
|---|---|---|
| [xemu for Xbox Dev Mode](https://github.com/skitzellez/xemu-uwp) | Original Xbox | Xbox Series X&#124;S |
| [Xenia for Game Vault](https://github.com/skitzellez/Xenia-GameVault) | Xbox 360 | Xbox Series X&#124;S |
| [XBSX2 for Game Vault](https://github.com/skitzellez/XBSX2-GameVault) | PlayStation 2 | Xbox Series X&#124;S |
| [Dolphin for Game Vault](https://github.com/skitzellez/Dolphin-GameVault) | GameCube and Wii | Xbox Series X&#124;S |
| [Game Vault · Wii U (Cemu)](https://github.com/skitzellez/Cemu-GameVault) | Wii U | Xbox Series X&#124;S |
| [**RetroArch for Game Vault**](https://github.com/skitzellez/RetroArch-GameVault) 📍 *you are here* | PlayStation, Dreamcast, arcade and classic systems | Xbox Series X&#124;S, Android |
| [Game Vault · Flash](https://github.com/skitzellez/Flash-GameVault) | Flash games | Xbox Series X&#124;S |
| [Game Vault · OpenBOR](https://github.com/skitzellez/OpenBOR-GameVault) | OpenBOR beat 'em up games | Xbox Series X&#124;S |
| [Gambatte for Game Vault](https://github.com/skitzellez/Gambatte-GameVault) | Game Boy link cable in online rooms | Xbox Series X&#124;S, Android |
| [X1 BOX for Game Vault](https://github.com/skitzellez/x1box-gamevault) | Original Xbox | Android |

**Extras**

| Project | What it is | Runs on |
|---|---|---|
| [Free games](https://github.com/skitzellez/game-vault-free-games) | Freeware and shareware for the FREE GAMES tab | Game Vault |
| [Fallout 1in2 for Xbox](https://github.com/skitzellez/Fallout1in2-Xbox) | The original Fallout, made for the controller | Xbox Series X&#124;S |
| [Game Vault updates](https://github.com/skitzellez/game-vault-updates) | Update data Game Vault reads when it starts | Game Vault |

**💬 Help, news and bug reports:** the [Game Vault Discord](https://discord.gg/RmhhWgguvw) (or this repo's Issues).
Please don't take problems with these builds to the original emulator teams: they don't make or support them.
<!-- gv-family:end -->

## 📜 Credits and licence

- **[RetroArch](https://www.retroarch.com)** and the libretro cores by libretro and contributors.
- **[XboxEmulationHub](https://github.com/XboxEmulationHub)** for the Xbox build this is based on, SDL3-uwp and
  mesa-uwp.
- Changes for Game Vault by **[Skitzelles](https://github.com/skitzellez)**, written with an AI assistant (Claude).

GPL-3.0, like RetroArch. RetroArch's own README: [README-RetroArch.md](README-RetroArch.md).
