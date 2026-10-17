/* TB-303: X0X's Open303 port (src/dsp/bass303.c). One voice, and the engine's own envelope and slide,
 * so the host's ADSR is not the loudness. Accent is a velocity above 110; a sliding step is the 303's slide. */
#include "bass303.h"

typedef struct {
  bass303_t b;
  uint8_t ready, held;
} b303_mem_t;
ENG_MEM_FITS("TB303", sizeof(b303_mem_t));

static b303_mem_t *b303_mem(track_t *t) { return (b303_mem_t *)ENG_MEM(t - trk); }

static void b303_apply(track_t *t, b303_mem_t *m) {
  for (int i = 0; i < BASS303_NPARAMS; i++) {
    int v = t->p[P_E0 + i];
    if (v < 0) {
      v = 0;
    }
    bass303_set(&m->b, i, v);
  }
}

static void b303_block(track_t *t) {
  b303_mem_t *m = b303_mem(t);
  if (!m->ready) {
    bass303_init(&m->b);
    m->ready = 1;
    m->held = 0;
  }
  b303_apply(t, m);
}

static void b303_note_on(track_t *t, voice_t *v) {
  b303_mem_t *m = b303_mem(t);
  if (!m->ready) {
    bass303_init(&m->b);
    m->ready = 1;
  }
  b303_apply(t, m);
  bass303_note_on(&m->b, v->note, v->vel > 110, v->gstep != 0);
  m->held = 1;
}

static int b303_done(track_t *t, voice_t *v) {
  (void)v;
  b303_mem_t *m = b303_mem(t);
  return m->ready && !m->held && m->b.idle;
}

static void b303_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m) {
  b303_mem_t *s = b303_mem(t);
  float tmp[CTL];
  (void)m;
  if (!s->ready) {
    return;
  }
  if (v->stage == 4) {
    bass303_all_off(&s->b);
    s->held = 0;
  } else if (!v->gate && s->held) {
    bass303_note_off(&s->b);
    s->held = 0;
  }
  if (n > CTL) {
    n = CTL;
  }
  bass303_render(&s->b, tmp, (int)n);
  for (uint32_t i = 0; i < n; i++) {
    float x = tmp[i];
    if (x > 2.0f) {
      x = 2.0f;
    } else if (x < -2.0f) {
      x = -2.0f;
    }
    out[i] += (int32_t)(x * 16000.0f);
  }
}

static const char *const N_303_WAVE[] = {"SAW", "SQR"};
static const char *const N_303_DRV[] = {"OFF", "SOFT", "RAT"};

static const preset_t B303_PRESETS[] = {
    {"ACID", {70, 100, 90, 70, 80, 0, 64, 100, 0, 0, 21, 7}, {0, 40, 0, 20}, 0, 1},
    {"ROUND", {50, 60, 50, 80, 40, 0, 64, 100, 40, 1, 30, 10}, {0, 50, 0, 24}, 0, 1},
    {"SQUARE", {60, 80, 70, 60, 90, 1, 64, 96, 0, 0, 21, 7}, {0, 40, 0, 20}, 0, 1},
    {"SOFT", {40, 30, 20, 90, 20, 0, 66, 110, 0, 0, 40, 12}, {0, 60, 0, 30}, 0, 1},
};

static const engine_t ENG_303 = {
    .name = "TB-303",
    .page_title = {"FILT", "VCO", "DRV"},
    .edit = {
        {"CUT", F_PCT, 0, 127, 70, 0, 0},
        {"RESO", F_PCT, 0, 127, 100, 0, 0},
        {"ENV", F_PCT, 0, 127, 90, 0, 0},
        {"DECAY", F_PCT, 0, 127, 70, 0, 0},
        {"ACC", F_PCT, 0, 127, 80, 0, 0},
        {"WAVE", F_ENUM, 0, 1, 0, N_303_WAVE, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"VOL", F_PCT, 0, 127, 100, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
        {"DTYPE", F_ENUM, 0, 2, 0, N_303_DRV, 0},
        {"SLIDE", F_PCT, 0, 127, 21, 0, 0},
        {"ADEC", F_PCT, 0, 127, 7, 0, 0},
    },
    .presets = B303_PRESETS,
    .npresets = NELEM(B303_PRESETS),
    .note_on = b303_note_on,
    .render = b303_render,
    .block = b303_block,
    .done = b303_done,
    .ownenv = 1,
    .poly = 1,
    .mem = sizeof(b303_mem_t),
};
