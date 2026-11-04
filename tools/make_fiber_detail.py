#!/usr/bin/env python3
"""Builds assets/wood/fiber_detail.png from ambientCG Wood058 (CC0).

The source photo set is reduced to *micro detail only* (D-39 stage 2): the
macro structure (growth rings, tone) comes from the procedural log field, so
the color map is high-pass filtered into a luminance ratio around 1.0.

Packed RGBA (8 bit, tileable, fibers along +V):
  R: albedo detail, ratio = 1 + (R/255 - 0.5) * 2 * RANGE
  G, B: tangent-space normal x, y (OpenGL convention), 0.5 = flat
  A: roughness detail (normalized around 0.5)

Usage: tools/make_fiber_detail.py <Wood058_1K-JPG folder> <output.png>
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

RANGE = 0.5  # +-50 % luminance ratio is representable


def wrap_blur(a: np.ndarray, radius: int, passes: int = 3) -> np.ndarray:
    """Box blur with wrap-around edges (keeps the texture tileable)."""
    out = a.astype(np.float64)
    for _ in range(passes):
        for axis in (0, 1):
            acc = np.zeros_like(out)
            for d in range(-radius, radius + 1):
                acc += np.roll(out, d, axis=axis)
            out = acc / (2 * radius + 1)
    return out


def main() -> None:
    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    stem = src.name
    color = np.asarray(Image.open(src / f"{stem}_Color.jpg").convert("RGB"), dtype=np.float64) / 255.0
    normal = np.asarray(Image.open(src / f"{stem}_NormalGL.jpg").convert("RGB"), dtype=np.float64) / 255.0
    rough = np.asarray(Image.open(src / f"{stem}_Roughness.jpg").convert("L"), dtype=np.float64) / 255.0

    linear = color ** 2.2
    luma = linear @ np.array([0.2126, 0.7152, 0.0722])
    ratio = luma / np.maximum(wrap_blur(luma, 24), 1e-4)            # high-pass (~5 mm features removed)
    r = np.clip(0.5 + (ratio - 1.0) / (2.0 * RANGE), 0.0, 1.0)

    nxy = normal[..., :2]                                           # keep x, y; z is implied
    a = np.clip(0.5 + (rough - rough.mean()), 0.0, 1.0)

    packed = np.dstack([r, nxy[..., 0], nxy[..., 1], a])
    Image.fromarray(np.round(packed * 255.0).astype(np.uint8), "RGBA").save(dst, optimize=True)
    print(f"wrote {dst} {packed.shape[1]}x{packed.shape[0]}; ratio range {ratio.min():.2f}..{ratio.max():.2f}")


if __name__ == "__main__":
    main()
