#include "gyms/transport_gym.hpp"
#include "systems/transport/transport_solvers.hpp"
#include "systems/transport/loss_functions.hpp"
#include "systems/common/data_sink.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void print_usage() {
    std::printf("Usage: gym_transport [options]\n");
    std::printf("  --preset=NAME    Transport preset (default: star_center)\n");
    std::printf("  --solver=NAME    Transport solver (default: greedy)\n");
    std::printf("  --loss=NAME      Loss function (default: l1)\n");
    std::printf("  --ticks=N        Max ticks (default: 500)\n");
    std::printf("  --output=PATH    Output CSV path (default: output/transport/results.csv)\n");

    std::printf("\nAvailable presets:\n");
    for (const auto& name : list_transport_presets()) {
        std::printf("  %s\n", name.c_str());
    }
    std::printf("\nAvailable solvers:\n");
    for (const auto& name : list_transport_solvers()) {
        std::printf("  %s\n", name.c_str());
    }
    std::printf("\nAvailable loss functions:\n");
    for (const auto& name : list_loss_functions()) {
        std::printf("  %s\n", name.c_str());
    }
}

int main(int argc, char* argv[]) {
    std::string preset_name = "star_center";
    std::string solver_name = "greedy";
    std::string loss_name = "l1";
    int max_ticks = 500;
    std::string output_path = "output/transport/results.csv";

    for (int i = 1; i < argc; i++) {
        if (std::strncmp(argv[i], "--preset=", 9) == 0)
            preset_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--solver=", 9) == 0)
            solver_name = argv[i] + 9;
        else if (std::strncmp(argv[i], "--loss=", 7) == 0)
            loss_name = argv[i] + 7;
        else if (std::strncmp(argv[i], "--ticks=", 8) == 0)
            max_ticks = std::atoi(argv[i] + 8);
        else if (std::strncmp(argv[i], "--output=", 9) == 0)
            output_path = argv[i] + 9;
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }

    auto preset = get_transport_preset(preset_name);
    if (!preset) {
        std::fprintf(stderr, "Unknown preset: %s\n", preset_name.c_str());
        print_usage();
        return 1;
    }
    {
        auto known = list_transport_solvers();
        bool valid = false;
        for (const auto& s : known) {
            if (s == solver_name) { valid = true; break; }
        }
        if (!valid) {
            std::fprintf(stderr, "Unknown solver: %s\n", solver_name.c_str());
            return 1;
        }
    }
    if (!get_loss_function(loss_name)) {
        std::fprintf(stderr, "Unknown loss function: %s\n", loss_name.c_str());
        return 1;
    }

    std::printf("Transport Gym: preset=%s, solver=%s, loss=%s, ticks=%d\n",
                preset_name.c_str(), solver_name.c_str(), loss_name.c_str(), max_ticks);

    TransportGymResult r = run_transport_gym(*preset, solver_name, loss_name, max_ticks);

    // Write per-tick CSV
    DataSink sink(output_path, {
        "tick", "loss", "loss_delta", "in_transit"
    });

    for (int t = 0; t < static_cast<int>(r.ticks.size()); t++) {
        const auto& td = r.ticks[t];
        sink.write_row({
            static_cast<double>(t),
            static_cast<double>(td.loss),
            static_cast<double>(td.loss_delta),
            static_cast<double>(td.troops_in_transit)
        });
    }
    sink.flush();

    std::printf("Initial loss: %.1f, Final loss: %.1f\n", r.initial_loss, r.final_loss);
    if (r.ticks_to_converge > 0)
        std::printf("Converged at tick %d\n", r.ticks_to_converge);
    else
        std::printf("Did not converge within %d ticks\n", max_ticks);
    std::printf("Written to %s (%d rows)\n", output_path.c_str(), sink.rows_written());
    return 0;
}
