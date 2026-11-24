/* BAR-ROCK -- factory registrations in two banks of sixteen.
 *
 * Bank 1 (registrations 1-16, CLEAN): stop combinations, soundboard and
 * reverb only. Every rack amount is zero, so the effects stage is a bypass.
 * Bank 2 (registrations 17-32, FX): the same voicings with a curated rack
 * scene. Detailed FX controls (rates, rotor depth, echo time...) stay where
 * the player last put them, so a registration never fights the player.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BR_REGISTRATIONS_H
#define BR_REGISTRATIONS_H

typedef struct {
  const char *name;
  float detune, bright, damping, body, volume, reverb;
  int choir_a, choir_b;
  float sat_type, drive, phaser_mix, chorus_mix, flanger_mix;
  int rotary_speed;                 /* 0 = off, 1 = slow, 2 = fast */
  float crush_mix;
} br_registration;

#define BR_BANK_REG_COUNT 16
#define BR_BANK_COUNT 2
#define BR_REG_COUNT (BR_BANK_COUNT * BR_BANK_REG_COUNT)

static const char *const BR_BANK_NAMES[BR_BANK_COUNT] = { "CLEAN", "FX" };

static const br_registration BR_REGISTRATIONS[BR_REG_COUNT] = {
  /* ---- CLEAN: soundboard + reverb only; no rack colour -----------------------
     name       det  bri  dam  body vol  rev   A B  sat drv pha cho fle rot cru */
  { "8 + 8",     6.0f, 0.82f, 0.85f, 0.60f, 1.00f, 0.28f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "WIDE 8",   18.0f, 0.86f, 0.82f, 0.65f, 0.98f, 0.46f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "CLOSE 8",   2.0f, 0.78f, 0.88f, 0.55f, 1.02f, 0.10f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "BRIGHT 8",  8.0f, 0.96f, 0.74f, 0.70f, 0.96f, 0.28f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "DARK 8",    5.0f, 0.60f, 0.93f, 0.45f, 1.04f, 0.38f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "SOLO A",    0.0f, 0.86f, 0.85f, 0.60f, 1.00f, 0.22f, 1, 0, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "SOLO B",    0.0f, 0.84f, 0.85f, 0.60f, 1.00f, 0.22f, 0, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "LONG 8",    4.0f, 0.80f, 0.98f, 0.75f, 0.96f, 0.68f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "SOFT 8",    3.0f, 0.66f, 0.90f, 0.50f, 1.08f, 0.32f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "PLUCK",    10.0f, 0.99f, 0.70f, 0.62f, 0.98f, 0.18f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "AIR 8",    22.0f, 0.74f, 0.86f, 0.58f, 1.00f, 0.58f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "DEEP 8",    6.0f, 0.72f, 0.96f, 0.85f, 1.00f, 0.42f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "DRY 8",     7.0f, 0.84f, 0.86f, 0.58f, 1.04f, 0.02f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "WET 8",     7.0f, 0.84f, 0.86f, 0.58f, 1.00f, 0.85f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "SHIMMER",  25.0f, 0.92f, 0.80f, 0.72f, 0.94f, 0.52f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "TUTTI",    12.0f, 0.90f, 0.88f, 0.80f, 1.06f, 0.34f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.00f, 0, 0.00f },

  /* ---- FX: the same voices plus a curated rack scene -----------------------
     name       det  bri  dam  body vol  rev   A B  sat drv pha cho fle rot cru */
  { "FX 8+8",    6.0f, 0.82f, 0.85f, 0.60f, 1.00f, 0.28f, 1, 1, 0, 0.00f, 0.00f, 0.12f, 0.00f, 0, 0.00f },
  { "FX WIDE",  18.0f, 0.86f, 0.82f, 0.65f, 0.98f, 0.46f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.28f, 0, 0.00f },
  { "FX CLOSE",  2.0f, 0.78f, 0.88f, 0.55f, 1.02f, 0.10f, 1, 1, 1, 0.08f, 0.10f, 0.00f, 0.00f, 0, 0.00f },
  { "FX BRIGHT", 8.0f, 0.96f, 0.74f, 0.70f, 0.96f, 0.28f, 1, 1, 1, 0.18f, 0.00f, 0.00f, 0.00f, 0, 0.00f },
  { "FX DARK",   5.0f, 0.60f, 0.93f, 0.45f, 1.04f, 0.38f, 1, 1, 0, 0.00f, 0.22f, 0.00f, 0.00f, 0, 0.00f },
  { "FX SOLO A", 0.0f, 0.86f, 0.85f, 0.60f, 1.00f, 0.22f, 1, 0, 0, 0.00f, 0.00f, 0.00f, 0.14f, 0, 0.00f },
  { "FX SOLO B", 0.0f, 0.84f, 0.85f, 0.60f, 1.00f, 0.22f, 0, 1, 0, 0.00f, 0.00f, 0.00f, 0.18f, 0, 0.00f },
  { "FX LONG",   4.0f, 0.80f, 0.98f, 0.75f, 0.96f, 0.68f, 1, 1, 0, 0.00f, 0.00f, 0.14f, 0.00f, 1, 0.00f },
  { "FX SOFT",   3.0f, 0.66f, 0.90f, 0.50f, 1.08f, 0.32f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.18f, 0, 0.00f },
  { "FX PLUCK", 10.0f, 0.99f, 0.70f, 0.62f, 0.98f, 0.18f, 1, 1, 2, 0.34f, 0.00f, 0.00f, 0.00f, 0, 0.10f },
  { "FX AIR",   22.0f, 0.74f, 0.86f, 0.58f, 1.00f, 0.58f, 1, 1, 0, 0.00f, 0.00f, 0.00f, 0.34f, 0, 0.00f },
  { "FX DEEP",   6.0f, 0.72f, 0.96f, 0.85f, 1.00f, 0.42f, 1, 1, 2, 0.12f, 0.00f, 0.00f, 0.00f, 1, 0.00f },
  { "FX DRY",    7.0f, 0.84f, 0.86f, 0.58f, 1.04f, 0.02f, 1, 1, 2, 0.06f, 0.08f, 0.00f, 0.00f, 0, 0.00f },
  { "FX WET",    7.0f, 0.84f, 0.86f, 0.58f, 1.00f, 0.85f, 1, 1, 0, 0.00f, 0.00f, 0.22f, 0.10f, 0, 0.00f },
  { "FX SHIMMER",25.0f, 0.92f, 0.80f, 0.72f, 0.94f, 0.52f, 1, 1, 0, 0.00f, 0.16f, 0.26f, 0.24f, 0, 0.00f },
  { "FX TUTTI", 12.0f, 0.90f, 0.88f, 0.80f, 1.06f, 0.34f, 1, 1, 2, 0.28f, 0.10f, 0.20f, 0.00f, 2, 0.00f }
};

static int br_bank_of(int r) { return r >= BR_BANK_REG_COUNT ? 1 : 0; }
static int br_slot_of(int r) { return r < 0 ? 0 : r % BR_BANK_REG_COUNT; }
static int br_make_reg(int bank, int slot)
{
  if (bank < 0) bank = 0;
  if (bank >= BR_BANK_COUNT) bank = BR_BANK_COUNT - 1;
  if (slot < 0) slot = 0;
  if (slot >= BR_BANK_REG_COUNT) slot = BR_BANK_REG_COUNT - 1;
  return bank * BR_BANK_REG_COUNT + slot;
}
static const char *br_bank_name(int r) { return BR_BANK_NAMES[br_bank_of(r)]; }

#endif
