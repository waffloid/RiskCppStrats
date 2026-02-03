#ifndef CRISKY_ATTENTION_AI_HPP
#define CRISKY_ATTENTION_AI_HPP

#include <vector>
#include "player/player_interface.hpp"

class AttentionAI : public PlayerInterface {
public:
    explicit AttentionAI(int player_id);

    void decide(const Game& game, int player_id, PlayerCommands& out) override;

private:
    // Persistent state
    std::vector<float> attention_;
    int player_id_;
    bool initialized_ = false;

    // Constants (matching Python)
    static constexpr float DIFFUSION_RATE = 0.02f;
    static constexpr float OUTFLOW_RATE = 0.1f;
    static constexpr int   MIN_TROOPS_TO_SEND = 5;
    static constexpr float ATTENTION_UNOCCUPIED_BORDER = 5.0f;
    static constexpr float ATTENTION_FACTORY_POWERPLANT_DELTA_DESIRE = 0.8f;

    void generate_attention_deltas(const Game& game, std::vector<float>& deltas);
    void diffuse_attention(const Game& game);
    void execute_troop_flow(const Game& game, PlayerCommands& out);
    void try_build_structures(const Game& game, PlayerCommands& out);
};

#endif
