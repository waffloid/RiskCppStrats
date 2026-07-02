"""Shared TikZ styles: CRisky's visual vocabulary.

One place to tweak how every concept diagram draws game entities. The finish
follows the dissertation diagrams this lib is adapted from: thin black
outlines, restrained fills, CM text, bold panel letters.
"""
from __future__ import annotations

from typing import Dict, Mapping, Optional

STYLES: Dict[str, str] = {
    # panels
    "panelframe": "draw=black!85, line width=0.5pt",
    "paneltitle": "font=\\small",
    # game entities (fills carry state; outlines stay black)
    "factory":    "fill=cyan!45!blue!35, draw=black, line width=0.5pt",
    "pplant":     "fill=orange!70!yellow!80, draw=black, line width=0.5pt",
    "ours":       "fill=cyan!45!blue!35, draw=black, line width=0.5pt",
    "enemy":      "fill=red!45, draw=black, line width=0.5pt",
    "neutral":    "fill=black!12, draw=black, line width=0.5pt",
    # edges & annotations
    "gedge":      "draw=black!70, line width=0.5pt",
    "frustrated": "draw=red!75!black, line width=0.9pt, "
                  "dash pattern=on 2pt off 1.6pt",
    "frontline":  "draw=black!55, line width=0.7pt, "
                  "dash pattern=on 3.5pt off 2.5pt",
    "flow":       "-{Stealth[length=4.5pt]}, draw=black!75, line width=0.7pt",
    "consign":    "-{Stealth[length=5pt]}, draw=green!55!black, "
                  "line width=0.8pt",
    "chosen":     "draw=green!55!black, line width=1.0pt",
    "note":       "font=\\small",
    "smallnote":  "font=\\footnotesize",
}


def tikzpicture_preamble(
    *,
    cv_unit: str = "1.6cm",
    overrides: Optional[Mapping[str, str]] = None,
    extra: Optional[Mapping[str, str]] = None,
    note: str = "Auto-generated -- do not hand-edit.",
) -> str:
    styles = dict(STYLES)
    if overrides:
        styles.update(overrides)
    if extra:
        for k, v in extra.items():
            if k in styles:
                raise ValueError(f"style {k!r} already defined; use overrides=")
            styles[k] = v

    lines = [
        f"% {note}",
        f"\\providecommand{{\\cvunit}}{{{cv_unit}}}",
        "\\begin{tikzpicture}[",
        "    x=\\cvunit, y=\\cvunit,",
    ]
    for name, opts in styles.items():
        lines.append(f"    {name}/.style={{{opts}}},")
    lines.append("  ]")
    return "\n".join(lines) + "\n"


def tikzpicture_footer() -> str:
    return "\\end{tikzpicture}%\n"
