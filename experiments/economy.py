"""Economy experiment wrappers."""

from .runner import GymRunner


class EconomyExperiment(GymRunner):
    def __init__(self, **kwargs):
        super().__init__("gym_economy", output_dir="output/economy", **kwargs)

    def solver_comparison(self, solvers=("greedy", "bootstrap"),
                          seeds=range(42, 52), **kwargs):
        return self.sweep({"solver": list(solvers)},
                          seed=42, runs=len(list(seeds)), **kwargs)
