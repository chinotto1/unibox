/* Wavetable: eight single-cycle waves (src/wt_data.h). POS picks a point along them; SCAN lets the
 * envelope move that point. The filter is ANALOG's. */
#include "wt_data.h"

static void wt_note_on(track_t *t, voice_t *v) {
  (void)t;
  v->s[0] = v->s[1] = 0;
}

static int32_t wt_sample(int32_t pos, uint32_t ph) {
  int idx = pos < 0 ? 0 : pos > 127 ? 127 : pos;
  int span = (WT_COUNT - 1) * 256;
  int at = idx * span / 127;
  int w0 = at >> 8, fr = at & 255;
  if (w0 >= WT_COUNT - 1) {
    w0 = WT_COUNT - 1;
    fr = 0;
  }
  uint32_t i = ph >> 24, j = (i + 1u) & (WT_LEN - 1u), xf = (ph >> 16) & 255u;
  int32_t a = WT[w0][i] + ((WT[w0][j] - WT[w0][i]) * (int32_t)xf >> 8);
  int32_t b = WT[w0 + 1][i] + ((WT[w0 + 1][j] - WT[w0 + 1][i]) * (int32_t)xf >> 8);
  return a + ((b - a) * fr >> 8);
}

static void wt_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m) {
  const int16_t *p = t->p;
  int32_t pos = p[P_E0] + (p[P_E1] * (m->envq15 >> 8) >> 7);
  int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E6] * (v->pitch16 - 48 * 16) >> 4);
  int32_t drv = p[P_E7];
  uint32_t ph = v->ph[0], inc = m->inc;
  int32_t ic1 = v->s[0], ic2 = v->s[1];
  int32_t drive = 32768 + (drv < 0 ? 0 : drv) * 500;
  tsvf_t flt;
  tsvf_coef(&flt, cut, p[P_E5] < 0 ? 0 : p[P_E5]);
  for (uint32_t i = 0; i < n; i++) {
    int32_t s = wt_sample(pos, ph);
    ph += inc;
    if (drv > 0) {
      s = softclip(((s >> 2) * (drive >> 2)) >> 11);
    }
    s = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 18000);
    out[i] += voice_amp(s, m, i) * 2;
  }
  v->ph[0] = ph;
  v->s[0] = ic1;
  v->s[1] = ic2;
}

static const preset_t WT_PRESETS[] = {
    {"BELL", {20, 40, 0, 0, 90, 20, 50, 0}, {4, 70, 0, 60}, 10, 0},
    {"SCAN", {0, 90, 0, 0, 75, 30, 40, 10}, {10, 80, 70, 50}, 24, 0},
    {"BASS", {70, 0, 0, 0, 45, 50, 30, 20}, {0, 60, 80, 30}, 16, 1},
    {"PAD", {40, 30, 0, 0, 60, 10, 20, 0}, {60, 80, 100, 80}, 8, 0},
};

static const engine_t ENG_WT = {
    .name = "WAVES",
    .page_title = {"WAVE", "FLT"},
    .edit = {
        {"POS", F_PCT, 0, 127, 0, 0, 0},
        {"SCAN", F_PCT, 0, 127, 0, 0, 0},
        {"FORM", F_PCT, 0, 127, 0, 0, 0},
        {"BEND", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 10, 0, 0},
        {"KEY", F_PCT, 0, 127, 40, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = WT_PRESETS,
    .npresets = NELEM(WT_PRESETS),
    .note_on = wt_note_on,
    .render = wt_render,
    .keep = 0x03,
    .poly = NVOICE,
};
