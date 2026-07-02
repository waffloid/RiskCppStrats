"""Standalone pdflatex compile harness (adapted from the dissertation lib).

A figure script writes <name>.tex, then calls build_pdf(NAME, HERE).
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd: list, cwd: Path) -> None:
    res = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.stderr.write(res.stdout + "\n" + res.stderr)
        raise SystemExit(f"command failed: {' '.join(cmd)}")


def build_pdf(name: str, here: Path, *, cv_unit: str = "1.6cm",
              extra_preamble: str = "") -> Path:
    standalone = here / "standalone.tex"
    standalone.write_text(
        "\\documentclass[tikz,border=4pt]{standalone}\n"
        "\\usepackage{tikz}\n"
        "\\usetikzlibrary{arrows.meta,bending}\n"
        "\\usepackage{amsmath,mathtools,amssymb}\n"
        f"\\newcommand{{\\cvunit}}{{{cv_unit}}}\n"
        f"{extra_preamble}"
        f"\\begin{{document}}\n\\input{{{name}.tex}}\n\\end{{document}}\n"
    )
    run(["pdflatex", "-interaction=nonstopmode", "-halt-on-error",
         "standalone.tex"], cwd=here)
    out_pdf = here / f"{name}.pdf"
    shutil.move(str(here / "standalone.pdf"), out_pdf)
    for ext in ("aux", "log"):
        p = here / f"standalone.{ext}"
        if p.exists():
            p.unlink()
    standalone.unlink(missing_ok=True)
    print(f"  pdf -> {out_pdf}")
    return out_pdf
