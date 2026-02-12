#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "engine/game.hpp"
#include "ai/players/attention_ai_player.hpp"
#include "ai/sub_agents/economy_agent.hpp"
#include "ai/sub_agents/expansion_agent.hpp"
#include "ai/sub_agents/direct_war_agent.hpp"
#include "ai/sub_agents/bootstrap_economy_agent.hpp"
#include "ai/players/passive_player.hpp"

// --- Individual: 3 weights for v3_bootstrap sub-agents ---
struct Individual {
    float w[3]; // [0]=BootstrapEconomy, [1]=Expansion, [2]=DirectWar
    float fitness = 0.0f;
};

static std::unique_ptr<PlayerInterface> make_ai(int player_id, const float w[3]) {
    auto ai = std::make_unique<AttentionAIPlayer>(player_id, AttentionAIPlayer::NoDefaults{});
    ai->add_sub_agent(std::make_unique<BootstrapEconomySubAgent>(), w[0]);
    ai->add_sub_agent(std::make_unique<ExpansionSubAgent>(), w[1]);
    ai->add_sub_agent(std::make_unique<DirectWarSubAgent>(), w[2]);
    return ai;
}

// Play a single game, return 0 if p0 wins, 1 if p1 wins, -1 if draw/timeout
static int play_game(const float w0[3], const float w1[3], uint64_t seed, int max_ticks = 5000) {
    GameConfig config;
    config.poisson_intensity = 0.04f;
    config.region_width = 50.0f;
    config.region_height = 50.0f;
    config.edge_distance_threshold = 15.0f;
    config.max_neighbors = 7;
    config.init_default_troops = 25;

    Game game(config, {0, 1}, seed);
    int n_total = game.n_players();

    auto ai0 = make_ai(0, w0);
    auto ai1 = make_ai(1, w1);

    std::vector<std::unique_ptr<PlayerInterface>> ais;
    ais.push_back(std::move(ai0));
    ais.push_back(std::move(ai1));
    for (int i = 2; i < n_total; i++)
        ais.push_back(std::make_unique<PassivePlayer>());

    std::vector<PlayerCommands> commands(n_total);

    for (int tick = 0; tick < max_ticks; tick++) {
        for (int p = 0; p < n_total; p++) {
            commands[p] = PlayerCommands{};
            if (game.is_alive(p))
                ais[p]->decide(game, p, commands[p]);
        }
        game.tick(1.0f, commands);
        if (game.is_game_over()) {
            if (game.is_alive(0) && !game.is_alive(1)) return 0;
            if (game.is_alive(1) && !game.is_alive(0)) return 1;
            return -1;
        }
    }
    return -1; // timeout = draw
}

int main() {
    // GA parameters
    constexpr int POP_SIZE = 20;
    constexpr int GENERATIONS = 100;
    constexpr int GAMES_PER_MATCHUP = 6;   // games per pair in round-robin
    constexpr int BENCHMARK_GAMES = 100;  // games vs default per generation
    constexpr int ELITE_COUNT = 4;
    constexpr float INIT_PERTURBATION = 0.5f;
    constexpr float MIN_PERTURBATION = 0.02f;

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);

    // Default v3 weights for benchmarking
    const float default_w[3] = {1.0f, 1.0f, 1.0f};

    // Initialize population around default weights with some spread
    std::vector<Individual> pop(POP_SIZE);
    pop[0].w[0] = 1.0f; pop[0].w[1] = 1.0f; pop[0].w[2] = 1.0f; // seed with default
    for (int i = 1; i < POP_SIZE; i++) {
        for (int j = 0; j < 3; j++) {
            pop[i].w[j] = 1.0f + (uni(rng) - 0.5f) * 2.0f;
        }
    }

    // Fixed seeds for games (consistency across generations)
    std::vector<uint64_t> game_seeds;
    for (int i = 0; i < 2000; i++)
        game_seeds.push_back(rng());

    setbuf(stdout, nullptr); // unbuffered output
    printf("GA tuning v3_bootstrap weights: [BootstrapEco, Expansion, DirectWar]\n");
    printf("Pop=%d, Gens=%d, Games/matchup=%d, Benchmark=%d games vs default\n\n",
           POP_SIZE, GENERATIONS, GAMES_PER_MATCHUP, BENCHMARK_GAMES);

    auto t_start = std::chrono::high_resolution_clock::now();

    for (int gen = 0; gen < GENERATIONS; gen++) {
        float perturbation = INIT_PERTURBATION * std::pow(MIN_PERTURBATION / INIT_PERTURBATION,
                                                           static_cast<float>(gen) / (GENERATIONS - 1));

        // Round-robin: each individual plays against every other
        for (auto& ind : pop) ind.fitness = 0.0f;

        int total_games = 0;
        for (int i = 0; i < POP_SIZE; i++) {
            for (int j = i + 1; j < POP_SIZE; j++) {
                for (int g = 0; g < GAMES_PER_MATCHUP; g++) {
                    uint64_t seed = game_seeds[(i * POP_SIZE + j + g) % game_seeds.size()];
                    // Alternate who is P0/P1 for fairness
                    int result;
                    if (g % 2 == 0)
                        result = play_game(pop[i].w, pop[j].w, seed);
                    else
                        result = play_game(pop[j].w, pop[i].w, seed);

                    if (result == 0) {
                        if (g % 2 == 0) pop[i].fitness += 1.0f;
                        else pop[j].fitness += 1.0f;
                    } else if (result == 1) {
                        if (g % 2 == 0) pop[j].fitness += 1.0f;
                        else pop[i].fitness += 1.0f;
                    } else {
                        pop[i].fitness += 0.5f;
                        pop[j].fitness += 0.5f;
                    }
                    total_games++;
                }
            }
        }

        // Normalize fitness to win rate
        float max_possible = static_cast<float>((POP_SIZE - 1) * GAMES_PER_MATCHUP);
        for (auto& ind : pop)
            ind.fitness /= max_possible;

        // Sort by fitness descending
        std::sort(pop.begin(), pop.end(), [](const Individual& a, const Individual& b) {
            return a.fitness > b.fitness;
        });

        // Benchmark #1 vs default weights
        int wins_vs_default = 0;
        for (int g = 0; g < BENCHMARK_GAMES; g++) {
            uint64_t seed = game_seeds[(gen * 17 + g) % game_seeds.size()];
            int result;
            if (g % 2 == 0)
                result = play_game(pop[0].w, default_w, seed);
            else
                result = play_game(default_w, pop[0].w, seed);

            if (g % 2 == 0 && result == 0) wins_vs_default++;
            if (g % 2 == 1 && result == 1) wins_vs_default++;
        }
        float win_pct = 100.0f * wins_vs_default / BENCHMARK_GAMES;

        auto t_now = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t_now - t_start).count();

        printf("Gen %3d | best=[%.3f, %.3f, %.3f] fit=%.3f | vs_default=%.0f%% | "
               "pert=%.3f | %d games | %.1fs\n",
               gen, pop[0].w[0], pop[0].w[1], pop[0].w[2], pop[0].fitness,
               win_pct, perturbation, total_games, elapsed);

        if (gen == GENERATIONS - 1) break; // don't breed on last gen

        // Selection & breeding
        // Keep elite, fill rest by breeding top half
        std::vector<Individual> next_pop;
        for (int i = 0; i < ELITE_COUNT; i++)
            next_pop.push_back(pop[i]);

        int top_half = POP_SIZE / 2;
        while (static_cast<int>(next_pop.size()) < POP_SIZE) {
            // Pick two parents from top half
            int p1 = rng() % top_half;
            int p2 = rng() % top_half;
            while (p2 == p1) p2 = rng() % top_half;

            Individual child;
            for (int j = 0; j < 3; j++) {
                // Average + perturbation
                float avg = (pop[p1].w[j] + pop[p2].w[j]) * 0.5f;
                std::normal_distribution<float> noise(0.0f, perturbation);
                child.w[j] = avg + noise(rng);
            }
            next_pop.push_back(child);
        }
        pop = std::move(next_pop);
    }

    printf("\n=== FINAL BEST WEIGHTS ===\n");
    printf("BootstrapEconomy: %.4f\n", pop[0].w[0]);
    printf("Expansion:        %.4f\n", pop[0].w[1]);
    printf("DirectWar:        %.4f\n", pop[0].w[2]);
    printf("\nTop 5:\n");
    for (int i = 0; i < 5 && i < POP_SIZE; i++) {
        printf("  #%d: [%.4f, %.4f, %.4f] fitness=%.3f\n",
               i + 1, pop[i].w[0], pop[i].w[1], pop[i].w[2], pop[i].fitness);
    }

    return 0;
}
