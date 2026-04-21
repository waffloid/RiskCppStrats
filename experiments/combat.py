"""Combat experiment wrappers."""

from .runner import GymRunner


class CombatExperiment(GymRunner):
    def __init__(self, **kwargs):
        super().__init__("gym_combat", output_dir="output/combat", **kwargs)

    def benchmark_battery(self, solver: str,
                          benchmarks=("corridor", "bipartite_3_5",
                                      "star_hub", "grid_5x5"),
                          opponent: str = "random", runs: int = 10, **kwargs):
        return self.sweep({"benchmark": list(benchmarks)},
                          solver=solver, opponent=opponent, runs=runs,
                          **kwargs)

    def spartan_sweep(self, solver: str, benchmark: str = "corridor",
                      spartan_values=(1.0, 2.0, 5.0, 10.0),
                      opponent: str = "random", runs: int = 10, **kwargs):
        return self.sweep({"spartan": list(spartan_values)},
                          solver=solver, benchmark=benchmark,
                          opponent=opponent, runs=runs, **kwargs)
