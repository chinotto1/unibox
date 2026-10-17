/* STRING: a plucked string (Karplus-Strong), written in the same form as Felucca's engines (felucca_engine.h).
 * It is included by synths.c rather than compiled on its own, hence the .h.
 *
 * A note fills a delay line, one period long, with a short burst: noise, a pick's ramp or a soft bump. BRIT
 * filters the burst, and POS shapes it like plucking a real string at a point along its length. Each pass
 * around the loop averages neighboring samples, goes through a one-pole low-pass (DAMP, which KTR makes darker
 * for higher notes) and loses a little level (DCAY), so the string dies away with its highs fading first. BOW
 * keeps feeding in noise (a bowed or blown string), and DRV drives the output. The period follows the pitch
 * every block (for glides and bends), minus the delay of the loop's filters, which keeps it in tune.
 * The two voices' delay lines live in the track's engine memory (ENG_MEM, shared with PHYS, since a track
 * only plays one engine at a time). */
#define STRING_LEN 2048 /* samples: down to ~21.5 Hz */
#define STRING_MASK (STRING_LEN - 1)
ENG_MEM_FITS("STRING", sizeof(int16_t) * STRING_LEN * NVOICE);

static const char *const N_STRING_SRC[] = {"NOISE", "PICK", "SOFT"};

static int16_t *string_line(track_t *t, voice_t *v) {
  return (int16_t *)ENG_MEM(t - trk) + (v - t->v) * STRING_LEN;
}

/* the period of a phase step, in samples, Q8 (one 32-bit divide) */
static uint32_t string_period(uint32_t inc) {
  uint32_t p = 0xFFFFFFFFu / ((inc >> 8) | 1u);
  return p < (4u << 8) ? 4u << 8 : p > ((STRING_LEN - 4u) << 8) ? (STRING_LEN - 4u) << 8
                                                                 : p;
}

/* the loop filter's coefficient, Q15: DAMP, and darker up the keyboard by KTR */
static int32_t string_damp(const track_t *t, int32_t pitch16) {
  int32_t d = t->p[P_E4] - (t->p[P_E6] * (pitch16 - 48 * 16) >> 10);
  d = clamp(d, 0, 127);
  return 6000 + d * 210; /* 0.18 .. 0.999 */
}

static void string_note_on(track_t *t, voice_t *v) {
  int16_t *d = string_line(t, v);
  uint32_t per = string_period(pitch_inc((uint32_t)clamp(v->pitch16, 0, 2047))) >> 8;
  int32_t src = t->p[P_E0], b = 1500 + t->p[P_E1] * 245, lp = 0; /* BRIT: the burst's low-pass, Q15 */
  int32_t k = (int32_t)per * t->p[P_E2] / 254, seed = 0x2545F491 + (int32_t)v->age * 7919;
  int32_t w = v->active && v->stage ? v->s[0] : (int32_t)per; /* a sounding string is plucked again where it is */
  uint32_t i;
  for (i = 0; i < per; i++) { /* the burst, filtered, into the line's last period */
    int32_t x;
    if (src == 1) { /* a pick: a ramp across the period, a little noise */
      x = 20000 - (int32_t)(i * 40000u / per) + ((int32_t)(noise32(&seed) >> 20) - 2048);
    } else if (src == 2) { /* soft: half a sine */
      x = sine_i(i * (0x80000000u / per)) * 3 / 4;
    } else {
      x = (int32_t)(noise32(&seed) >> 17) - 16384;
      x += x >> 1;
    }
    lp += mulq15(x - lp, b);
    d[(w - (int32_t)per + (int32_t)i) & STRING_MASK] = (int16_t)clamp(lp, -32000, 32000);
  }
  if (k > 0) { /* where it is plucked: a node every k samples (backwards: each reads the burst unchanged) */
    for (i = per; i-- > (uint32_t)k;) {
      int32_t a = (w - (int32_t)per + (int32_t)i) & STRING_MASK, c = (a - k) & STRING_MASK;
      d[a] = (int16_t)clamp(d[a] - d[c], -32000, 32000);
    }
  }
  v->s[0] = w;
  v->s[1] = 0;
  v->s[3] = 0;
  if (!v->s[2]) {
    v->s[2] = 0x1F123BB5 + (int32_t)v->age; /* BOW's noise */
  }
}

static void string_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m) {
  int16_t *d = string_line(t, v);
  int32_t a = string_damp(t, m->pitch16);
  /* the period, less the loop's own delay: half a sample for the average, (1 - a) / a for the low-pass */
  uint32_t lag = 128u + (uint32_t)(((32768 - a) << 8) / a), per = string_period(m->inc);
  per = per > lag + (4u << 8) ? per - lag : 4u << 8;
  int32_t g = 32767 - (127 - t->p[P_E5]) * (127 - t->p[P_E5]) / 6; /* DCAY: the loss a pass, Q15 */
  int32_t bow = t->p[P_E3] * 24, drv = t->p[P_E7];
  int32_t w = v->s[0], lp = v->s[1], nst = v->s[2], prev = v->s[3];
  for (uint32_t i = 0; i < n; i++) {
    uint32_t rp = ((uint32_t)w << 8) - per; /* the read point, a period back, Q8 */
    int32_t j = (int32_t)(rp >> 8) & STRING_MASK, fr = (int32_t)(rp & 255u);
    int32_t y = d[j] + (((d[(j + 1) & STRING_MASK] - d[j]) * fr) >> 8);
    int32_t x = (y + prev) >> 1; /* Karplus-Strong's average */
    prev = y;
    lp += mulq15(x - lp, a);
    x = mulq15(lp, g);
    if (bow) {
      x += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, bow);
    }
    d[w & STRING_MASK] = (int16_t)clamp(x, -32000, 32000);
    w = (w + 1) & STRING_MASK;
    if (drv) {
      y = softclip(y + y * drv / 32);
    }
    out[i] += voice_amp(y, m, i) * 2;
  }
  v->s[0] = w;
  v->s[1] = lp;
  v->s[2] = nst;
  v->s[3] = prev;
}

static const preset_t STRING_PRESETS[] = {
    {"NYLON", {0, 60, 30, 0, 70, 100, 40, 0}, {0, 90, 127, 60}, 0, 0},
    {"STEEL", {1, 100, 20, 0, 100, 110, 30, 0}, {0, 90, 127, 70}, 0, 0},
    {"BASS", {1, 50, 40, 0, 50, 104, 20, 10}, {0, 90, 127, 40}, 0, 1},
    {"HARP", {2, 70, 50, 0, 85, 116, 40, 0}, {0, 90, 127, 95}, 0, 0},
    {"KOTO", {1, 110, 15, 0, 110, 100, 20, 0}, {0, 90, 127, 60}, 0, 0},
    {"MUTED", {1, 60, 30, 0, 40, 60, 20, 0}, {0, 90, 127, 30}, 0, 0},
    {"BOWED", {2, 50, 30, 50, 60, 118, 30, 0}, {40, 90, 127, 70}, 0, 0},
    {"SITAR", {0, 120, 10, 0, 120, 112, 0, 40}, {0, 90, 127, 80}, 0, 0},
};

static const engine_t ENG_STRING = {
    .name = "STRING",
    .page_title = {"PLCK", "STRG"},
    .edit = {
        {"SRC", F_ENUM, 0, 2, 0, N_STRING_SRC, 0},
        {"BRIT", F_PCT, 0, 127, 64, 0, 0},
        {"POS", F_PCT, 0, 127, 30, 0, 0},
        {"BOW", F_PCT, 0, 127, 0, 0, 0},
        {"DAMP", F_PCT, 0, 127, 80, 0, 0},
        {"DCAY", F_PCT, 0, 127, 105, 0, 0},
        {"KTR", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = STRING_PRESETS,
    .npresets = NELEM(STRING_PRESETS),
    .note_on = string_note_on,
    .render = string_render,
    .knob = {P_E1, P_E4, P_E5, P_REL},
};
