/* Juno-106: the FM-106 DCO, ladder filter and chorus (integer, GPL-3.0-only), with this host's ADSR
 * as the VCA. Chorus is summed to mono because the mix bus is mono; the track's CHORUS send is a second one. */
#include "fm106_osc.h"
#include "fm106_vcf.h"
#include "fm106_chorus.h"

static const char *const N_JUNO_HPF[] = {"OFF", "BASS", "MID", "HIGH"};
static const char *const N_JUNO_CHO[] = {"OFF", "I", "II", "I+II"};

typedef struct {
  fm106_dco_t dco[NVOICE];
  fm106_vcf_t vcf[NVOICE];
  fm106_chorus_t ch;
  uint32_t lfo;
  uint8_t ready;
} juno_mem_t;
ENG_MEM_FITS("JUNO", sizeof(juno_mem_t));

static juno_mem_t *juno_mem(track_t *t) { return (juno_mem_t *)ENG_MEM(t - trk); }

static void juno_prepare(track_t *t, voice_t *v) {
  juno_mem_t *m = juno_mem(t);
  int i = (int)(v - t->v);
  if (!m->ready) {
    for (int k = 0; k < NVOICE; k++) {
      fm106_dco_init(&m->dco[k], (uint32_t)k + 1u);
      fm106_vcf_init(&m->vcf[k]);
    }
    fm106_chorus_init(&m->ch);
    m->ready = 1;
  }
  if (i < 0 || i >= NVOICE) {
    i = 0;
  }
  m->dco[i].saw_on = (uint8_t)(t->p[P_E0] > 0);
  m->dco[i].pulse_on = (uint8_t)(t->p[P_E1] > 0);
  m->dco[i].sub_on = (uint8_t)(t->p[P_E2] > 0);
  m->dco[i].sub_level = (uint8_t)(t->p[P_E2] < 0 ? 0 : t->p[P_E2] > 127 ? 127 : t->p[P_E2]);
  m->dco[i].noise_level = (uint8_t)(t->p[P_E3] < 0 ? 0 : t->p[P_E3] > 127 ? 127 : t->p[P_E3]);
}

static void juno_note_on(track_t *t, voice_t *v) {
  juno_prepare(t, v);
  fm106_dco_reset(&juno_mem(t)->dco[v - t->v]);
}

static void juno_block(track_t *t) {
  juno_mem_t *m = juno_mem(t);
  if (!m->ready) {
    juno_prepare(t, &t->v[0]);
  }
  m->ch.mode = (uint8_t)(t->p[P_E10] & 3);
}

static void juno_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *mod) {
  juno_mem_t *m = juno_mem(t);
  int i = (int)(v - t->v);
  const int16_t *p = t->p;
  if (i < 0 || i >= NVOICE) {
    return;
  }
  juno_prepare(t, v);
  fm106_dco_t *dco = &m->dco[i];
  fm106_vcf_t *vcf = &m->vcf[i];
  int32_t pw = p[P_E8] < 0 ? 0 : p[P_E8] > 127 ? 127 : p[P_E8];
  int32_t rate = p[P_E11] < 0 ? 0 : p[P_E11];
  uint32_t lfo_inc = (uint32_t)rate * 90000u;
  int hpf = p[P_E9] & 3;
  int32_t hpf_a = hpf == 0 ? 0 : hpf == 1 ? 200 : hpf == 2 ? 1200 : 4000;
  int32_t hp = v->s[4];
  for (uint32_t k = 0; k < n; k++) {
    int32_t lfo = (int32_t)(m->lfo >> 16) - 32768;
    if ((m->lfo += lfo_inc) & 0x80000000u) {
      lfo = -lfo;
    }
    int32_t env = mod->envq15 < 0 ? 0 : mod->envq15;
    int32_t s = fm106_dco_process(dco, mod->inc, (uint8_t)pw, rate ? 0 : 1, lfo, env);
    uint16_t env14 = (uint16_t)((env * 16383) >> 15);
    uint16_t cut = (uint16_t)((p[P_E4] < 0 ? 0 : p[P_E4] > 127 ? 127 : p[P_E4]) * 0x3F80 / 127);
    uint8_t envmod = (uint8_t)((p[P_E6] < 0 ? 0 : p[P_E6] > 127 ? 127 : p[P_E6]) * 2);
    uint8_t key = (uint8_t)((p[P_E7] < 0 ? 0 : p[P_E7] > 127 ? 127 : p[P_E7]) * 2);
    uint16_t dac = fm106_calc_vcf_freq(cut, 0, 0, envmod, key, 0, 0, 1, env14, (uint16_t)(v->note * 256));
    uint8_t res = (uint8_t)(p[P_E5] < 0 ? 0 : p[P_E5] > 127 ? 127 : p[P_E5]);
    s = fm106_vcf_process(vcf, s, dac, res);
    if (hpf_a) {
      hp += (s - hp) >> (hpf == 1 ? 6 : hpf == 2 ? 4 : 2);
      s -= hp;
    }
    if (m->ch.mode && i == 0) { /* one chorus for the part: the second voice stays dry */
      int32_t l, r;
      fm106_chorus_process(&m->ch, s, &l, &r);
      s = (l + r) >> 1;
    }
    out[k] += voice_amp(s, mod, k);
  }
  v->s[4] = hp;
}

static const preset_t JUNO_PRESETS[] = {
    {"POLY", {1, 1, 0, 0, 80, 30, 70, 40, 64, 0, 1, 20}, {8, 70, 90, 50}, 0, 0},
    {"STRING", {1, 1, 40, 0, 70, 15, 40, 50, 80, 0, 2, 12}, {40, 80, 100, 70}, 8, 0},
    {"BASS", {1, 0, 90, 0, 45, 40, 50, 70, 40, 1, 0, 0}, {0, 60, 0, 30}, 20, 1},
    {"BRASS", {1, 1, 0, 0, 55, 35, 90, 60, 70, 0, 0, 0}, {20, 70, 80, 40}, 30, 0},
};

static const engine_t ENG_JUNO = {
    .name = "JUNO",
    .page_title = {"DCO", "VCF", "CHO"},
    .edit = {
        {"SAW", F_ONOFF, 0, 1, 1, 0, 0},
        {"PULSE", F_ONOFF, 0, 1, 1, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"ENV", F_PCT, 0, 127, 60, 0, 0},
        {"KEY", F_PCT, 0, 127, 64, 0, 0},
        {"PW", F_PCT, 0, 127, 64, 0, 0},
        {"HPF", F_ENUM, 0, 3, 0, N_JUNO_HPF, 0},
        {"CHOR", F_ENUM, 0, 3, 1, N_JUNO_CHO, 0},
        {"LFO", F_PCT, 0, 127, 16, 0, 0},
    },
    .presets = JUNO_PRESETS,
    .npresets = NELEM(JUNO_PRESETS),
    .note_on = juno_note_on,
    .render = juno_render,
    .block = juno_block,
    .keep = 0,
    .poly = NVOICE,
    .mem = sizeof(juno_mem_t),
};
