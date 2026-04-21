"""Transport experiment wrappers."""

from .runner import GymRunner


class TransportExperiment(GymRunner):
    def __init__(self, **kwargs):
        super().__init__("gym_transport", output_dir="output/transport",
                         **kwargs)

    def convergence_analysis(self, presets=("star_center", "path_end",
                                            "bipartite_split"),
                             solver: str = "greedy",
                             loss: str = "l2", **kwargs):
        return self.sweep({"preset": list(presets)},
                          solver=solver, loss=loss, **kwargs)
