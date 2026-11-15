# Copyright (c) 2026 Skitzelles - part of RetroArch for Game Vault (GPL-3.0, as RetroArch).
# Draws the screen frames (assets\gv-frames): our own art, made here from noise and light, with no logos and no
# console's likeness. Each frame leaves an opening for one game shape at full height on a 1920x1080 screen, so it only
# fills the black bars beside the game; RetroArch stretches it to the TV. gv_play.inc picks the picture by the
# game's real shape. Usage: make_gv_frames.py <output folder>
import os
import sys

import numpy as np
from PIL import Image

W, H = 1920, 1080
SHAPES = {"43": 4 / 3, "34": 3 / 4, "109": 10 / 9, "32": 3 / 2}
Y, X = np.mgrid[0:H, 0:W].astype(np.float32)
rng = np.random.default_rng(1986)


def smooth_noise(sx, sy, seed):
    """Value noise: a coarse random grid made smooth, sx and sy pixels a cell."""
    r = np.random.default_rng(seed)
    g = r.random((int(H / sy) + 3, int(W / sx) + 3), dtype=np.float32)
    fx, fy = X / sx, Y / sy
    ix, iy = fx.astype(int), fy.astype(int)
    tx, ty = fx - ix, fy - iy
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a = g[iy, ix] * (1 - tx) + g[iy, ix + 1] * tx
    b = g[iy + 1, ix] * (1 - tx) + g[iy + 1, ix + 1] * tx
    return a * (1 - ty) + b * ty


def rounded_box(cx, cy, hw, hh, r):
    """Signed distance to a rounded box: below 0 inside."""
    qx, qy = np.abs(X - cx) - hw + r, np.abs(Y - cy) - hh + r
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def fill(d, aa=1.0):
    return np.clip(0.5 - d / aa, 0, 1)


def mix(base, color, amount):
    c = np.array(color, dtype=np.float32) if not isinstance(color, np.ndarray) else color
    return base * (1 - amount[..., None]) + c * amount[..., None]


def side_light(hw):
    """Room light: brighter near the top, falling away from the picture towards the screen's edges."""
    edge = np.clip((np.abs(X - W / 2) - hw) / max(W / 2 - hw, 1), 0, 1)
    return (1.06 - 0.28 * (Y / H) ** 1.4) * (1 - 0.34 * edge ** 1.6)


def wood(hw):
    warp = smooth_noise(240, 500, 11) * 90 + smooth_noise(60, 170, 12) * 12
    rings = np.sin((X + warp) * 0.18 + smooth_noise(30, 340, 13) * 5) * 0.5 + 0.5
    fine = smooth_noise(2.2, 60, 14)
    pores = np.clip((smooth_noise(1.6, 26, 15) - 0.68) * 7, 0, 1)
    v = 0.26 + 0.34 * np.clip(rings, 0, 1) ** 2.2 + 0.26 * fine - 0.2 * pores
    dark, light = np.array([38, 19, 9], np.float32), np.array([128, 74, 38], np.float32)
    rgb = dark + (light - dark) * v[..., None]
    return rgb * side_light(hw)[..., None]


def plastic(color, hw, seed):
    grain = smooth_noise(1.4, 1.4, seed) * 0.5 + smooth_noise(3.1, 3.1, seed + 1) * 0.5
    rgb = np.array(color, np.float32) * (0.93 + 0.14 * grain)[..., None]
    return rgb * side_light(hw)[..., None]


def rim(rgb, hole, width, color, shine):
    """A moulded lip round the opening: a dark wall with a bright top edge that catches the light."""
    lip = fill(hole - width, 1.5) * (1 - fill(hole, 1.5))   # only the band just outside the shape
    t = np.clip(hole / width, 0, 1)
    gy = np.gradient(hole, axis=0)
    gx = np.gradient(hole, axis=1)
    light = np.clip(-gy * 0.75 + gx * -0.35, -1, 1)   # light from above and a little to the left
    wall = np.array(color, np.float32)[None, None, :] * (0.55 + 0.75 * (1 - t))[..., None]
    wall = wall + (shine * np.clip(light, 0, 1) * np.clip(np.sin(t * np.pi), 0, 1) ** 1.5)[..., None]
    return mix(rgb, wall, lip)


def drop_shadow(rgb, hole, size, strength):
    amount = strength * np.clip(1 - hole / size, 0, 1) ** 2
    return rgb * (1 - amount)[..., None]


def slots(rgb, x0, x1, y0, y1, gap, thick):
    cy = np.round((Y - y0) / gap) * gap + y0
    d = np.maximum(np.abs(Y - cy) - thick / 2, np.maximum(x0 - X, X - x1))
    d = np.maximum(d, np.maximum(y0 - Y, Y - y1))
    hole = fill(d, 1.3)
    edge = fill(np.abs(d + 0.0) - 1.1, 1.2) * np.clip((Y - cy) / max(thick, 1) + 0.6, 0, 1)
    rgb = rgb * (1 - 0.86 * hole)[..., None]
    return rgb + (24 * edge * (1 - hole))[..., None]


def knob(rgb, cx, cy, r, angle):
    dist = np.hypot(X - cx, Y - cy)
    well = fill(dist - r * 1.2, 1.5)
    rgb = rgb * (1 - 0.66 * well)[..., None]
    shadow = fill(np.hypot(X - cx - 3, Y - cy - 6) - r * 1.04, 8)
    rgb = rgb * (1 - 0.55 * shadow)[..., None]
    body = fill(dist - r, 1.5)
    a = np.arctan2(Y - cy, X - cx)
    brushed = 0.5 + 0.5 * np.cos(a * 2 - 1.0)
    ridges = 0.5 + 0.5 * np.cos(a * 42)
    outer = np.clip((dist - r * 0.72) / (r * 0.28), 0, 1)
    v = (34 + 34 * brushed) * (1 - outer) + (24 + 26 * ridges) * outer
    face = np.stack([v * 1.02, v, v * 0.96], axis=-1)
    rgb = mix(rgb, face, body)
    px = cx + np.cos(angle) * r * 0.62
    py = cy + np.sin(angle) * r * 0.62
    tick = fill(np.hypot(X - px, Y - py) - r * 0.09, 1.3)
    return mix(rgb, np.array([226, 219, 200], np.float32), tick)


def finish(rgb, hole, hole_shadow, grain=1.4):
    """To RGBA: open where the game shows, with a soft shadow on the picture's very edge as a tube's glass has."""
    a = 1 - fill(hole, 1.5)
    inner = np.clip(1 + hole / hole_shadow, 0, 1) ** 2.6 * 0.62
    a = np.maximum(a, inner * (hole < 0))
    colour = rgb.copy()
    colour[hole < 0] = 0
    colour = colour + (rng.normal(0, grain, (H, W)).astype(np.float32))[..., None] * (a > 0.99)[..., None]
    out = np.dstack([np.clip(colour, 0, 255), a * 255])
    return Image.fromarray((out + 0.5).astype(np.uint8), "RGBA")


def tv(aspect):
    hw = H * aspect / 2
    hole = rounded_box(W / 2, H / 2, hw, H / 2 + 8, 34)
    rgb = wood(hw)
    # a dark inner surround, as the tube sits in on an old set
    surround = rounded_box(W / 2, H / 2, hw + 32, H / 2 + 40, 54)
    rgb = drop_shadow(rgb, surround, 42, 0.62)
    rgb = mix(rgb, plastic((23, 22, 21), hw, 40), fill(surround, 1.5))
    rgb = rim(rgb, surround, 7, (58, 54, 49), 54)
    side = W / 2 - hw - 32
    if side > 130:   # the controls on the right, where a set had them
        cx = W / 2 + hw + 32 + side / 2
        panel = rounded_box(cx, H / 2, side * 0.36, H * 0.43, 12)
        rgb = drop_shadow(rgb, panel, 16, 0.5)
        rgb = mix(rgb, plastic((29, 27, 25), hw, 44), fill(panel, 1.5))
        rgb = rim(rgb, panel, 3, (70, 64, 57), 36)
        r = min(side * 0.21, 62)
        rgb = knob(rgb, cx, H * 0.20, r, -1.0)
        rgb = knob(rgb, cx, H * 0.20 + r * 2.8, r * 0.82, 0.5)
        half = side * 0.25
        rgb = slots(rgb, cx - half, cx + half, H * 0.47, H * 0.87, 15, 5)
    return finish(rgb, hole, 26)


def arcade(aspect):
    hw = H * aspect / 2
    hole = rounded_box(W / 2, H / 2, hw, H / 2 + 8, 20)
    rgb = plastic((15, 15, 19), hw, 60)
    # the cabinet's side art: light in two colours rising up the panels, lines running out from the screen
    dist = np.clip((np.abs(X - W / 2) - hw) / max(W / 2 - hw, 1), 0, 1)
    wave = smooth_noise(400, 500, 61) * 0.5
    blue = np.array([18, 136, 255], np.float32)
    pink = np.array([255, 28, 122], np.float32)
    level = np.clip((Y / H) + wave - 0.25, 0, 1)
    tint = blue + (pink - blue) * level[..., None]
    lines = np.clip(1 - np.abs(((np.abs(X - W / 2) - hw) / 42) % 1 - 0.5) * 9, 0, 1)
    fade = (0.16 + 0.62 * np.exp(-dist * 2.0))
    rgb = rgb + tint * (lines * fade)[..., None] * 0.86
    glow = np.exp(-np.maximum(hole, 0) / 96)
    rgb = rgb + tint * (glow * 0.34)[..., None]
    # a black bezel with a bright edge, and the plastic edge strip of the cabinet at the screen's sides
    bezel = rounded_box(W / 2, H / 2, hw + 24, H / 2 + 32, 30)
    rgb = drop_shadow(rgb, bezel, 34, 0.7)
    rgb = mix(rgb, plastic((9, 9, 11), hw, 66), fill(bezel, 1.5))
    rgb = rim(rgb, bezel, 5, (64, 66, 76), 62)
    strip = np.clip((W / 2 - 13 - np.abs(X - W / 2)) / 13, 0, 1)
    edge = 1 - fill(np.abs(X - W / 2) - (W / 2 - 26), 1.2)
    t_mould = np.stack([30 + 90 * np.sin(strip * np.pi)] * 3, axis=-1) * np.array([0.82, 0.9, 1.05], np.float32)
    rgb = mix(rgb, t_mould, edge)
    return finish(rgb, hole, 18)


def handheld(aspect):
    hw = H * aspect / 2
    hole = rounded_box(W / 2, H / 2, hw, H / 2 + 8, 18)
    rgb = plastic((60, 62, 67), hw, 80)
    # a soft sheen across the shell, then the glossy black window round the screen
    sheen = np.exp(-((X - W * 0.2 - Y * 0.16) / 240) ** 2) * 16
    rgb = rgb + sheen[..., None]
    window = rounded_box(W / 2, H / 2, hw + 46, H / 2 + 54, 38)
    rgb = drop_shadow(rgb, window, 24, 0.5)
    glass = 14 + 15 * np.clip(1 - (Y + X * 0.28) / 620, 0, 1)
    rgb = mix(rgb, np.dstack([glass, glass, glass * 1.05]), fill(window, 1.5))
    rgb = rim(rgb, window, 4, (82, 84, 90), 44)
    side = W / 2 - hw - 46
    if side > 110:
        # the shell's speaker holes on the right, a power light on the left
        cx, r = W / 2 + hw + 46 + side / 2, 5.5
        gap = min(side * 0.14, 25)
        gx, gy = np.round((X - cx) / gap) * gap + cx, np.round((Y - H * 0.70) / gap) * gap + H * 0.70
        limit = np.hypot((gx - cx) / (gap * 3.2), (gy - H * 0.70) / (gap * 3.2)) <= 1
        dots = fill(np.hypot(X - gx, Y - gy) - r, 1.4) * limit
        rgb = rgb * (1 - 0.86 * dots)[..., None]
        lx, ly = W / 2 - hw - 46 - side / 2, H * 0.30
        halo = np.exp(-np.hypot(X - lx, Y - ly) / 15) * 0.8
        rgb = rgb + np.array([30, 150, 70], np.float32) * halo[..., None]
        dot = fill(np.hypot(X - lx, Y - ly) - 6, 1.3)
        rgb = mix(rgb, np.array([136, 255, 170], np.float32), dot)
    return finish(rgb, hole, 12)


KINDS = {"tv": tv, "arcade": arcade, "handheld": handheld}


def main(out):
    os.makedirs(out, exist_ok=True)
    for kind, draw in KINDS.items():
        for shape, aspect in SHAPES.items():
            name = f"{kind}-{shape}"
            img = draw(aspect)
            img.save(os.path.join(out, name + ".png"), optimize=True)
            with open(os.path.join(out, name + ".cfg"), "w", newline="\n") as f:
                f.write(f'overlays = 1\noverlay0_overlay = "{name}.png"\noverlay0_full_screen = true\noverlay0_descs = 0\n')
            print(name, os.path.getsize(os.path.join(out, name + ".png")) // 1024, "KB")


if __name__ == "__main__":
    main(sys.argv[1])
