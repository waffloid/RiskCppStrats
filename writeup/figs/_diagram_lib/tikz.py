"""Tiny TikZ source emitter -- collects line fragments and renders to a string.

Designed for figure-generator scripts that build a tikzpicture programmatically.
All numeric arguments are positions in the local TikZ coordinate system.
"""
from __future__ import annotations

from typing import List

import numpy as np


def fmt(x: float) -> str:
    """4-decimal float formatter used throughout the lib for TikZ coords."""
    return f"{x:.4f}"


class TikzWriter:
    """Append-only collector of TikZ source fragments."""

    def __init__(self) -> None:
        self.parts: List[str] = []

    # primitive ops -------------------------------------------------------
    def line(self, s: str) -> None:
        self.parts.append(s if s.endswith("\n") else s + "\n")

    def comment(self, s: str) -> None:
        self.line(f"  % {s}")

    def begin_scope(self, opts: str = "") -> None:
        self.line("  \\begin{scope}" + (f"[{opts}]" if opts else ""))

    def end_scope(self) -> None:
        self.line("  \\end{scope}")

    def clip_unit_box(self) -> None:
        self.line("    \\clip (0,0) rectangle (1,1);")

    # drawing ops ---------------------------------------------------------
    def fill_rect(self, x0: float, y0: float, x1: float, y1: float,
                  color_spec: str) -> None:
        self.line(f"    \\fill[{color_spec}] ({fmt(x0)},{fmt(y0)}) rectangle "
                  f"({fmt(x1)},{fmt(y1)});")

    def fill_triangle(self, verts: np.ndarray, color_spec: str) -> None:
        a, b, c = verts
        self.line(f"    \\fill[{color_spec}] "
                  f"({fmt(a[0])},{fmt(a[1])}) -- "
                  f"({fmt(b[0])},{fmt(b[1])}) -- "
                  f"({fmt(c[0])},{fmt(c[1])}) -- cycle;")

    def smooth_path(self, pts: np.ndarray, style: str) -> None:
        coords = " ".join(f"({fmt(p[0])},{fmt(p[1])})" for p in pts)
        self.line(f"    \\draw[{style}] plot[smooth] coordinates {{{coords}}};")

    def edge(self, a: np.ndarray, b: np.ndarray, style: str) -> None:
        self.line(f"    \\draw[{style}] ({fmt(a[0])},{fmt(a[1])}) -- "
                  f"({fmt(b[0])},{fmt(b[1])});")

    def arrow(self, a: np.ndarray, b: np.ndarray, style: str) -> None:
        self.edge(a, b, style)

    def dot(self, p: np.ndarray, style: str, radius: str = "0.5pt") -> None:
        self.line(f"    \\filldraw[{style}] ({fmt(p[0])},{fmt(p[1])}) "
                  f"circle ({radius});")

    def circle(self, p: np.ndarray, radius: str, style: str) -> None:
        """Outlined circle (no fill).  ``radius`` may be a TikZ length like
        ``'0.025'`` (figure units) or ``'2pt'``."""
        self.line(f"    \\draw[{style}] ({fmt(p[0])},{fmt(p[1])}) "
                  f"circle ({radius});")

    def filled_circle(self, p: np.ndarray, radius: str, style: str) -> None:
        self.line(f"    \\filldraw[{style}] ({fmt(p[0])},{fmt(p[1])}) "
                  f"circle ({radius});")

    def node(self, p: np.ndarray, body: str, opts: str = "") -> None:
        self.line(f"    \\node[{opts}] at ({fmt(p[0])},{fmt(p[1])}) {{{body}}};")

    def render(self) -> str:
        return "".join(self.parts)
