/* The sample instruments. Four banks live in the flash store; each bank is one instrument,
 * keyed from F3 upward. The screen drives a session; audio() feeds it USB audio. */
#ifndef SAMPLER_H
#define SAMPLER_H
#include <stdint.h>

#define SAMP_BANKS 4
#define SAMP_SLOTS 8

void sampler_init(void);
void sampler_pull(int16_t *scratch, uint32_t scratch_n, uint32_t frames); /* from audio(); scratch is the delay line */
int sampler_busy(void);                                                   /* the delay line is in use */

int sampler_bank(void);
void sampler_set_bank(int bank);
int sampler_time_ds(void); /* tenths of a second */
void sampler_set_time(int ds);
int sampler_armed(void); /* the slot that records next, or -1 while recording */
int sampler_slot(void);    /* that slot, including while it is recording */
int sampler_recording(void);
uint8_t sampler_vu(void); /* 0..255 */

const char *sampler_bank_name(int bank);
int sampler_slot_note(int bank, int slot); /* MIDI note, or -1 when the slot is empty */
int sampler_has(int bank, int note);
int sampler_voice(int bank, int note, const uint8_t **data, uint32_t *samples, uint8_t *idx0);

int sampler_start(void);          /* record the armed slot; 0 if there is no room */
int sampler_flush(char *msg);     /* finish a slot that just filled; 1: msg is set */
void sampler_begin_name(void);
int sampler_naming(void);
const char *sampler_name_buf(void);
void sampler_name_char(int c);
void sampler_name_back(void);
void sampler_name_save(void);

#endif
