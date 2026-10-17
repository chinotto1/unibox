/* FZ-1 style sampling. USB audio is decimated to 22050 Hz mono, held in the delay line, then
 * stored as 4-bit IMA ADPCM in one of four banks. A bank is an instrument: slot 0 is F3, then up. */
#include "sampler.h"

#include "fm1.h"

#define SAMP_MAGIC 0x31535055u
#define SAMP_DATA 0x00098000u
#define SAMP_BANK (72u * 1024u) /* 0x12000: one instrument, kept off the SFR literal check */
#define SAMP_NOTE0 KEY_FIRST_NOTE

static const int16_t STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t INDEX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

typedef struct {
  uint32_t off, samples;
  uint8_t idx0, note;
  uint8_t pad[2];
} slot_t;

typedef struct {
  char name[8];
  uint8_t n;
  uint8_t pad[3];
  slot_t slot[SAMP_SLOTS];
} bank_t;

typedef struct {
  uint32_t magic, crc;
  bank_t bank[SAMP_BANKS];
} dir_t;

static dir_t dir;
static int bank_i, time_ds = 10, arm, naming;
static char namebuf[9];
static volatile uint8_t rec_on, rec_done;
static volatile uint32_t rec_at, rec_goal;
static volatile uint8_t vu;
static int16_t *rec_ptr;
static uint8_t enc[24000];

static uint32_t sum(const uint8_t *p, uint32_t n) {
  uint32_t s = 0;
  while (n--) {
    s = s * 16777619u + *p++;
  }
  return s;
}

static void blank(void) {
  static const char *const NAME[SAMP_BANKS] = {"USR1", "USR2", "USR3", "USR4"};
  uint32_t i;
  for (i = 0; i < sizeof dir; i++) {
    ((uint8_t *)&dir)[i] = 0;
  }
  dir.magic = SAMP_MAGIC;
  for (i = 0; i < SAMP_BANKS; i++) {
    const char *n = NAME[i];
    int k = 0;
    while (n[k] && k < 7) {
      dir.bank[i].name[k] = n[k];
      k++;
    }
  }
}

static void dir_write(void) {
  dir.magic = SAMP_MAGIC;
  dir.crc = 0;
  dir.crc = sum((const uint8_t *)&dir, sizeof dir);
  if (store_erase(STORE_LO)) {
    return;
  }
  store_write(STORE_LO, &dir, sizeof dir);
}

void sampler_init(void) {
  const uint8_t *p = store_ptr(STORE_LO);
  uint32_t was;
  blank();
  if (!p) {
    return;
  }
  was = ((const dir_t *)p)->crc;
  if (((const dir_t *)p)->magic != SAMP_MAGIC) {
    return;
  }
  for (uint32_t i = 0; i < sizeof dir; i++) {
    ((uint8_t *)&dir)[i] = p[i];
  }
  dir.crc = 0;
  if (sum((const uint8_t *)&dir, sizeof dir) != was) {
    blank();
  }
}

int sampler_bank(void) { return bank_i; }
void sampler_set_bank(int bank) {
  if (bank < 0) {
    bank = 0;
  }
  if (bank >= SAMP_BANKS) {
    bank = SAMP_BANKS - 1;
  }
  bank_i = bank;
  arm = dir.bank[bank].n < SAMP_SLOTS ? dir.bank[bank].n : SAMP_SLOTS - 1;
}
int sampler_time_ds(void) { return time_ds; }
void sampler_set_time(int ds) {
  if (ds < 2) {
    ds = 2;
  }
  if (ds > 20) {
    ds = 20;
  }
  time_ds = ds;
}
int sampler_armed(void) { return rec_on ? -1 : arm; }
int sampler_slot(void) { return arm; }
int sampler_recording(void) { return rec_on; }
uint8_t sampler_vu(void) { return vu; }
const char *sampler_bank_name(int bank) {
  static char s[9];
  int i = 0;
  if (bank < 0 || bank >= SAMP_BANKS) {
    return "";
  }
  for (; i < 8 && dir.bank[bank].name[i]; i++) {
    s[i] = dir.bank[bank].name[i];
  }
  s[i] = 0;
  return s;
}
int sampler_slot_note(int bank, int slot) {
  if (bank < 0 || bank >= SAMP_BANKS || slot < 0 || slot >= dir.bank[bank].n) {
    return -1;
  }
  return dir.bank[bank].slot[slot].samples ? dir.bank[bank].slot[slot].note : -1;
}
int sampler_has(int bank, int note) {
  const uint8_t *d;
  uint32_t n;
  uint8_t ix;
  return sampler_voice(bank, note, &d, &n, &ix);
}
int sampler_voice(int bank, int note, const uint8_t **data, uint32_t *samples, uint8_t *idx0) {
  const bank_t *b;
  int i;
  if (bank < 0 || bank >= SAMP_BANKS) {
    return 0;
  }
  b = &dir.bank[bank];
  for (i = 0; i < b->n; i++) {
    if (b->slot[i].note == note && b->slot[i].samples) {
      *data = store_ptr(b->slot[i].off);
      *samples = b->slot[i].samples;
      *idx0 = b->slot[i].idx0;
      return *data != 0;
    }
  }
  return 0;
}

static uint32_t bank_used(int bank) {
  const bank_t *b = &dir.bank[bank];
  uint32_t u = 0;
  int i;
  for (i = 0; i < b->n; i++) {
    uint32_t bytes = (b->slot[i].samples + 1u) / 2u;
    u = (u + bytes + 0xFFFu) & ~0xFFFu;
  }
  return u;
}

int sampler_start(void) {
  uint32_t goal, room, base, i;
  if (rec_on || rec_done || arm < 0 || arm >= SAMP_SLOTS) {
    return 0;
  }
  goal = (uint32_t)time_ds * 2205u;
  if (goal > 44100u) {
    goal = 44100u;
  }
  if (arm == 0) {
    for (i = 0; i < sizeof dir.bank[bank_i].slot; i++) {
      ((uint8_t *)dir.bank[bank_i].slot)[i] = 0;
    }
    dir.bank[bank_i].n = 0;
    base = SAMP_DATA + (uint32_t)bank_i * SAMP_BANK;
    for (i = 0; i < SAMP_BANK; i += 0x1000u) {
      if (store_erase(base + i)) {
        return 0;
      }
    }
  }
  room = SAMP_BANK - bank_used(bank_i);
  if (room < 256u) {
    return 0;
  }
  if (goal / 2u > room) {
    goal = room * 2u;
  }
  rec_goal = goal;
  rec_at = 0;
  rec_done = 0;
  rec_on = 1;
  return 1;
}

void sampler_pull(int16_t *scratch, uint32_t scratch_n, uint32_t frames) {
  static int16_t lr[512];
  static int32_t acc;
  static int have;
  uint32_t n = frames > 256u ? 256u : frames, i;
  int32_t peak = 0;
  int got = usb_audio_in(lr, n);
  /* a slot is a length of time: silence still fills it, so a quiet host cannot stall the arm */
  if (!got) {
    if (!rec_on) {
      vu = 0;
      return;
    }
    for (i = 0; i < n; i++) {
      lr[2u * i] = lr[2u * i + 1u] = 0;
    }
  }
  for (i = 0; i < n; i++) {
    int32_t m = ((int32_t)lr[2u * i] + lr[2u * i + 1u]) >> 1;
    int32_t a = m < 0 ? -m : m;
    if (a > peak) {
      peak = a;
    }
    acc += m;
    if (++have < 2) {
      continue;
    }
    have = 0;
    m = acc >> 1;
    acc = 0;
    if (rec_on && rec_at < rec_goal && rec_at < scratch_n) {
      rec_ptr = scratch;
      scratch[rec_at++] = (int16_t)m;
    }
  }
  vu = (uint8_t)(peak > 32767 ? 255 : peak >> 7);
  if (rec_on && rec_at >= rec_goal) {
    rec_on = 0;
    rec_done = 1;
  }
}

int sampler_busy(void) { return rec_on || rec_done; }

static uint32_t encode(const int16_t *x, uint32_t n, uint8_t *out, uint8_t *idx0) {
  int32_t pred = 0, idx = 0, best = 0, err = 0x7FFFFFFF;
  uint32_t i, o = 0;
  /* the step index that starts closest, over the first few samples */
  for (idx = 0; idx < 89; idx += 8) {
    int32_t p = 0, id = idx, e = 0;
    uint32_t k, lim = n < 64u ? n : 64u;
    for (k = 0; k < lim; k++) {
      int32_t step = STEP[id], d = x[k] - p, code = 0, diff;
      if (d < 0) {
        code = 8;
        d = -d;
      }
      if (d >= step) {
        code |= 4;
        d -= step;
      }
      if (d >= step >> 1) {
        code |= 2;
        d -= step >> 1;
      }
      if (d >= step >> 2) {
        code |= 1;
      }
      diff = step >> 3;
      if (code & 4) {
        diff += step;
      }
      if (code & 2) {
        diff += step >> 1;
      }
      if (code & 1) {
        diff += step >> 2;
      }
      p += code & 8 ? -diff : diff;
      if (p > 32767) {
        p = 32767;
      }
      if (p < -32768) {
        p = -32768;
      }
      id += INDEX[code & 7];
      if (id < 0) {
        id = 0;
      }
      if (id > 88) {
        id = 88;
      }
      e += (x[k] - p) < 0 ? p - x[k] : x[k] - p;
    }
    if (e < err) {
      err = e;
      best = idx;
    }
  }
  pred = 0;
  idx = best;
  *idx0 = (uint8_t)best;
  for (i = 0; i < n; i++) {
    int32_t step = STEP[idx], d = x[i] - pred, code = 0, diff;
    if (d < 0) {
      code = 8;
      d = -d;
    }
    if (d >= step) {
      code |= 4;
      d -= step;
    }
    if (d >= step >> 1) {
      code |= 2;
      d -= step >> 1;
    }
    if (d >= step >> 2) {
      code |= 1;
    }
    diff = step >> 3;
    if (code & 4) {
      diff += step;
    }
    if (code & 2) {
      diff += step >> 1;
    }
    if (code & 1) {
      diff += step >> 2;
    }
    pred += code & 8 ? -diff : diff;
    if (pred > 32767) {
      pred = 32767;
    }
    if (pred < -32768) {
      pred = -32768;
    }
    idx += INDEX[code & 7];
    if (idx < 0) {
      idx = 0;
    }
    if (idx > 88) {
      idx = 88;
    }
    if ((i & 1u) == 0) {
      out[o] = (uint8_t)code;
    } else {
      out[o++] |= (uint8_t)(code << 4);
    }
  }
  if (n & 1u) {
    o++;
  }
  return o;
}

static void put_msg(char *msg, const char *s) {
  int i = 0;
  for (; s[i] && i < 14; i++) {
    msg[i] = s[i];
  }
  msg[i] = 0;
}

int sampler_flush(char *msg) {
  bank_t *b = &dir.bank[bank_i];
  uint32_t n, bytes, at, off;
  uint8_t idx0 = 0;
  if (!rec_done) {
    return 0;
  }
  rec_done = 0;
  n = rec_at;
  if (!rec_ptr || n < 16u) {
    put_msg(msg, "EMPTY");
    return 1;
  }
  if (n > sizeof enc * 2u) {
    n = (uint32_t)sizeof enc * 2u;
  }
  bytes = encode(rec_ptr, n, enc, &idx0);
  at = bank_used(bank_i);
  off = SAMP_DATA + (uint32_t)bank_i * SAMP_BANK + at;
  if (at + ((bytes + 0xFFFu) & ~0xFFFu) > SAMP_BANK) {
    put_msg(msg, "FULL");
    return 1;
  }
  if (arm != 0) {
    uint32_t sec = off & ~0xFFFu, last = (off + bytes - 1u) & ~0xFFFu;
    while (sec <= last) {
      if (store_erase(sec)) {
        put_msg(msg, "FLASH");
        return 1;
      }
      sec += 0x1000u;
    }
  }
  if (store_write(off, enc, bytes)) {
    put_msg(msg, "FLASH");
    return 1;
  }
  b->slot[arm].off = off;
  b->slot[arm].samples = n;
  b->slot[arm].idx0 = idx0;
  b->slot[arm].note = (uint8_t)(SAMP_NOTE0 + arm);
  if (b->n < arm + 1) {
    b->n = (uint8_t)(arm + 1);
  }
  dir_write();
  arm++;
  if (arm >= SAMP_SLOTS || SAMP_BANK - bank_used(bank_i) < 256u) {
    arm = arm >= SAMP_SLOTS ? 0 : arm;
    put_msg(msg, "INSTRUMENT");
  } else {
    put_msg(msg, "NEXT SLOT");
  }
  return 1;
}

void sampler_begin_name(void) {
  int i = 0;
  naming = 1;
  for (; i < 8 && dir.bank[bank_i].name[i]; i++) {
    namebuf[i] = dir.bank[bank_i].name[i];
  }
  namebuf[i] = 0;
}
int sampler_naming(void) { return naming; }
const char *sampler_name_buf(void) { return namebuf; }
void sampler_name_char(int c) {
  int i = 0;
  if (!naming) {
    return;
  }
  while (namebuf[i]) {
    i++;
  }
  if (i < 8) {
    namebuf[i] = (char)c;
    namebuf[i + 1] = 0;
  }
}
void sampler_name_back(void) {
  int i = 0;
  while (namebuf[i]) {
    i++;
  }
  if (i) {
    namebuf[i - 1] = 0;
  }
}
void sampler_name_save(void) {
  int i = 0;
  for (; i < 8; i++) {
    dir.bank[bank_i].name[i] = namebuf[i];
  }
  naming = 0;
  dir_write();
}
