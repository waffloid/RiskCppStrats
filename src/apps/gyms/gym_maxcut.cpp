#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/game.hpp"
#include "engine/game_config.hpp"
#include "gyms/economy_gym.hpp"
#include "gyms/ordering_gym.hpp"
#include "systems/graph_algo/maxcut_solvers.hpp"
#include "systems/graph_algo/qubo_objectives.hpp"
#include "systems/economy/economy_solvers.hpp"
#include "systems/ordering/ordering_solvers.hpp"

#include <sys/stat.h>

static void print_usage() {
    printf("Usage: gym_maxcut [options]\n");
    printf("  --objective=NAME    QUBO objective (or 'all'). Default: production\n");
    printf("  --ordering=NAME     Ordering solver (or 'all'). Default: sequential\n");
    printf("  --seed=N            Random seed. Default: 42\n");
    printf("  --runs=N            Number of runs. Default: 1\n");
    printf("  --nodes=N           Approximate node count. Default: 300\n");
    printf("  --output=PATH       CSV output path. Default: output/maxcut/results.csv\n");
    printf("  --production-csv=PATH  Per-tick production CSV.\n");
    printf("\nObjectives: ");
    for (const auto& o : list_qubo_objectives()) printf("%s ", o.c_str());
    printf("\nOrderings: ");
    for (const auto& o : list_ordering_solvers()) printf("%s ", o.c_str());
    printf("\n");
}

static std::string get_arg(int argc, char** argv, const std::string& prefix,
                            const std::string& default_val) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg.find(prefix) == 0) return arg.substr(prefix.size());
    }
    return default_val;
}

int main(int argc, char** argv) {
    std::string objective_name = get_arg(argc, argv, "--objective=", "production");
    std::string ordering_name = get_arg(argc, argv, "--ordering=", "sequential");
    uint64_t seed = static_cast<uint64_t>(std::atoi(get_arg(argc, argv, "--seed=", "42").c_str()));
    int runs = std::atoi(get_arg(argc, argv, "--runs=", "1").c_str());
    int nodes_hint = std::atoi(get_arg(argc, argv, "--nodes=", "300").c_str());
    std::string output_path = get_arg(argc, argv, "--output=", "output/maxcut/results.csv");
    std::string prod_csv = get_arg(argc, argv, "--production-csv=", "");

    // Build lists of objectives and orderings to sweep
    std::vector<std::string> objectives;
    if (objective_name == "all") {
        objectives = list_qubo_objectives();
    } else {
        objectives = {objective_name};
    }

    std::vector<std::string> orderings;
    if (ordering_name == "all") {
        orderings = list_ordering_solvers();
    } else {
        orderings = {ordering_name};
    }

    // Ensure output directory exists
    {
        std::string dir = output_path.substr(0, output_path.rfind('/'));
        if (!dir.empty()) {
            std::string cmd = "mkdir -p " + dir;
            (void)system(cmd.c_str());
        }
    }

    // Summary CSV
    FILE* csv = fopen(output_path.c_str(), "w");
    if (!csv) {
        printf("Error: cannot open %s for writing\n", output_path.c_str());
        return 1;
    }
    fprintf(csv, "objective,ordering,seed,n_nodes,production_rate,efficiency,total_ticks,accumulated_production,n_factories,n_powerplants\n");

    // Optional per-tick production CSV
    FILE* prod_file = nullptr;
    if (!prod_csv.empty()) {
        std::string dir = prod_csv.substr(0, prod_csv.rfind('/'));
        if (!dir.empty()) {
            std::string cmd = "mkdir -p " + dir;
            (void)system(cmd.c_str());
        }
        prod_file = fopen(prod_csv.c_str(), "w");
        if (prod_file) {
            fprintf(prod_file, "objective,ordering,seed,tick,production\n");
        }
    }

    for (int run = 0; run < runs; run++) {
        uint64_t run_seed = seed + static_cast<uint64_t>(run);

        for (const auto& obj : objectives) {
            // Build solver name for economy gym
            std::string solver_name = (obj == "production") ? "qubo" : ("qubo_" + obj);

            // Run economy gym to get the plan
            GameConfig config{};
            auto eco_state = run_economy_gym(solver_name, run_seed, nodes_hint, config);

            for (const auto& ord : orderings) {
                // Run ordering gym with the QUBO-derived plan
                auto ord_result = run_ordering_gym(solver_name, ord, run_seed, config);

                float efficiency = (eco_state.theoretical_max > 0.0f)
                    ? eco_state.production_rate / eco_state.theoretical_max : 0.0f;

                fprintf(csv, "%s,%s,%llu,%d,%.2f,%.4f,%d,%.2f,%d,%d\n",
                        obj.c_str(), ord.c_str(),
                        static_cast<unsigned long long>(run_seed),
                        eco_state.graph.num_nodes(),
                        eco_state.production_rate,
                        efficiency,
                        ord_result.total_ticks,
                        ord_result.accumulated_production,
                        eco_state.n_factories, eco_state.n_powerplants);

                // Per-tick production
                if (prod_file) {
                    for (int t = 0; t < static_cast<int>(ord_result.production_per_tick.size()); t++) {
                        fprintf(prod_file, "%s,%s,%llu,%d,%.2f\n",
                                obj.c_str(), ord.c_str(),
                                static_cast<unsigned long long>(run_seed),
                                t, ord_result.production_per_tick[t]);
                    }
                }

                printf("[%s x %s] seed=%llu nodes=%d prod=%.1f eff=%.1f%% ticks=%d accum=%.0f F=%d PP=%d\n",
                       obj.c_str(), ord.c_str(),
                       static_cast<unsigned long long>(run_seed),
                       eco_state.graph.num_nodes(),
                       eco_state.production_rate,
                       efficiency * 100.0f,
                       ord_result.total_ticks,
                       ord_result.accumulated_production,
                       eco_state.n_factories, eco_state.n_powerplants);
            }
        }
    }

    fclose(csv);
    if (prod_file) fclose(prod_file);
    printf("Results written to %s\n", output_path.c_str());
    return 0;
}
