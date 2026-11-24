/* BAR-ROCK post-FX rack, adapted from the clean-room bar-fly effects.
 *
 * Chain: saturation -> ring mod -> phaser -> chorus -> flanger
 *        -> tremolo / auto-pan -> bitcrusher -> Leslie -> stereo echo
 *
 * The harpsichord's fixed-memory Schroeder reverb stays in harpsi.c; this rack
 * is the colour stage after it.  Every amount is a 0..1 float (type/speed are
 * small ints stored as floats), published by main.c as value x1000.
 *
 * Audio flow only: no heap, no libc maths, no host calls, no files.  The Leslie
 * is a clean-room dual-rotor model with separate horn/drum motors, geometric
 * Doppler and a three-tap stereo mic mix.  With every amount at zero the rack
 * is a straight bypass, so the clean harpsichord is unchanged.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BR_FX_H
#define BR_FX_H

#include "vibe.h"

typedef struct {
  float sat_type;              /* 0 = off, 1 = solid state, 2 = tube */
  float drive;                 /* 0..1 */
  float rotary_on;             /* 0 / 1 */
  float rotary_speed;          /* 0 = stop, 1 = slow, 2 = fast */
  float rotary_depth;          /* 0..1, Doppler depth */
  float rotary_drive;          /* 0..1, horn softening */
  float rotary_balance;        /* 0 = bass, 0.5 = even, 1 = horn */
  float phaser_mix;            /* 0..1 */
  float phaser_rate;           /* Hz */
  float chorus_mix;            /* 0..1 */
  float chorus_rate;           /* Hz */
  float flanger_mix;           /* 0..1 */
  float trem_depth;            /* 0..1 */
  float pan_depth;             /* 0..1 */
  float ring_mix;              /* 0..1 */
  float crush_mix;             /* 0..1 */
  float delay_type;            /* 0 = off, 1 = ping-pong, 2 = tape */
  float delay_mix;             /* 0..1 */
  float delay_ms;              /* 80..240 */
  float delay_fb;              /* 0..1 */
} br_fx_ctrl;

void br_fx_init(float sample_rate);
void br_fx_reset(void);
void br_fx_process(float *l, float *r, int n, const br_fx_ctrl *c);
int  br_fx_rotor_hz100(void);  /* for the display; atomic, 0 when stopped */

#endif
