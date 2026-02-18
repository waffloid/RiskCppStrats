#ifndef CRISKY_VIZ_IMGUI_THEME_HPP
#define CRISKY_VIZ_IMGUI_THEME_HPP

#include "renderer/color_scheme.hpp"
#include "imgui.h"
#include "implot.h"

#include <algorithm>
#include <cmath>

// Convert Raylib Color to ImGui packed color (ABGR).
inline unsigned int color_to_im(Color c) {
    return IM_COL32(c.r, c.g, c.b, c.a);
}

// Return a theme-appropriate metric color (for non-player chart lines).
// index 0..3 cycles through scheme player colors 0,2,3,4 (skipping 1 which
// is the opponent in 2-player setups).
inline unsigned int scheme_metric_color(const ColorScheme& scheme, int index) {
    static constexpr int slots[] = {0, 2, 3, 4};
    int i = slots[index & 3];
    return color_to_im(scheme.player_colors[i]);
}

// Derive ImGui + ImPlot style from the game's ColorScheme.
// Call once per frame (cheap — just sets style arrays).
inline void apply_imgui_theme(const ColorScheme& scheme) {
    float bg_r = scheme.background.r / 255.0f;
    float bg_g = scheme.background.g / 255.0f;
    float bg_b = scheme.background.b / 255.0f;

    float lum = 0.299f * bg_r + 0.587f * bg_g + 0.114f * bg_b;
    bool dark = lum < 0.5f;

    // Multiplicative scale preserves hue/saturation of the background.
    // Dark themes: panels slightly brighter. Light themes: panels slightly darker.
    float win_scale = dark ? 1.25f : 0.85f;
    float win_r = std::clamp(bg_r * win_scale, 0.0f, 1.0f);
    float win_g = std::clamp(bg_g * win_scale, 0.0f, 1.0f);
    float win_b = std::clamp(bg_b * win_scale, 0.0f, 1.0f);

    // Minimum brightness floor so near-black backgrounds still get visible panels
    float win_min = dark ? 0.08f : 0.0f;
    win_r = std::max(win_r, win_min);
    win_g = std::max(win_g, win_min);
    win_b = std::max(win_b, win_min);

    // Layers via small additive steps (for internal contrast between components).
    // Direction: lighter on dark, darker on light.
    float dir = dark ? 1.0f : -1.0f;
    float step = 0.05f;

    auto layer = [&](float base_r, float base_g, float base_b, float offset) -> ImVec4 {
        return {std::clamp(base_r + dir * step * offset, 0.0f, 1.0f),
                std::clamp(base_g + dir * step * offset, 0.0f, 1.0f),
                std::clamp(base_b + dir * step * offset, 0.0f, 1.0f), 1.0f};
    };

    // Layer stack (from recessed to raised):
    //   frame (-1) < window (0) < button (+0.5) < title (+1.5) < border (+2.5)
    ImVec4 frame_bg = layer(win_r, win_g, win_b, -1.0f);
    ImVec4 btn_bg   = layer(win_r, win_g, win_b, 0.5f);
    ImVec4 title_bg = layer(win_r, win_g, win_b, 1.5f);
    ImVec4 border   = layer(win_r, win_g, win_b, 2.5f);
    border.w = 0.6f;

    // Hover/active: further step from each base
    ImVec4 frame_hov = layer(frame_bg.x, frame_bg.y, frame_bg.z, 1.0f);
    ImVec4 frame_act = layer(frame_bg.x, frame_bg.y, frame_bg.z, 2.0f);
    ImVec4 btn_hov   = layer(btn_bg.x, btn_bg.y, btn_bg.z, 1.0f);
    ImVec4 btn_act   = layer(btn_bg.x, btn_bg.y, btn_bg.z, 2.0f);

    // Text: high contrast against window
    float win_lum = 0.299f * win_r + 0.587f * win_g + 0.114f * win_b;
    ImVec4 text_col = win_lum > 0.5f
        ? ImVec4{0.05f, 0.05f, 0.05f, 1.0f}
        : ImVec4{0.93f, 0.93f, 0.93f, 1.0f};
    ImVec4 text_dim = {text_col.x, text_col.y, text_col.z, 0.5f};

    // Accent: edge_inner is a neutral tone designed for each scheme.
    // Used sparingly — only for interactive grab elements and the active tab overline.
    float ei_r = scheme.edge_inner.r / 255.0f;
    float ei_g = scheme.edge_inner.g / 255.0f;
    float ei_b = scheme.edge_inner.b / 255.0f;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    ImVec4* c = style.Colors;

    // --- Backgrounds ---
    c[ImGuiCol_WindowBg]       = {win_r, win_g, win_b, 0.95f};
    c[ImGuiCol_ChildBg]        = {win_r, win_g, win_b, 0.0f};
    c[ImGuiCol_PopupBg]        = {frame_bg.x, frame_bg.y, frame_bg.z, 0.97f};

    // --- Text ---
    c[ImGuiCol_Text]           = text_col;
    c[ImGuiCol_TextDisabled]   = text_dim;

    // --- Borders ---
    c[ImGuiCol_Border]         = border;
    c[ImGuiCol_BorderShadow]   = {0, 0, 0, 0};

    // --- Title bar ---
    c[ImGuiCol_TitleBg]           = title_bg;
    c[ImGuiCol_TitleBgActive]     = layer(title_bg.x, title_bg.y, title_bg.z, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]  = frame_bg;

    // --- Frames (recessed) ---
    c[ImGuiCol_FrameBg]        = frame_bg;
    c[ImGuiCol_FrameBgHovered] = frame_hov;
    c[ImGuiCol_FrameBgActive]  = frame_act;

    // --- Buttons ---
    c[ImGuiCol_Button]         = btn_bg;
    c[ImGuiCol_ButtonHovered]  = btn_hov;
    c[ImGuiCol_ButtonActive]   = btn_act;

    // --- Tabs ---
    c[ImGuiCol_Tab]                = frame_bg;
    c[ImGuiCol_TabHovered]         = btn_hov;
    c[ImGuiCol_TabSelected]        = title_bg;
    c[ImGuiCol_TabSelectedOverline]= {ei_r, ei_g, ei_b, 0.7f};
    c[ImGuiCol_TabDimmed]          = {frame_bg.x, frame_bg.y, frame_bg.z, 0.7f};
    c[ImGuiCol_TabDimmedSelected]  = {win_r, win_g, win_b, 0.9f};

    // --- Headers ---
    c[ImGuiCol_Header]         = frame_bg;
    c[ImGuiCol_HeaderHovered]  = btn_hov;
    c[ImGuiCol_HeaderActive]   = btn_act;

    // --- Separator ---
    c[ImGuiCol_Separator]        = border;
    c[ImGuiCol_SeparatorHovered] = {ei_r, ei_g, ei_b, 0.6f};
    c[ImGuiCol_SeparatorActive]  = {ei_r, ei_g, ei_b, 0.9f};

    // --- Resize grip ---
    c[ImGuiCol_ResizeGrip]        = {border.x, border.y, border.z, 0.3f};
    c[ImGuiCol_ResizeGripHovered] = {ei_r, ei_g, ei_b, 0.5f};
    c[ImGuiCol_ResizeGripActive]  = {ei_r, ei_g, ei_b, 0.8f};

    // --- Scrollbar ---
    c[ImGuiCol_ScrollbarBg]          = {frame_bg.x, frame_bg.y, frame_bg.z, 0.5f};
    c[ImGuiCol_ScrollbarGrab]        = {border.x, border.y, border.z, 0.5f};
    c[ImGuiCol_ScrollbarGrabHovered] = {ei_r, ei_g, ei_b, 0.5f};
    c[ImGuiCol_ScrollbarGrabActive]  = {ei_r, ei_g, ei_b, 0.8f};

    // --- Slider grab / check mark: edge_inner accent ---
    c[ImGuiCol_SliderGrab]        = {ei_r, ei_g, ei_b, 0.7f};
    c[ImGuiCol_SliderGrabActive]  = {ei_r, ei_g, ei_b, 1.0f};
    c[ImGuiCol_CheckMark]         = {ei_r, ei_g, ei_b, 0.9f};

    // --- Docking ---
    c[ImGuiCol_DockingPreview]    = {ei_r, ei_g, ei_b, 0.4f};
    c[ImGuiCol_DockingEmptyBg]    = {frame_bg.x, frame_bg.y, frame_bg.z, 1.0f};

    // --- ImPlot ---
    ImPlotStyle& pstyle = ImPlot::GetStyle();
    ImVec4* pc = pstyle.Colors;
    pc[ImPlotCol_FrameBg]      = {frame_bg.x, frame_bg.y, frame_bg.z, 0.5f};
    pc[ImPlotCol_PlotBg]       = {frame_bg.x, frame_bg.y, frame_bg.z, 0.3f};
    pc[ImPlotCol_PlotBorder]   = border;
    pc[ImPlotCol_LegendBg]     = {win_r, win_g, win_b, 0.9f};
    pc[ImPlotCol_LegendBorder] = border;
    pc[ImPlotCol_TitleText]    = text_col;
    pc[ImPlotCol_AxisText]     = text_col;
    pc[ImPlotCol_LegendText]   = text_col;
    pc[ImPlotCol_InlayText]    = text_col;
    pc[ImPlotCol_AxisGrid]     = {border.x, border.y, border.z, 0.3f};
    pc[ImPlotCol_AxisTick]     = {border.x, border.y, border.z, 0.5f};
}

#endif
