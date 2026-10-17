/* FOMAJAM's color themes: one for each FM-1 color that stores list (black, grey, blue, green, dark green,
 * purple, red, orange; owners also mention teal), plus two extras. A theme added here appears on the GLOBAL page.
 *
 *   bg, panel   the screen background; the header and knob bar
 *   off, beat   an empty step; an empty step on a beat (1, 5, 9, 13)
 *   text, dim   values; anything switched off or muted
 *   accent      the page name and the playhead
 *   knob[4]     KNOB1-4, the same color on every page
 *   track[9]    tracks 1-9 (also a synth's steps); the first 8 also color the drum voices: kick, snare, clap,
 *               closed hi-hat, open hi-hat and cymbals, low toms, high toms, bells */
#ifndef THEMES_H
#define THEMES_H
#include "groove.h"

#define HEX(c) RGB(((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)

typedef struct {
  const char *name;
  uint16_t bg, panel, off, beat, text, dim, accent;
  uint16_t knob[4];
  uint16_t track[TRACKS];
} theme_t;

/* the steps' and tracks' colors, shared by the finish themes (a pattern reads the same in any of them);
 * ORANGE, TEAL, WHITE and NEON have their own */
#define RAINBOW \
  {HEX(0xff4d4d), HEX(0xff8c1a), HEX(0xffd21f), HEX(0x9be33a), HEX(0x2ed39a), HEX(0x22c3e6), HEX(0x4d8bff), HEX(0xa36bff), HEX(0xff5cc8)}
#define KNOBS {HEX(0xff5a5a), HEX(0xffc21f), HEX(0x3ad6a0), HEX(0x4aa8ff)}

/* a dark theme: the screen, the header and knob bar, an empty step, an empty beat, the text, the dim, the accent */
#define DARK(name, bg, panel, off, beat, text, dim, accent) \
  {name, HEX(bg), HEX(panel), HEX(off), HEX(beat), HEX(text), HEX(dim), HEX(accent), KNOBS, RAINBOW}

static const theme_t THEMES[] = {
    DARK("BLACK", 0x000000, 0x17171a, 0x26262b, 0x3c3c44, 0xf2f2f2, 0x6e6e78, 0xff9a2e),
    DARK("GREY", 0x101114, 0x25272c, 0x31343b, 0x484c55, 0xf0f2f5, 0x7c818c, 0xd4d9e2),
    DARK("BLUE", 0x060a18, 0x0f1a36, 0x16244a, 0x22356a, 0xe8f0ff, 0x5f74a8, 0x3d8bff),
    DARK("GREEN", 0x061208, 0x112a16, 0x183a1f, 0x25542e, 0xeaffef, 0x5e9468, 0x5ee07a),
    DARK("FOREST", 0x030c08, 0x0b2016, 0x112c1f, 0x1b402e, 0xe2f2ea, 0x4f7a66, 0x2fae6e),
    DARK("PURPLE", 0x0e0818, 0x22143a, 0x2c1a4c, 0x40266c, 0xf3eaff, 0x8a74b0, 0xb07cff),
    DARK("RED", 0x160505, 0x330e0e, 0x431414, 0x611e1e, 0xfff0f0, 0xa06868, 0xff4848),
    {
        "ORANGE", /* the orange one */
        HEX(0x140800), HEX(0x2a1406), HEX(0x3a1d0a), HEX(0x5a3210), HEX(0xfff2e6), HEX(0x9a6a46), HEX(0xff7a1a),
        {HEX(0xff7a1a), HEX(0xffc14d), HEX(0xfff2e6), HEX(0xff4d2e)},
        {HEX(0xff3b1f), HEX(0xff6a1a), HEX(0xff9a2e), HEX(0xffc14d), HEX(0xffe08a), HEX(0xff8a5c), HEX(0xff5c7a), HEX(0xd9a0ff), HEX(0xfff2e6)},
    },
    {
        "TEAL", /* black with teal */
        HEX(0x031212), HEX(0x0b2424), HEX(0x113232), HEX(0x1c4a4a), HEX(0xe6fffb), HEX(0x4f8a85), HEX(0x19d3c5),
        {HEX(0x19d3c5), HEX(0x8af0e6), HEX(0xffb84d), HEX(0xff6b8b)},
        {HEX(0x19d3c5), HEX(0x2fe0a0), HEX(0x8af06a), HEX(0xe8f06a), HEX(0xffc65c), HEX(0xff9c5c), HEX(0xff6b8b), HEX(0xc08bff), HEX(0x62b6ff)},
    },
    {
        "WHITE", /* a spare: a light screen */
        HEX(0xf2f2ee), HEX(0xdcdcd6), HEX(0xcbcbc4), HEX(0xb4b4ac), HEX(0x18181a), HEX(0x8a8a8f), HEX(0xe8601c),
        {HEX(0xe03a3a), HEX(0xc98a00), HEX(0x14a080), HEX(0x2a6fd6)},
        {HEX(0xe03a3a), HEX(0xe8741c), HEX(0xc9a000), HEX(0x5aa020), HEX(0x14a080), HEX(0x1890b8), HEX(0x2a6fd6), HEX(0x7a4fd6), HEX(0xd0409a)},
    },
    {
        "NEON", /* a spare: loud */
        HEX(0x0a0014), HEX(0x1a0630), HEX(0x260a44), HEX(0x3a1266), HEX(0xffffff), HEX(0x8a6ab0), HEX(0x00ffd0),
        {HEX(0xff2fd6), HEX(0x00ffd0), HEX(0xfff200), HEX(0x6a8bff)},
        {HEX(0xff2fd6), HEX(0xff3b6b), HEX(0xff9b00), HEX(0xfff200), HEX(0x7dff3b), HEX(0x00ffd0), HEX(0x00b3ff), HEX(0x8a5bff), HEX(0xffffff)},
    },
};
#define THEME_COUNT ((int)(sizeof THEMES / sizeof THEMES[0]))

#endif
