#!/usr/bin/env python3
"""Concept diagrams for the writeup's background section.

These are explanatory illustrations (not experiment data); they render into
writeup/figures/ so the paper's every graphic is script-generated.

Usage: python3 -m experiments.diagrams.render
"""

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from experiments.plotstyle import plt, PALETTE, GRAY, TEXT  # noqa: E402

FIGDIR = Path(__file__).resolve().parents[2] / "writeup" / "figures"
FIGDIR.mkdir(exist_ok=True)

FACTORY = PALETTE[0]     # blue
POWERPLANT = PALETTE[2]  # yellow
ENEMY = PALETTE[5]       # red
OURS = PALETTE[0]
GOOD = PALETTE[1]        # aqua
R = 0.16                 # node radius in axis units


def node(ax, x, y, color, label=None, r=R, fontsize=8, text_color="white"):
    ax.add_patch(plt.Circle((x, y), r, facecolor=color, edgecolor="none", zorder=3))
    if label is not None:
        ax.text(x, y, label, ha="center", va="center", fontsize=fontsize,
                color=text_color, zorder=4, fontweight="bold")


def edge(ax, p, q, color=GRAY, lw=1.4, ls="-"):
    ax.plot([p[0], q[0]], [p[1], q[1]], color=color, lw=lw, ls=ls, zorder=1)


def clean(ax, title=None):
    ax.set_aspect("equal")
    ax.axis("off")
    if title:
        ax.set_title(title, fontsize=9)


# ── A. Economy: anti-magnet / frustration ──────────────────────────────

def fig_economy():
    fig, axes = plt.subplots(1, 3, figsize=(8.0, 2.9))

    # Panel 1: the payoff motif -- powerplant hub boosting a factory ring.
    ax = axes[0]
    hub = (0, 0)
    ring = [(math.cos(a) , math.sin(a)) for a in
            [math.pi/2 + i * 2*math.pi/5 for i in range(5)]]
    for p in ring:
        edge(ax, hub, p)
        node(ax, *p, FACTORY, "F")
    node(ax, *hub, POWERPLANT, "P", text_color="#333333")
    for p in ring:
        ax.annotate("+2", (p[0]*1.38, p[1]*1.38), ha="center", va="center",
                    fontsize=8, color=TEXT)
    ax.set_xlim(-1.75, 1.75); ax.set_ylim(-1.75, 1.75)
    clean(ax, "one powerplant boosts\nevery adjacent factory")

    # Panel 2: even cycle -- perfect alternation.
    ax = axes[1]
    hexpts = [(math.cos(a), math.sin(a)) for a in
              [math.pi/2 + i * math.pi/3 for i in range(6)]]
    for i in range(6):
        edge(ax, hexpts[i], hexpts[(i+1) % 6])
    for i, p in enumerate(hexpts):
        if i % 2 == 0:
            node(ax, *p, POWERPLANT, "P", text_color="#333333")
        else:
            node(ax, *p, FACTORY, "F")
    ax.set_xlim(-1.6, 1.6); ax.set_ylim(-1.6, 1.6)
    clean(ax, "even cycle: alternation\nis perfect")

    # Panel 3: odd cycle -- one edge is always frustrated.
    ax = axes[2]
    pent = [(math.cos(a), math.sin(a)) for a in
            [math.pi/2 + i * 2*math.pi/5 for i in range(5)]]
    kinds = ["P", "F", "P", "F", "F"]  # best effort on a 5-cycle
    for i in range(5):
        j = (i+1) % 5
        frustrated = kinds[i] == kinds[j]
        edge(ax, pent[i], pent[j],
             color=ENEMY if frustrated else GRAY,
             lw=2.2 if frustrated else 1.4,
             ls=(0, (4, 3)) if frustrated else "-")
    for p, k in zip(pent, kinds):
        node(ax, *p, POWERPLANT if k == "P" else FACTORY, k,
             text_color="#333333" if k == "P" else "white")
    ax.annotate("frustrated", ((pent[3][0]+pent[4][0])/2 - 0.08,
                               (pent[3][1]+pent[4][1])/2 - 0.32),
                ha="center", fontsize=8, color=ENEMY)
    ax.set_xlim(-1.6, 1.6); ax.set_ylim(-1.7, 1.6)
    clean(ax, "odd cycle: some edge\nis always wasted")

    fig.tight_layout()
    fig.savefig(FIGDIR / "economy_ising.pdf")
    plt.close(fig)


# ── B. Transport: pressure field vs shipping manifest ─────────────────

def fig_transport():
    # A fork: surplus at A; deficits at the two branch tips D (near) and F (far).
    pos = {"A": (0, 0), "B": (1, 0), "C": (2, 0),
           "D": (3, 0.75), "E": (3, -0.75), "F": (4, -0.75)}
    edges = [("A", "B"), ("B", "C"), ("C", "D"), ("C", "E"), ("E", "F")]
    supply = {"A": "+100"}
    demand = {"D": "-60", "F": "-40"}
    # crude 'pressure' (graph distance from the surplus, for coloring)
    pressure = {"A": 1.0, "B": 0.75, "C": 0.5, "D": 0.25, "E": 0.25, "F": 0.0}

    fig, axes = plt.subplots(1, 2, figsize=(8.0, 2.6))

    # Panel 1: pressure view.
    ax = axes[0]
    for a, b in edges:
        edge(ax, pos[a], pos[b])
    # downhill arrows, width ~ flow passing through
    flows = [("A", "B", 100), ("B", "C", 100), ("C", "D", 60),
             ("C", "E", 40), ("E", "F", 40)]
    for a, b, f in flows:
        (x1, y1), (x2, y2) = pos[a], pos[b]
        mx, my = (x1+x2)/2, (y1+y2)/2
        dx, dy = (x2-x1), (y2-y1)
        n = math.hypot(dx, dy)
        ax.annotate("", xy=(mx + 0.18*dx/n, my + 0.18*dy/n),
                    xytext=(mx - 0.18*dx/n, my - 0.18*dy/n),
                    arrowprops=dict(arrowstyle="-|>", lw=0.8 + f/45,
                                    color=TEXT, mutation_scale=9 + f/12))
    for name, p in pos.items():
        c = plt.cm.Blues(0.25 + 0.6 * pressure[name])
        node(ax, *p, c, name, r=0.2, text_color="#333333"
             if pressure[name] < 0.5 else "white")
    for name, tag in {**supply, **demand}.items():
        ax.annotate(tag, (pos[name][0], pos[name][1] + 0.36), ha="center",
                    fontsize=8.5, color=TEXT, fontweight="bold")
    ax.set_xlim(-0.5, 4.6); ax.set_ylim(-1.5, 1.35)
    clean(ax, "pressure view: build a height field,\ntroops roll downhill")

    # Panel 2: manifest view.
    ax = axes[1]
    for a, b in edges:
        edge(ax, pos[a], pos[b])
    for name, p in pos.items():
        base = OURS if name in supply else (GOOD if name in demand else "#c9c9c4")
        node(ax, *p, base, name, r=0.2,
             text_color="white" if name in supply or name in demand else "#333333")
    for name, tag in {**supply, **demand}.items():
        ax.annotate(tag, (pos[name][0], pos[name][1] + 0.36), ha="center",
                    fontsize=8.5, color=TEXT, fontweight="bold")
    ax.annotate("send 60 → D", xy=pos["D"], xytext=(0.4, 1.05),
                fontsize=8.5, color=TEXT,
                arrowprops=dict(arrowstyle="-|>", color=GOOD, lw=1.6,
                                connectionstyle="arc3,rad=-0.25"))
    ax.annotate("send 40 → F", xy=pos["F"], xytext=(0.9, -1.35),
                fontsize=8.5, color=TEXT,
                arrowprops=dict(arrowstyle="-|>", color=GOOD, lw=1.6,
                                connectionstyle="arc3,rad=0.3"))
    ax.set_xlim(-0.5, 4.6); ax.set_ylim(-1.6, 1.35)
    clean(ax, "manifest view: pair every surplus with\na destination, ship on cheapest routes")

    fig.tight_layout()
    fig.savefig(FIGDIR / "transport_views.pdf")
    plt.close(fig)


# ── C. War: knapsack shopping ──────────────────────────────────────────

def fig_knapsack():
    fig, ax = plt.subplots(figsize=(7.2, 2.9))

    ours = [("120", 0.5), ("80", 2.0), ("50", 3.5)]
    for label, x in ours:
        node(ax, x, 0, OURS, label, r=0.3)
    ax.annotate("our frontier: budget = 250 troops", (2.0, -0.62),
                ha="center", fontsize=9, color=TEXT)

    ax.plot([-0.4, 4.6], [0.75, 0.75], color=GRAY, lw=1.2, ls=(0, (5, 4)))
    ax.annotate("front line", (4.55, 0.83), ha="right", fontsize=8, color=GRAY)

    targets = [
        ("empty",      0.3,  "price 30",  "value 0.5", False),
        ("factory",    1.6,  "price 90",  "value 1",   True),
        ("powerplant", 2.9,  "price 150", "value 2",   True),
        ("capital",    4.2,  "price 300", "value 5",   False),
    ]
    for name, x, price, value, chosen in targets:
        node(ax, x, 1.5, ENEMY, None, r=0.3)
        ax.annotate(name, (x, 2.0), ha="center", va="bottom", fontsize=8.5,
                    color=TEXT, fontweight="bold")
        ax.annotate(f"{price}\n{value}", (x, 2.32), ha="center", va="bottom",
                    fontsize=8, color=TEXT)
        if chosen:
            ax.add_patch(plt.Circle((x, 1.5), 0.42, facecolor="none",
                                    edgecolor=GOOD, lw=2.2, zorder=5))
    ax.annotate("chosen basket: price 240 $\\leq$ 250, value 3", (2.0, 3.35),
                ha="center", fontsize=9, color=TEXT)
    # connect chosen to sources
    for sx in (0.5, 2.0):
        ax.annotate("", xy=(1.6, 1.22), xytext=(sx, 0.3),
                    arrowprops=dict(arrowstyle="-|>", color=GOOD, lw=1.2))
    for sx in (2.0, 3.5):
        ax.annotate("", xy=(2.9, 1.22), xytext=(sx, 0.3),
                    arrowprops=dict(arrowstyle="-|>", color=GOOD, lw=1.2))

    ax.set_xlim(-0.6, 4.9); ax.set_ylim(-0.95, 3.65)
    clean(ax)
    fig.tight_layout()
    fig.savefig(FIGDIR / "knapsack_war.pdf")
    plt.close(fig)


# ── D. Softmax: one auction vs many ────────────────────────────────────

def fig_softmax():
    xs = [i * 0.72 for i in range(13)]
    # two score bumps: tall at x=2 (index 3), slightly lower at x=8 (index 10)
    def score(i):
        return 1.0 * math.exp(-0.5 * (i - 3) ** 2) + 0.85 * math.exp(-0.5 * (i - 10) ** 2)
    scores = [score(i) for i in range(13)]
    smax = max(scores)

    fig, axes = plt.subplots(1, 2, figsize=(8.0, 2.3))
    for ax, local in ((axes[0], False), (axes[1], True)):
        for i, x in enumerate(xs):
            c = plt.cm.Blues(0.2 + 0.65 * scores[i] / smax)
            node(ax, x, 0, c, None, r=0.17)
        for i, x in enumerate(xs):
            if i in (3, 10):
                continue
            target = 3 if (not local or abs(i - 3) <= abs(i - 10)) else 10
            ax.annotate("", xy=(xs[target], 0.32), xytext=(x, 0.22),
                        arrowprops=dict(arrowstyle="-|>", color=GRAY, lw=0.9,
                                        connectionstyle="arc3,rad=-0.25"))
        for i, lbl in ((3, "score 1.0"), (10, "score 0.9")):
            ax.annotate(lbl, (xs[i], -0.55), ha="center", fontsize=8, color=TEXT)
        ax.set_xlim(-0.6, xs[-1] + 0.6)
        ax.set_ylim(-0.95, 1.75)
        clean(ax, "global softmax: one auction,\nthe best target soaks up everything"
              if not local else
              "local softmax (v8): pull decays with\ndistance, each region feeds its own target")

    fig.tight_layout()
    fig.savefig(FIGDIR / "softmax_views.pdf")
    plt.close(fig)


if __name__ == "__main__":
    fig_economy()
    fig_transport()
    fig_knapsack()
    fig_softmax()
    print(f"wrote 4 diagrams to {FIGDIR}")
