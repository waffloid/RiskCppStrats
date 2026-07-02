"""Shared matplotlib style for paper figures.

Categorical palette is a CVD-validated 8-slot set (worst adjacent pair
deltaE 24.2); hues are assigned to entities in fixed slot order, never cycled.
Figures target print: white surface, recessive grid, direct labels over legends
where practical, one axis per figure.
"""

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

# Fixed categorical slots (light surface).
PALETTE = ["#2a78d6", "#1baf7a", "#eda100", "#008300",
           "#4a3aa7", "#e34948", "#e87ba4", "#eb6834"]
GRAY = "#8a8a85"
TEXT = "#333333"

plt.rcParams.update({
    "figure.dpi": 150,
    "savefig.dpi": 150,
    "savefig.bbox": "tight",
    "font.size": 9,
    "axes.titlesize": 9.5,
    "axes.labelsize": 9,
    "axes.edgecolor": GRAY,
    "axes.labelcolor": TEXT,
    "axes.linewidth": 0.8,
    "axes.grid": True,
    "grid.color": "#e6e6e2",
    "grid.linewidth": 0.6,
    "xtick.color": TEXT,
    "ytick.color": TEXT,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "lines.linewidth": 2.0,
    "legend.frameon": False,
    "legend.fontsize": 8,
})


def style_axes(ax):
    """Recessive spines: keep left/bottom, drop top/right."""
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.set_axisbelow(True)
