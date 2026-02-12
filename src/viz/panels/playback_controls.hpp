#ifndef CRISKY_VIZ_PLAYBACK_CONTROLS_HPP
#define CRISKY_VIZ_PLAYBACK_CONTROLS_HPP

#include "viz/panel.hpp"

// Playback control panel: pause, step, speed slider.
// Operates on external state (pointers to speed and paused flags).
class PlaybackControls : public Panel {
public:
    PlaybackControls(float* speed, bool* paused, int* tick = nullptr);

    void draw() override;
    const char* title() const override { return "Playback"; }

private:
    float* speed_;
    bool* paused_;
    int* tick_;  // optional: display current tick
};

#endif
