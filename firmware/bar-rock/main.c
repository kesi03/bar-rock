/* BAR-ROCK -- a polyphonic harpsichord for VIBE FM.
 *
 * Sixteen Karplus-Strong voices, two slightly detuned 8' choirs, a two-pole
 * soundboard, algorithmic reverb, a bar-fly-derived colour rack (drive, Leslie,
 * modulation and echo) and sixteen registrations.  Keys and MIDI play
 * immediately; pads 1-16 select registrations and F3/PLAY starts a short Dido's
 * Lament excerpt.  No samples, no heap, no libc maths.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vibe.h"
#include "vibe_gfx.h"
#include "vibe_dsp.h"
#include "harpsi.h"
#include "fx.h"
#include "registrations.h"
#include "demo_snippet.h"

VIBE_PARALLEL_AUDIO

/* ---- parameters: ids are stable, never rename ------------------------------- */
enum {
  P_REG = 0, P_VOLUME, P_OCTAVE, P_TRANSPOSE,
  P_DETUNE, P_BRIGHT, P_DAMPING, P_BODY, P_REVERB,
  P_CHOIR_A, P_CHOIR_B,
  /* bar-fly-derived FX rack; append only, so old automation ids survive. */
  P_SAT_TYPE, P_DRIVE,
  P_ROTARY_ON, P_ROTARY_SPEED, P_ROTARY_DEPTH, P_ROTARY_DRIVE, P_ROTARY_BALANCE,
  P_PHASER_MIX, P_PHASER_RATE, P_CHORUS_MIX, P_CHORUS_RATE, P_FLANGER_MIX,
  P_TREM_DEPTH, P_PAN_DEPTH, P_RING_MIX, P_CRUSH_MIX,
  P_DELAY_TYPE, P_DELAY_MIX, P_DELAY_MS, P_DELAY_FB,
  P_COUNT
};

static const char *reg_names[BR_REG_COUNT];
static const char *const ON_OFF[] = { "OFF", "ON" };
static const char *const OCTS[] = { "-2", "-1", "0", "+1", "+2" };
static const char *const SATS[] = { "OFF", "SOLID", "TUBE" };
static const char *const ROTOR_SPEEDS[] = { "STOP", "SLOW", "FAST" };
static const char *const ECHO_TYPES[] = { "OFF", "PING", "TAPE" };

#define PV(id,n,mi,ma,de,u) { id, n, mi, ma, de, u, 0, 0 }
#define PE(id,n,mi,ma,de,u,st,ch) { id, n, mi, ma, de, u, st, ch }

static const vibe_param PARAMS[P_COUNT] = {
  PE("registration", "Registration", 0, BR_REG_COUNT - 1, 0, "", BR_REG_COUNT, reg_names),
  PV("volume",       "Volume",       0, 1.4f, 1.00f, ""),
  PE("octave",       "Octave",       0, 4,    2,    "", 5, OCTS),
  PV("transpose",    "Transpose",  -12, 12,   0,    "st"),
  PV("detune",       "Detune",       0, 25,   6,    "ct"),
  PV("bright",       "Brightness", 0.3f, 1.0f, 0.82f, ""),
  PV("damping",      "Damping",      0, 1,    0.85f, ""),
  PV("body",         "Soundboard",   0, 1,    0.60f, ""),
  PV("reverb",       "Reverb",       0, 1,    0.28f, ""),
  PE("choir_a",      "8' Choir A",   0, 1,    1,    "", 2, ON_OFF),
  PE("choir_b",      "8' Choir B",   0, 1,    1,    "", 2, ON_OFF),

  PE("sat_type",     "Distortion",   0, 2,    0,    "", 3, SATS),
  PV("drive",        "Drive",        0, 1,    0.00f, ""),
  PE("rotary_on",    "Leslie",       0, 1,    0,    "", 2, ON_OFF),
  PE("rotary_speed", "Rotor Speed",  0, 2,    1,    "", 3, ROTOR_SPEEDS),
  PV("rotary_depth", "Rotor Depth",  0, 1,    0.80f, ""),
  PV("rotary_drive", "Horn Drive",   0, 1,    0.20f, ""),
  PV("rotary_balance", "Horn / Bass", 0, 1,   0.50f, ""),
  PV("phaser_mix",   "Phaser",       0, 1,    0.00f, ""),
  PV("phaser_rate",  "Phase Rate", 0.1f, 8.0f, 0.60f, "Hz"),
  PV("chorus_mix",   "Chorus",       0, 1,    0.00f, ""),
  PV("chorus_rate",  "Chorus Rate", 0.1f, 5.0f, 0.45f, "Hz"),
  PV("flanger_mix",  "Flanger",      0, 1,    0.00f, ""),
  PV("trem_depth",   "Tremolo",      0, 1,    0.00f, ""),
  PV("pan_depth",    "Auto Pan",     0, 1,    0.00f, ""),
  PV("ring_mix",     "Ring Mod",     0, 1,    0.00f, ""),
  PV("crush_mix",    "Lo-Fi Crush",  0, 1,    0.00f, ""),
  PE("delay_type",   "Echo Type",    0, 2,    0,    "", 3, ECHO_TYPES),
  PV("delay_mix",    "Echo Mix",     0, 1,    0.00f, ""),
  PV("delay_ms",     "Echo Time",   80, 240, 280,   "ms"),
  PV("delay_fb",     "Echo Feedback", 0, 1,   0.30f, "")
};
#undef PV
#undef PE

static const char *const GROUPS[P_COUNT] = {
  "SOUND · VOICE", "SOUND · VOICE", "SOUND · VOICE", "SOUND · VOICE",
  "STRINGS · CHOIRS", "STRINGS · CHOIRS", "STRINGS · CHOIRS",
  "STRINGS · SOUNDBOARD", "STRINGS · SOUNDBOARD",
  "STRINGS · SOUNDBOARD", "STRINGS · SOUNDBOARD",
  "FX · DRIVE", "FX · DRIVE",
  "FX · ROTARY", "FX · ROTARY", "FX · ROTARY", "FX · ROTARY", "FX · ROTARY",
  "FX · MOD", "FX · MOD", "FX · MOD", "FX · MOD",
  "FX · COLOUR", "FX · COLOUR", "FX · COLOUR", "FX · COLOUR", "FX · COLOUR",
  "SPACE · ECHO", "SPACE · ECHO", "SPACE · ECHO", "SPACE · ECHO"
};

static const vibe_card CARDS[] = {
  { "VOICE",       6, "Sixteen registrations for two 8' plucked-string choirs." },
  { "CHOIRS",      6, "Detune, brightness and string damping of the Karplus-Strong paths." },
  { "SOUNDBOARD",  5, "Soundboard resonance, algorithmic reverb and the two 8' stops." },
  { "DRIVE",       4, "Solid / tube distortion, lo-fi crush and ring modulation." },
  { "ROTARY",      6, "Clean-room dual-rotor Leslie: motors, Doppler, drive and mic mix." },
  { "MOD",         6, "Four-stage phaser and two-tap stereo chorus." },
  { "COLOUR",      6, "Flanger, tremolo, auto-pan and the horn / bass mic balance." },
  { "ECHO",        6, "Ping-pong and tape echo: type, mix, time and feedback." }
};

/* ---- pages ----------------------------------------------------------------- */
enum {
  UI_SOUND, UI_STRINGS, UI_STOPS, UI_DRIVE, UI_ROTARY,
  UI_MOD, UI_COLOUR, UI_ECHO, UI_SYSTEM, UI_N
};
static const int PAGE_PARAM[UI_N][4] = {
  { P_REG, P_OCTAVE, P_TRANSPOSE, P_VOLUME },
  { P_DETUNE, P_BRIGHT, P_DAMPING, P_REVERB },
  { P_CHOIR_A, P_CHOIR_B, P_BODY, P_REG },
  { P_SAT_TYPE, P_DRIVE, P_CRUSH_MIX, P_RING_MIX },
  { P_ROTARY_ON, P_ROTARY_SPEED, P_ROTARY_DEPTH, P_ROTARY_DRIVE },
  { P_PHASER_MIX, P_PHASER_RATE, P_CHORUS_MIX, P_CHORUS_RATE },
  { P_FLANGER_MIX, P_TREM_DEPTH, P_PAN_DEPTH, P_ROTARY_BALANCE },
  { P_DELAY_TYPE, P_DELAY_MIX, P_DELAY_MS, P_DELAY_FB },
  { P_VOLUME, P_REVERB, P_OCTAVE, P_TRANSPOSE }
};
static const char *const PAGE_NAME[UI_N] = {
  "SOUND", "STRINGS", "STOPS", "DRIVE", "ROTARY",
  "MOD", "COLOUR", "ECHO", "SYSTEM"
};
static const char *const F_LABELS[6] = { "PAGE", "REG", "DEMO", "PREV", "HELP", "PANIC" };
static const char *const K_LABELS[6] = { "SOUND", "STRINGS", "STOPS", "FX", "HELP", "PANIC" };

static const char *const HELP[] = {
  "KEYS / MIDI", "play the harpsichord (16 voices)",
  "PADS 1-16", "select a registration",
  "F1 / ENC A", "next page: drive, rotary, mod, colour, echo",
  "F2 / ENC B", "next registration",
  "F3 / PLAY", "play or stop the Dido snippet",
  "F4", "previous registration",
  "F5 / HOME / K5", "this overview",
  "F6 / K6 / REC", "panic / all notes off",
  "K1-K4", "SOUND / STRINGS / STOPS / FX",
  "FX (K4)", "press again to step through the FX pages",
  "KNOBS 1-4", "edit the four values shown",
  "REVERB", "algorithmic room on STRINGS / SYSTEM",
  "DEMO", "12 s of Purcell's Dido's Lament; PANIC stops it",
  "DATA + ENTER", "FOCUS: walk, press to edit",
  "RETURN", "close overview, back to SOUND",
  "FOCUS", "hold ENTER; DATA walks values and reg tiles"
};

/* ---- state ----------------------------------------------------------------- */
static float sr = 48000.0f;
static int g_params[P_COUNT];               /* atomic, value x1000 */
static float ui_vals[P_COUNT];
static int cur_reg, applying, ui_page, help, draw_tick;
static char status[128];
static int status_len, status_pending;
static int meter_l, meter_r;                /* atomic, 0..1000 */

static int aget(const volatile int *p) { return __atomic_load_n(p, __ATOMIC_ACQUIRE); }
static void aset(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_RELEASE); }
static int scaled(float v) { return (int)(v * 1000.0f + (v >= 0 ? 0.5f : -0.5f)); }
static float q1000(int v) { return (float)v * 0.001f; }

static void set_param(int id, float v);
static void apply_registration(int r);

/* Control-flow -> audio-flow note queue.  1 = note on, 2 = note off, 3 = panic.
 * Transposition is applied here so a note off matches its note on. */
#define QN 64
static volatile unsigned char q_type[QN], q_note[QN], q_vel[QN];
static volatile unsigned int qw, qr;
static volatile int last_note = -1, last_vel, note_hits;
static int pad_flash[16];

/* DEMO command: 0 = idle, 1 = start, 2 = stop.  The sequencer itself runs in
 * the audio flow so it needs no timers, storage or host calls. */
static volatile int demo_cmd, demo_playing;
static float demo_phase;
static int demo_index;

static void post(int type, int note, int vel)
{
  unsigned int w, r;
  if (type == 1 || type == 2) {
    if (note < 128) {
      note += ((int)ui_vals[P_OCTAVE] - 2) * 12 + (int)ui_vals[P_TRANSPOSE];
      if (note < 0) note = 0;
      if (note > 127) note = 127;
    }
    last_note = note;
    last_vel = type == 1 ? vel : 0;
    if (type == 1) note_hits++;
  }
  VIBE_ACQUIRE();
  r = qr;
  w = qw;
  if (w - r >= QN) return;
  q_note[w % QN] = (unsigned char)(note & 127);
  q_vel[w % QN] = (unsigned char)(vel & 127);
  q_type[w % QN] = (unsigned char)type;
  VIBE_RELEASE();
  qw = w + 1;
}

static void stop_demo(void)
{
  __atomic_store_n(&demo_cmd, 2, __ATOMIC_RELEASE);
}

static void start_demo(void)
{
  __atomic_store_n(&demo_cmd, 1, __ATOMIC_RELEASE);
}

static void toggle_demo(void)
{
  if (__atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE)) stop_demo();
  else start_demo();
}

static void panic(void)
{
  int i;
  for (i = 0; i < 16; i++) pad_flash[i] = 0;
  stop_demo();
  post(3, 0, 0);
}

static void store_state(void)
{
  int32_t buf[P_COUNT], i;
  for (i = 0; i < P_COUNT; i++) buf[i] = (int32_t)aget(&g_params[i]);
  vibe_store_write("bar-rock-state", buf, (int)sizeof buf);
}

static void load_state(void)
{
  int32_t buf[P_COUNT];
  int i, k;
  float v;
  int n = vibe_store_read("bar-rock-state", buf, (int)sizeof buf);
  if (n != (int)sizeof buf) return;
  for (i = 0; i < P_COUNT; i++) {
    v = (float)buf[i] * 0.001f;
    if (v < PARAMS[i].min || v > PARAMS[i].max) return;
  }
  for (i = 0; i < P_COUNT; i++) {
    v = (float)buf[i] * 0.001f;
    if (PARAMS[i].steps > 1) {
      k = (int)(v + 0.5f);
      if (k < 0) k = 0;
      if (k >= PARAMS[i].steps) k = PARAMS[i].steps - 1;
      v = (float)k;
    }
    ui_vals[i] = v;
    aset(&g_params[i], scaled(v));
    vibe_param_set(i, v);
  }
  cur_reg = (int)(ui_vals[P_REG] + 0.5f);
}

static void set_param(int id, float v)
{
  if (id < 0 || id >= P_COUNT) return;
  v = vd_clamp(v, PARAMS[id].min, PARAMS[id].max);
  if (PARAMS[id].steps > 1) {
    int k = (int)(v + 0.5f);
    if (k < 0) k = 0;
    if (k >= PARAMS[id].steps) k = PARAMS[id].steps - 1;
    v = (float)k;
  }
  ui_vals[id] = v;
  aset(&g_params[id], scaled(v));
  vibe_param_set(id, v);
  if (id == P_REG && !applying) apply_registration((int)(v + 0.5f));
}

static void reset_stops(void)
{
  int i;
  set_param(P_VOLUME, PARAMS[P_VOLUME].def);
  set_param(P_DETUNE, PARAMS[P_DETUNE].def);
  set_param(P_BRIGHT, PARAMS[P_BRIGHT].def);
  set_param(P_DAMPING, PARAMS[P_DAMPING].def);
  set_param(P_BODY, PARAMS[P_BODY].def);
  set_param(P_REVERB, PARAMS[P_REVERB].def);
  set_param(P_CHOIR_A, PARAMS[P_CHOIR_A].def);
  set_param(P_CHOIR_B, PARAMS[P_CHOIR_B].def);
  for (i = P_SAT_TYPE; i < P_COUNT; i++)
    set_param(i, PARAMS[i].def);
}

static void apply_registration(int r)
{
  const br_registration *reg;
  if (r < 0) r = 0;
  if (r >= BR_REG_COUNT) r = BR_REG_COUNT - 1;
  applying = 1;
  cur_reg = r;
  set_param(P_REG, (float)r);
  reset_stops();
  reg = &BR_REGISTRATIONS[r];
  set_param(P_VOLUME, reg->volume);
  set_param(P_DETUNE, reg->detune);
  set_param(P_BRIGHT, reg->bright);
  set_param(P_DAMPING, reg->damping);
  set_param(P_BODY, reg->body);
  set_param(P_REVERB, reg->reverb);
  set_param(P_CHOIR_A, (float)reg->choir_a);
  set_param(P_CHOIR_B, (float)reg->choir_b);
  set_param(P_SAT_TYPE, reg->sat_type);
  set_param(P_DRIVE, reg->drive);
  set_param(P_PHASER_MIX, reg->phaser_mix);
  set_param(P_CHORUS_MIX, reg->chorus_mix);
  set_param(P_FLANGER_MIX, reg->flanger_mix);
  set_param(P_CRUSH_MIX, reg->crush_mix);
  set_param(P_ROTARY_SPEED, (float)reg->rotary_speed);
  set_param(P_ROTARY_ON, reg->rotary_speed > 0 ? 1.0f : 0.0f);
  applying = 0;
}

static void next_registration(int d)
{
  int r = cur_reg + d;
  while (r < 0) r += BR_REG_COUNT;
  while (r >= BR_REG_COUNT) r -= BR_REG_COUNT;
  apply_registration(r);
}

static void nudge(int id, float d)
{
  float v;
  if (id < 0 || id >= P_COUNT) return;
  if (PARAMS[id].steps > 1) {
    v = ui_vals[id] + (d > 0 ? 1.0f : -1.0f);
  } else {
    v = ui_vals[id] + d * (PARAMS[id].max - PARAMS[id].min) / 50.0f;
  }
  set_param(id, v);
}

static int append_txt(char *dst, int at, int cap, const char *s)
{
  while (*s && at < cap - 1) dst[at++] = *s++;
  dst[at] = 0;
  return at;
}

static void build_status(void)
{
  char t[24];
  int n = 0;
  status[0] = 0;
  n = append_txt(status, n, (int)sizeof status, BR_REGISTRATIONS[cur_reg].name);
  n = append_txt(status, n, (int)sizeof status, " | voices ");
  n = append_txt(status, n, (int)sizeof status, vg_itoa(br_harpsi_voice_count(), t));
  n = append_txt(status, n, (int)sizeof status, " | stops ");
  n = append_txt(status, n, (int)sizeof status,
                 aget(&g_params[P_CHOIR_A]) ? (aget(&g_params[P_CHOIR_B]) ? "8+8" : "A") : "B");
  n = append_txt(status, n, (int)sizeof status, " | rev ");
  n = append_txt(status, n, (int)sizeof status,
                 vg_itoa((int)(q1000(aget(&g_params[P_REVERB])) * 100.0f + 0.5f), t));
  n = append_txt(status, n, (int)sizeof status, "%");
  {
    int sat = (int)(ui_vals[P_SAT_TYPE] + 0.5f);
    if (sat > 0 && sat < 3) {
      n = append_txt(status, n, (int)sizeof status, " | ");
      n = append_txt(status, n, (int)sizeof status, SATS[sat]);
      n = append_txt(status, n, (int)sizeof status, " ");
      n = append_txt(status, n, (int)sizeof status,
                     vg_itoa((int)(ui_vals[P_DRIVE] * 100.0f + 0.5f), t));
      n = append_txt(status, n, (int)sizeof status, "%");
    }
    if (aget(&g_params[P_ROTARY_ON]) > 500) {
      int sp = (int)(ui_vals[P_ROTARY_SPEED] + 0.5f);
      int hz = br_fx_rotor_hz100();
      n = append_txt(status, n, (int)sizeof status, " | LES ");
      if (sp >= 0 && sp < 3) n = append_txt(status, n, (int)sizeof status, ROTOR_SPEEDS[sp]);
      if (hz > 0) {
        n = append_txt(status, n, (int)sizeof status, " ");
        n = append_txt(status, n, (int)sizeof status, vg_itoa((hz + 50) / 100, t));
        n = append_txt(status, n, (int)sizeof status, "Hz");
      }
    }
  }
  n = append_txt(status, n, (int)sizeof status, " | note ");
  n = append_txt(status, n, (int)sizeof status, last_note >= 0 ? vg_itoa(last_note, t) : "--");
  n = append_txt(status, n, (int)sizeof status, "/");
  n = append_txt(status, n, (int)sizeof status, vg_itoa(last_vel, t));
  n = append_txt(status, n, (int)sizeof status, " (");
  n = append_txt(status, n, (int)sizeof status, vg_itoa(note_hits, t));
  n = append_txt(status, n, (int)sizeof status, ")");
  if (__atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE))
    n = append_txt(status, n, (int)sizeof status, " | DEMO");
  status_len = n;
  status_pending = 1;
}

/* ---- editor byte bridge ----------------------------------------------------- */
static void handle_editor(const uint8_t *b, int n)
{
  if (!b || n < 1) return;
  if (b[0] == 0x20) status_pending = 1;
  else if (b[0] == 0x21) store_state();
  else if (b[0] == 0x22) { apply_registration(0); store_state(); }
  else if (b[0] == 0x23) panic();
  else if (b[0] == 0x24) next_registration(1);
  else if (b[0] == 0x25) next_registration(-1);
  else if (b[0] == 0x26) toggle_demo();
  else if (b[0] == 0x27) stop_demo();
  else if (b[0] == 0x2A) set_param(P_ROTARY_ON, ui_vals[P_ROTARY_ON] > 0.5f ? 0.0f : 1.0f);
  else if (b[0] == 0x2B) set_param(P_SAT_TYPE, ui_vals[P_SAT_TYPE] + 1.0f > 2.0f ? 0.0f : ui_vals[P_SAT_TYPE] + 1.0f);
  else if (b[0] == 0x28 && n >= 2) post(1, b[1], 108);
  else if (b[0] == 0x29 && n >= 2) post(2, b[1], 0);
  else if (b[0] >= 0x30 && b[0] < 0x30 + BR_REG_COUNT)
    apply_registration(b[0] - 0x30);
}

/* ---- controls --------------------------------------------------------------- */
static void handle_button(int id)
{
  switch (id) {
  case VB_F1: ui_page = (ui_page + 1) % UI_N; break;
  case VB_F2: next_registration(1); break;
  case VB_F3: case VB_PLAY: toggle_demo(); break;
  case VB_F4: next_registration(-1); break;
  case VB_F5: case VB_K5: case VB_HOME: help = !help; break;
  case VB_F6: case VB_K6: case VB_REC: case VB_STOP: panic(); break;
  case VB_K1: ui_page = UI_SOUND; break;
  case VB_K2: ui_page = UI_STRINGS; break;
  case VB_K3: ui_page = UI_STOPS; break;
  case VB_K4:
    ui_page = (ui_page >= UI_DRIVE && ui_page < UI_ECHO)
              ? ui_page + 1 : UI_DRIVE;
    break;
  case VB_RETURN: help = 0; ui_page = UI_SOUND; break;
  default: break;
  }
}

void vibe_on_event(const vibe_event *e)
{
  int fid = -1, r, p;
  if (!e) return;
  r = vf_event(e, &fid);
  if (r == VF_PRESS) {
    if (fid == 900) toggle_demo();
    else if (fid == 901) panic();
    else if (fid >= 910 && fid < 910 + BR_REG_COUNT) apply_registration(fid - 910);
    return;
  }
  if (r == VF_CHANGED) {
    if (fid >= 0 && fid < P_COUNT) set_param(fid, ui_vals[fid]);
    return;
  }
  if (r == VF_MOVED) return;

  if (e->type == VIBE_EV_NOTE) {
    post(e->down ? 1 : 2, (int)e->id, (int)(e->value * 127.0f + 0.5f));
    return;
  }
  if (e->type == VIBE_EV_MIDI) {
    /* SDK 8 makes MIDI notes into VIBE_EV_NOTE; accept raw notes as well. */
    int st = e->midi[0] & 0xF0;
    if (st == 0x90 && e->midi[2] > 0)
      post(1, e->midi[1] & 127, e->midi[2]);
    else if (st == 0x80 || st == 0x90)
      post(2, e->midi[1] & 127, 0);
    return;
  }
  if (e->type == VIBE_EV_KEY) {
    int note = 48 + (int)e->id;
    post(e->down ? 1 : 2, note, e->down ? (int)(e->value * 127.0f + 0.5f) : 0);
    return;
  }
  if (e->type == VIBE_EV_PAD) {
    p = (int)e->id;
    if (p < 0 || p >= 16) return;
    if (e->down) pad_flash[p] = 10;
    if (p < BR_REG_COUNT && e->down) apply_registration(p);
    return;
  }
  if (e->type == VIBE_EV_PARAM) {
    int i = (int)e->id;
    if (i >= 0 && i < P_COUNT) {
      ui_vals[i] = e->value;
      aset(&g_params[i], scaled(e->value));
      if (i == P_REG && !applying) apply_registration((int)(e->value + 0.5f));
    }
    return;
  }
  if (e->type == VIBE_EV_EDITOR_DATA) { handle_editor(vibe_editor_data(), (int)e->value); return; }
  if (e->type == VIBE_EV_SAVE) { store_state(); return; }
  if (e->type == VIBE_EV_BUTTON) { if (e->down) handle_button((int)e->id); return; }
  if (e->type == VIBE_EV_ENCODER) {
    float d = e->value;
    if (d == 0.0f) return;
    if (e->id == VE_A) { ui_page = (ui_page + (d > 0 ? 1 : UI_N - 1)) % UI_N; return; }
    if (e->id == VE_B) { next_registration(d > 0 ? 1 : -1); return; }
    if (e->id >= VE_1 && e->id <= VE_4) { nudge(PAGE_PARAM[ui_page][e->id - VE_1], d); return; }
  }
}

/* ---- audio ------------------------------------------------------------------ */
static br_harpsi_ctrl hc;
static br_fx_ctrl fc;

static void fill_controls(void)
{
  hc.detune = q1000(aget(&g_params[P_DETUNE]));
  hc.brightness = q1000(aget(&g_params[P_BRIGHT]));
  hc.damping = q1000(aget(&g_params[P_DAMPING]));
  hc.body = q1000(aget(&g_params[P_BODY]));
  hc.reverb = q1000(aget(&g_params[P_REVERB]));
  hc.volume = q1000(aget(&g_params[P_VOLUME]));
  hc.choir_a = aget(&g_params[P_CHOIR_A]) > 500 ? 1.0f : 0.0f;
  hc.choir_b = aget(&g_params[P_CHOIR_B]) > 500 ? 1.0f : 0.0f;
}

static void fill_fx(void)
{
  fc.sat_type = q1000(aget(&g_params[P_SAT_TYPE]));
  fc.drive = q1000(aget(&g_params[P_DRIVE]));
  fc.rotary_on = aget(&g_params[P_ROTARY_ON]) > 500 ? 1.0f : 0.0f;
  fc.rotary_speed = q1000(aget(&g_params[P_ROTARY_SPEED]));
  fc.rotary_depth = q1000(aget(&g_params[P_ROTARY_DEPTH]));
  fc.rotary_drive = q1000(aget(&g_params[P_ROTARY_DRIVE]));
  fc.rotary_balance = q1000(aget(&g_params[P_ROTARY_BALANCE]));
  fc.phaser_mix = q1000(aget(&g_params[P_PHASER_MIX]));
  fc.phaser_rate = q1000(aget(&g_params[P_PHASER_RATE]));
  fc.chorus_mix = q1000(aget(&g_params[P_CHORUS_MIX]));
  fc.chorus_rate = q1000(aget(&g_params[P_CHORUS_RATE]));
  fc.flanger_mix = q1000(aget(&g_params[P_FLANGER_MIX]));
  fc.trem_depth = q1000(aget(&g_params[P_TREM_DEPTH]));
  fc.pan_depth = q1000(aget(&g_params[P_PAN_DEPTH]));
  fc.ring_mix = q1000(aget(&g_params[P_RING_MIX]));
  fc.crush_mix = q1000(aget(&g_params[P_CRUSH_MIX]));
  fc.delay_type = q1000(aget(&g_params[P_DELAY_TYPE]));
  fc.delay_mix = q1000(aget(&g_params[P_DELAY_MIX]));
  fc.delay_ms = q1000(aget(&g_params[P_DELAY_MS]));
  fc.delay_fb = q1000(aget(&g_params[P_DELAY_FB]));
}

void vibe_on_audio(const float *in, float *out_l, float *out_r, int frames)
{
  int i, total = frames, n;
  unsigned int t, end;
  int type, nte, vel, cmd, playing;
  float peak_l = 0.0f, peak_r = 0.0f;

  (void)in;
  if (total <= 0) return;
  n = total > VIBE_MAX_BLOCK ? VIBE_MAX_BLOCK : total;

  /* The demo command is the only cross-flow state; the note list itself is
   * read-only and indexed in the audio flow. */
  cmd = __atomic_exchange_n(&demo_cmd, 0, __ATOMIC_ACQ_REL);
  if (cmd == 1 || cmd == 2) {
    demo_index = 0;
    demo_phase = 0.0f;
    __atomic_store_n(&demo_playing, cmd == 1 ? 1 : 0, __ATOMIC_RELEASE);
    br_harpsi_all_off();
  }
  playing = __atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE);

  fill_controls();
  fill_fx();
  if (playing) {
    /* The excerpt always uses both 8' stops, whatever the player's current
     * registration, without changing the published stop parameters. */
    hc.choir_a = 1.0f;
    hc.choir_b = 1.0f;
  }
  br_harpsi_set_ctrl(&hc);

  t = qr;
  end = qw;
  VIBE_ACQUIRE();
  while (t != end) {
    type = q_type[t % QN];
    nte = q_note[t % QN];
    vel = q_vel[t % QN];
    if (type == 1) br_harpsi_note_on(nte, vel);
    else if (type == 2) br_harpsi_note_off(nte);
    else if (type == 3) br_harpsi_all_off();
    t++;
  }
  qr = t;
  VIBE_RELEASE();

  if (playing) {
    float due = demo_phase * 1000.0f;
    while (demo_index < BR_DEMO_NOTE_COUNT &&
           (float)BR_DEMO_NOTES[demo_index].t_ms <= due) {
      const br_demo_note *dn = &BR_DEMO_NOTES[demo_index++];
      if (dn->vel) br_harpsi_note_on(dn->note, dn->vel);
      else br_harpsi_note_off(dn->note);
    }
    demo_phase += (float)n / sr;
    if (demo_index >= BR_DEMO_NOTE_COUNT) {
      demo_index = 0;
      demo_phase = 0.0f;
      __atomic_store_n(&demo_playing, 0, __ATOMIC_RELEASE);
    }
  }

  br_harpsi_render(out_l, out_r, n, &hc);
  br_fx_process(out_l, out_r, n, &fc);

  for (i = 0; i < n; i++) {
    float al = vd_absf(out_l[i]), ar = vd_absf(out_r[i]);
    peak_l = al > peak_l ? al : peak_l;
    peak_r = ar > peak_r ? ar : peak_r;
  }
  __atomic_store_n(&meter_l, scaled(peak_l), __ATOMIC_RELEASE);
  __atomic_store_n(&meter_r, scaled(peak_r), __ATOMIC_RELEASE);

  for (i = n; i < total; i++) { out_l[i] = 0.0f; out_r[i] = 0.0f; }
}

/* ---- display ---------------------------------------------------------------- */
#define C_BG      0x100C14
#define C_PANEL   0x1D1428
#define C_PANEL2  0x2A1B39
#define C_ACC     0xFF9F1C
#define C_GOLD    0xF2C14E
#define C_CREAM   0xFFF0D0
#define C_AVO     0x8FA44B
#define C_RED     0xE04A4A
#define C_BLUE    0x46B6FF
#define C_VIOLET  0xB987FF
#define C_MUTED   0xA08DB5

/* One small motif: a harpsichord against a gold 8' disc. */
static void draw_harpsi(float x, float y, float t)
{
  int i;
  vg_circle(x, y - 3, 15.0f, C_GOLD);
  vg_rect(x - 24, y - 5, 48, 2, C_BG);
  vg_rect(x - 24, y + 2, 48, 2, C_BG);
  vg_round(x - 22, y + 9, 44, 12, 3, C_VIOLET);
  for (i = 0; i < 9; i++)
    vg_rect(x - 19 + i * 4, y + 11, 3, 8, C_CREAM);
  vg_line(x - 24, y + 24, x + 24, y + 24, 1.0f, C_BLUE);
  vg_line(x - 24 + ((int)(t * 8.0f) % 8), y + 27,
          x - 18 + ((int)(t * 8.0f) % 8), y + 27, 1.0f, C_ACC);
}

static int param_decimals(int id)
{
  float span = PARAMS[id].max - PARAMS[id].min;
  if (span <= 3.0f) return 2;
  if (span <= 100.0f) return 1;
  return 0;
}

void vibe_on_draw(uint16_t *fb)
{
  int i, id, playing;
  char val[24], val2[24];
  float t, ml, mr;
  vg_begin(fb);

  if (help) {
    vf_begin();
    vf_enable(0);
    vf_end(C_ACC);
    vg_controls("BAR-ROCK CONTROLS", HELP, (int)(sizeof HELP / sizeof HELP[0]) / 2);
    draw_tick++;
    if (status_pending && vibe_editor_post((const uint8_t *)status, status_len) >= 0)
      status_pending = 0;
    return;
  }

  vf_enable(1);

  vg_clear(C_BG);
  vg_rect(0, 0, 480, 44, C_PANEL);
  vg_rect(0, 43, 480, 2, C_ACC);
  draw_harpsi(452, 22, (float)draw_tick * 0.05f);
  vg_text("BAR-ROCK", 14, 7, VG_BIG, C_GOLD, VG_LEFT);
  vg_text(BR_REGISTRATIONS[cur_reg].name, 16, 29, VG_SMALL, C_CREAM, VG_LEFT);
  vg_text("NOTE", 180, 29, VG_SMALL, C_MUTED, VG_LEFT);
  vg_text(last_note >= 0 ? vg_itoa(last_note, val2) : "--", 220, 29,
          VG_SMALL, last_note >= 0 ? C_GOLD : C_MUTED, VG_LEFT);
  vg_text(PAGE_NAME[ui_page], 400, 13, VG_BOLD, C_ACC, VG_RIGHT);
  playing = __atomic_load_n(&demo_playing, __ATOMIC_ACQUIRE);
  if (aget(&g_params[P_ROTARY_ON]) > 500) {
    int sp = (int)(ui_vals[P_ROTARY_SPEED] + 0.5f);
    vg_text(sp == 2 ? "LES FAST" : "LES SLOW", 252, 29, VG_SMALL, C_VIOLET, VG_LEFT);
  } else if (aget(&g_params[P_SAT_TYPE]) > 0) {
    vg_text("DRIVE", 252, 29, VG_SMALL, C_RED, VG_LEFT);
  }
  if (playing) vg_text("DEMO", 330, 29, VG_SMALL, C_ACC, VG_LEFT);

  vf_begin();
  for (i = 0; i < 4; i++) {
    int y = 58 + i * 47;
    float step;
    id = PAGE_PARAM[ui_page][i];
    vg_round(10, y, 460, 43, 5, i & 1 ? C_PANEL : C_PANEL2);
    vg_text(PARAMS[id].name, 22, y + 3, VG_SMALL, C_MUTED, VG_LEFT);
    if (PARAMS[id].steps > 1) {
      int k = (int)(ui_vals[id] + 0.5f);
      if (k < 0) k = 0;
      if (k >= PARAMS[id].steps) k = PARAMS[id].steps - 1;
      vg_text(PARAMS[id].choices[k], 458, y + 3, VG_MED, C_CREAM, VG_RIGHT);
    } else {
      vg_text(vg_ftoa(ui_vals[id], param_decimals(id), val), 458, y + 3,
              VG_BIG, C_CREAM, VG_RIGHT);
    }
    t = (ui_vals[id] - PARAMS[id].min) / (PARAMS[id].max - PARAMS[id].min);
    t = vd_clamp(t, 0.0f, 1.0f);
    vg_rect(22, y + 32, 420, 5, C_BG);
    vg_rect(22, y + 32, (int)(420.0f * t), 5, C_ACC);
    step = PARAMS[id].steps > 1 ? 1.0f : (PARAMS[id].max - PARAMS[id].min) / 100.0f;
    vf_value(id, 10, y, 460, 43, &ui_vals[id], PARAMS[id].min, PARAMS[id].max, step);
  }

  /* Registration strip: two rows of eight, pads 1-16 / FOCUS ids 910-925. */
  for (i = 0; i < BR_REG_COUNT; i++) {
    int x = 14 + (i % 8) * 57;
    int y = 246 + (i / 8) * 11;
    vg_round(x, y, 52, 10, 2,
             i == cur_reg ? C_ACC : (pad_flash[i] > 0 ? C_VIOLET : C_PANEL2));
    vg_text(vg_itoa(i + 1, val), x + 26, y + 1, VG_SMALL,
            i == cur_reg ? C_BG : C_CREAM, VG_CENTER);
    vf_button(910 + i, x, y, 52, 10);
  }
  vg_text("REG 1-16 / PADS 1-16", 470, 246, VG_SMALL, C_MUTED, VG_RIGHT);

  /* Meters, voices, shortcuts. */
  ml = q1000(aget(&meter_l)); mr = q1000(aget(&meter_r));
  ml = vd_clamp(ml, 0.0f, 1.0f); mr = vd_clamp(mr, 0.0f, 1.0f);
  vg_text("L", 14, 270, VG_SMALL, C_MUTED, VG_LEFT);
  vg_rect(28, 272, 130, 5, C_PANEL2);
  vg_rect(28, 272, (int)(130.0f * ml), 5, ml > 0.85f ? C_RED : C_AVO);
  vg_text("R", 170, 270, VG_SMALL, C_MUTED, VG_LEFT);
  vg_rect(184, 272, 130, 5, C_PANEL2);
  vg_rect(184, 272, (int)(130.0f * mr), 5, mr > 0.85f ? C_RED : C_AVO);
  vg_text("VOICES", 330, 270, VG_SMALL, C_MUTED, VG_LEFT);
  vg_text(vg_itoa(br_harpsi_voice_count(), val), 402, 269, VG_BOLD, C_GOLD, VG_RIGHT);
  vg_round(414, 268, 54, 10, 3, playing ? C_ACC : C_PANEL2);
  vg_text(playing ? "STOP" : "DEMO", 441, 269, VG_SMALL,
          playing ? C_BG : C_CREAM, VG_CENTER);
  vg_round(414, 279, 54, 10, 3, C_PANEL2);
  vg_text("PANIC", 441, 280, VG_SMALL, C_CREAM, VG_CENTER);
  vf_button(900, 414, 268, 54, 10);
  vf_button(901, 414, 279, 54, 10);

  vg_rect(0, 289, 480, 2, C_ACC);
  vg_rect(0, 291, 480, 29, C_PANEL);
  for (i = 0; i < 6; i++) {
    const char *lab = (i == 2 && playing) ? "STOP" : F_LABELS[i];
    vg_text(lab, 8 + i * 79, 292, VG_SMALL,
            i == 1 ? C_GOLD : C_MUTED, VG_LEFT);
    vf_key(VB_F1 + i, 4 + i * 79, 290, 76, 16);
  }
  for (i = 0; i < 6; i++) {
    vg_text(K_LABELS[i], 8 + i * 79, 306, VG_SMALL,
            i < 4 ? C_AVO : C_MUTED, VG_LEFT);
    vf_key(VB_K1 + i, 4 + i * 79, 304, 76, 16);
  }
  vf_end(C_ACC);

  for (i = 0; i < 16; i++) {
    uint32_t c = C_PANEL2;
    float lvl = 0.0f;
    if (pad_flash[i] > 0) {
      c = i < BR_REG_COUNT ? C_ACC : C_AVO;
      lvl = (float)pad_flash[i] / 10.0f;
      pad_flash[i]--;
    } else if (i == cur_reg) {
      c = C_ACC; lvl = 0.35f;
    }
    vibe_led_pad(i, c, lvl);
  }

  for (i = 0; i < 6; i++)
    vibe_label(VB_F1 + i, (i == 2 && playing) ? "STOP" : F_LABELS[i]);
  for (i = 0; i < 6; i++) vibe_label(VB_K1 + i, K_LABELS[i]);
  vibe_link_page(PAGE_NAME[ui_page]);
  for (i = 0; i < 4; i++) {
    int pid = PAGE_PARAM[ui_page][i];
    float v = (ui_vals[pid] - PARAMS[pid].min) /
              (PARAMS[pid].max - PARAMS[pid].min);
    vg_ftoa(vd_clamp(v, 0.0f, 1.0f), 2, val);
    vibe_link_encoder(VE_1 + i, PARAMS[pid].name, val);
  }

  draw_tick++;
  if ((draw_tick % 8) == 0) build_status();
  if (status_pending && vibe_editor_post((const uint8_t *)status, status_len) >= 0)
    status_pending = 0;
}

/* ---- init ------------------------------------------------------------------- */
void vibe_on_init(float sample_rate)
{
  int i;
  sr = sample_rate > 1000.0f ? sample_rate : 48000.0f;
  br_harpsi_init(sr);
  br_fx_init(sr);
  for (i = 0; i < BR_REG_COUNT; i++) reg_names[i] = BR_REGISTRATIONS[i].name;

  for (i = 0; i < P_COUNT; i++) {
    ui_vals[i] = PARAMS[i].def;
    aset(&g_params[i], scaled(PARAMS[i].def));
  }
  vibe_params(PARAMS, P_COUNT);
  vibe_param_groups(GROUPS);
  vibe_editor_cards(CARDS, (int)(sizeof CARDS / sizeof CARDS[0]));
  vibe_editor_accent(C_ACC);
  vibe_editor_tab_color("SOUND", C_ACC);
  vibe_editor_tab_color("STRINGS", C_GOLD);
  vibe_editor_tab_color("FX", C_VIOLET);
  vibe_editor_tab_color("SPACE", C_BLUE);

  vibe_editor_control(P_REG, VIBE_CTL_MENU);
  vibe_editor_control(P_OCTAVE, VIBE_CTL_MENU);
  vibe_editor_control(P_CHOIR_A, VIBE_CTL_TOGGLE);
  vibe_editor_control(P_CHOIR_B, VIBE_CTL_TOGGLE);
  vibe_editor_control(P_TRANSPOSE, VIBE_CTL_BIPOLAR);
  vibe_editor_control(P_SAT_TYPE, VIBE_CTL_MENU);
  vibe_editor_control(P_ROTARY_ON, VIBE_CTL_TOGGLE);
  vibe_editor_control(P_ROTARY_SPEED, VIBE_CTL_MENU);
  vibe_editor_control(P_DELAY_TYPE, VIBE_CTL_MENU);

  vibe_param_help(P_REG, "Sixteen stop combinations for the two 8' choirs; pads 1-16 and F2/F4 select them.");
  vibe_param_help(P_DETUNE, "Cents between choir A and choir B. A little detune gives the harpsichord its shimmer.");
  vibe_param_help(P_BRIGHT, "Karplus-Strong lowpass blend: higher is brighter and pluckier.");
  vibe_param_help(P_DAMPING, "String damping. Lower is a faster natural decay.");
  vibe_param_help(P_BODY, "Level of the two-pole soundboard resonance.");
  vibe_param_help(P_REVERB, "Fixed-memory Schroeder stereo reverb: four damped combs and two allpasses per channel, no samples.");
  vibe_param_help(P_CHOIR_A, "The first 8' string choir. Pads 1-16 select registrations; DEMO plays both.");
  vibe_param_help(P_CHOIR_B, "The second 8' string choir, slightly detuned from the first.");
  vibe_param_help(P_SAT_TYPE, "Off, solid-state or asymmetric tube saturation before the colour rack.");
  vibe_param_help(P_DRIVE, "Input gain into the distortion; tube mode also adds a little asymmetry.");
  vibe_param_help(P_ROTARY_ON, "Clean-room dual-rotor Leslie: separate horn/bass motors and a stereo mic mix.");
  vibe_param_help(P_ROTARY_SPEED, "Stop, slow or fast rotor. Speed changes ramp instead of jumping.");
  vibe_param_help(P_ROTARY_DEPTH, "Geometric Doppler depth of the two spinning rotors.");
  vibe_param_help(P_ROTARY_DRIVE, "Softens the split horn signal before the rotor baffle taps.");
  vibe_param_help(P_ROTARY_BALANCE, "Mixes the horn and bass rotor microphones; 0.5 is even.");
  vibe_param_help(P_PHASER_MIX, "Four-stage phase sweep; always smoothed to avoid clicks.");
  vibe_param_help(P_PHASER_RATE, "Speed of the shared phaser LFO in Hz.");
  vibe_param_help(P_CHORUS_MIX, "Two slowly modulated stereo delay taps for ensemble shimmer.");
  vibe_param_help(P_CHORUS_RATE, "Speed of the two tap LFOs in Hz.");
  vibe_param_help(P_FLANGER_MIX, "Short swept comb with gentle regeneration.");
  vibe_param_help(P_TREM_DEPTH, "Sine tremolo on the two output channels.");
  vibe_param_help(P_PAN_DEPTH, "Sine auto-pan across the stereo field.");
  vibe_param_help(P_RING_MIX, "Sine ring modulator at 110 Hz; creates metallic sidebands.");
  vibe_param_help(P_CRUSH_MIX, "Fixed 10-bit sample-and-hold lo-fi colour, borrowed from bar-fly.");
  vibe_param_help(P_DELAY_TYPE, "Off, ping-pong cross-fed echo or damped tape echo with flutter.");
  vibe_param_help(P_DELAY_MIX, "Wet level of the stereo echo stage.");
  vibe_param_help(P_DELAY_MS, "Echo tap time, 80-240 ms; clamped to the fixed buffer at any sample rate.");
  vibe_param_help(P_DELAY_FB, "Echo regeneration; the tape mode damps each repeat.");

  apply_registration(0);
  load_state();

  vibe_notes(VIBE_NOTES);
  vibe_keys_octave(2);
  vf_mode(VF_ON_HOLD);
  vf_clicks(1);
  vf_set_focus(P_VOLUME);

  qw = qr = 0;
  demo_index = 0;
  demo_phase = 0.0f;
  __atomic_store_n(&demo_cmd, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&demo_playing, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&meter_l, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&meter_r, 0, __ATOMIC_RELEASE);
  ui_page = UI_SOUND;
  help = 0;
  draw_tick = 0;
  build_status();
}
