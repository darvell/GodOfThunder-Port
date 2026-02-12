#include "opl2_emu.h"

#include <string.h>

/*
  This is a pragmatic, lightweight YM3812-style synthesizer.

  It is NOT a cycle-accurate OPL2 core. It aims to sound plausible for
  God of Thunder's shipped music, which is a stream of register writes.

  Implementation choices:
  - Floating-point phase and envelope math for simplicity.
  - Linear sine table lookup (quarter-wave table, 1024-step phase).
  - Envelope in attenuation units of 0.75 dB ("TL steps") for convenient mixing
    with the Total Level register (also 0.75 dB steps).
*/

#define OPL2_NUM_CHANNELS 9
#define OPL2_NUM_OPERATORS 18

#define OPL2_ENV_MAX_UNITS 128.0 /* 96 dB / 0.75 dB */

#define PHASE_STEPS 1024
#define PHASE_MASK  (PHASE_STEPS - 1)

typedef enum {
  ENV_OFF = 0,
  ENV_ATTACK,
  ENV_DECAY,
  ENV_SUSTAIN,
  ENV_RELEASE
} EnvState;

typedef struct {
  uint8_t reg20; /* AM/VIB/EGT/KSR/MULT */
  uint8_t reg40; /* KSL/TL */
  uint8_t reg60; /* AR/DR */
  uint8_t reg80; /* SL/RR */
  uint8_t regE0; /* waveform */

  /* Derived / cached */
  int mult;          /* 0..15 */
  int ksr;           /* 0/1 */
  int egt;           /* 0/1 */
  int vib;           /* 0/1 (ignored) */
  int am;            /* 0/1 (ignored) */
  int ksl;           /* 0..3 (approx) */
  int tl;            /* 0..63 */
  int ar, dr, rr;    /* 0..15 */
  int sl;            /* 0..15 */
  int wave;          /* 0..3 */

  double phase;      /* cycles [0..1) */
  double phase_step; /* cycles/sample */

  EnvState env_state;
  double env_att;    /* attenuation in 0.75dB units; 0 loud, 128 silent */

  double sustain_att; /* sustain attenuation in 0.75dB units */
  double att_step_a;  /* attack step (units/sample) */
  double att_step_d;  /* decay step (units/sample) */
  double att_step_r;  /* release step (units/sample) */
} Opl2Op;

typedef struct {
  uint16_t fnum;   /* 10-bit */
  uint8_t block;   /* 0..7 */
  uint8_t key_on;  /* 0/1 */
  uint8_t fb;      /* 0..7 */
  uint8_t conn;    /* 0/1 */

  int mod_op;      /* operator index */
  int car_op;      /* operator index */

  int16_t fb1;
  int16_t fb2;
} Opl2Ch;

typedef struct {
  uint8_t regs[256];
  Opl2Op ops[OPL2_NUM_OPERATORS];
  Opl2Ch ch[OPL2_NUM_CHANNELS];

  uint8_t wave_enable; /* reg 0x01 bit 5 */

  /* amplitude table: attenuation units (0.75 dB) -> linear Q15 */
  int16_t att_to_amp[256];

  /* Quarter-wave sine table (0..pi/2), Q15, 256 entries */
  int16_t sin_q15[256];
} Opl2;

static Opl2 g_opl2;

static const int kChModOp[OPL2_NUM_CHANNELS] = { 0, 1, 2, 6, 7, 8, 12, 13, 14 };
static const int kChCarOp[OPL2_NUM_CHANNELS] = { 3, 4, 5, 9, 10, 11, 15, 16, 17 };

static const int kRegToOp[32] = {
  /* 0x00..0x05 */ 0, 1, 2, 3, 4, 5,
  /* 0x06..0x07 */ -1, -1,
  /* 0x08..0x0D */ 6, 7, 8, 9, 10, 11,
  /* 0x0E..0x0F */ -1, -1,
  /* 0x10..0x15 */ 12, 13, 14, 15, 16, 17,
  /* 0x16..0x1F */ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

/* Hard-coded YM3812 master clock assumption for tuning. */
static const double kOplClockHz = 14318180.0;

static double clampd(double x, double lo, double hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

static int clampi(int x, int lo, int hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

/*
  Derive a key code from block/fnum (approx).
  The real OPL2 uses a somewhat involved keycode generation with Note Select.
  For our purposes, block plus the top 2 bits of fnum works acceptably.
*/
static int keycode_for(uint16_t fnum, uint8_t block) {
  int kc = ((int)block << 2) | (int)((fnum >> 8) & 0x03);
  return kc; /* 0..31 */
}

/*
  Envelope rate approximation.

  We model attenuation change in "TL steps" (0.75dB). The full envelope range is
  ~96dB => 128 units.

  Rate values are 0..15. We approximate the classic exponential doubling of
  speed with the rate, and then apply a crude key-scaling via effective rate.

  This is not an exact YM3812 rate table, but it yields plausible results.
*/
static double env_step_units_per_sample(int rate0_15) {
  /* Base is chosen so rate 15 crosses full range in ~2ms. */
  const double base_seconds = 0.002;
  double seconds;
  int r = clampi(rate0_15, 0, 15);
  if (r == 0) {
    return 0.0;
  }

  /* seconds = base_seconds * 2^(15 - r) */
  seconds = base_seconds * (double)(1 << (15 - r));
  return (OPL2_ENV_MAX_UNITS / (seconds * (double)OPL2_EMU_SAMPLE_RATE));
}

static void update_op_cache(Opl2* o, int op_index) {
  Opl2Op* op = &o->ops[op_index];
  uint8_t r20 = op->reg20;
  uint8_t r40 = op->reg40;
  uint8_t r60 = op->reg60;
  uint8_t r80 = op->reg80;
  uint8_t rE0 = op->regE0;

  op->am   = (r20 & 0x80) ? 1 : 0;
  op->vib  = (r20 & 0x40) ? 1 : 0;
  op->egt  = (r20 & 0x20) ? 1 : 0;
  op->ksr  = (r20 & 0x10) ? 1 : 0;
  op->mult = (int)(r20 & 0x0F);

  op->ksl = (int)((r40 >> 6) & 0x03);
  op->tl  = (int)(r40 & 0x3F);

  op->ar = (int)((r60 >> 4) & 0x0F);
  op->dr = (int)(r60 & 0x0F);

  op->sl = (int)((r80 >> 4) & 0x0F);
  op->rr = (int)(r80 & 0x0F);

  op->wave = (int)(rE0 & 0x03);
  if (!o->wave_enable) {
    op->wave = 0;
  }

  op->sustain_att = (double)(op->sl * 4); /* 3 dB steps -> 0.75dB units */
}

static void update_op_env_steps(Opl2* o, int op_index, int ch_index) {
  Opl2Op* op = &o->ops[op_index];
  Opl2Ch* ch = &o->ch[ch_index];
  int kc = keycode_for(ch->fnum, ch->block);
  int ksr_add;
  int ar_eff, dr_eff, rr_eff;

  /* Crude key scaling rate: if KSR set, use full keycode, else reduce effect. */
  ksr_add = op->ksr ? (kc >> 1) : (kc >> 3);

  ar_eff = clampi(op->ar + ksr_add, 0, 15);
  dr_eff = clampi(op->dr + ksr_add, 0, 15);
  rr_eff = clampi(op->rr + ksr_add, 0, 15);

  op->att_step_a = env_step_units_per_sample(ar_eff);
  op->att_step_d = env_step_units_per_sample(dr_eff);
  op->att_step_r = env_step_units_per_sample(rr_eff);
}

static void update_channel_freq(Opl2* o, int ch_index) {
  Opl2Ch* ch = &o->ch[ch_index];
  int mod_i = ch->mod_op;
  int car_i = ch->car_op;
  Opl2Op* mod = &o->ops[mod_i];
  Opl2Op* car = &o->ops[car_i];

  /* Internal phase generator runs at clock/72, output sample rate is clock/288.
     Standard formula: f_hz ~= fnum * 2^block * (clock/72) / 2^19 */
  const double f_internal = kOplClockHz / 72.0;
  double base_hz = ((double)ch->fnum * (double)(1 << ch->block) * f_internal) / 524288.0;

  /* MULT: 0 means 0.5, otherwise 1..15 */
  {
    double mm = (mod->mult == 0) ? 0.5 : (double)mod->mult;
    double cm = (car->mult == 0) ? 0.5 : (double)car->mult;
    mod->phase_step = (base_hz * mm) / (double)OPL2_EMU_SAMPLE_RATE;
    car->phase_step = (base_hz * cm) / (double)OPL2_EMU_SAMPLE_RATE;
  }

  update_op_env_steps(o, mod_i, ch_index);
  update_op_env_steps(o, car_i, ch_index);
}

static void init_tables(Opl2* o) {
  int i;

  /* Build attenuation->amplitude table (Q15), using a constant per-step ratio.
     Each attenuation unit is 0.75 dB.
     ratio = 10^(-0.75/20) ~= 0.9170040432 */
  {
    const double ratio = 0.9170040432046712;
    double a = 1.0;
    for (i = 0; i < 256; i++) {
      int v = (int)(a * 32767.0 + 0.5);
      if (v < 0) v = 0;
      if (v > 32767) v = 32767;
      o->att_to_amp[i] = (int16_t)v;
      a *= ratio;
    }
  }

  /* Quarter sine table in Q15. Precomputed values to avoid libm dependency.
     Generated as round(sin(i * pi/2 / 256) * 32767).
  */
  {
    static const int16_t kSinQ15_256[256] = {
      0, 201, 402, 603, 804, 1005, 1206, 1407,
      1608, 1809, 2009, 2210, 2410, 2611, 2811, 3012,
      3212, 3412, 3612, 3811, 4011, 4210, 4410, 4609,
      4808, 5007, 5205, 5404, 5602, 5800, 5998, 6195,
      6393, 6590, 6786, 6983, 7179, 7375, 7571, 7767,
      7962, 8157, 8351, 8545, 8739, 8933, 9126, 9319,
      9512, 9704, 9896, 10087, 10278, 10469, 10659, 10849,
      11039, 11228, 11417, 11605, 11793, 11980, 12167, 12353,
      12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
      14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269,
      15446, 15623, 15800, 15976, 16151, 16325, 16499, 16673,
      16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
      18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357,
      19519, 19680, 19841, 20000, 20159, 20317, 20475, 20631,
      20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
      22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027,
      23170, 23311, 23452, 23592, 23731, 23870, 24007, 24143,
      24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
      25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198,
      26319, 26438, 26556, 26674, 26790, 26905, 27019, 27133,
      27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
      28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803,
      28898, 28992, 29085, 29177, 29268, 29358, 29447, 29534,
      29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
      30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783,
      30852, 30919, 30985, 31050, 31113, 31176, 31237, 31297,
      31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
      31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098,
      32137, 32176, 32213, 32250, 32285, 32318, 32351, 32382,
      32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
      32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717,
      32728, 32737, 32745, 32752, 32757, 32761, 32765, 32766
    };
    memcpy(o->sin_q15, kSinQ15_256, sizeof(kSinQ15_256));
  }
}

static int16_t sin_full_q15(const Opl2* o, int phase0_1023) {
  int p = phase0_1023 & PHASE_MASK;
  int quadrant = (p >> 8) & 3; /* 0..3 */
  int i = p & 0xFF;
  int16_t v;

  switch (quadrant) {
    case 0: v = o->sin_q15[i]; break;
    case 1: v = o->sin_q15[255 - i]; break;
    case 2: v = (int16_t)(-o->sin_q15[i]); break;
    default: v = (int16_t)(-o->sin_q15[255 - i]); break;
  }
  return v;
}

static int16_t apply_waveform(int wave, int16_t s) {
  switch (wave & 3) {
    default:
    case 0: /* sine */
      return s;
    case 1: /* half-sine (rectify negative half) */
      return (s < 0) ? 0 : s;
    case 2: /* absolute sine */
      return (s < 0) ? (int16_t)(-s) : s;
    case 3: /* pulse-ish */
      return (s < 0) ? 0 : (int16_t)32767;
  }
}

static void env_key_on(Opl2Op* op) {
  op->env_state = ENV_ATTACK;
  op->env_att = OPL2_ENV_MAX_UNITS;
}

static void env_key_off(Opl2Op* op) {
  if (op->env_state != ENV_OFF) {
    op->env_state = ENV_RELEASE;
  }
}

static void env_advance(Opl2Op* op) {
  switch (op->env_state) {
    case ENV_OFF:
      op->env_att = OPL2_ENV_MAX_UNITS;
      break;
    case ENV_ATTACK:
      if (op->att_step_a <= 0.0) {
        op->env_att = 0.0;
        op->env_state = ENV_DECAY;
      } else {
        op->env_att -= op->att_step_a;
        if (op->env_att <= 0.0) {
          op->env_att = 0.0;
          op->env_state = ENV_DECAY;
        }
      }
      break;
    case ENV_DECAY:
      if (op->att_step_d <= 0.0) {
        op->env_att = op->sustain_att;
        op->env_state = op->egt ? ENV_SUSTAIN : ENV_RELEASE;
      } else {
        op->env_att += op->att_step_d;
        if (op->env_att >= op->sustain_att) {
          op->env_att = op->sustain_att;
          op->env_state = op->egt ? ENV_SUSTAIN : ENV_RELEASE;
        }
      }
      break;
    case ENV_SUSTAIN:
      /* Hold sustain level until key off. */
      op->env_att = op->sustain_att;
      break;
    case ENV_RELEASE:
      if (op->att_step_r <= 0.0) {
        op->env_att = OPL2_ENV_MAX_UNITS;
        op->env_state = ENV_OFF;
      } else {
        op->env_att += op->att_step_r;
        if (op->env_att >= OPL2_ENV_MAX_UNITS) {
          op->env_att = OPL2_ENV_MAX_UNITS;
          op->env_state = ENV_OFF;
        }
      }
      break;
    default:
      op->env_state = ENV_OFF;
      op->env_att = OPL2_ENV_MAX_UNITS;
      break;
  }
}

static int16_t op_render(Opl2* o, Opl2Op* op, double pm_cycles, int ch_block) {
  /* Advance envelope first (OPL updates envelope at sample rate). */
  env_advance(op);

  /* Total level: TL is 0.75dB steps; KSL is approximated from block. */
  {
    double ksl_att = 0.0;
    int att_i;
    int16_t amp;
    int phase_i;
    int16_t s;
    int32_t out;

    if (op->ksl) {
      /* Very rough: higher block => more attenuation, scaled by KSL. */
      ksl_att = (double)(op->ksl * ch_block) * 2.0;
    }

    att_i = (int)(op->env_att + (double)op->tl + ksl_att + 0.5);
    att_i = clampi(att_i, 0, 255);
    amp = o->att_to_amp[att_i];

    /* Phase: base phase plus phase modulation. */
    {
      double ph = op->phase + pm_cycles;
      /* Wrap into [0..1) */
      ph -= (double)((int)ph);
      if (ph < 0.0) ph += 1.0;
      phase_i = (int)(ph * (double)PHASE_STEPS) & PHASE_MASK;
    }

    s = sin_full_q15(o, phase_i);
    s = apply_waveform(op->wave, s);

    out = (int32_t)s * (int32_t)amp;
    out >>= 15;

    /* Advance phase for next sample. */
    op->phase += op->phase_step;
    op->phase -= (double)((int)op->phase);

    return (int16_t)clampi((int)out, -32768, 32767);
  }
}

void opl2_init(void) {
  int i;

  memset(&g_opl2, 0, sizeof(g_opl2));

  init_tables(&g_opl2);

  g_opl2.wave_enable = 1; /* match DetectAndInitAdLib enabling wave select */

  for (i = 0; i < OPL2_NUM_CHANNELS; i++) {
    g_opl2.ch[i].mod_op = kChModOp[i];
    g_opl2.ch[i].car_op = kChCarOp[i];
    g_opl2.ch[i].fnum = 0;
    g_opl2.ch[i].block = 0;
    g_opl2.ch[i].key_on = 0;
    g_opl2.ch[i].fb = 0;
    g_opl2.ch[i].conn = 0;
    g_opl2.ch[i].fb1 = 0;
    g_opl2.ch[i].fb2 = 0;
  }

  for (i = 0; i < OPL2_NUM_OPERATORS; i++) {
    Opl2Op* op = &g_opl2.ops[i];
    op->reg20 = 0;
    op->reg40 = 0;
    op->reg60 = 0;
    op->reg80 = 0;
    op->regE0 = 0;
    op->phase = 0.0;
    op->phase_step = 0.0;
    op->env_state = ENV_OFF;
    op->env_att = OPL2_ENV_MAX_UNITS;
    update_op_cache(&g_opl2, i);
  }

  /* Seed a few registers to typical post-reset state. */
  g_opl2.regs[0x01] = 0x20; /* wave select enable */
}

static int op_index_from_reg(uint8_t reg) {
  int off = (int)(reg & 0x1F);
  if (off < 0 || off >= 32) return -1;
  return kRegToOp[off];
}

static int channel_from_reg(uint8_t reg) {
  if (reg >= 0xA0 && reg <= 0xA8) return (int)(reg - 0xA0);
  if (reg >= 0xB0 && reg <= 0xB8) return (int)(reg - 0xB0);
  if (reg >= 0xC0 && reg <= 0xC8) return (int)(reg - 0xC0);
  return -1;
}

void opl2_write(uint8_t reg, uint8_t val) {
  Opl2* o = &g_opl2;
  int opi, chi;

  o->regs[reg] = val;

  if (reg == 0x01) {
    o->wave_enable = (val & 0x20) ? 1 : 0;
    /* Update cached waveforms */
    for (opi = 0; opi < OPL2_NUM_OPERATORS; opi++) {
      update_op_cache(o, opi);
    }
    return;
  }

  if (reg == 0xBD) {
    /* AM/VIB depth + rhythm mode (ignored for now). */
    return;
  }

  /* Operator registers */
  if ((reg >= 0x20 && reg <= 0x35) ||
      (reg >= 0x40 && reg <= 0x55) ||
      (reg >= 0x60 && reg <= 0x75) ||
      (reg >= 0x80 && reg <= 0x95) ||
      (reg >= 0xE0 && reg <= 0xF5)) {
    opi = op_index_from_reg(reg);
    if (opi >= 0) {
      Opl2Op* op = &o->ops[opi];
      if (reg >= 0x20 && reg <= 0x35) op->reg20 = val;
      else if (reg >= 0x40 && reg <= 0x55) op->reg40 = val;
      else if (reg >= 0x60 && reg <= 0x75) op->reg60 = val;
      else if (reg >= 0x80 && reg <= 0x95) op->reg80 = val;
      else op->regE0 = val;

      update_op_cache(o, opi);

      /* If mult/ksr changed, refresh channel-derived params. */
      for (chi = 0; chi < OPL2_NUM_CHANNELS; chi++) {
        if (o->ch[chi].mod_op == opi || o->ch[chi].car_op == opi) {
          update_channel_freq(o, chi);
        }
      }
    }
    return;
  }

  /* Channel frequency/key on */
  if ((reg >= 0xA0 && reg <= 0xA8) || (reg >= 0xB0 && reg <= 0xB8)) {
    chi = channel_from_reg(reg);
    if (chi >= 0) {
      Opl2Ch* ch = &o->ch[chi];
      uint8_t old_key = ch->key_on;

      if (reg >= 0xA0 && reg <= 0xA8) {
        ch->fnum = (uint16_t)((ch->fnum & 0x300) | (uint16_t)val);
      } else {
        ch->fnum = (uint16_t)((ch->fnum & 0x0FF) | (uint16_t)((val & 0x03) << 8));
        ch->block = (uint8_t)((val >> 2) & 0x07);
        ch->key_on = (val & 0x20) ? 1 : 0;
      }

      update_channel_freq(o, chi);

      if (ch->key_on && !old_key) {
        env_key_on(&o->ops[ch->mod_op]);
        env_key_on(&o->ops[ch->car_op]);
      } else if (!ch->key_on && old_key) {
        env_key_off(&o->ops[ch->mod_op]);
        env_key_off(&o->ops[ch->car_op]);
      }
    }
    return;
  }

  /* Channel algorithm/feedback */
  if (reg >= 0xC0 && reg <= 0xC8) {
    chi = (int)(reg - 0xC0);
    if (chi >= 0 && chi < OPL2_NUM_CHANNELS) {
      Opl2Ch* ch = &o->ch[chi];
      ch->conn = (val & 0x01) ? 1 : 0;
      ch->fb = (uint8_t)((val >> 1) & 0x07);
    }
    return;
  }
}

void opl2_generate(int16_t* buf, int samples) {
  Opl2* o = &g_opl2;
  int i;

  if (!buf || samples <= 0) {
    return;
  }

  for (i = 0; i < samples; i++) {
    int ch;
    int32_t mix = 0;

    for (ch = 0; ch < OPL2_NUM_CHANNELS; ch++) {
      Opl2Ch* c = &o->ch[ch];
      Opl2Op* mod = &o->ops[c->mod_op];
      Opl2Op* car = &o->ops[c->car_op];

      /* Skip silent channels quickly. */
      if (!c->key_on && mod->env_state == ENV_OFF && car->env_state == ENV_OFF) {
        continue;
      }

      /* Feedback phase modulation into modulator. */
      {
        int16_t fb_mix = (int16_t)(((int)c->fb1 + (int)c->fb2) / 2);
        double fb_cycles;
        double mod_cycles;
        int16_t mod_out;
        int16_t car_out;

        /* Very rough feedback scaling: doubles per step, with a small base. */
        if (c->fb == 0) {
          fb_cycles = 0.0;
        } else {
          double fb_scale = (double)(1 << (c->fb - 1));
          fb_cycles = ((double)fb_mix / 32768.0) * (0.002 * fb_scale);
        }

        mod_out = op_render(o, mod, fb_cycles, (int)c->block);

        /* Save for next-sample feedback. */
        c->fb2 = c->fb1;
        c->fb1 = mod_out;

        /* Modulator -> carrier phase modulation depth. */
        mod_cycles = ((double)mod_out / 32768.0) * 0.02;

        if (!c->conn) {
          car_out = op_render(o, car, mod_cycles, (int)c->block);
          mix += (int32_t)car_out;
        } else {
          car_out = op_render(o, car, 0.0, (int)c->block);
          mix += (int32_t)car_out + (int32_t)mod_out;
        }
      }
    }

    /* Prevent clipping: scale down a bit. */
    mix /= 4;
    buf[i] = (int16_t)clampi((int)mix, -32768, 32767);
  }
}
