/* Copyright (c) 2026 Skitzelles - part of Game Vault, https://github.com/skitzellez/xbox-game-vault
 * GPL-3.0-or-later, like RetroArch.
 *
 * Racing wheels on the Xbox, read as Xbox pads (see uwp_wheel.cpp).
 */
#ifndef UWP_WHEEL_H
#define UWP_WHEEL_H

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* starts watching for wheels (once) */
void uwp_wheel_init(void);

/* the n-th connected wheel as an XInput pad reading; false when there is no n-th wheel */
bool uwp_wheel_get_state(unsigned n, XINPUT_STATE *out);

#ifdef __cplusplus
}
#endif

#endif
