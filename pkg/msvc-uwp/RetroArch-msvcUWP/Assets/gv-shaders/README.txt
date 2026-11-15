Screen styles for Game Vault (Options > Screen style), from libretro/slang-shaders
(https://github.com/libretro/slang-shaders), unchanged:
  crt.slangp   - zfast_crt (finemask), Copyright (C) 2017 Greg Hogan (SoltanGris42), GNU GPL v2 or later
  tv.slangp    - crt-consumer, GNU GPL v2 or later
  pro.slangp   - crt-aperture, by EasyMode, GNU GPL
  curve.slangp - crt-geom, Copyright (C) 2010-2012 cgwg, Themaister and DOLLS, GNU GPL v2 or later
  lcd.slangp   - lcd3x, by Gigaherz, public domain
  sharp.slangp - sharp-bilinear-simple, by rsn8887, public domain
  fsr.slangp   - AMD FidelityFX Super Resolution 1 (EASU + RCAS), edge-smoothing/fsr; FSR 1 headers (ffx_a.h,
                 ffx_fsr1.h) Copyright (c) AMD, MIT licence; the slang passes Unlicense. Film grain set to 0.
                 Used for "Sharper picture (FSR)" on 3D systems (fsr=1 in Game Vault's link).
  fsr.glslp    - the same FSR 1 for cores that draw with OpenGL (N64, PS1 Beetle HW: RetroArch's "gl" driver
                 runs GLSL, not slang), from libretro/glsl-shaders fsr (ported from shadertoy stXSWB, MIT licence).
  Both FSR passes: the sharpening setting is "Sharpness" in percent (GV_SHARPNESS) instead of FSR_SHARPENING.

The full library in assets\shaders_slang is libretro/slang-shaders (https://github.com/libretro/slang-shaders),
commit 2345ee9, unchanged; every shader keeps the licence written in its own files.
