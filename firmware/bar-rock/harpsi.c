/* BAR-ROCK -- see harpsi.h.  The DSP is the compact polyphonic harpsichord
 * algorithm (Karplus-Strong plucked strings + soundboard one-poles), ported to
 * the VIBE SDK: vd_noise / vd_sin / vd_mtof / vd_exp2 replace rand / sinf /
 * powf, the host sample rate replaces the hard-coded 48000, and all per-sample
 * state lives in fixed arrays.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "harpsi.h"
#include "vibe_dsp.h"

/* Fixed-memory Schroeder reverb: four damped combs and two allpasses per
 * channel.  No impulse responses, no heap, no host calls in the audio path. */
#define BR_RV_COMBS     4
#define BR_RV_MAX_COMB  4600   /* 0.047 s at 96 kHz */
#define BR_RV_MAX_AP    700    /* 0.007 s at 96 kHz */

typedef struct {
  float buf[BR_RV_MAX_COMB];
  float lp;
  int idx, size;
} br_comb;

typedef struct {
  float buf[BR_RV_MAX_AP];
  int idx, size;
} br_allpass;

typedef struct {
  br_comb comb[BR_RV_COMBS];
  br_allpass ap[2];
} br_reverb;

static const float COMB_TIME[BR_RV_COMBS] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };
static const float AP_TIME[2] = { 0.0050f, 0.0017f };

static float br_sr = 48000.0f;
static float natural_decay = 0.99996f;
static float damped_decay = 0.99712f;
static float panic_decay = 0.98563f;

static br_voice voices[BR_HARPSI_VOICES];
static br_reverb rev[2];
static unsigned int age_counter;
static br_harpsi_ctrl last_ctrl = { 6.0f, 0.82f, 0.85f, 0.60f, 0.28f, 1.00f, 1.0f, 1.0f };
static float body1, body2;
static volatile int active_count;

/* A decay that reaches -60 dB in `seconds`, independent of the host rate. */
static float decay_for(float seconds)
{
  float d = 1.0f - 6.9f / (br_sr * seconds);
  return d < 0.5f ? 0.5f : (d > 0.99999f ? 0.99999f : d);
}

static int delay_size(float seconds, int max)
{
  int n = (int)(br_sr * seconds);
  if (n < 4) n = 4;
  if (n > max - 1) n = max - 1;
  return n;
}

static void reverb_init_channel(br_reverb *r)
{
  int i, j;
  for (i = 0; i < BR_RV_COMBS; i++) {
    for (j = 0; j < BR_RV_MAX_COMB; j++) r->comb[i].buf[j] = 0.0f;
    r->comb[i].lp = 0.0f;
    r->comb[i].idx = 0;
    r->comb[i].size = delay_size(COMB_TIME[i], BR_RV_MAX_COMB);
  }
  for (i = 0; i < 2; i++) {
    for (j = 0; j < BR_RV_MAX_AP; j++) r->ap[i].buf[j] = 0.0f;
    r->ap[i].idx = 0;
    r->ap[i].size = delay_size(AP_TIME[i], BR_RV_MAX_AP);
  }
}

static inline float comb_tick(br_comb *c, float x)
{
  float y = c->buf[c->idx];
  float next;
  if (vd_absf(y) < 1e-7f) y = 0.0f;
  c->lp = y * 0.42f + c->lp * 0.58f;
  next = x + 0.78f * c->lp;
  if (vd_absf(next) < 1e-7f) next = 0.0f;
  c->buf[c->idx] = next;
  if (++c->idx >= c->size) c->idx = 0;
  return y;
}

static inline float allpass_tick(br_allpass *a, float x)
{
  float y = a->buf[a->idx];
  float next = x + 0.5f * y;
  if (vd_absf(next) < 1e-7f) next = 0.0f;
  a->buf[a->idx] = next;
  if (++a->idx >= a->size) a->idx = 0;
  return y - 0.5f * x;
}

static inline float reverb_tick(br_reverb *r, float x)
{
  float sum = 0.0f;
  int i;
  for (i = 0; i < BR_RV_COMBS; i++)
    sum += comb_tick(&r->comb[i], x) * 0.11f;
  sum = allpass_tick(&r->ap[0], sum);
  sum = allpass_tick(&r->ap[1], sum);
  return sum;
}

void br_harpsi_init(float sr)
{
  int i;
  br_sr = sr > 1000.0f ? sr : 48000.0f;
  natural_decay = decay_for(3.6f);
  damped_decay = decay_for(0.05f);
  panic_decay = decay_for(0.01f);
  for (i = 0; i < BR_HARPSI_VOICES; i++) {
    voices[i].active = 0;
    voices[i].level = 0.0f;
    voices[i].age = 0;
  }
  age_counter = 0;
  body1 = 0.0f;
  body2 = 0.0f;
  reverb_init_channel(&rev[0]);
  reverb_init_channel(&rev[1]);
  active_count = 0;
}

void br_harpsi_set_ctrl(const br_harpsi_ctrl *c)
{
  if (c) last_ctrl = *c;
}

static void ks_init(br_string *ks, float freq, float brightness, float damping)
{
  vd_noise rng;
  int i, size = (int)(br_sr / freq);

  rng.s = vd_rand();
  if (size < 4) size = 4;
  if (size > BR_KS_MAX_DELAY) size = BR_KS_MAX_DELAY;
  ks->size = size;
  ks->pos = 0;
  ks->damping = damping;
  ks->brightness = brightness;

  /* Half-sine shaped noise burst: sin(pi*i/size) is vd_sin(i/(2*size))
   * because vd_sin() takes phase in turns, not radians. */
  for (i = 0; i < size; i++) {
    float n = vd_noise_next(&rng);
    float shape = vd_sin((float)i / (2.0f * (float)size));
    ks->buffer[i] = n * shape * 0.65f;
  }
}

static inline float ks_process(br_string *ks)
{
  int p1 = ks->pos;
  int p2 = p1 + 1 >= ks->size ? 0 : p1 + 1;
  float y1 = ks->buffer[p1];
  float y2 = ks->buffer[p2];
  float next = 0.5f * (y1 + y2) * ks->damping;

  next = y1 + (next - y1) * ks->brightness;
  if (vd_absf(next) < 1e-7f) next = 0.0f;   /* flush denormals in the loop */
  ks->buffer[p1] = next;
  ks->pos = p2;
  return y1;
}

/* Free voice, else the quietest one, else the oldest: fixed-size allocation. */
static br_voice *alloc_voice(void)
{
  br_voice *v = &voices[0];
  int i;
  for (i = 0; i < BR_HARPSI_VOICES; i++)
    if (!voices[i].active) return &voices[i];
  for (i = 1; i < BR_HARPSI_VOICES; i++) {
    if (voices[i].level < v->level ||
        (voices[i].level == v->level && voices[i].age < v->age))
      v = &voices[i];
  }
  return v;
}

void br_harpsi_note_on(int note, int vel)
{
  br_voice *v = alloc_voice();
  float freq = vd_mtof((float)note);
  float bright = 0.55f + 0.43f * last_ctrl.brightness;
  float damping = 0.9930f + 0.00695f * last_ctrl.damping;
  float cents = vd_clamp(last_ctrl.detune, 0.0f, 50.0f);

  ks_init(&v->a, freq, bright, damping);
  ks_init(&v->b, freq * vd_exp2(cents / 1200.0f), bright, damping);

  v->note = note;
  v->age = ++age_counter;
  v->level = 0.35f + 0.65f * vd_clamp((float)vel / 127.0f, 0.0f, 1.0f);
  v->decay = natural_decay;
  v->active = 1;
}

void br_harpsi_note_off(int note)
{
  br_voice *oldest = 0;
  int i;
  /* Release one voice per note-off.  A multi-track MIDI file can hold the same
   * pitch in two parts at once; damping every match would cut the other part. */
  for (i = 0; i < BR_HARPSI_VOICES; i++) {
    br_voice *v = &voices[i];
    if (v->active && v->note == note && (!oldest || v->age < oldest->age))
      oldest = v;
  }
  if (oldest) oldest->decay = damped_decay;
}

void br_harpsi_all_off(void)
{
  int i;
  for (i = 0; i < BR_HARPSI_VOICES; i++)
    if (voices[i].active) voices[i].decay = panic_decay;
}

void br_harpsi_render(float *out_l, float *out_r, int n, const br_harpsi_ctrl *c)
{
  /* The two choirs sit either side of the centre by a small, fixed amount. */
  const float spread = 0.35f;
  float ca = c->choir_a > 0.5f ? 1.0f : 0.0f;
  float cb = c->choir_b > 0.5f ? 1.0f : 0.0f;
  float vol = c->volume;
  float body_mix = c->body;
  float wet = vd_clamp(c->reverb, 0.0f, 1.0f);
  float dry_gain = 1.0f - 0.20f * wet;
  float wet_gain = 0.85f * wet;
  int i, v, active = 0;

  for (i = 0; i < n; i++) {
    float ml = 0.0f, mr = 0.0f, mix, res, dl, dr, l, r;

    for (v = 0; v < BR_HARPSI_VOICES; v++) {
      br_voice *vo = &voices[v];
      float a, b;
      if (!vo->active) continue;
      a = ks_process(&vo->a) * vo->level * ca;
      b = ks_process(&vo->b) * vo->level * cb;
      vo->level *= vo->decay;
      if (vo->level < 0.00005f) vo->active = 0;
      ml += a * (0.5f + 0.5f * spread) + b * (0.5f - 0.5f * spread);
      mr += a * (0.5f - 0.5f * spread) + b * (0.5f + 0.5f * spread);
    }

    /* Two cheap one-poles stand in for the soundboard. */
    mix = 0.5f * (ml + mr);
    body1 += 0.003f * (mix - body1);
    body2 += 0.0015f * (mix - body2);
    res = body_mix * (0.35f * body1 + 0.20f * body2);
    dl = ml + res;
    dr = mr + res;
    l = (dl * dry_gain + reverb_tick(&rev[0], dl) * wet_gain) * vol * 0.70f;
    r = (dr * dry_gain + reverb_tick(&rev[1], dr) * wet_gain) * vol * 0.70f;
    out_l[i] = vd_tanh(l);
    out_r[i] = vd_tanh(r);
  }

  for (v = 0; v < BR_HARPSI_VOICES; v++)
    if (voices[v].active) active++;
  __atomic_store_n(&active_count, active, __ATOMIC_RELEASE);
}

int br_harpsi_voice_count(void)
{
  return __atomic_load_n(&active_count, __ATOMIC_ACQUIRE);
}
