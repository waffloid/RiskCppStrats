#include "gyms/economy_gym.hpp"
#include "systems/common/data_sink.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void print_usage() {
    std::printf("Usage: gym_economy [options]\n");
    std::printf("  --solver=NAME    Economy solver: greedy, bootstrap, mcmc, branch_bound (default: greedy)\n");
    std::printf("  --seed=N         Random seed (default: 42)\n");
    std::printf("  --runs=N         Number of runs (default: 1)\n");
    std::printf("  --output=PATH    Output CSV path (default: output/economy/results.csv)\n");
}

int main(int argc, char* argv[]) {
    std::string solver_name = "greedy";
    uint64_t seed = 42;
    int runs = 1;
    int n_nodes_hint = 50;
    std::string output_path = "output/economy/results.csv";

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--solver=", 9) == 0) {
            solver_name = argv[i] + 9;
        } else if (std::strncmp(argv[i], "--seed=", 7) == 0) {
            seed = static_cast<uint64_t>(std::atoll(argv[i] + 7));
        } else if (std::strncmp(argv[i], "--runs=", 7) == 0) {
            runs = std::atoi(argv[i] + 7);
        } else if (std::strncmp(argv[i], "--nodes=", 8) == 0) {
            n_nodes_hint = std::atoi(argv[i] + 8);
        } else if (std::strncmp(argv[i], "--output=", 9) == 0) {
            output_path = argv[i] + 9;
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    // Verify solver exists.
    if (!get_economy_solver(solver_name)) {
        std::fprintf(stderr, "Unknown solver: %s\n", solver_name.c_str());
        print_usage();
        return 1;
    }

    GameConfig config{};

    DataSink sink(output_path, {
        "run_id", "solver", "graph_seed", "n_nodes", "n_edges",
        "n_factories", "n_powerplants", "production_rate",
        "theoretical_max", "efficiency"
    });

    std::printf("Economy Gym: solver=%s, runs=%d, seed=%lu\n",
                solver_name.c_str(), runs, static_cast<unsigned long>(seed));

    for (int run = 0; run < runs; run++) {
        uint64_t run_seed = seed + static_cast<uint64_t>(run);
        EconomyGymState result = run_economy_gym(solver_name, run_seed, n_nodes_hint, config);

        sink.write_row({
            static_cast<double>(run),
            0.0,  // solver encoded as string in header; numeric placeholder
            static_cast<double>(run_seed),
            static_cast<double>(result.graph.num_nodes()),
            static_cast<double>(result.graph.num_edges()),
            static_cast<double>(result.n_factories),
            static_cast<double>(result.n_powerplants),
            static_cast<double>(result.production_rate),
            static_cast<double>(result.theoretical_max),
            static_cast<double>(result.efficiency)
        });

        if (runs <= 10 || (run + 1) % (runs / 10) == 0) {
            std::printf("  run %d/%d: %d nodes, %d factories, %d powerplants, "
                        "prod=%.1f, max=%.1f, eff=%.3f\n",
                        run + 1, runs,
                        result.graph.num_nodes(),
                        result.n_factories, result.n_powerplants,
                        result.production_rate, result.theoretical_max,
                        result.efficiency);
        }
    }

    sink.flush();
    std::printf("Results written to %s (%d rows)\n", output_path.c_str(), sink.rows_written());
    return 0;
}
