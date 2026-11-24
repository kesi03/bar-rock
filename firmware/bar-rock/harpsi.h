/* BAR-ROCK -- fixed-voice, allocation-free Karplus-Strong harpsichord engine.
 *
 * 16 voices, two slightly detuned 8' choirs, a cheap two-pole soundboard, a
 * fixed-memory Schroeder stereo reverb and note on/off damping.  Everything is
 * static: no malloc, no libc maths, no
 * sample data.  Audio-flow only; the host sample rate is supplied at init.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BR_HARPSI_H
#define BR_HARPSI_H

#include "vibe.h"

#define BR_HARPSI_VOICES 16
#define BR_KS_MAX_DELAY  4096   /* covers MIDI 21 at 96 kHz; extreme lows clamp */

typedef struct {
  float buffer[BR_KS_MAX_DELAY];
  int size;
  int pos;
  float damping;
  float brightness;
} br_string;

typedef struct {
  int active;
  int note;
  unsigned int age;
  float level;
  float decay;
  br_string a;                  /* 8' choir A */
  br_string b;                  /* 8' choir B, slightly detuned */
} br_voice;

/* Snapshot from the control flow, read once per audio block. */
typedef struct {
  float detune;                 /* cents between the two 8' choirs */
  float brightness;             /* 0..1: KS lowpass blend */
  float damping;                /* 0..1: string damping */
  float body;                   /* 0..1: soundboard contribution */
  float reverb;                 /* 0..1: algorithmic room contribution */
  float volume;                 /* master level */
  float choir_a;                /* 0 = off, 1 = on */
  float choir_b;                /* 0 = off, 1 = on */
} br_harpsi_ctrl;

void br_harpsi_init(float sr);
void br_harpsi_set_ctrl(const br_harpsi_ctrl *c);
void br_harpsi_note_on(int note, int vel);
void br_harpsi_note_off(int note);
void br_harpsi_all_off(void);
void br_harpsi_render(float *out_l, float *out_r, int n, const br_harpsi_ctrl *c);
int  br_harpsi_voice_count(void);

#endif
