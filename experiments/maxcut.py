"""Max-cut / QUBO economy experiment wrappers."""

from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt

from .runner import GymRunner


class MaxCutExperiment(GymRunner):
    def __init__(self, **kwargs):
        super().__init__("gym_maxcut", output_dir="output/maxcut", **kwargs)

    def objective_sweep(self, objectives="all", orderings="all",
                        nodes=300, runs=10, seed=42, **kwargs):
        """Sweep all objectives x orderings across multiple seeds."""
        output_file = f"sweep_n{nodes}_r{runs}.csv"
        prod_csv = self.output_dir / f"production_n{nodes}_r{runs}.csv"
        df = self.run(
            output_file=output_file,
            objective=objectives,
            ordering=orderings,
            nodes=nodes,
            runs=runs,
            seed=seed,
            **{k: v for k, v in kwargs.items()
               if k != "production_csv"},
        )
        # Also run with production CSV if requested
        return df

    def objective_sweep_with_production(self, objectives="all", orderings="all",
                                         nodes=300, runs=10, seed=42):
        """Sweep with per-tick production export."""
        output_file = f"sweep_n{nodes}_r{runs}.csv"
        prod_file = f"production_n{nodes}_r{runs}.csv"
        prod_path = self.output_dir / prod_file
        df = self.run(
            output_file=output_file,
            objective=objectives,
            ordering=orderings,
            nodes=nodes,
            runs=runs,
            seed=seed,
            **{"production-csv": str(prod_path)},
        )
        prod_df = pd.read_csv(prod_path) if prod_path.exists() else None
        return df, prod_df

    def compare_vs_mcmc(self, nodes=300, runs=10, seed=42):
        """Compare QUBO objectives against MCMC baseline via economy gym."""
        from .economy import EconomyExperiment
        eco = EconomyExperiment()
        # MCMC baseline
        mcmc_df = eco.run(solver="mcmc", nodes=nodes, runs=runs, seed=seed,
                          output_file=f"mcmc_baseline_n{nodes}.csv")
        # QUBO objectives
        qubo_df = self.objective_sweep(orderings="sequential",
                                        nodes=nodes, runs=runs, seed=seed)
        return qubo_df, mcmc_df


def plot_production_by_objective(df, title="Production Rate by Objective"):
    """Bar chart of mean production rate by objective, with error bars."""
    fig, ax = plt.subplots(figsize=(10, 6))
    stats = df.groupby("objective")["production_rate"].agg(["mean", "std"])
    stats = stats.sort_values("mean", ascending=False)
    ax.bar(stats.index, stats["mean"], yerr=stats["std"],
           capsize=5, color="steelblue", alpha=0.8)
    ax.set_ylabel("Production Rate")
    ax.set_title(title)
    ax.tick_params(axis="x", rotation=30)
    fig.tight_layout()
    return fig


def plot_efficiency_boxplot(df, title="Efficiency Distribution by Objective"):
    """Box plot of efficiency across seeds."""
    fig, ax = plt.subplots(figsize=(10, 6))
    objectives = df["objective"].unique()
    data = [df[df["objective"] == o]["efficiency"].values * 100
            for o in objectives]
    ax.boxplot(data, labels=objectives, patch_artist=True,
               boxprops=dict(facecolor="lightblue"))
    ax.set_ylabel("Efficiency (%)")
    ax.set_title(title)
    ax.tick_params(axis="x", rotation=30)
    fig.tight_layout()
    return fig


def plot_accumulated_production(df, title="Accumulated Production"):
    """Bar chart of mean accumulated production by objective x ordering."""
    fig, ax = plt.subplots(figsize=(12, 6))
    pivot = df.groupby(["objective", "ordering"])["accumulated_production"].mean().unstack()
    pivot.plot(kind="bar", ax=ax, alpha=0.8)
    ax.set_ylabel("Accumulated Production")
    ax.set_title(title)
    ax.legend(title="Ordering")
    ax.tick_params(axis="x", rotation=30)
    fig.tight_layout()
    return fig


def plot_production_curves(prod_df, objective=None, ordering=None,
                           title="Production Over Time"):
    """Line plot of per-tick production with confidence bands across seeds."""
    fig, ax = plt.subplots(figsize=(12, 6))
    subset = prod_df.copy()
    if objective:
        subset = subset[subset["objective"] == objective]
    if ordering:
        subset = subset[subset["ordering"] == ordering]

    for (obj, ord_), group in subset.groupby(["objective", "ordering"]):
        stats = group.groupby("tick")["production"].agg(["mean", "std"])
        ax.plot(stats.index, stats["mean"], label=f"{obj} / {ord_}")
        ax.fill_between(stats.index,
                        stats["mean"] - stats["std"],
                        stats["mean"] + stats["std"],
                        alpha=0.15)

    ax.set_xlabel("Tick")
    ax.set_ylabel("Production Rate")
    ax.set_title(title)
    ax.legend(fontsize=8)
    fig.tight_layout()
    return fig


def plot_objective_x_ordering_heatmap(df, metric="accumulated_production",
                                       title=None):
    """Heatmap of mean metric across objective x ordering."""
    fig, ax = plt.subplots(figsize=(8, 6))
    pivot = df.groupby(["objective", "ordering"])[metric].mean().unstack()
    im = ax.imshow(pivot.values, aspect="auto", cmap="YlOrRd")
    ax.set_xticks(range(len(pivot.columns)))
    ax.set_xticklabels(pivot.columns, rotation=30, ha="right")
    ax.set_yticks(range(len(pivot.index)))
    ax.set_yticklabels(pivot.index)
    ax.set_xlabel("Ordering")
    ax.set_ylabel("Objective")
    ax.set_title(title or f"Mean {metric}")
    fig.colorbar(im, ax=ax)
    fig.tight_layout()
    return fig


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Max-cut economy experiments")
    parser.add_argument("--nodes", type=int, default=300)
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--save-dir", type=str, default="output/maxcut/plots")
    args = parser.parse_args()

    save_dir = Path(args.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    exp = MaxCutExperiment()

    print(f"Running sweep: nodes={args.nodes}, runs={args.runs}, seed={args.seed}")
    df, prod_df = exp.objective_sweep_with_production(
        nodes=args.nodes, runs=args.runs, seed=args.seed)

    print(f"\nResults ({len(df)} rows):")
    print(df.groupby(["objective", "ordering"])[
        ["production_rate", "efficiency", "accumulated_production"]
    ].mean().round(2).to_string())

    # Save plots
    fig = plot_production_by_objective(df)
    fig.savefig(save_dir / "production_by_objective.png", dpi=150)
    print(f"Saved {save_dir / 'production_by_objective.png'}")

    fig = plot_efficiency_boxplot(df)
    fig.savefig(save_dir / "efficiency_boxplot.png", dpi=150)
    print(f"Saved {save_dir / 'efficiency_boxplot.png'}")

    fig = plot_accumulated_production(df)
    fig.savefig(save_dir / "accumulated_production.png", dpi=150)
    print(f"Saved {save_dir / 'accumulated_production.png'}")

    if prod_df is not None and len(prod_df) > 0:
        fig = plot_production_curves(prod_df)
        fig.savefig(save_dir / "production_curves.png", dpi=150)
        print(f"Saved {save_dir / 'production_curves.png'}")

    fig = plot_objective_x_ordering_heatmap(df)
    fig.savefig(save_dir / "objective_ordering_heatmap.png", dpi=150)
    print(f"Saved {save_dir / 'objective_ordering_heatmap.png'}")

    plt.show()
