@echo off
rem Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
rem Builds the Xbox (UWP) package of this RetroArch tree with the Visual Studio 2022 Build Tools.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0pkg\msvc-uwp"
rem Mesa 26.2.2 (XboxEmulationHub/mesa-uwp) instead of the older Mesa that came with the tree: the old one
rem crashed when a core re-created its OpenGL context (ScummVM starting a game). dxil.dll stays.
for %%f in (libgallium_wgl opengl32 libEGL libGLESv1_CM libGLESv2 z-1 spirv_to_dxil vulkan_dzn) do copy /y "%~dp0..\deps\mesa-26\x\bin\%%f.dll" "RetroArch-msvcUWP\MESA\x64\" >nul || exit /b 1
msbuild RetroArch-msvcUWP.sln /m /v:m /nologo /p:PlatformToolset=v143 /p:Configuration=Release /p:Platform=x64 /t:restore /p:RestorePackagesConfig=true || exit /b 1
msbuild RetroArch-msvcUWP.sln /m /v:m /nologo /p:PlatformToolset=v143 /p:Configuration=Release /p:Platform=x64 /p:AppxPackageSigningEnabled=true %*
