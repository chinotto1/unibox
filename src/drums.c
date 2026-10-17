/* FOMAJAM's drum machines. Each track plays one machine, and each step plays one of its voices.
 *
 *   modeled   808, 909, SDS, CR78: voices built from a few voice types (tools/drums.py, from trommelsynthesizer)
 *   ROM       recordings of the machines' own samples (tools/roms.py), stored as 4-bit IMA ADPCM
 *   SMP       the .wav files in samples/ (tools/samples.py), one voice per file
 *   ANALOG .. after these come the synth engines (synths.c); they are not drum machines, and their voices are notes
 *
 * A track can play two voices at once, so a hi-hat over a ringing kick doesn't cut it off. A closed hi-hat
 * does cut off an open one on the same track. This code runs in audio(): integers only. */
#include "drums.h"

#include "gains.h"
#include "groove.h"
#include "roms.h"
#include "samples.h"
#include "tables.h"

#define FULL (1 << 30) /* an envelope at its top, Q30 */
#define HZ_INC 97392u  /* phase step of 1 Hz: 2^32 / 44100 */
#define POLY 2
#define M_ROM M_MODELLED
#define M_SMP (M_MODELLED + ROM_MACHINES) /* when there are samples */
#define IS_SMP(m) (SAMPLE_COUNT > 0 && (m) == M_SMP)
#define M_SYNTH (M_SMP + (SAMPLE_COUNT > 0)) /* synths.c's engines from here */

static int clamp(int v, int lo, int hi) {
  return v < lo ? lo : v > hi ? hi
                              : v;
}

/* ---- the machines */
#define M_MIDI (M_SYNTH + synth_count()) /* the last: MIDI out */

int machine_count(void) { return M_MIDI + 1; }

int machine_midi(int m) { return m == M_MIDI; }

int machine_synth(int m) { return m >= M_SYNTH; }

int machine_engine(int m) { return m >= M_SYNTH && m < M_MIDI ? m - M_SYNTH : -1; }

const char *machine_name(int m) {
  return m < M_ROM ? MODEL_NAME[m] : m < M_SMP ? ROM_NAME[m - M_ROM]
                                 : IS_SMP(m)   ? "SMP"
                                 : m == M_MIDI ? "MIDI"
                                               : synth_name(m - M_SYNTH);
}

const char *machine_label(int m) {
  return m < M_ROM ? MODEL_LABEL[m] : m < M_SMP ? ROM_LABEL[m - M_ROM]
                                  : IS_SMP(m)   ? "SMP"
                                  : m == M_MIDI ? "MID"
                                                : synth_label(m - M_SYNTH);
}

static int same(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++, b++;
  }
  return *a == *b;
}

int machine_find(const char *name, int otherwise) {
  for (int m = 0; m < machine_count(); m++) {
    if (same(machine_name(m), name)) {
      return m;
    }
  }
  return otherwise;
}

int machine_voices(int m) { return IS_SMP(m) ? SAMPLE_COUNT : m >= M_SYNTH ? 128
                                                                           : V_COUNT; }

static int own(int m, int v) {
  if (v < 0 || v >= machine_voices(m)) {
    return 0;
  }
  return m < M_ROM ? PATCH[m][v].kind != K_NONE : m < M_SMP ? ROM[m - M_ROM][v].len > 0
                                                            : 1; /* SMP, the synths */
}

/* the voice that really plays v on m, and how far it is retuned; -1: none */
static int resolve(int m, int v, int *semis) {
  *semis = 0;
  if (own(m, v)) {
    return v;
  }
  if (m >= M_SMP || v < 0 || v >= V_COUNT) {
    return -1;
  }
  for (int i = 0; i < 3 && FALLBACK[v][i][0]; i++) {
    int w = FALLBACK[v][i][0] - 1;
    if (own(m, w)) {
      *semis = FALLBACK[v][i][1];
      return w;
    }
  }
  return -1;
}

int machine_has(int m, int v) {
  int s;
  return own(m, v) ? 1 : resolve(m, v, &s) >= 0 ? 2
                                                : 0;
}

const char *voice_name(int m, int v) {
  if (IS_SMP(m)) {
    return SAMPLES[clamp(v, 0, SAMPLE_COUNT - 1)].name;
  }
  if (m >= M_SYNTH) { /* a note: C#3 */
    static const char *const NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static char s[6];
    const char *n = NOTE[clamp(v, 0, 127) % 12];
    int i = 0, o = clamp(v, 0, 127) / 12 - 1;
    while (*n) {
      s[i++] = *n++;
    }
    if (o < 0) {
      s[i++] = '-', o = -o;
    }
    s[i++] = (char)('0' + o);
    s[i] = 0;
    return s;
  }
  return VOICE_NAME[clamp(v, 0, V_COUNT - 1)];
}

/* ---- building blocks */
typedef struct {
  int32_t a1, a2, a3, k, ic1, ic2; /* Q24 */
} svf_t;

static void svf_set(svf_t *s, int row, int f) {
  const int32_t *c = &SVF[(row * 128 + clamp(f, 0, 127)) * 3];
  s->a1 = c[0];
  s->a2 = c[1];
  s->a3 = c[2];
  s->k = SVF_K[row];
  s->ic1 = s->ic2 = 0;
}

/* the trapezoidal state-variable filter: stable at any frequency */
static inline int32_t svf_tick(svf_t *s, int32_t x, int32_t *bp, int32_t *hp) {
  int32_t v3 = x - s->ic2;
  int32_t v1 = (int32_t)(((int64_t)s->a1 * s->ic1 + (int64_t)s->a2 * v3) >> 24);
  int32_t v2 = s->ic2 + (int32_t)(((int64_t)s->a2 * s->ic1 + (int64_t)s->a3 * v3) >> 24);
  s->ic1 = 2 * v1 - s->ic1;
  s->ic2 = 2 * v2 - s->ic2;
  *bp = v1;
  if (hp) {
    *hp = x - (int32_t)(((int64_t)s->k * v1) >> 24) - v2;
  }
  return v2;
}

static inline int32_t decay(int32_t env, uint32_t coef) { return (int32_t)(((int64_t)env * coef) >> 31); }
static inline int32_t scale(int32_t x, int32_t env) { return (int32_t)(((int64_t)x * env) >> 30); }

static int32_t tanh_q15(int32_t u) { /* u: Q12 */
  int32_t a = u < 0 ? -u : u, y;
  if (a >= (8 << 12)) {
    y = 32767;
  } else {
    int i = a >> 7, fr = a & 127;
    y = TANH[i] + ((TANH[i + 1] - TANH[i]) * fr >> 7);
  }
  return u < 0 ? -y : y;
}

/* a phase step moved by semitones */
static uint32_t tuned(uint32_t inc, int semis) {
  int s = semis + 48, oct = s / 12 - 4;
  uint32_t r = (uint32_t)(((uint64_t)inc * SEMI[s % 12]) >> 16);
  return oct >= 0 ? r << oct : r >> -oct;
}

static uint32_t xr = 2463534242u;
static inline int32_t noise(void) {
  xr ^= xr << 13;
  xr ^= xr >> 17;
  xr ^= xr << 5;
  return (int32_t)xr >> 16; /* +-32767 */
}

/* ---- voices */
typedef struct {
  const patch_t *p;
  const uint8_t *rom;        /* ADPCM, or */
  const int16_t *smp;        /* 16-bit */
  uint32_t pos, step, len;   /* pos, step: Q16 */
  uint32_t dn;               /* ADPCM: samples decoded */
  int32_t pred, idx, ia, ib; /* ADPCM: the decoder, the two samples before dn */
  uint32_t ph[6], inc[6], t, end;
  int32_t env[5];
  uint32_t coef[5];
  svf_t s1, s2;
  int32_t lp1, lp2, ca, cb, chk;
  int32_t m[6]; /* amounts, per kind */
  int32_t gain;
  uint8_t kind, voice, on, choke;
} dv_t;

static dv_t dv[TRACKS][POLY];

static const uint16_t METAL_HZ10[6] = {2053, 3044, 3696, 5227, 5400, 8000};

/* TONE moves a frequency index by up to +-range/2 octaves */
static int tone_shift(int tone, int range12) { return (tone - 64) * range12 / 128; }

/* IMA ADPCM: sample n of d, from the decoder's state (pred, idx) after sample n - 1 */
static inline int32_t ima_next(const uint8_t *d, uint32_t n, int32_t *pred, int32_t *idx) {
  int code = (d[n >> 1] >> ((n & 1) << 2)) & 15;
  int32_t step = IMA_STEP[*idx], diff = step >> 3;
  if (code & 4) {
    diff += step;
  }
  if (code & 2) {
    diff += step >> 1;
  }
  if (code & 1) {
    diff += step >> 2;
  }
  *pred = clamp(code & 8 ? *pred - diff : *pred + diff, -32768, 32767);
  *idx = clamp(*idx + IMA_INDEX[code & 7], 0, 88);
  return *pred;
}

static void env_start(dv_t *v, int i, int tau) {
  v->env[i] = FULL;
  v->coef[i] = TAU[clamp(tau, 0, 255)];
}

static void voice_start(dv_t *v, const track_t *d, int m, int voice, int semis) {
  int tn = d->tone, ex = d->extra, ds = (d->decay - 64) * 36 / 128; /* DECAY: +-1.5 octaves of time */
  semis += (d->tune - 64) * 12 / 64;
  v->kind = 0;
  v->t = 0;
  v->chk = FULL;
  v->choke = 0;
  v->lp1 = v->lp2 = 0;
  for (int i = 0; i < 6; i++) {
    v->ph[i] = 0;
  }
  if (m >= M_ROM) { /* a recording */
    if (IS_SMP(m)) {
      v->rom = 0;
      v->smp = SAMPLES[voice].data;
      v->len = SAMPLES[voice].len;
      v->step = tuned(1u << 15, semis); /* the samples are at half the rate */
    } else {
      const rom_t *r = &ROM[m - M_ROM][voice];
      v->smp = 0;
      v->rom = r->data;
      v->len = r->len;
      v->step = tuned(r->step, semis);
      v->idx = r->idx0;
      v->pred = v->ia = v->ib = 0;
      v->dn = 0;
    }
    v->pos = 0;
    env_start(v, 0, d->decay < 64 ? SAMPLE_DECAY[d->decay] + 0 : 255);
    if (d->decay >= 64) {
      v->coef[0] = 0x7FFFFFFFu; /* as recorded */
    }
    v->ca = tn < 64 ? ONEPOLE[SAMPLE_LP[tn]] : 0;
    v->cb = ONEPOLE[FREQ_SHELF];
    v->m[0] = tn > 64 ? (tn - 64) * 64 : 0; /* the shelf's lift, Q12 */
    v->kind = K_NONE;
    v->p = 0;
    return;
  }
  const patch_t *p = &PATCH[m][voice];
  v->p = p;
  v->kind = p->kind;
  v->rom = 0;
  v->smp = 0;
  switch (p->kind) {
    case K_SWEPT: {
      int td = p->k[4];
      v->inc[0] = tuned(p->hz[0] * HZ_INC, semis);
      env_start(v, 0, p->t[0]);      /* the sweep */
      env_start(v, 1, p->t[1] + ds); /* the body */
      env_start(v, 2, TAU_CLICK);
      v->m[0] = p->k[0];
      v->m[1] = p->k[1] * (ex * 64) >> 12; /* click * 2 * EXTRA */
      if (!td) {
        v->m[1] = v->m[1] * (tn * 64) >> 12; /* * 2 * TONE */
      }
      v->ca = ONEPOLE[clamp(p->f[0] + (td ? 0 : tone_shift(tn, 36)), 0, 127)];
      v->m[2] = td ? p->k[2] * (1638 + tn * 39) >> 12 : p->k[2]; /* drive */
      v->m[3] = p->k[3];
      v->m[4] = (1 << 27) / tanh_q15(v->m[2]); /* 1 / tanh(drive), Q12 */
      break;
    }
    case K_SNARE:
      v->inc[0] = tuned(p->hz[0] * HZ_INC, semis);
      v->inc[1] = tuned(p->hz[1] * HZ_INC, semis);
      env_start(v, 0, p->t[0] + ds);
      env_start(v, 1, p->t[1] + ds);
      env_start(v, 2, p->t[2] + ds + SNAPPY[ex]); /* the noise */
      env_start(v, 3, TAU_DROP);
      env_start(v, 4, TAU_SNAP);
      svf_set(&v->s1, p->qc, p->f[0]);
      v->ca = ONEPOLE[clamp(p->f[1] + tone_shift(tn, 24), 0, 127)];
      v->m[0] = p->k[0];
      v->m[1] = p->k[1] * (1229 + ex * 45) >> 12; /* noise * (0.3 + 1.4 EXTRA) */
      v->m[2] = p->k[2];
      v->m[3] = 2048 + tn * 32; /* the second drum's balance */
      break;
    case K_METAL:
      for (int i = 0; i < 6; i++) {
        v->inc[i] = tuned((uint32_t)((METAL_HZ10[i] * (HZ_INC / 10)) * (uint64_t)p->k[0] >> 12), semis);
      }
      svf_set(&v->s1, p->qc, p->f[0] + semis);
      svf_set(&v->s2, 0, p->f[1] + tone_shift(tn, 14));
      env_start(v, 0, p->t[0] + ds);
      env_start(v, 1, p->t[1] + ds);
      env_start(v, 2, TAU_TICK);
      v->m[0] = clamp(p->k[1] * (1638 + tn * 39) >> 12, 0, 4096); /* the slow decay's share */
      v->m[1] = p->k[2];
      v->m[2] = p->k[3];
      break;
    case K_PING:
      v->inc[0] = tuned(p->hz[0] * HZ_INC, semis);
      v->inc[1] = tuned(p->hz[1] * HZ_INC, semis);
      env_start(v, 0, p->t[0] + ds);
      env_start(v, 1, p->t[1] + ds);
      env_start(v, 2, p->t[2] + ds);
      if (p->k[4]) {
        svf_set(&v->s1, p->qc, p->f[0] + semis);
      }
      v->m[0] = p->k[0] * (1638 + tn * 39) >> 12;
      v->m[1] = p->k[1];
      v->m[4] = (1 << 27) / tanh_q15(p->k[1]);
      break;
    case K_CLAP:
      svf_set(&v->s1, p->qc, p->f[0] + tone_shift(tn, 12));
      svf_set(&v->s2, 0, p->f[1]);
      env_start(v, 0, p->t[0]);
      env_start(v, 1, p->t[1] + ds);
      v->end = p->hz[0] * p->k[0]; /* the bursts end here */
      break;
    case K_NOISE:
      svf_set(&v->s1, 0, p->f[0] + tone_shift(tn, 12));
      env_start(v, 0, p->t[0]);
      env_start(v, 1, p->t[1] + ds);
      break;
  }
}

static void drum_hit(int t, int v, int vel) {
  const track_t *d = &song.track[t];
  int m = d->machine < machine_count() ? d->machine : 0, semis;
  int r = m >= M_SYNTH ? -1 : resolve(m, v, &semis);
  if (r < 0) {
    return;
  }
  dv_t *pick = &dv[t][0];
  for (int i = 0; i < POLY; i++) { /* the same voice again, else a free one, else the oldest */
    dv_t *x = &dv[t][i];
    if (x->on && x->voice == v) {
      pick = x;
      break;
    }
    if (!x->on && pick->on) {
      pick = x;
    } else if (x->on && pick->on && x->t > pick->t) {
      pick = x;
    }
  }
  if (r == V_CH) {
    for (int i = 0; i < POLY; i++) {
      if (dv[t][i].on && dv[t][i].voice == V_OH && &dv[t][i] != pick) {
        dv[t][i].choke = 1;
      }
    }
  }
  voice_start(pick, d, m, r, semis);
  int cal = m < M_ROM ? GAIN[m][r] : 32767;
  pick->gain = (int32_t)(((int64_t)(d->level * vel * 2) * cal) >> 15);
  pick->voice = (uint8_t)v;
  pick->on = 1;
  hits[t]++;
}

/* one voice into out for n frames */
static void voice_render(dv_t *v, int32_t *out, uint32_t n) {
  const patch_t *p = v->p;
  uint32_t choke = TAU[TAU_CHOKE];
  for (uint32_t i = 0; i < n; i++) {
    int32_t x = 0, quiet = 0, bp, hp;
    switch (v->kind) {
      case K_SWEPT: {
        int32_t sweep = v->m[0] * (v->env[0] >> 15) >> 15; /* Q12 */
        v->ph[0] += v->inc[0] + (uint32_t)(((int64_t)v->inc[0] * sweep) >> 12);
        int32_t e = v->env[1], nz = noise();
        x = scale(sine(v->ph[0]), e);
        v->lp1 += (int32_t)(((int64_t)v->ca * (nz - v->lp1)) >> 16);
        x += scale(v->lp1, v->env[2]) * v->m[1] >> 12;
        if (v->m[3]) {
          v->lp2 += (nz - v->lp2) / 5;
          x += scale(v->lp2, scale(e, e)) * v->m[3] >> 12;
        }
        x = tanh_q15(x * v->m[2] >> 15) * v->m[4] >> 12;
        v->env[0] = decay(v->env[0], v->coef[0]);
        v->env[1] = decay(e, v->coef[1]);
        v->env[2] = decay(v->env[2], v->coef[2]);
        quiet = v->t > 441 && e < (FULL >> 11);
        break;
      }
      case K_SNARE: {
        int32_t drop = v->m[0] * (v->env[3] >> 15) >> 15; /* Q12 */
        v->ph[0] += v->inc[0] + (uint32_t)(((int64_t)v->inc[0] * drop) >> 12);
        v->ph[1] += v->inc[1] + (uint32_t)(((int64_t)v->inc[1] * drop) >> 12);
        int32_t body = scale(sine(v->ph[0]), v->env[0]) * 3 / 4 + (scale(sine(v->ph[1]), v->env[1]) * v->m[3] >> 13);
        svf_tick(&v->s1, noise(), &bp, &hp);
        v->lp1 += (int32_t)(((int64_t)v->ca * (hp - v->lp1)) >> 16);
        x = body * 3 / 5 + (scale(v->lp1, v->env[2]) * v->m[1] >> 13);
        x += scale(noise(), v->env[4]) * v->m[2] >> 12;
        for (int j = 0; j < 5; j++) {
          v->env[j] = decay(v->env[j], v->coef[j]);
        }
        quiet = v->t > 882 && v->env[0] < (FULL >> 11) && v->env[1] < (FULL >> 11) && v->env[2] < (FULL >> 11);
        break;
      }
      case K_METAL: {
        int c = 0;
        for (int j = 0; j < 6; j++) {
          v->ph[j] += v->inc[j];
          c += (int32_t)v->ph[j] < 0 ? -1 : 1;
        }
        int32_t s = c * 10922, nz = v->m[1];
        x = (s * (4096 - nz) >> 12) + (noise() * nz * 4 / 5 >> 12);
        svf_tick(&v->s1, x, &bp, 0);
        svf_tick(&v->s2, bp * 3 / 2 + x * 3 / 10, &bp, &hp);
        int32_t e = (int32_t)(((int64_t)v->env[0] * (4096 - v->m[0]) + (int64_t)v->env[1] * v->m[0]) >> 12);
        x = scale(hp + (scale(noise(), v->env[2]) * v->m[2] >> 13), e);
        v->env[0] = decay(v->env[0], v->coef[0]);
        v->env[1] = decay(v->env[1], v->coef[1]);
        v->env[2] = decay(v->env[2], v->coef[2]);
        quiet = v->t > 441 && e < (FULL >> 12);
        break;
      }
      case K_PING: {
        v->ph[0] += v->inc[0];
        v->ph[1] += v->inc[1];
        int32_t e;
        if (p->k[2]) { /* two square waves, band-passed */
          x = ((int32_t)v->ph[0] < 0 ? -32767 : 32767) + (((int32_t)v->ph[1] < 0 ? -32767 : 32767) * v->m[0] >> 12);
          e = (int32_t)(((int64_t)v->env[0] * (4096 - p->k[3]) + (int64_t)v->env[2] * p->k[3]) >> 12);
          if (p->k[4]) {
            svf_tick(&v->s1, x, &bp, 0);
            x = bp;
          }
          x = scale(x, e);
        } else { /* two sines, driven */
          x = scale(sine(v->ph[0]), v->env[0]) + (scale(sine(v->ph[1]), v->env[1]) * v->m[0] >> 12);
          x = tanh_q15(x * v->m[1] >> 15) * v->m[4] >> 12;
          e = v->env[0] > v->env[1] ? v->env[0] : v->env[1];
        }
        for (int j = 0; j < 3; j++) {
          v->env[j] = decay(v->env[j], v->coef[j]);
        }
        quiet = v->t > 220 && e < (FULL >> 11);
        break;
      }
      case K_CLAP: {
        int32_t e;
        if (v->t < v->end) { /* the bursts: each starts loud */
          if (v->t % p->hz[0] == 0) {
            v->env[0] = FULL;
          }
          e = v->env[0];
        } else {
          if (v->t == v->end) {
            v->env[0] = v->env[1] = FULL;
          }
          e = v->env[0] + (int32_t)(((int64_t)v->env[1] * p->k[1]) >> 12);
          v->env[1] = decay(v->env[1], v->coef[1]);
        }
        v->env[0] = decay(v->env[0], v->coef[0]);
        svf_tick(&v->s1, noise(), &bp, 0);
        svf_tick(&v->s2, bp, &bp, &hp);
        x = scale(hp, e);
        quiet = v->t > v->end + 441 && e < (FULL >> 11);
        break;
      }
      case K_NOISE: {
        svf_tick(&v->s1, noise(), &bp, &hp);
        int32_t e = scale(FULL - v->env[0], v->env[1]);
        x = scale(hp, e);
        v->env[0] = decay(v->env[0], v->coef[0]);
        v->env[1] = decay(v->env[1], v->coef[1]);
        quiet = v->t > 220 && e < (FULL >> 11);
        break;
      }
      default: { /* a recording */
        uint32_t k = v->pos >> 16;
        if (k + 1 >= v->len) {
          v->on = 0;
          return;
        }
        int32_t a, b, f = (int32_t)(v->pos & 0xFFFF) >> 1;
        if (v->rom) { /* decode up to sample k + 1 (the pitch moves it at most a few a frame) */
          while (v->dn < k + 2) {
            v->ia = v->ib;
            v->ib = ima_next(v->rom, v->dn++, &v->pred, &v->idx);
          }
          a = v->ia;
          b = v->ib;
        } else {
          a = v->smp[k];
          b = v->smp[k + 1];
        }
        x = scale(a + ((b - a) * f >> 15), v->env[0]);
        v->pos += v->step;
        if (v->ca) {
          v->lp1 += (int32_t)(((int64_t)v->ca * (x - v->lp1)) >> 16);
          x = v->lp1;
        } else if (v->m[0]) {
          v->lp2 += (int32_t)(((int64_t)v->cb * (x - v->lp2)) >> 16);
          x += (x - v->lp2) * v->m[0] >> 12;
        }
        v->env[0] = decay(v->env[0], v->coef[0]);
        quiet = v->env[0] < (FULL >> 11);
      }
    }
    if (v->choke) {
      v->chk = decay(v->chk, choke);
      x = scale(x, v->chk);
      quiet |= v->chk < (FULL >> 10);
    }
    v->t++;
    x = (int32_t)(((int64_t)x * v->gain) >> 15);
    out[i] += x;
    if (quiet) {
      v->on = 0;
      return;
    }
  }
}

void drums_render(int t, int32_t *out, uint32_t n) {
  for (int i = 0; i < POLY; i++) {
    if (dv[t][i].on) {
      voice_render(&dv[t][i], out, n);
    }
  }
}

void drums_hit(int t, int v, int vel) { drum_hit(t, v, vel); }

/* ---- for the screen: how loud a voice is over the first second */
void drum_envelope(int t, int v, uint8_t *h, int n) {
  const track_t *d = &song.track[t];
  int m = d->machine < machine_count() ? d->machine : 0, semis;
  int r = m >= M_SYNTH ? -1 : resolve(m, v, &semis);
  for (int i = 0; i < n; i++) {
    h[i] = 0;
  }
  if (r < 0 || n <= 0) {
    return;
  }
  if (m >= M_ROM) { /* the recording's own peaks */
    uint32_t len = IS_SMP(m) ? SAMPLES[r].len : ROM[m - M_ROM][r].len;
    uint32_t rate = IS_SMP(m) ? 22050 : (ROM[m - M_ROM][r].step * 44100u) >> 16;
    int32_t pred = 0, idx = IS_SMP(m) ? 0 : ROM[m - M_ROM][r].idx0;
    for (uint32_t k = 0; k < len; k++) {
      int col = (int)(k * 1000u / rate * (uint32_t)n / 1000u), a;
      if (col >= n) {
        break;
      }
      a = IS_SMP(m) ? SAMPLES[r].data[k] : ima_next(ROM[m - M_ROM][r].data, k, &pred, &idx);
      a = (a < 0 ? -a : a) >> 7;
      if (a > h[col]) {
        h[col] = (uint8_t)(a > 255 ? 255 : a);
      }
    }
    return;
  }
  const patch_t *p = &PATCH[m][r];
  int ds = (d->decay - 64) * 36 / 128;
  int tau = p->kind == K_SWEPT ? p->t[1] : p->kind == K_SNARE ? (p->t[0] > p->t[2] ? p->t[0] : p->t[2])
                                       : p->kind == K_PING    ? p->t[0]
                                                              : p->t[1];
  uint64_t e = FULL, k = TAU[clamp(tau + ds, 0, 255)], step = 0x80000000u;
  for (uint32_t b = 44100u / (uint32_t)n, c = (uint32_t)k; b; b >>= 1) { /* the multiplier per column */
    if (b & 1) {
      step = step * c >> 31;
    }
    c = (uint32_t)((uint64_t)c * c >> 31);
  }
  for (int i = 0; i < n; i++) {
    h[i] = (uint8_t)((e * 255) >> 30);
    e = e * step >> 31;
  }
}

const int16_t *gain_table(void) { return &GAIN[0][0]; } /* for test/render.c --calibrate */
