#!/usr/bin/env python3
"""Build every concept diagram: python3 writeup/figs/build_all.py"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

for gen in sorted(HERE.glob("*/generate.py")):
    print(f"[{gen.parent.name}]")
    subprocess.run([sys.executable, str(gen)], check=True)
