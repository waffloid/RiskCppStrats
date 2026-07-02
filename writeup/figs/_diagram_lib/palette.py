"""Palette: maps a normalised scalar to an xcolor `fill=...` token.

The remap is  s = (1 - alpha) + alpha * t, so alpha controls how much of the
colormap is actually used:
    alpha = 1    -> full palette
    alpha = 0.5  -> upper half only
    alpha = 0    -> a single tone (top of the colormap)

Also provides ``save_image`` for rasterising a 2-D scalar field to a smooth
PNG/SVG-compatible bitmap, which figure scripts embed via ``\\includegraphics``
to avoid visible block artefacts from per-cell ``\\fill`` rectangles.
"""
from __future__ import annotations

from pathlib import Path
from typing import Union

import numpy as np
from matplotlib import colormaps
import matplotlib.image as mpimg


class Palette:
    def __init__(self, cmap_name: str = "viridis", alpha: float = 0.75):
        self.cmap = colormaps.get_cmap(cmap_name)
        self.alpha = alpha

    # --- TikZ tokens for a single scalar value -------------------------
    def rgb_spec(self, t: float) -> str:
        """Bare ``{rgb,1:...}`` token for use anywhere xcolor accepts a colour
        (e.g. ``fill=<...>``, ``draw=<...>``, ``color=<...>``)."""
        v = float(np.clip(t, 0.0, 1.0))
        s = (1.0 - self.alpha) + self.alpha * v
        r, g, b, _ = self.cmap(float(np.clip(s, 0.0, 1.0)))
        return f"{{rgb,1:red,{r:.4f};green,{g:.4f};blue,{b:.4f}}}"

    def fill_spec(self, t: float) -> str:
        return f"fill={self.rgb_spec(t)}"

    def draw_spec(self, t: float) -> str:
        return f"draw={self.rgb_spec(t)}"

    # --- Raster a 2-D scalar field through this palette ----------------
    def render_array(self, scalar: np.ndarray) -> np.ndarray:
        """Apply the alpha remap + colormap to a 2-D scalar grid in [0, 1].
        Returns an (H, W, 3) uint8 array suitable for ``mpimg.imsave``."""
        a = np.clip(scalar, 0.0, 1.0)
        s = (1.0 - self.alpha) + self.alpha * a
        rgba = self.cmap(np.clip(s, 0.0, 1.0))
        return (rgba[..., :3] * 255.0).clip(0, 255).astype(np.uint8)

    def save_image(self, scalar: np.ndarray, path: Union[str, Path]) -> Path:
        """Save a (H, W) scalar field in [0, 1] as a coloured PNG.  The image
        is written with origin at the bottom-left so it can be embedded into a
        TikZ scene at ``(0,0) anchor=south west`` without further transforms."""
        img = self.render_array(scalar)
        # flip so row 0 becomes the top of the image (mpimg.imsave writes
        # row 0 at the top, but we want row 0 = y=0 at the bottom of the figure)
        img = np.flipud(img)
        path = Path(path)
        mpimg.imsave(path, img)
        return path
