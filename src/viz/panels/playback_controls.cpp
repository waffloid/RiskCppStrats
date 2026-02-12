#include "viz/panels/playback_controls.hpp"
#include "imgui.h"

PlaybackControls::PlaybackControls(float* speed, bool* paused, int* tick)
    : speed_(speed), paused_(paused), tick_(tick) {}

void PlaybackControls::draw() {
    if (tick_) {
        ImGui::Text("Tick: %d", *tick_);
        ImGui::Separator();
    }

    if (ImGui::Button(*paused_ ? "Play" : "Pause")) {
        *paused_ = !*paused_;
    }

    ImGui::SameLine();
    if (ImGui::Button("Step") && *paused_) {
        // Signal one step: temporarily unpause (caller checks this)
        *paused_ = false;
    }

    ImGui::SliderFloat("Speed", speed_, 0.1f, 10.0f, "%.1fx");

    if (ImGui::Button("1x")) *speed_ = 1.0f;
    ImGui::SameLine();
    if (ImGui::Button("2x")) *speed_ = 2.0f;
    ImGui::SameLine();
    if (ImGui::Button("5x")) *speed_ = 5.0f;
    ImGui::SameLine();
    if (ImGui::Button("10x")) *speed_ = 10.0f;
}
