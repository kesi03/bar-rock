/* BAR-ROCK post-FX rack.  See fx.h.  DSP shapes and the rotary geometry follow
 * bar-fly's clean-room rack, trimmed to the effects that suit a plucked
 * harpsichord and kept entirely in fixed arrays.  All coefficient setup happens
 * once per block; the per-sample loop uses only SDK DSP helpers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "vibe_dsp.h"
#include "fx.h"

#define BR_FX_CHORUS_LEN  2048
#define BR_FX_FLANGER_LEN 1024
#define BR_FX_DELAY_MAX   26000
#define BR_FX_ROTOR_LEN   4096
#define BR_FX_LES_TAPS    3

/* Rotor geometry in metres and metres/second. */
#define BR_FX_HORN_R  0.19f
#define BR_FX_DRUM_R  0.22f
#define BR_FX_MIC_X   0.24f
#define BR_FX_MIC_Y   0.42f
#define BR_FX_SOUND_C 343.0f

static float fx_sr = 48000.0f;
static float fx_ratio = 1.0f;

/* --- saturation ------------------------------------------------------------- */
static float sat_dc[2];

/* --- phaser ----------------------------------------------------------------- */
static float ph_buf[2][4][512];
static int ph_pos[2];
static float ph_phase;

/* --- chorus / flanger -------------------------------------------------------- */
static float ch_buf[2][BR_FX_CHORUS_LEN];
static int ch_pos[2];
static float ch_phase;
static float fl_buf[2][BR_FX_FLANGER_LEN];
static int fl_pos[2];
static float fl_phase;

/* --- tremolo / auto-pan / ring mod / crusher -------------------------------- */
static float trem_phase, pan_phase;
static float ring_phase, ring_inc;
static float crush_hold[2], crush_levels;
static int crush_count, crush_div;

/* --- Leslie ---------------------------------------------------------------- */
typedef struct { float phase, hz, target_hz, coef; } les_rotor;
static vd_svf les_split;
static vd_svf_c les_split_c;
static float les_hbuf[BR_FX_ROTOR_LEN], les_dbuf[BR_FX_ROTOR_LEN];
static int les_w;
static les_rotor les_horn, les_drum;
static float les_horn_lp, les_drum_tone;
static float les_horn_hp_coef, les_drum_tone_coef;
static float les_base_delay, les_dist_scale, les_center_d;
static float rotary_wet, rotary_wet_coef;
static volatile int rotor_hz100;

/* Three taps spread across each rotor's baffle/reflection pattern. */
static const float les_tap_sin[BR_FX_LES_TAPS] = { -0.482f, 0.0f, 0.482f };
static const float les_tap_cos[BR_FX_LES_TAPS] = {  0.876f, 1.0f, 0.876f };
static const float les_tap_gain[BR_FX_LES_TAPS] = {  0.22f, 0.56f, 0.22f };

/* --- stereo echo ------------------------------------------------------------ */
static float dl_buf[2][BR_FX_DELAY_MAX];
static int dl_pos[2], dl_len;
static float dl_lp[2], dl_wow, dl_wow_phase;

/* Smoothed controls: modulation depths and mixes fade in/out without zippers. */
static br_fx_ctrl sm;

static float approach(float v, float target, float k)
{
  v += (target - v) * k;
  if (vd_absf(v) < 1.0e-7f) v = 0.0f;
  return v;
}

static float line_read(const float *buf, int size, int w, float back)
{
  int base = (int)back, r;
  float frac = back - (float)base, a, b;
  r = w - base;
  while (r < 0) r += size;
  while (r >= size) r -= size;
  b = buf[r];
  r--; if (r < 0) r += size;
  a = buf[r];
  return a * frac + b * (1.0f - frac);
}

static float clean(float v)
{
  return vd_absf(v) < 1.0e-7f ? 0.0f : v;
}

void br_fx_init(float sample_rate)
{
  fx_sr = sample_rate > 1000.0f ? sample_rate : 48000.0f;
  fx_ratio = fx_sr / 44100.0f;
  dl_len = (int)(0.280f * fx_sr + 0.5f);
  if (dl_len < 64) dl_len = 64;
  if (dl_len > BR_FX_DELAY_MAX) dl_len = BR_FX_DELAY_MAX;
  vd_svf_set(&les_split_c, 800.0f, 0.7071f, fx_sr);
  les_horn_hp_coef = vd_coef(1.0f / (VD_TAU * 140.0f), fx_sr);
  les_drum_tone_coef = vd_coef(1.0f / (VD_TAU * 2600.0f), fx_sr);
  les_base_delay = 0.004f * fx_sr;
  les_dist_scale = fx_sr / BR_FX_SOUND_C;
  les_center_d = vd_sqrt(BR_FX_MIC_X * BR_FX_MIC_X + BR_FX_MIC_Y * BR_FX_MIC_Y);
  br_fx_reset();
}

void br_fx_reset(void)
{
  int ch, i, o;
  sat_dc[0] = sat_dc[1] = 0.0f;
  for (ch = 0; ch < 2; ch++) {
    for (o = 0; o < 4; o++)
      for (i = 0; i < 512; i++) ph_buf[ch][o][i] = 0.0f;
    for (i = 0; i < BR_FX_CHORUS_LEN; i++) ch_buf[ch][i] = 0.0f;
    for (i = 0; i < BR_FX_FLANGER_LEN; i++) fl_buf[ch][i] = 0.0f;
    for (i = 0; i < BR_FX_DELAY_MAX; i++) dl_buf[ch][i] = 0.0f;
  }
  ph_pos[0] = ph_pos[1] = 0;
  ph_phase = 0.0f;
  ch_pos[0] = ch_pos[1] = 0;
  ch_phase = 0.0f;
  fl_pos[0] = fl_pos[1] = 0;
  fl_phase = 0.0f;
  trem_phase = 0.0f;
  pan_phase = 0.0f;
  ring_phase = 0.0f;
  ring_inc = 0.0f;
  crush_hold[0] = crush_hold[1] = 0.0f;
  crush_levels = 512.0f;
  crush_count = 0;
  crush_div = 1;
  dl_pos[0] = dl_pos[1] = 0;
  dl_lp[0] = dl_lp[1] = 0.0f;
  dl_wow = 0.0f;
  dl_wow_phase = 0.0f;
  les_split.ic1 = les_split.ic2 = 0.0f;
  les_horn_lp = 0.0f;
  les_drum_tone = 0.0f;
  les_w = 0;
  les_horn.phase = les_drum.phase = 0.0f;
  les_horn.hz = les_drum.hz = 0.0f;
  les_horn.target_hz = les_drum.target_hz = 0.0f;
  les_horn.coef = les_drum.coef = 0.0f;
  rotary_wet = 0.0f;
  for (i = 0; i < BR_FX_ROTOR_LEN; i++) les_hbuf[i] = les_dbuf[i] = 0.0f;
  sm.sat_type = 0.0f;
  sm.drive = 0.0f;
  sm.rotary_on = 0.0f;
  sm.rotary_speed = 0.0f;
  sm.rotary_depth = 0.0f;
  sm.rotary_drive = 0.0f;
  sm.rotary_balance = 0.0f;
  sm.phaser_mix = 0.0f;
  sm.phaser_rate = 0.0f;
  sm.chorus_mix = 0.0f;
  sm.chorus_rate = 0.0f;
  sm.flanger_mix = 0.0f;
  sm.trem_depth = 0.0f;
  sm.pan_depth = 0.0f;
  sm.ring_mix = 0.0f;
  sm.crush_mix = 0.0f;
  sm.delay_type = 0.0f;
  sm.delay_mix = 0.0f;
  sm.delay_ms = 280.0f;
  sm.delay_fb = 0.0f;
  __atomic_store_n(&rotor_hz100, 0, __ATOMIC_RELEASE);
}

int br_fx_rotor_hz100(void)
{
  return __atomic_load_n(&rotor_hz100, __ATOMIC_ACQUIRE);
}

static void les_motor_set(les_rotor *q, float target, float up_s,
                          float down_s, float stop_s)
{
  float t = target > q->target_hz ? up_s : (target <= 0.0001f ? stop_s : down_s);
  q->target_hz = target;
  q->coef = vd_coef(t, fx_sr);
}

static void les_motor_tick(les_rotor *q)
{
  q->hz += (q->target_hz - q->hz) * q->coef;
  if (q->target_hz <= 0.0001f && q->hz < 0.002f) q->hz = 0.0f;
  if (q->hz < 0.0f) q->hz = 0.0f;
  q->phase += q->hz / fx_sr;
  if (q->phase >= 1.0f) q->phase -= 1.0f;
}

/* One rotor heard by one microphone: three baffle taps with geometry-based
 * path length, directivity and inverse-distance gain. */
static float les_rotor_read(const float *buf, float phase, float radius,
                            float mic_x, float depth)
{
  float s = vd_sin(phase), c = vd_sin(phase + 0.25f);
  float out = 0.0f;
  int k;
  for (k = 0; k < BR_FX_LES_TAPS; k++) {
    float ts = s * les_tap_cos[k] + c * les_tap_sin[k];
    float tc = c * les_tap_cos[k] - s * les_tap_sin[k];
    float dx = mic_x - radius * tc;
    float dy = BR_FX_MIC_Y - radius * ts;
    float d = vd_sqrt(dx * dx + dy * dy);
    if (d < 0.001f) d = 0.001f;
    {
      float path = (d - les_center_d) * les_dist_scale * depth;
      float tap = line_read(buf, BR_FX_ROTOR_LEN, les_w, les_base_delay + path);
      float dot = (c * dx + s * dy) / d;
      float dir = 0.50f + 0.50f * dot;
      if (dir < 0.08f) dir = 0.08f;
      out += tap * les_tap_gain[k] * dir * (les_center_d / d);
    }
  }
  return out;
}

static float saturate(float x, int type, float drive, float *dc)
{
  float g = 1.0f + 5.0f * drive;
  x *= g;
  if (type == 2) {                    /* tube: asymmetric soft clip, DC blocked */
    float y = vd_tanh(x * 0.85f);
    y = vd_tanh(y + 0.16f * y * y);
    *dc += (y - *dc) * 0.0005f;
    return y - *dc;
  }
  return vd_tanh(x);                  /* solid state */
}

void br_fx_process(float *l, float *r, int n, const br_fx_ctrl *c)
{
  float k, hz_h = 0.0f, hz_d = 0.0f, rot_depth, rot_drive;
  float horn_gain = 1.0f, drum_gain = 1.0f, ring_hz = 110.0f;
  float ph_rate, ch_rate, fl_rate = 0.35f;
  int sat_type, rotary_on, rotary_speed, delay_type, crush_div_now;
  int ph_on, ch_on, fl_on, ring_on, crush_on, trem_on, pan_on;
  int rot_on, delay_on, any, i, ch, o;

  if (!l || !r || !c || n <= 0) return;

  /* Snapshot and smooth.  Type/speed are decisions, not continuous amounts. */
  k = vd_coef(0.02f, fx_sr);
  sm.sat_type = c->sat_type;
  sm.rotary_on = c->rotary_on;
  sm.rotary_speed = c->rotary_speed;
  sm.delay_type = c->delay_type;
  sm.drive = approach(sm.drive, vd_clamp(c->drive, 0.0f, 1.0f), k);
  sm.rotary_depth = approach(sm.rotary_depth, vd_clamp(c->rotary_depth, 0.0f, 1.0f), k);
  sm.rotary_drive = approach(sm.rotary_drive, vd_clamp(c->rotary_drive, 0.0f, 1.0f), k);
  sm.rotary_balance = approach(sm.rotary_balance, vd_clamp(c->rotary_balance, 0.0f, 1.0f), k);
  sm.phaser_mix = approach(sm.phaser_mix, vd_clamp(c->phaser_mix, 0.0f, 1.0f), k);
  sm.phaser_rate = approach(sm.phaser_rate, vd_clamp(c->phaser_rate, 0.05f, 8.0f), k);
  sm.chorus_mix = approach(sm.chorus_mix, vd_clamp(c->chorus_mix, 0.0f, 1.0f), k);
  sm.chorus_rate = approach(sm.chorus_rate, vd_clamp(c->chorus_rate, 0.05f, 5.0f), k);
  sm.flanger_mix = approach(sm.flanger_mix, vd_clamp(c->flanger_mix, 0.0f, 1.0f), k);
  sm.trem_depth = approach(sm.trem_depth, vd_clamp(c->trem_depth, 0.0f, 1.0f), k);
  sm.pan_depth = approach(sm.pan_depth, vd_clamp(c->pan_depth, 0.0f, 1.0f), k);
  sm.ring_mix = approach(sm.ring_mix, vd_clamp(c->ring_mix, 0.0f, 1.0f), k);
  sm.crush_mix = approach(sm.crush_mix, vd_clamp(c->crush_mix, 0.0f, 1.0f), k);
  sm.delay_mix = approach(sm.delay_mix, vd_clamp(c->delay_mix, 0.0f, 1.0f), k);
  sm.delay_ms = approach(sm.delay_ms, vd_clamp(c->delay_ms, 80.0f, 240.0f), k);
  sm.delay_fb = approach(sm.delay_fb, vd_clamp(c->delay_fb, 0.0f, 1.0f), k);

  sat_type = (int)(sm.sat_type + 0.5f);
  rotary_on = sm.rotary_on > 0.5f ? 1 : 0;
  rotary_speed = (int)(sm.rotary_speed + 0.5f);
  delay_type = (int)(sm.delay_type + 0.5f);
  ph_on = sm.phaser_mix > 0.0005f;
  ch_on = sm.chorus_mix > 0.0005f;
  fl_on = sm.flanger_mix > 0.0005f;
  ring_on = sm.ring_mix > 0.0005f;
  crush_on = sm.crush_mix > 0.0005f;
  trem_on = sm.trem_depth > 0.0005f;
  pan_on = sm.pan_depth > 0.0005f;
  rot_on = rotary_on || rotary_wet > 0.0005f;
  delay_on = delay_type > 0 && sm.delay_mix > 0.0005f;
  any = (sat_type > 0) || ph_on || ch_on || fl_on || ring_on || crush_on ||
        trem_on || pan_on || rot_on || delay_on;

  if (!any) {
    __atomic_store_n(&rotor_hz100, 0, __ATOMIC_RELEASE);
    return;                           /* straight bypass, exactly unchanged */
  }

  /* --- block-rate setup ----------------------------------------------------- */
  ph_rate = sm.phaser_rate;
  ch_rate = sm.chorus_rate;
  rot_depth = 0.12f + 0.88f * sm.rotary_depth;
  rot_drive = sm.rotary_drive;
  {
    float bal = sm.rotary_balance;
    horn_gain = 0.55f + 0.90f * bal;
    drum_gain = 0.55f + 0.90f * (1.0f - bal);
  }
  ring_inc = ring_hz / fx_sr;
  if (ring_hz > 0.0f) ring_inc = ring_hz / fx_sr;
  crush_levels = vd_pow(2.0f, 9.0f);  /* fixed 10-bit word length */
  crush_div_now = 4;                  /* fixed 16 kHz sample-and-hold at 64 kHz */

  if (delay_on) {
    int len = (int)(sm.delay_ms * 0.001f * fx_sr + 0.5f);
    if (len < 64) len = 64;
    if (len > BR_FX_DELAY_MAX) len = BR_FX_DELAY_MAX;
    dl_len = len;
    if (dl_pos[0] >= dl_len) dl_pos[0] = 0;
    if (dl_pos[1] >= dl_len) dl_pos[1] = 0;
  }

  if (rotary_on && rotary_speed == 2) { hz_h = 7.0f; hz_d = 6.0f; }
  else if (rotary_on && rotary_speed == 1) { hz_h = 0.90f; hz_d = 0.80f; }
  les_motor_set(&les_horn, hz_h, 0.22f, 0.38f, 0.55f);
  les_motor_set(&les_drum, hz_d, 1.10f, 0.65f, 1.40f);
  rotary_wet_coef = vd_coef(0.025f, fx_sr);

  /* --- per-sample chain ----------------------------------------------------- */
  for (i = 0; i < n; i++) {
    float xl = l[i], xr = r[i];

    if (sat_type > 0) {
      xl = saturate(xl, sat_type, sm.drive, &sat_dc[0]);
      xr = saturate(xr, sat_type, sm.drive, &sat_dc[1]);
    }

    if (ring_on) {
      ring_phase += ring_inc;
      if (ring_phase >= 1.0f) ring_phase -= 1.0f;
      {
        float mod = 0.50f + 0.50f * vd_sin(ring_phase);
        xl += (xl * mod - xl) * sm.ring_mix;
        xr += (xr * mod - xr) * sm.ring_mix;
      }
    }

    if (ph_on) {
      float g = 0.15f + 0.72f * 0.70f * (0.5f + 0.5f * vd_sin(ph_phase));
      ph_phase += ph_rate / fx_sr;
      if (ph_phase >= 1.0f) ph_phase -= 1.0f;
      for (ch = 0; ch < 2; ch++) {
        float dry = ch == 0 ? xl : xr;
        float y = dry;
        for (o = 0; o < 4; o++) {
          float yy = ph_buf[ch][o][ph_pos[ch]];
          ph_buf[ch][o][ph_pos[ch]] = clean(y + yy * g);
          y = yy - g * y;
        }
        y = dry * (1.0f - sm.phaser_mix) + y * sm.phaser_mix;
        if (ch == 0) xl = y; else xr = y;
      }
      ph_pos[0]++; if (ph_pos[0] >= 512) ph_pos[0] = 0;
      ph_pos[1]++; if (ph_pos[1] >= 512) ph_pos[1] = 0;
    }

    if (ch_on) {
      float p1 = vd_sin(ch_phase), p2 = vd_sin(ch_phase + 0.5f);
      float base1 = 610.0f * fx_ratio, base2 = 480.0f * fx_ratio;
      float dep1 = 110.0f * fx_ratio * 0.55f;
      float dep2 = 90.0f * fx_ratio * 0.55f;
      float t1 = line_read(ch_buf[0], BR_FX_CHORUS_LEN, ch_pos[0], base1 + dep1 * p1);
      float t2 = line_read(ch_buf[1], BR_FX_CHORUS_LEN, ch_pos[1], base2 + dep2 * p2);
      float dry = 1.0f - 0.35f * sm.chorus_mix, wet = 0.85f * sm.chorus_mix;
      ch_buf[0][ch_pos[0]] = clean(xl);
      ch_buf[1][ch_pos[1]] = clean(xr);
      ch_pos[0]++; if (ch_pos[0] >= BR_FX_CHORUS_LEN) ch_pos[0] = 0;
      ch_pos[1]++; if (ch_pos[1] >= BR_FX_CHORUS_LEN) ch_pos[1] = 0;
      ch_phase += ch_rate / fx_sr;
      if (ch_phase >= 1.0f) ch_phase -= 1.0f;
      xl = xl * dry + t1 * wet;
      xr = xr * dry + t2 * wet;
    }

    if (fl_on) {
      float sweep = 0.5f + 0.5f * vd_sin(fl_phase);
      float back = 2.0f * fx_ratio + (1.0f + 8.0f * 0.55f * fx_ratio) * sweep;
      float fb = 0.22f + 0.30f * 0.55f;
      float yl = line_read(fl_buf[0], BR_FX_FLANGER_LEN, fl_pos[0], back);
      float yr = line_read(fl_buf[1], BR_FX_FLANGER_LEN, fl_pos[1], back);
      fl_buf[0][fl_pos[0]] = clean(xl + yl * fb);
      fl_buf[1][fl_pos[1]] = clean(xr + yr * fb);
      fl_pos[0]++; if (fl_pos[0] >= BR_FX_FLANGER_LEN) fl_pos[0] = 0;
      fl_pos[1]++; if (fl_pos[1] >= BR_FX_FLANGER_LEN) fl_pos[1] = 0;
      fl_phase += fl_rate / fx_sr;
      if (fl_phase >= 1.0f) fl_phase -= 1.0f;
      xl += (yl - xl) * sm.flanger_mix;
      xr += (yr - xr) * sm.flanger_mix;
    }

    if (trem_on || pan_on) {
      if (trem_on) {
        float sL = 0.5f + 0.5f * vd_sin(trem_phase);
        float sR = 0.5f + 0.5f * vd_sin(trem_phase + 0.08f);
        xl *= 1.0f - sm.trem_depth * (1.0f - sL);
        xr *= 1.0f - sm.trem_depth * (1.0f - sR);
      }
      if (pan_on) {
        float pos = sm.pan_depth * vd_sin(pan_phase);
        float pL = 1.4142f * vd_sqrt(1.0f - pos);
        float pR = 1.4142f * vd_sqrt(1.0f + pos);
        xl *= pL;
        xr *= pR;
      }
      trem_phase += 5.2f / fx_sr;
      if (trem_phase >= 1.0f) trem_phase -= 1.0f;
      pan_phase += 0.7f / fx_sr;
      if (pan_phase >= 1.0f) pan_phase -= 1.0f;
    }

    if (crush_on) {
      if (--crush_count <= 0) {
        float ql = vd_clamp(xl, -2.0f, 2.0f) * crush_levels;
        float qr = vd_clamp(xr, -2.0f, 2.0f) * crush_levels;
        crush_hold[0] = (float)(int)ql / crush_levels;
        crush_hold[1] = (float)(int)qr / crush_levels;
        crush_count = crush_div_now;
      }
      xl += (crush_hold[0] - xl) * sm.crush_mix;
      xr += (crush_hold[1] - xr) * sm.crush_mix;
    }

    /* Motors coast even while the wet mix fades away. */
    if (rot_on) {
      les_motor_tick(&les_horn);
      les_motor_tick(&les_drum);
      {
        float mono = (xl + xr) * 0.5f;
        float dry_l = xl, dry_r = xr;
        vd_svf_out sv = vd_svf_tick(&les_split, &les_split_c, mono);
        float lpn = les_horn_lp + (sv.hp - les_horn_lp) * les_horn_hp_coef;
        float horn = sv.hp - lpn;
        float drum;
        float hl, hr, bl, br, wet_l, wet_r;
        les_horn_lp = lpn;
        les_drum_tone += (sv.lp - les_drum_tone) * les_drum_tone_coef;
        drum = les_drum_tone;
        if (rot_drive > 0.0f) horn = vd_tanh(horn * (1.0f + 5.0f * rot_drive));

        les_hbuf[les_w] = clean(horn);
        les_dbuf[les_w] = clean(drum);
        les_w++; if (les_w >= BR_FX_ROTOR_LEN) les_w = 0;

        hl = les_rotor_read(les_hbuf, les_horn.phase, BR_FX_HORN_R, -BR_FX_MIC_X, rot_depth) * horn_gain;
        hr = les_rotor_read(les_hbuf, les_horn.phase, BR_FX_HORN_R,  BR_FX_MIC_X, rot_depth) * horn_gain;
        bl = les_rotor_read(les_dbuf, les_drum.phase, BR_FX_DRUM_R, -BR_FX_MIC_X, rot_depth) * drum_gain;
        br = les_rotor_read(les_dbuf, les_drum.phase, BR_FX_DRUM_R,  BR_FX_MIC_X, rot_depth) * drum_gain;
        wet_l = (hl + bl) * 0.90f + mono * 0.045f;
        wet_r = (hr + br) * 0.90f + mono * 0.045f;
        rotary_wet += ((rotary_on ? 1.0f : 0.0f) - rotary_wet) * rotary_wet_coef;
        if (rotary_wet > 1.0f) rotary_wet = 1.0f;
        xl = dry_l + (wet_l - dry_l) * rotary_wet;
        xr = dry_r + (wet_r - dry_r) * rotary_wet;
      }
    }

    if (delay_on) {
      float yl = line_read(dl_buf[0], dl_len, dl_pos[0], (float)dl_len + dl_wow);
      float yr = line_read(dl_buf[1], dl_len, dl_pos[1], (float)dl_len - dl_wow);
      float fb = sm.delay_fb * (delay_type == 2 ? 0.82f : 0.85f);
      float damp = delay_type == 2 ? 0.28f : 0.55f;
      dl_lp[0] += (yl - dl_lp[0]) * damp;
      dl_lp[1] += (yr - dl_lp[1]) * damp;
      if (delay_type == 1) {
        dl_buf[0][dl_pos[0]] = clean(xl + dl_lp[1] * fb);
        dl_buf[1][dl_pos[1]] = clean(xr + dl_lp[0] * fb);
      } else {
        dl_wow = 6.0f * fx_ratio * vd_sin(dl_wow_phase);
        dl_wow_phase += 0.7f / fx_sr;
        if (dl_wow_phase >= 1.0f) dl_wow_phase -= 1.0f;
        dl_buf[0][dl_pos[0]] = clean(xl + dl_lp[0] * fb);
        dl_buf[1][dl_pos[1]] = clean(xr + dl_lp[1] * fb);
      }
      dl_pos[0]++; if (dl_pos[0] >= dl_len) dl_pos[0] = 0;
      dl_pos[1]++; if (dl_pos[1] >= dl_len) dl_pos[1] = 0;
      xl += yl * sm.delay_mix;
      xr += yr * sm.delay_mix;
    }

    /* Final soft limiter keeps every effect combination inside the device,
     * with a little headroom so stacked drive/delay never parks at exactly 1. */
    xl = 0.92f * vd_tanh(xl * 1.06f);
    xr = 0.92f * vd_tanh(xr * 1.06f);
    if (!(xl > -1.0e20f && xl < 1.0e20f)) xl = 0.0f;
    if (!(xr > -1.0e20f && xr < 1.0e20f)) xr = 0.0f;
    l[i] = xl;
    r[i] = xr;
  }

  if (rot_on)
    __atomic_store_n(&rotor_hz100, (int)(les_horn.hz * 100.0f + 0.5f), __ATOMIC_RELEASE);
  else
    __atomic_store_n(&rotor_hz100, 0, __ATOMIC_RELEASE);
}
