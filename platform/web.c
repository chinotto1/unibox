/* SPDX-License-Identifier: GPL-3.0-only */
/* foma in the browser: the API of fm1.h over a simulated FM-1, compiled with your code to
 * WebAssembly (tools/build.py) and run in an AudioWorklet (web/worklet.js). Producing sound moves
 * the device's clock: one pass of loop() per millisecond, the audio callback every 256 frames, as
 * on the FM-1. The page sends the panel in and reads the screen, the LEDs, MIDI and the settings. */
#include <stdint.h>

#include "fm1.h"
#include "libc.c"

typedef struct { /* as gfx.c */
  uint8_t h;
  uint8_t pad;
  uint8_t first, last;
  const uint8_t *adv;
  const uint8_t *bw;
  const uint16_t *off;
  const uint8_t *data;
} felucca_font_t;
#include "felucca_font.h"
#include "common.c"

#define EXPORT(name) __attribute__((export_name(#name)))
#define HALF_FRAMES 256u

static uint32_t now_ms, now_us, fb_gen;

uint32_t millis(void) { return now_ms; }
uint32_t micros(void) { return now_us; }

/* ---- the panel: the page sets what is held; loop() sees it as the FM-1 shows it */
static uint32_t web_k, web_b, web_master_v = 700;
static int32_t web_e[ENC_COUNT];
static uint32_t in_keys, in_kp, in_kr, in_btn, in_bp, in_br, led_k, led_b;
static int32_t in_enc[ENC_COUNT];
static uint32_t taps_k, taps_b; /* presses shorter than a millisecond still count */

EXPORT(web_keys)
void web_keys(uint32_t m) {
  taps_k |= m & ~web_k;
  web_k = m;
}
EXPORT(web_buttons)
void web_buttons(uint32_t m) {
  taps_b |= m & ~web_b;
  web_b = m;
}
EXPORT(web_encoder)
void web_encoder(int e, int32_t n) {
  if ((unsigned)e < ENC_COUNT) {
    web_e[e] += n;
  }
}
EXPORT(web_master)
void web_master(uint32_t v) { web_master_v = v > 1023u ? 1023u : v; }
EXPORT(web_led_keys)
uint32_t web_led_keys(void) { return led_k; }
EXPORT(web_led_buttons)
uint32_t web_led_buttons(void) { return led_b; }

static void input_snapshot(void) {
  int e;
  in_kp = taps_k | (web_k & ~in_keys);
  in_kr = (in_keys & ~web_k) | (taps_k & ~web_k);
  in_keys = web_k;
  in_bp = taps_b | (web_b & ~in_btn);
  in_br = (in_btn & ~web_b) | (taps_b & ~web_b);
  in_btn = web_b;
  taps_k = taps_b = 0;
  for (e = 0; e < ENC_COUNT; e++) {
    in_enc[e] = web_e[e];
    web_e[e] = 0;
  }
}
uint32_t keys(void) { return in_keys; }
int key_held(int k) { return (unsigned)k < KEY_COUNT && (in_keys >> k & 1u); }
int key_pressed(int k) { return (unsigned)k < KEY_COUNT && (in_kp >> k & 1u); }
int key_released(int k) { return (unsigned)k < KEY_COUNT && (in_kr >> k & 1u); }
uint32_t buttons(void) { return in_btn; }
int button_held(int b) { return (unsigned)b < BTN_COUNT && (in_btn >> b & 1u); }
int button_pressed(int b) { return (unsigned)b < BTN_COUNT && (in_bp >> b & 1u); }
int button_released(int b) { return (unsigned)b < BTN_COUNT && (in_br >> b & 1u); }
int32_t encoder(int e) { return (unsigned)e < ENC_COUNT ? in_enc[e] : 0; }
uint32_t master_knob(void) { return web_master_v; }
void led_key(int k, int on) {
  if ((unsigned)k < KEY_COUNT) {
    led_k = on ? led_k | 1u << k : led_k & ~(1u << k);
  }
}
void led_button(int b, int on) {
  if ((unsigned)b < BTN_COUNT) {
    led_b = on ? led_b | 1u << b : led_b & ~(1u << b);
  }
}

/* ---- the screen: the page reads fb when fb_gen moves */
EXPORT(web_fb)
uint16_t *web_fb(void) { return fb; }
EXPORT(web_fb_gen)
uint32_t web_fb_gen(void) { return fb_gen; }
static void screen_flush(void) {
  if (fb_y0 < fb_y1) {
    fb_gen++;
  }
  fb_y0 = SCREEN_H;
  fb_y1 = 0;
}

/* ---- MIDI: two rings of packed messages (status | d1 << 8 | d2 << 16) */
#define MQ 256u
static uint32_t mq_in[MQ], mq_out[MQ], mi_w, mi_r, mo_w, mo_r;
EXPORT(web_midi_in)
void web_midi_in(uint32_t st, uint32_t d1, uint32_t d2) {
  if (mi_w - mi_r < MQ) {
    mq_in[mi_w++ % MQ] = (st & 0xFFu) | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16;
  }
}
EXPORT(web_midi_out)
int32_t web_midi_out(void) { /* -1: none */
  return mo_r == mo_w ? -1 : (int32_t)mq_out[mo_r++ % MQ];
}
int midi_read(uint8_t *status, uint8_t *d1, uint8_t *d2) {
  uint32_t m;
  if (mi_r == mi_w) {
    return 0;
  }
  m = mq_in[mi_r++ % MQ];
  *status = (uint8_t)m;
  *d1 = (uint8_t)(m >> 8);
  *d2 = (uint8_t)(m >> 16);
  return 1;
}
void midi_send(uint8_t status, uint8_t d1, uint8_t d2) {
  if (mo_w - mo_r < MQ) {
    mq_out[mo_w++ % MQ] = status | (uint32_t)(d1 & 0x7Fu) << 8 | (uint32_t)(d2 & 0x7Fu) << 16;
  }
}

/* ---- settings: the page keeps them (localStorage) and gives them back before boot */
static uint8_t store[SETTINGS_MAX];
static int32_t store_len = -1;
static uint32_t store_gen;
EXPORT(web_store)
uint8_t *web_store(void) { return store; }
EXPORT(web_store_len)
int32_t web_store_len(void) { return store_len; }
EXPORT(web_store_set_len)
void web_store_set_len(int32_t n) { store_len = n > (int32_t)SETTINGS_MAX ? (int32_t)SETTINGS_MAX : n; }
EXPORT(web_store_gen)
uint32_t web_store_gen(void) { return store_gen; }
int settings_load(void *p, uint32_t n) {
  if (store_len < 0 || (uint32_t)store_len != n) {
    return -1;
  }
  memcpy(p, store, n);
  return 0;
}
int settings_save(const void *p, uint32_t n) {
  if (n > sizeof store) {
    return -1;
  }
  memcpy(store, p, n);
  store_len = (int32_t)n;
  store_gen++;
  return 0;
}

/* the sample store, in RAM: the page can read it, it does not survive a reload */
#define STORE_N (STORE_HI - STORE_LO)
static uint8_t flash_mem[STORE_N];
static uint8_t flash_ready;
static void flash_init(void) {
  uint32_t i;
  if (flash_ready) {
    return;
  }
  for (i = 0; i < STORE_N; i++) {
    flash_mem[i] = 0xFF;
  }
  flash_ready = 1;
}
const uint8_t *store_ptr(uint32_t off) {
  flash_init();
  if (off < STORE_LO || off >= STORE_HI) {
    return 0;
  }
  return flash_mem + (off - STORE_LO);
}
int store_erase(uint32_t off) {
  uint32_t i;
  flash_init();
  if ((off & 0xFFFu) || off < STORE_LO || off + 0x1000u > STORE_HI) {
    return -1;
  }
  for (i = 0; i < 0x1000u; i++) {
    flash_mem[off - STORE_LO + i] = 0xFF;
  }
  return 0;
}
int store_write(uint32_t off, const void *src, uint32_t n) {
  const uint8_t *s = src;
  uint32_t i;
  flash_init();
  if (!n || off < STORE_LO || off + n > STORE_HI) {
    return -1;
  }
  for (i = 0; i < n; i++) {
    flash_mem[off - STORE_LO + i] = s[i];
  }
  return 0;
}

/* USB audio in: the worklet copies a block here before web_render */
#define AIN_N 2048u
static float ain_l[1024], ain_r[1024];
static int16_t ain[AIN_N * 2u];
static volatile uint32_t ain_w, ain_rd;
static uint8_t ain_on;
EXPORT(web_in_l)
float *web_in_l(void) { return ain_l; }
EXPORT(web_in_r)
float *web_in_r(void) { return ain_r; }
EXPORT(web_in_ready)
void web_in_ready(uint32_t n) {
  uint32_t i;
  if (n > 1024u) {
    n = 1024u;
  }
  for (i = 0; i < n; i++) {
    float l = ain_l[i], r = ain_r[i];
    if (ain_w - ain_rd >= AIN_N) {
      ain_rd++;
    }
    ain[(ain_w % AIN_N) * 2u] = (int16_t)(l > 1.0f ? 32767 : l < -1.0f ? -32767 : (int32_t)(l * 32767.0f));
    ain[(ain_w % AIN_N) * 2u + 1u] = (int16_t)(r > 1.0f ? 32767 : r < -1.0f ? -32767 : (int32_t)(r * 32767.0f));
    ain_w++;
  }
  ain_on = 1;
}
int usb_audio_active(void) { return ain_on && ain_w != ain_rd; }
int usb_audio_in(int16_t *lr, uint32_t frames) {
  uint32_t i;
  if (!ain_on) {
    return 0;
  }
  for (i = 0; i < frames; i++) {
    if (ain_rd != ain_w) {
      lr[2u * i] = ain[(ain_rd % AIN_N) * 2u];
      lr[2u * i + 1u] = ain[(ain_rd % AIN_N) * 2u + 1u];
      ain_rd++;
    } else {
      lr[2u * i] = lr[2u * i + 1u] = 0;
    }
  }
  return 1;
}

/* ---- print: a text ring the page drains into its console */
#define LOG 4096u
static char log_buf[LOG];
static uint32_t log_w, log_r;
static char log_out[LOG + 1];
void print(const char *s) {
  while (*s) {
    log_buf[log_w++ % LOG] = *s++;
    if (log_w - log_r > LOG) {
      log_r = log_w - LOG;
    }
  }
  log_buf[log_w++ % LOG] = '\n';
}
EXPORT(web_log)
char *web_log(void) { /* "" when nothing new */
  uint32_t n = 0;
  while (log_r != log_w) {
    log_out[n++] = log_buf[log_r++ % LOG];
  }
  log_out[n] = 0;
  return log_out;
}

/* ---- the clock: each millisecond one pass of loop(); the sound in blocks of 256 frames into a ring */
#define RING 4096u
static float ring[RING * 2u], out_l[1024], out_r[1024];
static uint32_t ring_w, ring_r, owed;
static int32_t blk[2u * HALF_FRAMES];

static void tick(void) {
  now_ms++;
  now_us += 1000u;
  input_snapshot();
  loop();
  screen_flush();
  owed += SAMPLE_RATE;
  while (owed >= 1000u * HALF_FRAMES) {
    uint32_t i, q12 = (web_master_v * web_master_v) >> 8;
    owed -= 1000u * HALF_FRAMES;
    for (i = 0; i < 2u * HALF_FRAMES; i++) {
      blk[i] = 0;
    }
    audio(blk, HALF_FRAMES);
    for (i = 0; i < HALF_FRAMES; i++) {
      uint32_t r = (ring_w++ % RING) * 2u, c;
      for (c = 0; c < 2u; c++) {
        int32_t v = (blk[2u * i + c] * (int32_t)q12) >> 12;
        v = v > 32767 ? 32767 : v < -32767 ? -32767
                                           : v;
        ring[r + c] = (float)v / 65536.0f; /* the FM-1's DAC gets it 6 dB down */
      }
    }
  }
}

EXPORT(web_boot)
void web_boot(void) {
  setup();
  screen_flush();
  fb_gen++;
}
EXPORT(web_out_l)
float *web_out_l(void) { return out_l; }
EXPORT(web_out_r)
float *web_out_r(void) { return out_r; }
EXPORT(web_now)
uint32_t web_now(void) { return now_ms; }
EXPORT(web_render)
void web_render(uint32_t n) { /* n frames (<= 1024) into out_l / out_r */
  uint32_t k;
  if (n > 1024u) {
    n = 1024u;
  }
  while (ring_w - ring_r < n) {
    tick();
  }
  for (k = 0; k < n; k++) {
    uint32_t i = (ring_r++ % RING) * 2u;
    out_l[k] = ring[i];
    out_r[k] = ring[i + 1u];
  }
}
