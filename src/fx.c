/* FOMAJAM's effects, after the delay (engine.c): a reverb and a chorus that tracks send to, then the master
 * filter and the distortion. Integers only: this runs in audio(). */
#include "groove.h"
#include "tables.h"

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : v > hi ? hi
                              : v;
}

/* ---- the reverb: Freeverb's 8 combs (each damped in its loop) and 4 allpasses, mono */
static const uint16_t COMB[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, ALLP[4] = {556, 441, 341, 225};
#define RLEN (1116 + 1188 + 1277 + 1356 + 1422 + 1491 + 1557 + 1617 + 556 + 441 + 341 + 225)
static int16_t rline[RLEN] POOL;
static uint16_t rpos[12];
static int32_t rlp[8];
static uint8_t r_idle; /* 1: nothing in and the tail has died: skip it all */

static int32_t mulq(int32_t a, int32_t b) { /* a * b (Q15), rounded toward 0: else a loop never quite dies out */
  int32_t p = a * b;
  return p < 0 ? -(-p >> 15) : p >> 15;
}

void fx_reverb(const int32_t *in, int32_t *out, uint32_t n) {
  int32_t any = 0;
  for (uint32_t i = 0; i < n; i++) {
    any |= in[i];
  }
  if (!any && r_idle) {
    return;
  }
  int32_t fb = 22938 + song.rsize * 9175 / 127, damp = song.rdamp * 19660 / 127, peak = 0; /* Q15: 0.7..0.98, 0..0.6 */
  for (uint32_t i = 0; i < n; i++) {
    int32_t x = in[i] >> 4, sum = 0;
    int16_t *b = rline;
    for (int c = 0; c < 8; c++) {
      int32_t y = b[rpos[c]];
      rlp[c] = y + mulq(rlp[c] - y, damp);
      b[rpos[c]] = (int16_t)clamp(x + mulq(rlp[c], fb), -32767, 32767);
      if (++rpos[c] == COMB[c]) {
        rpos[c] = 0;
      }
      sum += y;
      b += COMB[c];
    }
    sum >>= 1;
    for (int a = 0; a < 4; a++) {
      int32_t bo = b[rpos[8 + a]];
      b[rpos[8 + a]] = (int16_t)clamp(sum + bo / 2, -32767, 32767);
      if (++rpos[8 + a] == ALLP[a]) {
        rpos[8 + a] = 0;
      }
      sum = bo - sum;
      b += ALLP[a];
    }
    out[i] += sum;
    peak |= sum > 8 || sum < -8;
  }
  r_idle = !any && !peak;
}

/* ---- the chorus: two taps swept by a triangle (the second half a cycle behind), around 12 ms */
#define CLEN 2048
static int16_t cline[CLEN] POOL;
static uint32_t cpos, cph;
static uint16_t c_quiet; /* frames since anything came in */

static int32_t tap(int32_t d) { /* d: the delay in frames, Q8 */
  int32_t whole = d >> 8, frac = d & 255;
  int32_t a = cline[(cpos - (uint32_t)whole) & (CLEN - 1)], b = cline[(cpos - (uint32_t)whole - 1) & (CLEN - 1)];
  return a + ((b - a) * frac >> 8);
}

void fx_chorus(const int32_t *in, int32_t *out, uint32_t n) {
  int32_t any = 0;
  for (uint32_t i = 0; i < n; i++) {
    any |= in[i];
  }
  if (!any && c_quiet > CLEN) {
    return;
  }
  c_quiet = any ? 0 : (uint16_t)(c_quiet + n);
  uint32_t inc = 4870u + song.crate * 3800u; /* 0.05 .. 5 Hz */
  int32_t depth = 256 + song.cdepth * 2560;  /* Q8 frames: up to ~ 7 ms either way */
  for (uint32_t i = 0; i < n; i++) {
    cline[cpos & (CLEN - 1)] = (int16_t)clamp(in[i], -32767, 32767);
    cph += inc;
    int32_t t1 = (int32_t)(cph >> 16) - 32768, t2 = (int32_t)((cph + 0x80000000u) >> 16) - 32768; /* saws */
    t1 = (t1 < 0 ? -t1 : t1) * 2 - 32768;                                                         /* triangles */
    t2 = (t2 < 0 ? -t2 : t2) * 2 - 32768;
    int32_t base = 529 << 8;
    out[i] += (tap(base + (t1 * (depth >> 8) >> 15) * 256) + tap(base + (t2 * (depth >> 8) >> 15) * 256)) >> 1;
    cpos++;
  }
}

/* ---- the master filter: one knob, low-pass below the middle, high-pass above (a TPT state variable filter) */
static int32_t f1, f2; /* its state, Q8 (so a low cutoff's tiny steps add up) */

static void filter(int32_t *x, uint32_t n) {
  int f = song.filter;
  if (f == 64) {
    f1 = f2 = 0;
    return;
  }
  int64_t g = FILT_G[f & 127], k = 92682 - song.reso * 86000 / 127; /* Q16: k = 1/Q, 1.41 .. 0.1 */
  uint32_t den = (uint32_t)(65536 + ((g * (g + k)) >> 16));
  int64_t a1 = ((uint32_t)1 << 31) / den; /* Q15 */
  int64_t a2 = (g * a1) >> 1;             /* Q30 */
  int64_t a3 = (g * a2) >> 16;            /* Q30 */
  for (uint32_t i = 0; i < n; i++) {
    int32_t v0 = clamp(x[i], -98304, 98304) * 256, v3 = v0 - f2;
    int32_t v1 = (int32_t)(((a1 << 15) * f1 + a2 * v3) >> 30);
    int32_t v2 = f2 + (int32_t)((a2 * f1 + a3 * v3) >> 30);
    f1 = 2 * v1 - f1;
    f2 = 2 * v2 - f2;
    x[i] = (f < 64 ? v2 : v0 - (int32_t)((k * v1) >> 16) - v2) >> 8;
  }
}

/* ---- the distortion: DRIVE is how much, TYPE what kind */
const char *const DIST_NAME[DISTS] = {"SOFT", "HARD", "TUBE", "DIODE", "FUZZ", "FOLD", "WAVE", "RECT", "BITS", "RATE", "CRUSH"};

static int32_t sat(int32_t v) {                                              /* a smooth tanh, +-32767 */
  int32_t s = clamp(v, -98304, 98304), u2 = (int32_t)((int64_t)s * s >> 15); /* (s / 32768)^2, Q15 */
  int32_t r = ((27 * 32768 + u2) << 10) / ((27 * 32768 + 9 * u2) >> 5);      /* (27 + u^2) / (27 + 9 u^2), Q15 */
  return (int32_t)((int64_t)s * r >> 15);
}

static int32_t fold(int32_t v) { /* a triangle fold back into +-32768 */
  uint32_t p = (uint32_t)(v + 32768) & 131071;
  return p < 65536 ? (int32_t)p - 32768 : 98304 - (int32_t)p;
}

static int32_t hold_x, hold_n;

static int32_t shape(int32_t x, int type, int d, int live) { /* d: DRIVE 0..127 */
  int32_t v;
  switch (type) {
    case D_HARD:
      return clamp(x * (256 + d * 16) >> 8, -24000, 24000);
    case D_TUBE: /* warm: more on one side (even harmonics) */
      v = x * (256 + d * 10) >> 8;
      return sat(v + (int32_t)((int64_t)v * (v < 0 ? -v : v) >> 15) * d / 254);
    case D_DIODE: /* soft above, a hard knee below */
      v = x * (256 + d * 14) >> 8;
      return v > 0 ? sat(v) : clamp(v, -14000, 0) + sat(v - clamp(v, -14000, 0)) / 8;
    case D_FUZZ:
      v = x * (512 + d * 48) >> 8;
      return clamp(v, -12000, 20000) * 3 / 2;
    case D_FOLD:
      return fold(x * (256 + d * 20) >> 8);
    case D_WAVE: { /* a fold with round tops: like a sine shaper */
      int64_t s = fold(x * (256 + d * 20) >> 8);
      return (int32_t)(s * 3 / 2 - (s * s * s >> 31));
    }
    case D_RECT: { /* rectified in, as DRIVE turns: an octave up, fuzzy */
      v = sat(x * (256 + d * 8) >> 8);
      return (x * (127 - d) + (v < 0 ? -v : v) * 2 * d) / 127;
    }
    case D_BITS: {
      int sh = d * 13 / 127;
      return clamp(x, -32767, 32767) >> sh << sh;
    }
    case D_RATE:
    case D_CRUSH: {
      int every = 1 + d * (type == D_RATE ? 31 : 15) / 127;
      if (!live) {
        v = x;
      } else if (hold_n <= 0) {
        hold_x = x;
        hold_n = every;
        v = x;
      } else {
        v = hold_x;
      }
      hold_n -= live;
      if (type == D_CRUSH) {
        int sh = d * 10 / 127;
        v = clamp(v, -32767, 32767) >> sh << sh;
      }
      return v;
    }
  }
  return x * (128 + d * 6) >> 8; /* SOFT: the output's soft clip does it */
}

int32_t fx_curve(int32_t x) { return shape(x, song.dtype < DISTS ? song.dtype : 0, song.drive, 0); }

static int32_t dc_x, dc_y;

void fx_master(int32_t *x, uint32_t n) {
  filter(x, n);
  int type = song.dtype < DISTS ? song.dtype : 0, d = song.drive;
  for (uint32_t i = 0; i < n; i++) {
    int32_t v = shape(x[i], type, d, 1);
    if (type != D_SOFT) { /* the uneven kinds leave a DC offset: take it out */
      dc_y = v - dc_x + (dc_y * 32604 >> 15);
      dc_x = v;
      v = dc_y;
    }
    x[i] = v;
  }
}
