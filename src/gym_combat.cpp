#include "gyms/combat_gym.hpp"
#include "gyms/combat_benchmarks.hpp"
#include "systems/combat/combat_solvers.hpp"
#include "systems/common/data_sink.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void print_usage() {
    std::printf("Usage: gym_combat [options]\n");
    std::printf("  --solver=NAME      AI model for player 0 (default: v2_knapsack)\n");
    std::printf("  --opponent=NAME    AI model for player 1 (default: random)\n");
    std::printf("  --benchmark=NAME   Benchmark map (default: corridor)\n");
    std::printf("  --runs=N           Number of runs (default: 1)\n");
    std::printf("  --spartan=X        Troop multiplier for opponent (default: 1.0)\n");
    std::printf("  --output=PATH      Output CSV path (default: output/combat/results.csv)\n");

    std::printf("\nAvailable benchmarks:\n");
    for (const auto& name : list_combat_benchmarks()) {
        std::printf("  %s\n", name.c_str());
    }
    std::printf("\nAvailable models:\n");
    for (const auto& name : list_combat_solvers()) {
        std::printf("  %s\n", name.c_str());
    }
}

int main(int argc, char* argv[]) {
    std::string solver_name = "v2_knapsack";
    std::string opponent_name = "random";
    std::string benchmark_name = "corridor";
    int runs = 1;
    float spartan = 1.0f;
    std::string output_path = "output/combat/results.csv";

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--solver=", 9) == 0)
            solver_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--opponent=", 11) == 0)
            opponent_name = argv[i] + 11;
        else if (std::strncmp(argv[i], "--benchmark=", 12) == 0)
            benchmark_name = argv[i] + 12;
        else if (std::strncmp(argv[i], "--runs=", 7) == 0)
            runs = std::atoi(argv[i] + 7);
        else if (std::strncmp(argv[i], "--spartan=", 10) == 0)
            spartan = static_cast<float>(std::atof(argv[i] + 10));
        else if (std::strncmp(argv[i], "--output=", 9) == 0)
            output_path = argv[i] + 9;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    auto bm = get_combat_benchmark(benchmark_name);
    if (!bm) {
        std::fprintf(stderr, "Unknown benchmark: %s\n", benchmark_name.c_str());
        print_usage();
        return 1;
    }
    if (!get_combat_solver(solver_name)) {
        std::fprintf(stderr, "Unknown solver: %s\n", solver_name.c_str());
        return 1;
    }
    if (!get_combat_solver(opponent_name)) {
        std::fprintf(stderr, "Unknown opponent: %s\n", opponent_name.c_str());
        return 1;
    }

    DataSink sink(output_path, {
        "run_id", "spartan_mult", "ticks", "winner",
        "nodes_p0", "nodes_p1", "troops_p0", "troops_p1",
        "deaths_p0", "deaths_p1"
    });

    std::printf("Combat Gym: solver=%s, opponent=%s, benchmark=%s, runs=%d, spartan=%.1f\n",
                solver_name.c_str(), opponent_name.c_str(), benchmark_name.c_str(),
                runs, spartan);

    int p0_wins = 0, p1_wins = 0, draws = 0;

    for (int run = 0; run < runs; run++) {
        CombatGymResult r = run_combat_gym(*bm, solver_name, opponent_name, spartan);

        sink.write_row({
            static_cast<double>(run),
            static_cast<double>(spartan),
            static_cast<double>(r.ticks_elapsed),
            static_cast<double>(r.winner),
            static_cast<double>(r.nodes_p0),
            static_cast<double>(r.nodes_p1),
            static_cast<double>(r.troops_p0),
            static_cast<double>(r.troops_p1),
            static_cast<double>(r.deaths_p0),
            static_cast<double>(r.deaths_p1)
        });

        if (r.winner == 0) p0_wins++;
        else if (r.winner == 1) p1_wins++;
        else draws++;

        if (runs <= 10 || (run + 1) % (runs / 10) == 0) {
            std::printf("  run %d/%d: %d ticks, winner=%d, nodes=(%d,%d), deaths=(%d,%d)\n",
                        run + 1, runs, r.ticks_elapsed, r.winner,
                        r.nodes_p0, r.nodes_p1, r.deaths_p0, r.deaths_p1);
        }
    }

    sink.flush();
    std::printf("\nResults: p0_wins=%d, p1_wins=%d, draws=%d\n", p0_wins, p1_wins, draws);
    std::printf("Written to %s (%d rows)\n", output_path.c_str(), sink.rows_written());
    return 0;
}
