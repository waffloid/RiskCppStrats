#ifndef CRISKY_COLOR_SCHEME_HPP
#define CRISKY_COLOR_SCHEME_HPP

#include "raylib.h"

enum EffectFlags {
    EFFECT_NONE        = 0,
    EFFECT_GLOW        = 1 << 0,  // Additive blend glow (Cyberpunk, Retrowave)
    EFFECT_GRADIENT_BG = 1 << 1,  // Gradient behind noise (Retrowave)
    EFFECT_SCANLINES   = 1 << 2,  // CRT scanline overlay (Terminal)
};

struct ColorScheme {
    const char* name;

    // Player colors (8 slots)
    Color player_colors[8];

    // Environment/UI colors
    Color background;
    Color edge_outer;
    Color edge_inner;
    Color unowned_node;
    Color shadow;
    Color node_outline;
    Color state_icon;
    Color text_separator;

    // Visual effects (bitwise OR of EffectFlags)
    int effect;
    static constexpr int MAX_GRADIENT_STOPS = 6;
    Color gradient_stops[MAX_GRADIENT_STOPS]; // For EFFECT_GRADIENT_BG
    int gradient_num_stops;                   // For EFFECT_GRADIENT_BG (0 = unused)
    float glow_radius_mult;  // For EFFECT_GLOW (multiplier of node radius)
    float glow_intensity;    // For EFFECT_GLOW (alpha value 0-1)

    // Computed: high-contrast system color (white or black) based on background luminance
    // Perceptual luminance is non-linear; threshold at ~75/255 so most mid-tone
    // and light backgrounds get dark SYS, only truly dark ones get white.
    Color sys_color() const {
        float lum = 0.299f * background.r + 0.587f * background.g + 0.114f * background.b;
        return (lum > 75.0f) ? Color{0, 0, 0, 128} : Color{255, 255, 255, 128};
    }
};

enum ColorSchemeId {
    SCHEME_DEFAULT = 0,
    SCHEME_CYBERPUNK,
    SCHEME_DESERT,
    SCHEME_OCEAN,
    SCHEME_NEON,
    SCHEME_FOREST,
    SCHEME_MONOCHROME,
    SCHEME_SOLARIZED,
    SCHEME_DRACULA,
    SCHEME_MONOKAI,
    SCHEME_NORD,
    SCHEME_GRUVBOX,
    SCHEME_RETROWAVE,
    SCHEME_VOLCANIC,
    SCHEME_ARCTIC,
    SCHEME_CANDY,
    SCHEME_TERMINAL,
};

inline const ColorScheme COLOR_SCHEMES[] = {
    // 0: Default (current colors)
    {
        "Default",
        {
            Color{255, 60, 60, 255},    // red
            Color{60, 120, 255, 255},   // blue
            Color{160, 160, 160, 255},  // grey
            Color{255, 220, 30, 255},   // yellow
            Color{180, 60, 255, 255},   // purple
            Color{255, 140, 30, 255},   // orange
            Color{255, 80, 180, 255},   // pink
            Color{30, 210, 230, 255},   // cyan
        },
        Color{50, 80, 40, 255},         // background
        Color{60, 55, 50, 140},         // edge_outer
        Color{150, 140, 130, 255},      // edge_inner
        Color{210, 210, 200, 255},      // unowned_node
        Color{0, 0, 0, 60},            // shadow
        Color{60, 55, 50, 100},         // node_outline
        Color{255, 255, 255, 200},      // state_icon
        Color{80, 70, 60, 200},         // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 1: Cyberpunk (neon on dark)
    {
        "Cyberpunk",
        {
            Color{255, 20, 147, 255},   // hot pink
            Color{0, 255, 255, 255},    // cyan
            Color{148, 0, 211, 255},    // purple
            Color{50, 255, 50, 255},    // lime green
            Color{255, 165, 0, 255},    // orange
            Color{0, 120, 255, 255},    // electric blue
            Color{255, 0, 200, 255},    // magenta
            Color{255, 255, 0, 255},    // yellow
        },
        Color{10, 15, 25, 255},         // background
        Color{20, 30, 60, 180},         // edge_outer
        Color{0, 100, 255, 200},        // edge_inner (neon blue glow)
        Color{40, 45, 60, 255},         // unowned_node
        Color{0, 0, 0, 80},            // shadow
        Color{0, 80, 200, 120},         // node_outline
        Color{200, 220, 255, 220},      // state_icon
        Color{0, 150, 255, 200},        // text_separator
        EFFECT_GLOW, {}, 0, 2.5f, 0.4f,
    },

    // 2: Desert (warm earth tones)
    {
        "Desert",
        {
            Color{180, 80, 60, 255},    // terracotta
            Color{220, 200, 120, 255},  // sandy yellow
            Color{210, 130, 50, 255},   // burnt orange
            Color{200, 140, 140, 255},  // dusty rose
            Color{140, 170, 120, 255},  // sage green
            Color{190, 160, 60, 255},   // ochre
            Color{170, 80, 40, 255},    // rust
            Color{240, 230, 210, 255},  // cream
        },
        Color{200, 180, 140, 255},      // background
        Color{140, 120, 90, 180},       // edge_outer
        Color{180, 165, 130, 255},      // edge_inner
        Color{220, 210, 190, 255},      // unowned_node
        Color{80, 60, 40, 50},          // shadow
        Color{120, 100, 70, 120},       // node_outline
        Color{100, 80, 60, 200},        // state_icon
        Color{140, 120, 90, 200},       // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 3: Ocean (cool blues and teals)
    {
        "Ocean",
        {
            Color{255, 100, 80, 255},   // coral
            Color{64, 224, 208, 255},   // turquoise
            Color{30, 40, 100, 255},    // navy
            Color{100, 220, 180, 255},  // seafoam
            Color{75, 0, 130, 255},     // indigo
            Color{0, 200, 200, 255},    // aqua
            Color{0, 128, 128, 255},    // teal
            Color{240, 240, 250, 255},  // pearl
        },
        Color{20, 40, 80, 255},         // background
        Color{15, 40, 60, 180},         // edge_outer
        Color{60, 140, 160, 200},       // edge_inner
        Color{140, 160, 180, 255},      // unowned_node
        Color{0, 0, 20, 60},           // shadow
        Color{30, 80, 100, 120},        // node_outline
        Color{200, 230, 255, 200},      // state_icon
        Color{60, 100, 140, 200},       // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 4: Neon (bright saturated on black)
    {
        "Neon",
        {
            Color{255, 16, 240, 255},   // neon pink
            Color{57, 255, 20, 255},    // neon green
            Color{0, 100, 255, 255},    // electric blue
            Color{255, 255, 0, 255},    // neon yellow
            Color{180, 0, 255, 255},    // neon purple
            Color{255, 100, 0, 255},    // neon orange
            Color{255, 0, 144, 255},    // hot magenta
            Color{128, 255, 0, 255},    // lime
        },
        Color{0, 0, 0, 255},            // background
        Color{30, 30, 30, 180},         // edge_outer
        Color{160, 160, 160, 200},      // edge_inner (bright outline)
        Color{40, 40, 40, 255},         // unowned_node
        Color{0, 0, 0, 100},           // shadow
        Color{80, 80, 80, 150},         // node_outline
        Color{255, 255, 255, 230},      // state_icon
        Color{120, 120, 120, 200},      // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 5: Forest (natural greens)
    {
        "Forest",
        {
            Color{180, 30, 30, 255},    // crimson
            Color{220, 200, 50, 255},   // golden yellow
            Color{100, 140, 60, 255},   // moss green
            Color{34, 100, 34, 255},    // forest green
            Color{120, 80, 50, 255},    // bark brown
            Color{220, 130, 40, 255},   // autumn orange
            Color{120, 50, 140, 255},   // berry purple
            Color{100, 180, 230, 255},  // sky blue
        },
        Color{30, 60, 40, 255},         // background
        Color{50, 35, 20, 180},         // edge_outer (dark brown)
        Color{120, 90, 60, 255},        // edge_inner (lighter wood)
        Color{140, 180, 120, 255},      // unowned_node (light leaf green)
        Color{0, 0, 0, 50},            // shadow
        Color{50, 40, 30, 120},         // node_outline
        Color{230, 240, 220, 200},      // state_icon
        Color{80, 60, 40, 200},         // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 6: Monochrome (B&W with grey scale)
    {
        "Monochrome",
        {
            Color{10, 10, 10, 255},     // black
            Color{245, 245, 245, 255},  // white
            Color{190, 190, 190, 255},  // light grey
            Color{60, 60, 60, 255},     // dark grey
            Color{128, 128, 128, 255},  // medium grey
            Color{230, 230, 230, 255},  // off-white
            Color{45, 45, 45, 255},     // charcoal
            Color{200, 200, 200, 255},  // silver
        },
        Color{100, 100, 100, 255},      // background
        Color{50, 50, 50, 180},         // edge_outer
        Color{170, 170, 170, 255},      // edge_inner
        Color{240, 240, 240, 255},      // unowned_node
        Color{0, 0, 0, 60},            // shadow
        Color{60, 60, 60, 120},         // node_outline
        Color{255, 255, 255, 200},      // state_icon
        Color{80, 80, 80, 200},         // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 7: Solarized Dark
    {
        "Solarized",
        {
            Color{220, 50, 47, 255},    // red
            Color{38, 139, 210, 255},   // blue
            Color{133, 153, 0, 255},    // green
            Color{181, 137, 0, 255},    // yellow
            Color{211, 54, 130, 255},   // magenta
            Color{203, 75, 22, 255},    // orange
            Color{108, 113, 196, 255},  // violet
            Color{42, 161, 152, 255},   // cyan
        },
        Color{0, 43, 54, 255},          // background (base03)
        Color{7, 54, 66, 180},          // edge_outer (base02)
        Color{88, 110, 117, 200},       // edge_inner (base01)
        Color{131, 148, 150, 255},      // unowned_node (base0)
        Color{0, 0, 0, 60},            // shadow
        Color{7, 54, 66, 140},          // node_outline (base02)
        Color{238, 232, 213, 200},      // state_icon (base2)
        Color{88, 110, 117, 200},       // text_separator (base01)
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 8: Dracula
    {
        "Dracula",
        {
            Color{255, 85, 85, 255},    // red
            Color{139, 233, 253, 255},  // cyan
            Color{80, 250, 123, 255},   // green
            Color{241, 250, 140, 255},  // yellow
            Color{189, 147, 249, 255},  // purple
            Color{255, 184, 108, 255},  // orange
            Color{255, 121, 198, 255},  // pink
            Color{248, 248, 242, 255},  // foreground
        },
        Color{40, 42, 54, 255},         // background
        Color{68, 71, 90, 180},         // edge_outer (current line)
        Color{98, 114, 164, 200},       // edge_inner (comment)
        Color{68, 71, 90, 255},         // unowned_node
        Color{0, 0, 0, 70},            // shadow
        Color{98, 114, 164, 120},       // node_outline
        Color{248, 248, 242, 200},      // state_icon
        Color{98, 114, 164, 200},       // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 9: Monokai
    {
        "Monokai",
        {
            Color{249, 38, 114, 255},   // pink
            Color{102, 217, 239, 255},  // cyan
            Color{166, 226, 46, 255},   // green
            Color{230, 219, 116, 255},  // yellow
            Color{174, 129, 255, 255},  // purple
            Color{253, 151, 31, 255},   // orange
            Color{249, 38, 114, 255},   // rose
            Color{248, 248, 242, 255},  // foreground
        },
        Color{39, 40, 34, 255},         // background
        Color{60, 60, 50, 180},         // edge_outer
        Color{117, 113, 94, 200},       // edge_inner (comment grey)
        Color{80, 80, 68, 255},         // unowned_node
        Color{0, 0, 0, 70},            // shadow
        Color{70, 70, 58, 130},         // node_outline
        Color{248, 248, 242, 200},      // state_icon
        Color{117, 113, 94, 200},       // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 10: Nord
    {
        "Nord",
        {
            Color{191, 97, 106, 255},   // aurora red
            Color{136, 192, 208, 255},  // frost blue
            Color{163, 190, 140, 255},  // aurora green
            Color{235, 203, 139, 255},  // aurora yellow
            Color{180, 142, 173, 255},  // aurora purple
            Color{208, 135, 112, 255},  // aurora orange
            Color{143, 188, 187, 255},  // frost teal
            Color{229, 233, 240, 255},  // snow storm
        },
        Color{46, 52, 64, 255},         // background (polar night)
        Color{59, 66, 82, 180},         // edge_outer (polar night 1)
        Color{76, 86, 106, 200},        // edge_inner (polar night 3)
        Color{76, 86, 106, 255},        // unowned_node
        Color{0, 0, 0, 50},            // shadow
        Color{59, 66, 82, 140},         // node_outline
        Color{229, 233, 240, 200},      // state_icon (snow storm)
        Color{76, 86, 106, 200},        // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 11: Gruvbox
    {
        "Gruvbox",
        {
            Color{204, 36, 29, 255},    // red
            Color{69, 133, 136, 255},   // aqua
            Color{152, 151, 26, 255},   // green
            Color{215, 153, 33, 255},   // yellow
            Color{177, 98, 134, 255},   // purple
            Color{214, 93, 14, 255},    // orange
            Color{104, 157, 106, 255},  // faded green
            Color{251, 241, 199, 255},  // fg
        },
        Color{40, 40, 40, 255},         // background (bg0)
        Color{60, 56, 54, 180},         // edge_outer (bg1)
        Color{124, 111, 100, 200},      // edge_inner (grey)
        Color{80, 73, 69, 255},         // unowned_node (bg2)
        Color{0, 0, 0, 60},            // shadow
        Color{60, 56, 54, 140},         // node_outline
        Color{235, 219, 178, 200},      // state_icon (fg)
        Color{124, 111, 100, 200},      // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 12: Retrowave (80s synthwave)
    {
        "Retrowave",
        {
            Color{255, 50, 150, 255},   // hot pink
            Color{0, 220, 255, 255},    // electric cyan
            Color{180, 60, 255, 255},   // deep purple
            Color{255, 200, 50, 255},   // sunset yellow
            Color{255, 100, 50, 255},   // sunset orange
            Color{100, 255, 200, 255},  // mint
            Color{255, 0, 200, 255},    // magenta
            Color{150, 100, 255, 255},  // lavender
        },
        Color{15, 5, 30, 255},          // background (deep purple-black)
        Color{40, 10, 60, 180},         // edge_outer
        Color{180, 50, 200, 180},       // edge_inner (purple glow)
        Color{50, 30, 70, 255},         // unowned_node
        Color{0, 0, 0, 80},            // shadow
        Color{100, 20, 140, 120},       // node_outline
        Color{255, 200, 255, 210},      // state_icon
        Color{180, 80, 220, 200},       // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 13: Volcanic (lava and obsidian)
    {
        "Volcanic",
        {
            Color{255, 80, 20, 255},    // lava orange
            Color{255, 200, 40, 255},   // molten yellow
            Color{200, 30, 10, 255},    // deep red
            Color{255, 140, 60, 255},   // ember
            Color{180, 50, 20, 255},    // dark lava
            Color{255, 160, 80, 255},   // bright ember
            Color{140, 20, 10, 255},    // dark magma
            Color{255, 220, 120, 255},  // hot glow
        },
        Color{25, 20, 20, 255},         // background (obsidian)
        Color{50, 25, 15, 180},         // edge_outer (dark rock)
        Color{140, 50, 20, 180},        // edge_inner (magma crack)
        Color{60, 50, 45, 255},         // unowned_node (cool rock)
        Color{0, 0, 0, 80},            // shadow
        Color{80, 30, 10, 130},         // node_outline
        Color{255, 220, 180, 200},      // state_icon
        Color{140, 60, 20, 200},        // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 14: Arctic (ice and aurora)
    {
        "Arctic",
        {
            Color{60, 80, 160, 255},    // deep ice blue
            Color{80, 200, 120, 255},   // aurora green
            Color{160, 60, 200, 255},   // aurora purple
            Color{40, 160, 180, 255},   // glacier teal
            Color{200, 80, 140, 255},   // aurora pink
            Color{100, 140, 220, 255},  // sky blue
            Color{50, 130, 100, 255},   // deep aurora
            Color{30, 50, 100, 255},    // midnight blue
        },
        Color{230, 235, 240, 255},      // background (snow)
        Color{180, 195, 210, 180},      // edge_outer (ice shadow)
        Color{200, 215, 230, 255},      // edge_inner (pale ice)
        Color{210, 220, 230, 255},      // unowned_node (light ice)
        Color{100, 120, 140, 40},       // shadow (blue-grey)
        Color{170, 185, 200, 120},      // node_outline
        Color{40, 50, 70, 180},         // state_icon (dark for contrast)
        Color{140, 160, 180, 200},      // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 15: Candy (pastel pop)
    {
        "Candy",
        {
            Color{255, 130, 170, 255},  // bubblegum pink
            Color{130, 220, 190, 255},  // mint
            Color{180, 150, 230, 255},  // lavender
            Color{255, 200, 140, 255},  // peach
            Color{255, 240, 130, 255},  // lemon
            Color{140, 200, 255, 255},  // baby blue
            Color{255, 160, 130, 255},  // salmon
            Color{170, 230, 150, 255},  // lime sorbet
        },
        Color{250, 245, 250, 255},      // background (soft white-pink)
        Color{210, 200, 220, 180},      // edge_outer
        Color{230, 220, 235, 255},      // edge_inner
        Color{235, 230, 240, 255},      // unowned_node
        Color{120, 100, 140, 35},       // shadow (light purple)
        Color{200, 190, 210, 120},      // node_outline
        Color{80, 60, 100, 170},        // state_icon (dark for contrast)
        Color{180, 160, 200, 200},      // text_separator
        EFFECT_NONE, {}, 0, 0.0f, 0.0f,
    },

    // 16: Terminal (green phosphor CRT)
    {
        "Terminal",
        {
            Color{0, 255, 65, 255},     // bright green
            Color{0, 200, 50, 255},     // medium green
            Color{0, 160, 40, 255},     // dark green
            Color{0, 255, 130, 255},    // green-cyan
            Color{50, 255, 50, 255},    // lime green
            Color{0, 220, 80, 255},     // green
            Color{0, 180, 60, 255},     // forest green
            Color{100, 255, 100, 255},  // pale green
        },
        Color{5, 5, 5, 255},            // background (CRT black)
        Color{0, 40, 10, 180},          // edge_outer (dim green)
        Color{0, 100, 25, 180},         // edge_inner (green scanline)
        Color{0, 80, 20, 255},          // unowned_node (dim terminal)
        Color{0, 0, 0, 80},            // shadow
        Color{0, 60, 15, 140},          // node_outline
        Color{0, 255, 65, 200},         // state_icon (phosphor green)
        Color{0, 120, 30, 200},         // text_separator
        EFFECT_SCANLINES, {}, 0, 0.0f, 0.0f,
    },
};

inline const int NUM_COLOR_SCHEMES = sizeof(COLOR_SCHEMES) / sizeof(COLOR_SCHEMES[0]);

#endif
