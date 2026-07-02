"""TikZ diagram scaffolding for the writeup's concept figures.

Adapted from the _diagram_lib in Joel's dissertation Figs/ (TikzWriter,
Palette, standalone-compile harness); styles rewritten for CRisky's visual
vocabulary (factories, powerplants, armies, front lines).

Each figure lives in writeup/figs/<name>/generate.py and builds
<name>.pdf in place; run everything with:

    python3 writeup/figs/build_all.py
"""
from .tikz import TikzWriter, fmt
from .palette import Palette
from .styles import tikzpicture_preamble, tikzpicture_footer, STYLES
from .build import build_pdf

__all__ = [
    "TikzWriter", "fmt", "Palette",
    "tikzpicture_preamble", "tikzpicture_footer", "STYLES",
    "build_pdf",
]
