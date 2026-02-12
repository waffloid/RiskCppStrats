"""Ordering experiment wrappers."""

from .runner import GymRunner


class OrderingExperiment(GymRunner):
    def __init__(self, **kwargs):
        super().__init__("gym_ordering", output_dir="output/ordering",
                         **kwargs)

    def ordering_comparison(self, orderings=("sequential", "cheapest_first",
                                             "nearest_first",
                                             "production_gradient"),
                            economy: str = "bootstrap",
                            runs: int = 10, **kwargs):
        return self.sweep({"ordering": list(orderings)},
                          economy=economy, runs=runs, **kwargs)
