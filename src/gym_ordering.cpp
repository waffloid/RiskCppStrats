#include "gyms/ordering_gym.hpp"
#include "gyms/economy_gym.hpp"  // get_economy_solver
#include "systems/economy/economy_solvers.hpp"
#include "systems/ordering/ordering_solvers.hpp"
#include "systems/common/data_sink.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void print_usage() {
    std::printf("Usage: gym_ordering [options]\n");
    std::printf("  --economy=NAME    Economy solver (default: bootstrap)\n");
    std::printf("  --ordering=NAME   Ordering solver (default: sequential)\n");
    std::printf("  --seed=N          Random seed (default: 42)\n");
    std::printf("  --runs=N          Number of runs (default: 1)\n");
    std::printf("  --output=PATH     Output CSV path (default: output/ordering/results.csv)\n");

    std::printf("\nAvailable economy solvers:\n");
    for (const auto& name : std::vector<std::string>{"greedy", "bootstrap"}) {
        std::printf("  %s\n", name.c_str());
    }
    std::printf("\nAvailable ordering solvers:\n");
    for (const auto& name : list_ordering_solvers()) {
        std::printf("  %s\n", name.c_str());
    }
}

int main(int argc, char* argv[]) {
    std::string economy_name = "bootstrap";
    std::string ordering_name = "sequential";
    uint64_t seed = 42;
    int runs = 1;
    std::string output_path = "output/ordering/results.csv";

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--economy=", 10) == 0)
            economy_name = argv[i] + 10;
        else if (std::strncmp(argv[i], "--ordering=", 11) == 0)
            ordering_name = argv[i] + 11;
        else if (std::strncmp(argv[i], "--seed=", 7) == 0)
            seed = static_cast<uint64_t>(std::atoll(argv[i] + 7));
        else if (std::strncmp(argv[i], "--runs=", 7) == 0)
            runs = std::atoi(argv[i] + 7);
        else if (std::strncmp(argv[i], "--output=", 9) == 0)
            output_path = argv[i] + 9;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    if (!get_economy_solver(economy_name)) {
        std::fprintf(stderr, "Unknown economy solver: %s\n", economy_name.c_str());
        return 1;
    }
    if (!get_ordering_solver(ordering_name)) {
        std::fprintf(stderr, "Unknown ordering solver: %s\n", ordering_name.c_str());
        return 1;
    }

    DataSink sink(output_path, {
        "run_id", "seed", "n_steps", "total_ticks", "accumulated_production"
    });

    std::printf("Ordering Gym: economy=%s, ordering=%s, runs=%d, seed=%lu\n",
                economy_name.c_str(), ordering_name.c_str(), runs,
                static_cast<unsigned long>(seed));

    GameConfig config{};

    for (int run = 0; run < runs; run++) {
        uint64_t run_seed = seed + static_cast<uint64_t>(run);
        OrderingGymResult r = run_ordering_gym(economy_name, ordering_name, run_seed, config);

        sink.write_row({
            static_cast<double>(run),
            static_cast<double>(run_seed),
            static_cast<double>(r.n_steps),
            static_cast<double>(r.total_ticks),
            static_cast<double>(r.accumulated_production)
        });

        if (runs <= 10 || (run + 1) % (runs / 10) == 0) {
            std::printf("  run %d/%d: %d steps, %d ticks, accum_prod=%.0f\n",
                        run + 1, runs, r.n_steps, r.total_ticks,
                        r.accumulated_production);
        }
    }

    sink.flush();
    std::printf("Written to %s (%d rows)\n", output_path.c_str(), sink.rows_written());
    return 0;
}
