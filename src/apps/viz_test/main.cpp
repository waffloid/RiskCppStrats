#include "viz/ring_buffer.hpp"
#include "viz/panel_host.hpp"

#include "raylib.h"
#include "imgui.h"
#include "implot.h"
#include "rlImGui.h"

#include <cmath>
#include <cstdio>
#include <vector>

// Simple demo panel that plots a sine wave from a RingBuffer.
class SineWavePanel : public Panel {
public:
    explicit SineWavePanel(RingBuffer<float>& buf) : buf_(buf) {}

    void draw() override {
        auto data = buf_.to_vector();
        if (data.empty()) return;

        if (ImPlot::BeginPlot("##sine", ImVec2(-1, 200))) {
            ImPlot::PlotLine("sin(t)", data.data(), static_cast<int>(data.size()));
            ImPlot::EndPlot();
        }

        ImGui::Text("Buffer: %d / %d samples", buf_.size(), buf_.capacity());
    }

    const char* title() const override { return "Sine Wave"; }

private:
    RingBuffer<float>& buf_;
};

int main() {
    std::printf("CRisky Viz Test — ImGui + ImPlot + RingBuffer\n");

    InitWindow(1200, 800, "CRisky Viz Test");
    SetTargetFPS(60);

    rlImGuiSetup(true);
    ImPlot::CreateContext();

    // Enable docking
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    RingBuffer<float> sine_buf(512);
    PanelHost host;
    host.add(std::make_unique<SineWavePanel>(sine_buf));

    int tick = 0;

    while (!WindowShouldClose()) {
        // Push data
        sine_buf.push(std::sin(static_cast<float>(tick) * 0.05f));
        tick++;

        BeginDrawing();
        ClearBackground(DARKGRAY);

        // Raylib content
        DrawText("CRisky Viz Test", 10, 10, 20, LIGHTGRAY);
        DrawText("ImGui panels should appear as dockable windows", 10, 40, 16, GRAY);

        // ImGui frame
        rlImGuiBegin();

        // Dockspace over full viewport
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Draw ImGui demo for reference
        ImGui::ShowDemoWindow();

        // Draw our panels
        host.draw();

        rlImGuiEnd();

        EndDrawing();
    }

    ImPlot::DestroyContext();
    rlImGuiShutdown();
    CloseWindow();

    return 0;
}
