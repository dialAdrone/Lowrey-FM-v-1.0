/* ============================================================================
 * Lowrey FM v 1.0 - firmware for the M-VAVE FM-1 (main.c)
 * https://github.com/dialAdrone/Lowrey-FM-v-1.0
 *
 * Copyright (C) 2026 dialAdrone
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License, version 3 only
 * (GPL-3.0-only), as published by the Free Software Foundation. It comes
 * WITHOUT ANY WARRANTY. See the LICENSE file for the full text.
 *
 * Built with FM1 Studio (fm1studio.com, Tim Samshuijzen, GPL-3.0) against its
 * fm1.h API, which follows felucca-bsp (Leo Kuroshita / Hugelton Instruments)
 * and fm1-base (Charles Vestal).
 *
 * Third-party material in this file:
 * - Mellotron M400S flute tapes: Taijiguy's (Bernie Kornowicz) recordings,
 *   "free to use, but cannot be used in any commercial, for profit software".
 *   NOT GPL: this firmware may be shared, but not sold.
 * - The tape preparation (12 tapes, 11025 Hz, IMA ADPCM, loops) and how the
 *   flute is played back come from FMEL-1 by AntonioAltea
 *   (github.com/AntonioAltea/fmel-1), GPL-3.0-only.
 * - Reverb: the Freeverb design (Jezar at Dreampoint, public domain).
 * ============================================================================ */

#include "fm1.h"
#include <stdint.h>
#include <math.h>

/* ---------------- Constants & Brown Background + Dark Blue Card Palette (RGB565) ---------------- */
#define NUM_VOICES           8u
#define MAX_NOTES            6u   /* polyphony (shared by all voice layers) */
#define AOL_FLOOR 1.0e-4f   /* AOL: a tone's level when faded out (-80 dB) */

/* Warm brown main screen + dark blue instrument button tabs */
/* Fixed scene colors (splash / truck page never change with the theme) */
#define COL_BG_DEF       0x2903u  /* rich dark chocolate brown main background */
#define COL_STAGE_BG_DEF 0x3985u  /* warm walnut brown panel */
#define COL_NIGHT_SKY    0x0024u  /* very dark midnight blue sky for power-on screen */
#define COL_BTN_OFF_DEF  0x0828u  /* dark navy-blue button card when off */
#define COL_BTN_ON_DEF   0x11CEu  /* deeper rich blue button card when active */

/* Theme colors, changed at runtime by the ALGORITHM (main) and PRESET (button) knobs */
static uint16_t g_col_bg      = COL_BG_DEF;
static uint16_t g_col_stage   = COL_STAGE_BG_DEF;
static uint16_t g_col_btn_off = COL_BTN_OFF_DEF;
static uint16_t g_col_btn_on  = COL_BTN_ON_DEF;
#define COL_BG       g_col_bg
#define COL_STAGE_BG g_col_stage
#define COL_BTN_OFF  g_col_btn_off
#define COL_BTN_ON   g_col_btn_on
#define COL_INK          0x1080u  /* deep dark ink outline */
#define COL_BORDER       0xB529u  /* warm tan bevel border */
#define COL_TEXT         0xFFFBu  /* soft warm cream-white */
#define COL_DIM          0xC618u  /* soft silver-tan secondary text */

#define COL_GOLD_HI      0xFFF0u  /* bright sunshine yellow */
#define COL_GOLD         0xFD20u  /* warm golden yellow */
#define COL_GOLD_SH      0xB380u  /* amber ochre */
#define COL_CREAM        0xFFF9u  /* moon cream */
#define COL_TRUCK_HI     0xC447u  /* light chestnut brown */
#define COL_TRUCK_MD     0x8A22u  /* classic brown pickup truck */
#define COL_TRUCK_SH     0x5180u  /* dark saddle brown */
#define COL_LEAF_HI      0xB7EBu  /* soft spring green */
#define COL_LEAF_MD      0x5DC8u  /* cute green */
#define COL_LEAF_SH      0x2B84u  /* deep moss green */
#define COL_APPLE_HI     0xFCAEu  /* soft coral highlight */
#define COL_APPLE_MD     0xE944u  /* warm apple / Golden Gate red */
#define COL_APPLE_SH     0x8800u  /* deep berry / bridge shade */
#define COL_WORM         0xFDB8u  /* cute peach-pink worm */
#define COL_BUBBLE_HI    0xE7FFu  /* soft pale sky highlight */
#define COL_BUBBLE_MD    0x95DFu  /* gentle bubble tint */
#define COL_BUBBLE_SH    0x5B99u  /* soft shaded rim */
#define COL_SMOKE        0xD69Au  /* soft slow smoke puff */

#define DELAY_MAX_SAMPLES 22050u  /* ~0.5s at 44100 Hz */

#define SPLASH_DURATION_MS 6400u  /* long enough for truck to drive all the way across */

/* Output gain at full MASTER. The old 0.155 left the output peaking around
 * -20..-30 dBFS (quiet on the speaker); this brings big chords near full
 * scale, with a soft limiter at the very end catching anything above. */
#define MAX_OUTPUT_GAIN 1.40f

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* Color themes. Both knobs step through the same list.
 * Main theme:   bg = screen background, panel = text panels (kept dark enough for light text)
 * Button theme: off / on card colors (labels switch to dark ink on light cards) */
typedef struct { const char *name; uint16_t bg, panel, btn_off, btn_on; } ColorTheme;
static const ColorTheme THEMES[] = {
    { "CLASSIC",    COL_BG_DEF,          COL_STAGE_BG_DEF,   COL_BTN_OFF_DEF,      COL_BTN_ON_DEF      },
    { "DARK BLUE",  RGB565( 16, 24, 56), RGB565( 30, 44, 90), RGB565( 20, 30, 70), RGB565( 40, 64,136) },
    { "SEAFOAM",    RGB565(160,220,200), RGB565( 36, 92, 84), RGB565(150,212,190), RGB565( 64,148,128) },
    { "DUSTY PINK", RGB565(200,150,160), RGB565(110, 60, 75), RGB565(196,146,156), RGB565(150, 86,106) },
    { "MUTED BROWN",RGB565(140,110, 90), RGB565( 80, 60, 48), RGB565(140,108, 86), RGB565(100, 70, 50) },
    { "STRAWBERRY", RGB565(250,182,196), RGB565(150, 70, 95), RGB565(246,176,190), RGB565(216,108,138) },
    { "VANILLA",    RGB565(246,236,202), RGB565(120,100, 70), RGB565(240,230,196), RGB565(204,178,112) },
    { "CHOCOLATE",  RGB565(120, 72, 48), RGB565( 70, 40, 28), RGB565(110, 66, 42), RGB565(152, 96, 60) },
};
#define NUM_THEMES (sizeof(THEMES) / sizeof(THEMES[0]))
static uint32_t g_main_theme = 0u;
static uint32_t g_btn_theme = 0u;

static void apply_themes(void) {
    g_col_bg      = THEMES[g_main_theme].bg;
    g_col_stage   = THEMES[g_main_theme].panel;
    g_col_btn_off = THEMES[g_btn_theme].btn_off;
    g_col_btn_on  = THEMES[g_btn_theme].btn_on;
}

/* Dark ink on light backgrounds, cream text on dark ones */
static int is_light_color(uint16_t c) {
    uint32_t r = (c >> 11) & 0x1Fu, g = (c >> 5) & 0x3Fu, b = c & 0x1Fu;
    uint32_t luma = r * 8u * 3u + g * 4u * 6u + b * 8u; /* ~ 0..2550 */
    return luma > 1500u;
}

/* ---------------- Voice Definitions (FM-1 Button Order) ---------------- */
enum {
    VOICE_FLUTE = 0,  /* FM1_BTN_ENV  (4)  - crescent moon + slowly twinkling star */
    VOICE_HARPSI,     /* FM1_BTN_LFO  (5)  - Palestinian flag */
    VOICE_HORN,       /* FM1_BTN_EDIT (6)  - daisy with slowly falling petal */
    VOICE_REED,       /* FM1_BTN_GLO  (7)  - round bubble popping slowly */
    VOICE_STRINGS,    /* FM1_BTN_ARP  (10) - burning cigarette with smoke */
    VOICE_VOX,        /* FM1_BTN_SEQ  (11) - apple with bite & slowly popping worm */
    VOICE_VIBES,      /* FM1_BTN_PLAY (12) - chimes slowly changing colors */
    VOICE_BASS        /* FM1_BTN_REC  (13) - brown truck with slow smoke puff */
};

static const uint32_t VOICE_BTN_MAP[NUM_VOICES] = {
    FM1_BTN_ENV,  /* 0: Flute */
    FM1_BTN_LFO,  /* 1: Harpsichord */
    FM1_BTN_EDIT, /* 2: Horn */
    FM1_BTN_GLO,  /* 3: Reed */
    FM1_BTN_ARP,  /* 4: Strings (Cigarette) */
    FM1_BTN_SEQ,  /* 5: Vox */
    FM1_BTN_PLAY, /* 6: Vibraphone */
    FM1_BTN_REC   /* 7: Bass */
};

static const char *VOICE_NAMES[NUM_VOICES] = {
    "FLUTE", "HARPSI", "HORN", "REED",
    "STRINGS", "VOX", "VIBES", "BASS"
};

/* Bank B voices (same buttons as bank A) */
enum {
    VB_LOWREY = 0,  /* eye blinking: Lowrey flute, 10 harmonics from the real organ's spectrum */
    VB_SID,         /* floppy disk: modem noise layer (screech, dropouts, crunch) */
    VB_POCKET,      /* sun that becomes a moon while played: lo-fi square organ */
    VB_PS20,        /* waves: breathy home-keyboard organ with vibrato */
    VB_MBOX,        /* rose dropping petals: soft music box, an octave up */
    VB_PHONE,       /* rotary phone, handset lifting: old-school dial-up call (dial tone, touch-tones, bongs) */
    VB_WARM,        /* burning police car: fat, mellow sine */
    VB_HIGH         /* spilling bottle: a clean high tone an octave up (CHIP's old tone) */
};

static const char *VOICE_NAMES_B[NUM_VOICES] = {
    "LOWREY", "CHIP", "POCKET", "PS-20",
    "M.BOX", "AOL", "WARM", "HIGH"
};

static const uint16_t VOICE_COLORS_B[NUM_VOICES] = {
    COL_BUBBLE_HI, /* LOWREY (eye) */
    COL_DIM,       /* SID (disk) */
    COL_GOLD_HI,   /* POCKET (sun / moon) */
    COL_BUBBLE_MD, /* PS-20 (waves) */
    COL_APPLE_MD,  /* CELESTA (rose) */
    COL_APPLE_HI,  /* PHONE (rotary phone) */
    COL_GOLD,      /* WARM (campfire) */
    COL_LEAF_HI    /* STEEL (bottle) */
};

static const uint16_t VOICE_COLORS[NUM_VOICES] = {
    COL_GOLD,      /* Flute */
    COL_APPLE_MD,  /* Harpsi (Palestinian flag) */
    COL_LEAF_HI,   /* Horn (daisy) */
    COL_BUBBLE_HI, /* Reed (bubble) */
    COL_GOLD_SH,   /* Strings (cigarette) */
    COL_APPLE_HI,  /* Vox (apple) */
    COL_GOLD,      /* Vibes */
    COL_TRUCK_HI   /* Bass (brown truck) */
};

/* ---------------- UI, Octave, Hold, Env & FX State ---------------- */
enum {
    VIEW_SPLASH = -1,
    VIEW_HOME = 0,
    VIEW_FX_REVERB,
    VIEW_FX_DELAY,
    VIEW_ENV_AD,
    VIEW_SCOPE,   /* SELECT knob page 2: live oscilloscope */
    VIEW_TRUCK    /* SELECT knob page 3: truck drives while notes play */
};

static int is_home_page(int view) {
    return view == VIEW_HOME || view == VIEW_SCOPE || view == VIEW_TRUCK;
}

static int g_view = VIEW_SPLASH;
static int g_voice_on[NUM_VOICES];
static int g_bank = 0;                       /* 0 = bank A (organ), 1 = bank B; hold HOME to switch */
static int g_bank_voices[2][NUM_VOICES];     /* each bank remembers its own voice buttons */
static int g_octave_shift = 0; /* -2 .. +2 octaves */
static int g_hold_on = 0;

/* Envelope & Filter controls (0.0f .. 1.0f) */
static float g_attack = 0.04f;        /* maps to ~2ms .. 450ms */
static float g_decay = 0.55f;         /* maps to ~60ms .. 3260ms (KNOB3) */
static float g_filter_cutoff = 0.78f; /* low-pass cutoff (KNOB4) */
static float g_mel_level = 0.0f;      /* Mellotron flute layer (SAVE screen, KNOB2); 0 = off */

/* FX parameters (0.0f .. 1.0f) */
static float g_rev_mix = 0.25f;
static float g_rev_decay = 0.65f;
static float g_dly_mix = 0.0f;   /* delay starts off */
static float g_dly_time = 0.35f;
static float g_dly_feedback = 0.38f;

/* ---------------- Synth & FX DSP State ---------------- */
typedef struct {
    int active;
    int sustained;
    int note_idx;     /* keyboard: key 0..26; MIDI: MIDI note number 0..127 */
    int is_midi;      /* 1 = played from MIDI: true pitch, no octave shift */
    float phase;      /* main oscillator (flute/harpsi/horn/reed/strings) */
    float phase_vib;  /* vibrato oscillator (vox/vibes) */
    float phase_sub;  /* half-speed oscillator (bass) */
    float env;
    int stage;        /* 0 = attack, 1 = decay/sustain, 2 = release */
    float pluck_state;
    /* Mellotron flute tape head */
    int mf_zone;      /* which of the 12 tapes */
    uint32_t mf_pos;  /* next tape sample to decode */
    int32_t mf_pred;  /* IMA decoder: predictor */
    int32_t mf_idx;   /* IMA decoder: step index */
    uint32_t mf_byte; /* the tape byte being read (2 samples per byte) */
    /* Bank B */
    float strike, strike2, strike3;  /* struck-voice envelopes (celesta, steel) */
    float bph[3];                    /* inharmonic partial phases */
    float pwm_ph;                    /* SID pulse-width sweep */
    float hold_l, hold_r;            /* POCKET sample-and-hold */
    int hold_n;
    uint32_t lfsr;                   /* noise (modem screech, key clicks) */
    float age;                       /* seconds since the note started (modem handshake) */
    float mph;                       /* modem tone phase */
    int hop_left;                    /* modem: blocks until the next tone hop */
    float hop_ratio;                 /* modem: current tone (x the note) */
    uint32_t chip_seed;              /* CHIP: which glitch pattern this note loops */
    int burst;                       /* modem: screech on in this block */
    float pha, phb;                  /* PHONE: tone phases */
    float aol_lp, aol_hp;            /* AOL: telephone-line filter */
    float aol_nlp;                   /* AOL: data hiss */
    int aol_cc;                      /* AOL: crunch counter */
    float aol_held;                  /* AOL: crunch held sample */
    float aol_ga, aol_gb;            /* AOL: tone levels, glided so they never click */
    int aol_fl;                      /* AOL: answer-tone phase is flipped */
    int aol_chop;                    /* AOL: blocks left in this static burst */
    float aol_chlv;                  /* AOL: this burst's static level */
    float phfar;                     /* HIGH: tone phase */
    float click;                     /* key click / pluck */
    float nlp;                       /* modem: narrowed noise */
    int ccount;                      /* modem: crunch counter */
    float cheld_l;                   /* modem: crunch held sample */
    float mf_s[4];    /* last 4 tape samples (Hermite between [1] and [2]) */
    float mf_frac, mf_rate;
} NoteVoice;

/* One note pool shared by every voice layer: each note's oscillator and
 * envelope are computed ONCE per sample, then every enabled layer mixes its
 * own harmonics from it. Previously each layer ran its own copy of the same
 * note, so CPU cost was notes x layers. */
static NoteVoice g_pool[MAX_NOTES];
static float g_pan_l[NUM_VOICES];
static float g_pan_r[NUM_VOICES];
static uint32_t g_prev_notes_mask = 0u;
static float g_lfo_phase = 0.0f;
static float g_lp_l = 0.0f;
static float g_lp_r = 0.0f;

/* Oscilloscope capture: filtered dry mix written by audio(), read by the screen */
#define SCOPE_BUF_LEN 2048u
static float g_scope_buf[SCOPE_BUF_LEN];
static volatile uint32_t g_scope_w = 0u;
static uint32_t g_truck_step = 0u;

/* Note-sounding flag aligned cleanly with audible note decay */
static volatile uint32_t g_any_note_sounding = 0u;
/* A key/MIDI note is down (or caught by HOLD), not just a decaying tail */
static volatile uint32_t g_any_note_held = 0u;

/* Delay line (mono) */
static float g_dly[DELAY_MAX_SAMPLES];   /* mono delay line (88 KB) */
static uint32_t g_dly_write = 0u;

/* ---- Reverb (Freeverb design: 8 damped combs + 4 allpasses per side) ----
 * Delay lengths are the classic Freeverb tunings at 44.1 kHz; the right side
 * is 23 samples longer for stereo width. Lines are stored as 16-bit numbers
 * (value x 16384) to keep RAM down: ~51 KB for the whole reverb. */
#define RV_NCOMB  8u
#define RV_NAP    4u
#define RV_SPREAD 11u      /* (half rate: 23 samples at 44.1 kHz) */
#define RV_SCALE  16384.0f
#ifndef RV_WET_GAIN
#define RV_WET_GAIN 2.4f     /* wet level at the smallest room; tuned by measurement */
#endif
#ifndef RV_COMP_EXP
#define RV_COMP_EXP 0.6f     /* how strongly bigger rooms are turned down */
#endif
#ifndef RV_DRY_DROP
#define RV_DRY_DROP 0.5f     /* how much the dry sound dips at 100% mix */
#endif
/* The reverb runs at half rate (22.05 kHz): half the work and half the RAM.
 * Freeverb's lengths, halved. Its tail is soft and dark, so nothing is lost. */
static const uint16_t RV_COMB_LEN[RV_NCOMB] = { 557u, 593u, 639u, 677u, 709u, 743u, 779u, 809u };
static const uint16_t RV_AP_LEN[RV_NAP] = { 277u, 221u, 169u, 113u };
#define RV_POOL_LEN (2u * (5506u + 780u) + RV_NCOMB * RV_SPREAD + RV_NAP * RV_SPREAD)
static int16_t g_rv_pool[RV_POOL_LEN];
typedef struct { int16_t *buf; uint16_t len, idx; float damp_state; } RvLine;
static RvLine g_rv_comb[2][RV_NCOMB];
static RvLine g_rv_ap[2][RV_NAP];

static void reverb_init(void) {
    uint32_t at = 0u;
    for (uint32_t ch = 0u; ch < 2u; ch++) {
        uint32_t extra = ch ? RV_SPREAD : 0u;
        for (uint32_t k = 0u; k < RV_NCOMB; k++) {
            RvLine *c = &g_rv_comb[ch][k];
            c->len = (uint16_t)(RV_COMB_LEN[k] + extra);
            c->buf = &g_rv_pool[at];
            c->idx = 0u;
            c->damp_state = 0.0f;
            at += c->len;
        }
        for (uint32_t k = 0u; k < RV_NAP; k++) {
            RvLine *a = &g_rv_ap[ch][k];
            a->len = (uint16_t)(RV_AP_LEN[k] + extra);
            a->buf = &g_rv_pool[at];
            a->idx = 0u;
            at += a->len;
        }
    }
    for (uint32_t i = 0u; i < RV_POOL_LEN; i++) g_rv_pool[i] = 0;
}

static inline __attribute__((always_inline)) int16_t rv_store(float x) {
    float v = x * RV_SCALE;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    return (int16_t)v;
}

/* One sample: mono input in, stereo reverb out (before the wet level) */
static inline __attribute__((always_inline)) void reverb_process(float in, float room, float damp, float *out_l, float *out_r) {
    const float inv = 1.0f / RV_SCALE;
    for (uint32_t ch = 0u; ch < 2u; ch++) {
        float acc = 0.0f;
        for (uint32_t k = 0u; k < RV_NCOMB; k++) {
            RvLine *c = &g_rv_comb[ch][k];
            float y = (float)c->buf[c->idx] * inv;
            c->damp_state = y * (1.0f - damp) + c->damp_state * damp;   /* darker each pass */
            c->buf[c->idx] = rv_store(in + c->damp_state * room);
            if (++c->idx >= c->len) c->idx = 0u;
            acc += y;
        }
        for (uint32_t k = 0u; k < RV_NAP; k++) {
            RvLine *a = &g_rv_ap[ch][k];
            float b = (float)a->buf[a->idx] * inv;
            a->buf[a->idx] = rv_store(acc + b * 0.5f);
            if (++a->idx >= a->len) a->idx = 0u;
            acc = b - acc;
        }
        if (ch == 0u) *out_l = acc; else *out_r = acc;
    }
}

/* ---------------- LED Tracking (Update Only on Change) ---------------- */
static int g_led_state[FM1_NLED];
static uint32_t g_tick_ms = 0u;
static volatile uint32_t g_audio_frames = 0u; /* counted in audio(): the real clock */
static uint32_t g_splash_start_ms = 0u;

static void set_led_if_changed(uint32_t id, int level) {
    if (id >= FM1_NLED) return;
    if (g_led_state[id] != level) {
        g_led_state[id] = level;
        fm1_led_level(id, level);
    }
}

static void update_leds(void) {
    /* Steady lights only: a button is lit when its voice/function is selected,
     * and dark otherwise. No blinking or breathing. */
    set_led_if_changed(FM1_BTN_OCT_DOWN, (g_octave_shift < 0) ? FM1_LED_ON : FM1_LED_OFF);
    set_led_if_changed(FM1_BTN_OCT_UP,   (g_octave_shift > 0) ? FM1_LED_ON : FM1_LED_OFF);

    set_led_if_changed(FM1_BTN_HOME, is_home_page(g_view) ? FM1_LED_ON : FM1_LED_OFF);
    set_led_if_changed(FM1_BTN_FX, (g_view == VIEW_FX_REVERB || g_view == VIEW_FX_DELAY)
                                   ? FM1_LED_ON : FM1_LED_OFF);
    set_led_if_changed(FM1_BTN_SCL, g_hold_on ? FM1_LED_ON : FM1_LED_OFF);
    /* SAVE: lit on its screen; elsewhere a mid glow while the Mellotron flute is in */
    set_led_if_changed(FM1_BTN_SAVE, (g_view == VIEW_ENV_AD) ? FM1_LED_ON :
                                     ((g_mel_level > 0.005f) ? FM1_LED_MID : FM1_LED_OFF));

    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        set_led_if_changed(VOICE_BTN_MAP[v], g_voice_on[v] ? FM1_LED_ON : FM1_LED_OFF);
    }

    set_led_if_changed(FM1_LED_PLAY_RED, FM1_LED_OFF);

    uint32_t notes = fm1_in.notes;
    for (uint32_t n = 0u; n < FM1_NOTE_KEYS; n++) {
        uint32_t key_id = FM1_KEY_NOTE0 + n;
        int is_down = ((notes >> n) & 1u) != 0u;
        set_led_if_changed(key_id, is_down ? FM1_LED_ON : FM1_LED_OFF);
    }
}

/* ---------------- 5x7 Bitmap Font & Sprite Renderer ---------------- */
static const uint8_t FONT_5X7[96][7] = {
    /* 32 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    /* 34 '"' */ {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00},
    /* 35 '#' */ {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},
    /* 36 '$' */ {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
    /* 37 '%' */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    /* 38 '&' */ {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},
    /* 39 '\''*/ {0x04,0x04,0x08,0x00,0x00,0x00,0x00},
    /* 40 '(' */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    /* 41 ')' */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    /* 42 '*' */ {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
    /* 43 '+' */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    /* 44 ',' */ {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
    /* 45 '-' */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    /* 46 '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    /* 47 '/' */ {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    /* 48 '0' */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* 49 '1' */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* 50 '2' */ {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    /* 51 '3' */ {0x1F,0x02,0x04,0x06,0x01,0x11,0x0E},
    /* 52 '4' */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* 53 '5' */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* 54 '6' */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* 55 '7' */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* 56 '8' */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* 57 '9' */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* 58 ':' */ {0x00,0x04,0x04,0x00,0x04,0x04,0x00},
    /* 59 ';' */ {0x00,0x04,0x04,0x00,0x04,0x04,0x08},
    /* 60 '<' */ {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
    /* 61 '=' */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    /* 62 '>' */ {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
    /* 63 '?' */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    /* 64 '@' */ {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
    /* 65 'A' */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 66 'B' */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* 67 'C' */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* 68 'D' */ {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    /* 69 'E' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* 70 'F' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* 71 'G' */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    /* 72 'H' */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 73 'I' */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* 74 'J' */ {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    /* 75 'K' */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* 76 'L' */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* 77 'M' */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* 78 'N' */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* 79 'O' */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 80 'P' */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* 81 'Q' */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* 82 'R' */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* 83 'S' */ {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
    /* 84 'T' */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* 85 'U' */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 86 'V' */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    /* 87 'W' */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* 88 'X' */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* 89 'Y' */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* 90 'Z' */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* 91 '[' */ {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    /* 92 '\\'*/ {0x10,0x10,0x08,0x04,0x02,0x01,0x01},
    /* 93 ']' */ {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
    /* 94 '^' */ {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
    /* 95 '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}
};

static uint16_t g_blit_buf[3000u];

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static void draw_text_blit(uint32_t x, uint32_t y, const char *s,
                           uint16_t fg, uint16_t bg, uint32_t scale) {
    if (!s || scale == 0u || scale > 3u) return;
    uint32_t len = 0u;
    while (s[len] != '\0') len++;
    if (len == 0u) return;

    uint32_t glyph_w = 6u * scale;
    uint32_t glyph_h = 8u * scale;
    uint32_t w = len * glyph_w;
    uint32_t h = glyph_h;
    if (x + w > FM1_LCD_WIDTH || y + h > FM1_LCD_HEIGHT) return;
    if (w * h > 3000u) return;

    for (uint32_t i = 0u; i < w * h; i++) g_blit_buf[i] = bg;

    for (uint32_t ci = 0u; ci < len; ci++) {
        char c = to_upper_ascii(s[ci]);
        uint32_t idx = (c >= 32 && c <= 127) ? (uint32_t)(c - 32) : 0u;
        const uint8_t *g = FONT_5X7[idx];
        uint32_t gx = ci * glyph_w;
        for (uint32_t row = 0u; row < 7u; row++) {
            uint8_t bits = g[row];
            for (uint32_t col = 0u; col < 5u; col++) {
                if ((bits >> (4u - col)) & 1u) {
                    for (uint32_t sy = 0u; sy < scale; sy++) {
                        for (uint32_t sx = 0u; sx < scale; sx++) {
                            uint32_t px = gx + col * scale + sx;
                            uint32_t py = (row * scale) + sy;
                            g_blit_buf[py * w + px] = fg;
                        }
                    }
                }
            }
        }
    }
    fm1_lcd_blit(x, y, w, h, g_blit_buf, w * h);
}

static void draw_retro_star(uint32_t x, uint32_t y, uint16_t col, uint16_t bg) {
    if (x + 6u > FM1_LCD_WIDTH || y + 6u > FM1_LCD_HEIGHT) return;
    for (uint32_t i = 0u; i < 36u; i++) g_blit_buf[i] = bg;
    g_blit_buf[2u] = col; g_blit_buf[3u] = col;
    g_blit_buf[8u] = col; g_blit_buf[9u] = col;
    for (uint32_t c = 0u; c < 6u; c++) {
        g_blit_buf[12u + c] = col;
        g_blit_buf[18u + c] = col;
    }
    g_blit_buf[26u] = col; g_blit_buf[27u] = col;
    g_blit_buf[32u] = col; g_blit_buf[33u] = col;
    fm1_lcd_blit(x, y, 6u, 6u, g_blit_buf, 36u);
}

/* ---------------- Power-On Splash Scene (Flicker-Free Differential Updates) ---------------- */
static void draw_splash_tree(uint32_t x, uint32_t ground_y) {
    fm1_lcd_fill_rect(x + 6u, ground_y - 10u, 4u, 10u, COL_TRUCK_SH);
    fm1_lcd_fill_rect(x + 1u, ground_y - 18u, 14u, 9u, COL_LEAF_SH);
    fm1_lcd_fill_rect(x + 2u, ground_y - 23u, 12u, 7u, COL_LEAF_MD);
    fm1_lcd_fill_rect(x + 4u, ground_y - 26u, 8u, 4u, COL_LEAF_HI);
}

static void draw_splash_big_flower(uint32_t x, uint32_t ground_y) {
    /* Tall stem & leaves (~20px tall) */
    fm1_lcd_fill_rect(x + 6u, ground_y - 12u, 2u, 12u, COL_LEAF_MD);
    fm1_lcd_fill_rect(x + 2u, ground_y - 8u, 4u, 3u, COL_LEAF_HI);
    fm1_lcd_fill_rect(x + 8u, ground_y - 10u, 4u, 3u, COL_LEAF_MD);

    fm1_lcd_fill_rect(x + 5u, ground_y - 21u, 4u, 4u, COL_TEXT);
    fm1_lcd_fill_rect(x + 1u, ground_y - 17u, 4u, 4u, COL_TEXT);
    fm1_lcd_fill_rect(x + 9u, ground_y - 17u, 4u, 4u, COL_TEXT);
    fm1_lcd_fill_rect(x + 5u, ground_y - 13u, 4u, 4u, COL_TEXT);

    fm1_lcd_fill_rect(x + 5u, ground_y - 17u, 4u, 4u, COL_GOLD);
    fm1_lcd_fill_rect(x + 6u, ground_y - 17u, 2u, 2u, COL_GOLD_HI);
}

static void draw_splash_flag(int ripple) {
    /* Erase only the 18x14 flag bounding box before drawing rippled flag */
    fm1_lcd_fill_rect(28u, 115u, 18u, 14u, COL_NIGHT_SKY);
    /* Re-paint bay water strip where flag bottom overlaps y >= 118 */
    fm1_lcd_fill_rect(28u, 118u, 18u, 11u, COL_BTN_OFF_DEF);

    uint32_t fy = (uint32_t)(116 + ripple);
    fm1_lcd_fill_rect(28u, fy, 18u, 4u, COL_INK);
    fm1_lcd_fill_rect(28u, fy + 4u, 18u, 4u, COL_TEXT);
    fm1_lcd_fill_rect(28u, fy + 8u, 18u, 4u, COL_LEAF_MD);
    for (uint32_t r = 0u; r < 12u; r++) {
        int d = (r < 6u) ? (int)(6u - r) : (int)(r - 6u);
        int w = 8 - (d * 8) / 6;
        if (w < 1) w = 1;
        fm1_lcd_fill_rect(28u, fy + r, (uint32_t)w, 1u, COL_APPLE_MD);
    }
}

static void draw_splash_title_below_road(void) {
    /* "Lowrey FM v 1.0" = 15 chars * 12px (scale 2) = 180px wide -> centered at x = 30 */
    fm1_lcd_fill_rect(0u, 188u, 240u, 52u, COL_BG_DEF);
    draw_text_blit(30u, 194u, "Lowrey FM v 1.0", COL_GOLD_HI, COL_BG_DEF, 2u);
}

/* Draws the static background elements of the splash screen ONCE to prevent SPI LCD flashing */
static void draw_splash_static_bg(void) {
    uint16_t sky_col = COL_NIGHT_SKY;
    uint16_t water_col = COL_BTN_OFF_DEF;
    uint16_t gg_red = COL_APPLE_MD;
    uint16_t gg_shade = COL_APPLE_SH;
    uint16_t road_col = COL_STAGE_BG_DEF;
    uint16_t grass_col = COL_LEAF_SH;

    /* Deep dark blue starry sky & bay water band */
    fm1_lcd_fill_rect(0u, 0u, 240u, 118u, sky_col);
    fm1_lcd_fill_rect(0u, 118u, 240u, 34u, water_col);

    /* Crescent moon in upper-left sky */
    fm1_lcd_fill_rect(20u, 16u, 14u, 22u, COL_GOLD);
    fm1_lcd_fill_rect(22u, 18u, 4u, 18u, COL_CREAM);
    fm1_lcd_fill_rect(27u, 17u, 9u, 18u, sky_col);

    /* Golden Gate Bridge spanning across the bay in the background */
    fm1_lcd_fill_rect(0u, 103u, 240u, 4u, gg_red);
    fm1_lcd_fill_rect(0u, 107u, 240u, 2u, gg_shade);

    const uint32_t gg_tx[2] = { 42u, 178u };
    for (uint32_t t = 0u; t < 2u; t++) {
        uint32_t bx = gg_tx[t];
        fm1_lcd_fill_rect(bx, 60u, 4u, 56u, gg_red);
        fm1_lcd_fill_rect(bx + 8u, 60u, 4u, 56u, gg_red);
        fm1_lcd_fill_rect(bx + 3u, 62u, 1u, 54u, gg_shade);
        fm1_lcd_fill_rect(bx, 64u, 12u, 2u, gg_red);
        fm1_lcd_fill_rect(bx, 76u, 12u, 2u, gg_red);
        fm1_lcd_fill_rect(bx, 88u, 12u, 2u, gg_red);
        fm1_lcd_fill_rect(bx, 98u, 12u, 2u, gg_red);
    }

    for (uint32_t x = 4u; x < 236u; x++) {
        int cy = 102;
        if (x < 44u) {
            int dx = 44 - (int)x;
            cy = 62 + (dx * dx) / 48;
        } else if (x <= 188u) {
            int dx = (int)x - 116;
            cy = 63 + (dx * dx) / 132;
        } else {
            int dx = (int)x - 188;
            cy = 62 + (dx * dx) / 56;
        }
        if (cy < 103 && cy >= 60) {
            fm1_lcd_fill_rect(x, (uint32_t)cy, 1u, 2u, gg_red);
            if ((x % 6u) == 0u) {
                fm1_lcd_fill_rect(x, (uint32_t)cy + 2u, 1u, (uint32_t)(103 - cy - 2), gg_shade);
            }
        }
    }

    /* Cozy house on the hillside in the background */
    fm1_lcd_fill_rect(134u, 122u, 3u, 8u, COL_TRUCK_SH);
    fm1_lcd_fill_rect(116u, 132u, 24u, 14u, COL_TRUCK_MD);
    fm1_lcd_fill_rect(114u, 129u, 28u, 3u, COL_APPLE_SH);
    fm1_lcd_fill_rect(118u, 126u, 20u, 3u, COL_APPLE_SH);
    fm1_lcd_fill_rect(122u, 123u, 12u, 3u, COL_APPLE_SH);
    fm1_lcd_fill_rect(120u, 135u, 5u, 5u, COL_GOLD_HI);
    fm1_lcd_fill_rect(130u, 136u, 5u, 10u, COL_TRUCK_SH);

    /* Roadside meadow strip + road + lower frame */
    fm1_lcd_fill_rect(0u, 146u, 240u, 16u, grass_col);
    fm1_lcd_fill_rect(0u, 162u, 240u, 26u, road_col);
    fm1_lcd_fill_rect(0u, 188u, 240u, 52u, COL_BG_DEF);

    /* Trees & giant truck-sized flowers along the meadow */
    draw_splash_tree(6u, 148u);
    draw_splash_tree(94u, 148u);
    draw_splash_tree(218u, 148u);

    draw_splash_big_flower(54u, 150u);
    draw_splash_big_flower(154u, 150u);
    draw_splash_big_flower(196u, 150u);

    /* Flagpole */
    fm1_lcd_fill_rect(26u, 114u, 2u, 34u, COL_TEXT);

    /* Title underneath the road (no white screen before the truck) */
    draw_splash_title_below_road();
}

/* Updates only the animated regions on the splash screen (stars, flag, truck + road) */
static void draw_splash_dynamic(uint32_t anim_step, uint32_t truck_step, uint32_t total_steps) {
    uint16_t sky_col = COL_NIGHT_SKY;
    uint16_t road_col = COL_STAGE_BG_DEF;
    uint16_t grass_col = COL_LEAF_SH;

    /* 1. Twinkling star field (1px updates, zero erase flash) */
    static const uint8_t STAR_XY[16][2] = {
        {14, 14}, {36, 26}, {58, 12}, {82, 30},
        {104, 16}, {126, 24}, {148, 10}, {168, 28},
        {24, 44}, {66, 48}, {114, 42}, {158, 46},
        {42, 62}, {94, 58}, {140, 64}, {176, 54}
    };
    for (uint32_t s = 0u; s < 16u; s++) {
        uint16_t sc = ((s + anim_step / 4u) % 3u == 0u) ? COL_GOLD_HI : COL_TEXT;
        fm1_lcd_fill_rect(STAR_XY[s][0], STAR_XY[s][1], 1u, 1u, sc);
    }

    /* 2. Twinkling star near moon (5x5 bounding box clear + draw) */
    fm1_lcd_fill_rect(40u, 20u, 5u, 5u, sky_col);
    uint32_t tw = (anim_step / 3u) % 4u;
    if (tw == 0u) {
        fm1_lcd_fill_rect(42u, 22u, 1u, 1u, COL_GOLD);
    } else if (tw == 1u || tw == 3u) {
        fm1_lcd_fill_rect(42u, 21u, 1u, 3u, COL_GOLD_HI);
        fm1_lcd_fill_rect(41u, 22u, 3u, 1u, COL_GOLD_HI);
    } else {
        fm1_lcd_fill_rect(42u, 20u, 1u, 5u, COL_GOLD_HI);
        fm1_lcd_fill_rect(40u, 22u, 5u, 1u, COL_GOLD_HI);
    }

    /* 3. Waving flag */
    int ripple = (int)((anim_step / 3u) % 2u);
    draw_splash_flag(ripple);

    /* 4. Restore road strip & bottom 4px of grass where truck/smoke drive, then redraw truck */
    fm1_lcd_fill_rect(0u, 158u, 240u, 4u, grass_col);
    fm1_lcd_fill_rect(0u, 162u, 240u, 26u, road_col);

    for (uint32_t dx = 8u; dx < 232u; dx += 24u) {
        fm1_lcd_fill_rect(dx, 174u, 12u, 2u, COL_GOLD_HI);
    }

    uint32_t span = 280u;
    uint32_t prog = (total_steps > 0u) ? ((truck_step * span) / total_steps) : 0u;
    if (prog > span) prog = span;
    int tx_draw = (int)prog - 36;
    uint32_t ty_draw = 158u;

    /* Multiple puffs of smoke trailing behind the truck */
    for (int p = 0; p < 3; p++) {
        uint32_t phase = (anim_step + (uint32_t)p * 2u) % 6u;
        int sx = tx_draw - 5 - p * 8 - (int)phase;
        int sy = (int)ty_draw + 9 - p * 3 - (int)(phase / 2u);
        uint32_t sz = (p == 0) ? 3u : ((p == 1) ? 4u : 2u);
        if (sx >= 0 && sx + (int)sz < 240 && sy >= 158 && sy + (int)sz < 186) {
            fm1_lcd_fill_rect((uint32_t)sx, (uint32_t)sy, sz, sz,
                              (p == 2) ? COL_TEXT : COL_SMOKE);
        }
    }

    /* Draw brown pickup truck body & wheels clipped cleanly to screen */
    if (tx_draw > -36 && tx_draw < 240) {
        int x0 = tx_draw;
        if (x0 + 18 >= 0 && x0 + 18 < 240) {
            uint32_t cx = (uint32_t)(x0 + 18);
            uint32_t cw = (cx + 10u > 240u) ? (240u - cx) : 10u;
            fm1_lcd_fill_rect(cx, ty_draw, cw, 8u, COL_TRUCK_MD);
            if (cw > 3u) {
                uint32_t ww = (cw > 7u) ? 5u : (cw - 2u);
                fm1_lcd_fill_rect(cx + 2u, ty_draw + 2u, ww, 4u, COL_TEXT);
            }
        }
        if (x0 + 30 > 0 && x0 < 240) {
            uint32_t bx = (x0 < 0) ? 0u : (uint32_t)x0;
            uint32_t bw = (x0 < 0) ? (uint32_t)(30 + x0) : 30u;
            if (bx + bw > 240u) bw = 240u - bx;
            fm1_lcd_fill_rect(bx, ty_draw + 7u, bw, 9u, COL_TRUCK_MD);
            fm1_lcd_fill_rect(bx, ty_draw + 8u, (bw > 10u) ? 10u : bw, 2u, COL_TRUCK_HI);
        }
        if (x0 + 30 >= 0 && x0 + 31 < 240) {
            fm1_lcd_fill_rect((uint32_t)(x0 + 30), ty_draw + 9u, 2u, 3u, COL_GOLD_HI);
        }
        if (x0 + 4 >= 0 && x0 + 9 < 240) {
            fm1_lcd_fill_rect((uint32_t)(x0 + 4), ty_draw + 15u, 6u, 6u, COL_INK);
            fm1_lcd_fill_rect((uint32_t)(x0 + 6), ty_draw + 17u, 2u, 2u, COL_TEXT);
        }
        if (x0 + 20 >= 0 && x0 + 25 < 240) {
            fm1_lcd_fill_rect((uint32_t)(x0 + 20), ty_draw + 15u, 6u, 6u, COL_INK);
            fm1_lcd_fill_rect((uint32_t)(x0 + 22), ty_draw + 17u, 2u, 2u, COL_TEXT);
        }
    }

}

/* ---------------- Wobbling Golden Gate (truck page) ----------------
 * Redraws only the bridge band (y 56..113) in 12-row strips, so every pixel
 * is written once (no flicker). The deck and cables sway between the towers;
 * the towers stay put. Flat when amp is 0. */
#define BRIDGE_Y0    56
#define BRIDGE_ROWS  62   /* y 56..117, just above the water */
#define BRIDGE_STRIP 12

static float g_bridge_amp = 0.0f;   /* current sway in pixels */
static float g_bridge_ph = 0.0f;    /* sway phase (radians) */
static float g_truck_pos = 0.0f;    /* truck position in animation steps */
static int g_bridge_flat = 1;       /* last drawn state was flat */

static int bridge_deck_dy(int x, float amp, float ph) {
    const float PI = 3.14159265f;
    float d = 0.0f;
    if (x > 53 && x < 178) {            /* main span between the towers */
        float u = (float)(x - 54) / 124.0f;
        d = 0.75f * sinf(PI * u) * sinf(ph) + 0.40f * sinf(2.0f * PI * u) * sinf(2.0f * ph + 0.8f);
    } else if (x < 42) {                /* left side span */
        float u = (float)x / 42.0f;
        d = 0.35f * sinf(PI * u) * sinf(ph + 1.5f);
    } else if (x > 189) {               /* right side span */
        float u = (float)(x - 190) / 50.0f;
        d = 0.35f * sinf(PI * u) * sinf(ph + 2.5f);
    }
    int dy = (int)floorf(d * amp + 0.5f);
    if (dy > 9) dy = 9;       /* deck bottom stays above the water (y 117) */
    if (dy < -12) dy = -12;
    return dy;
}

static int bridge_cable_y(int x) {     /* same curve as the static scene, -1 = none */
    if (x < 4 || x >= 236) return -1;
    int cy;
    if (x < 44) {
        int dx = 44 - x;
        cy = 62 + (dx * dx) / 48;
    } else if (x <= 188) {
        int dx = x - 116;
        cy = 63 + (dx * dx) / 132;
    } else {
        int dx = x - 188;
        cy = 62 + (dx * dx) / 56;
    }
    return (cy < 103 && cy >= 60) ? cy : -1;
}

static void draw_bridge_band(float amp, float ph) {
    static int8_t deck_dy[240];
    static int16_t cable_y[240];
    const uint16_t sky = COL_NIGHT_SKY, red = COL_APPLE_MD, shade = COL_APPLE_SH;
    const int tower_x[2] = { 42, 178 };

    for (int x = 0; x < 240; x++) {
        int dy = bridge_deck_dy(x, amp, ph);
        deck_dy[x] = (int8_t)dy;
        int cy = bridge_cable_y(x);
        /* cables move more the lower they hang (near the deck) */
        cable_y[x] = (int16_t)((cy < 0) ? -1 : cy + (dy * (cy - 60)) / 43);
    }

    for (int r0 = 0; r0 < BRIDGE_ROWS; r0 += BRIDGE_STRIP) {
        int rows = (r0 + BRIDGE_STRIP <= BRIDGE_ROWS) ? BRIDGE_STRIP : (BRIDGE_ROWS - r0);
        int y_top = BRIDGE_Y0 + r0;
        int y_end = y_top + rows;
        for (int i = 0; i < 240 * rows; i++) g_blit_buf[i] = sky;

        #define BPX(px, py, c) do { int _y = (py); \
            if (_y >= y_top && _y < y_end) g_blit_buf[(_y - y_top) * 240 + (px)] = (c); } while (0)

        /* deck: 4 rows red + 2 rows shade */
        for (int x = 0; x < 240; x++) {
            int d = deck_dy[x];
            for (int y = 103; y < 107; y++) BPX(x, y + d, red);
            BPX(x, 107 + d, shade);
            BPX(x, 108 + d, shade);
        }
        /* towers (static) */
        for (int t = 0; t < 2; t++) {
            int bx = tower_x[t];
            for (int y = 60; y < 116; y++) {
                for (int k = 0; k < 4; k++) { BPX(bx + k, y, red); BPX(bx + 8 + k, y, red); }
                if (y >= 62) BPX(bx + 3, y, shade);
            }
            const int bars[4] = { 64, 76, 88, 98 };

            for (int b = 0; b < 4; b++)
                for (int k = 0; k < 12; k++) { BPX(bx + k, bars[b], red); BPX(bx + k, bars[b] + 1, red); }
        }
        /* flagpole top passes through the band (the flag itself is redrawn after) */
        for (int y = 114; y < 118; y++) { BPX(26, y, COL_TEXT); BPX(27, y, COL_TEXT); }

        /* main cables + hangers down to the (moving) deck */
        for (int x = 4; x < 236; x++) {
            int cy = cable_y[x];
            if (cy < 0) continue;
            BPX(x, cy, red);
            BPX(x, cy + 1, red);
            if ((x % 6) == 0) {
                for (int y = cy + 2; y < 103 + deck_dy[x]; y++) BPX(x, y, shade);
            }
        }
        #undef BPX
        fm1_lcd_blit(0u, (uint32_t)y_top, 240u, (uint32_t)rows, g_blit_buf, 240u * (uint32_t)rows);
    }
}

/* Dark blue button tabs on warm brown screen with smooth single-blit icon updates */
static void draw_voice_card_frame(uint32_t v, uint32_t x, uint32_t y, int is_on) {
    const uint32_t W = 58u;
    const uint32_t H = 60u;
    if (x + W > FM1_LCD_WIDTH || y + H > FM1_LCD_HEIGHT) return;

    uint16_t card_bg = is_on ? COL_BTN_ON : COL_BTN_OFF;
    uint16_t border = is_on ? (g_bank ? VOICE_COLORS_B[v] : VOICE_COLORS[v]) : COL_BORDER;

    fm1_lcd_fill_rect(x, y, W, H, border);
    if (is_on) {
        fm1_lcd_fill_rect(x + 2u, y + 2u, W - 4u, H - 4u, COL_GOLD_HI);
        fm1_lcd_fill_rect(x + 3u, y + 3u, W - 6u, H - 6u, card_bg);
    } else {
        fm1_lcd_fill_rect(x + 2u, y + 2u, W - 4u, H - 4u, card_bg);
    }

    char short_lbl[8];
    const char *src = g_bank ? VOICE_NAMES_B[v] : VOICE_NAMES[v];
    uint32_t k = 0u;
    while (src[k] != '\0' && k < 7u) {
        short_lbl[k] = src[k];
        k++;
    }
    short_lbl[k] = '\0';

    uint16_t lbl_col;
    if (is_light_color(card_bg)) {
        lbl_col = is_on ? COL_INK : 0x4208u;
    } else {
        lbl_col = is_on ? COL_TEXT : COL_DIM;
    }
    draw_text_blit(x + 5u, y + 46u, short_lbl, lbl_col, card_bg, 1u);
}

/* ---- small drawing helpers for the bank B icons (42 x 40 icon buffer) ---- */
#define IC_W 42
#define IC_H 40
static void ic_px(int x, int y, uint16_t c) {
    if (x >= 0 && x < IC_W && y >= 0 && y < IC_H) g_blit_buf[y * IC_W + x] = c;
}
static void ic_box(int x, int y, int w, int h, uint16_t c) {
    for (int yy = y; yy < y + h; yy++) for (int xx = x; xx < x + w; xx++) ic_px(xx, yy, c);
}
/* filled ellipse */
static void ic_disc(int cx, int cy, int rx, int ry, uint16_t c) {
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    for (int dy = -ry; dy <= ry; dy++)
        for (int dx = -rx; dx <= rx; dx++)
            if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry) ic_px(cx + dx, cy + dy, c);
}
/* thick line (a capsule of width w) */
static void ic_line(float x0, float y0, float x1, float y1, float w, uint16_t c) {
    float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    int xa = (int)((x0 < x1 ? x0 : x1) - w), xb = (int)((x0 > x1 ? x0 : x1) + w);
    int ya = (int)((y0 < y1 ? y0 : y1) - w), yb = (int)((y0 > y1 ? y0 : y1) + w);
    float r2 = 0.25f * w * w;
    for (int y = ya; y <= yb; y++) {
        for (int x = xa; x <= xb; x++) {
            float t = len2 > 0.0f ? (((float)x - x0) * vx + ((float)y - y0) * vy) / len2 : 0.0f;
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            float dx = (float)x - (x0 + t * vx), dy = (float)y - (y0 + t * vy);
            if (dx * dx + dy * dy <= r2) ic_px(x, y, c);
        }
    }
}

static int g_fire = 0;             /* WARM icon: 0 = calm ... 8 = ablaze (burns while notes play) */
static uint32_t g_fire_ms = 0u;
static int g_pk_morph = 0;          /* POCKET icon: 0 = sun ... 8 = moon (moon while notes play) */
static uint32_t g_pk_ms = 0u;      /* when it last moved a step */

static void draw_bank_b_icon(uint32_t v, int is_on, int step, uint16_t card_bg) {
    const uint16_t muted = 0x8410u;
    const uint16_t dark = is_on ? COL_INK : COL_BORDER;
    #define ON(c) (is_on ? (c) : muted)

    if (v == VB_LOWREY) {                       /* an eye that blinks */
        static const int OPEN[4] = { 8, 4, 0, 4 };
        int o = OPEN[step];
        ic_disc(21, 20, 15, 9, ON(COL_WORM));                    /* lids */
        if (o > 0) {
            ic_disc(21, 20, 14, o, ON(COL_TEXT));                /* white */
            for (int dy = -o; dy <= o; dy++)                     /* iris + pupil, clipped by the lids */
                for (int dx = -5; dx <= 5; dx++) {
                    int d2 = dx * dx + dy * dy;
                    if (d2 <= 25) ic_px(21 + dx, 20 + dy, d2 <= 5 ? dark : ON(COL_BUBBLE_SH));
                }
            ic_px(23, 18, ON(COL_CREAM));
            ic_px(23, 17, ON(COL_CREAM));
        }
        for (int x = 7; x <= 35; x++) ic_px(x, 20 - o - 1 + ((x - 21) * (x - 21)) / (o > 0 ? 60 : 1000), dark);   /* lid line */
        for (int k = 0; k < 5; k++) {                            /* lashes */
            int lx = 11 + k * 5, ly = 20 - o - 2;
            if (o == 0) ly = 21;
            ic_px(lx, ly, dark);
            ic_px(lx - 1, ly + (o == 0 ? 1 : -1), dark);
        }
        if (is_on && step > 0) {                                 /* a tear, rolling down to the left */
            static const int TX[4] = { 0, 6, 4, 2 };
            static const int TY[4] = { 0, 22, 27, 32 };
            int tx = TX[step], ty = TY[step];
            if (step >= 2) {                                     /* wet trail from the corner */
                for (int k = 0; k < step - 1; k++) ic_px(7 - k * 2, 24 + k * 5, COL_BUBBLE_SH);
            }
            ic_px(tx + 2, ty, COL_BUBBLE_MD);                    /* tip */
            ic_box(tx + 1, ty + 1, 3, 1, COL_BUBBLE_MD);
            ic_box(tx, ty + 2, 5, 3, COL_BUBBLE_MD);             /* round bottom */
            ic_box(tx + 1, ty + 5, 3, 1, COL_BUBBLE_MD);
            ic_box(tx + 1, ty + 2, 3, 3, COL_BUBBLE_HI);         /* water */
            ic_px(tx + 2, ty + 1, COL_BUBBLE_HI);
            ic_px(tx + 1, ty + 2, COL_TEXT);                     /* glint */
        }
    } else if (v == VB_SID) {                   /* a floppy disk that glitches */
        uint16_t body = is_on ? 0x4A69u : COL_INK;
        ic_box(8, 5, 26, 28, body);
        ic_box(9, 6, 24, 26, is_on ? 0x6B4Du : COL_INK);
        ic_box(14, 5, 14, 10, ON(COL_DIM));                      /* metal shutter */
        ic_box(22, 7, 3, 6, body);
        ic_box(12, 19, 18, 12, ON(COL_TEXT));                    /* label */
        for (int k = 0; k < 3; k++) ic_box(14, 22 + k * 3, 14, 1, ON(COL_BUBBLE_SH));
        ic_px(10, 30, ON(COL_DIM));
        if (is_on && step > 0) {                                 /* glitch bands */
            static const uint16_t GC[3] = { COL_APPLE_MD, COL_LEAF_HI, COL_BUBBLE_HI };
            for (int k = 0; k < 3; k++) {
                int gy = 6 + ((step * 7 + k * 11) % 24);
                int gx = 4 + ((step * 5 + k * 9) % 14);
                ic_box(gx, gy, 10 + k * 3, 2, GC[(k + step) % 3]);
            }
            for (int k = 0; k < 6; k++) ic_px(2 + ((k * 13 + step * 7) % 38), 2 + ((k * 7 + step * 11) % 36), GC[k % 3]);
        }
    } else if (v == VB_POCKET) {                /* a smiling sun that turns into a smiling moon while you play */
        int m = g_pk_morph;                                      /* 0 = sun ... 8 = moon */
        static const uint16_t BODY[9] = { COL_GOLD, COL_GOLD, COL_GOLD, COL_GOLD_HI, COL_GOLD_HI,
                                          COL_CREAM, COL_CREAM, COL_CREAM, COL_CREAM };
        static const uint16_t FACE[9] = { COL_GOLD_HI, COL_GOLD_HI, COL_GOLD_HI, COL_GOLD_HI, COL_CREAM,
                                          COL_CREAM, COL_CREAM, COL_CREAM, COL_CREAM };
        static const int CUT[9] = { 0, 0, 0, 0, 19, 16, 13, 10, 8 };   /* the night's shadow sliding over */
        if (m < 4) {                                             /* rays (shrinking as it sets) */
            static const int DX[8] = { 2, 1, 0, -1, -2, -1, 0, 1 };
            static const int DY[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };
            for (int k = 0; k < 8; k++) {
                static const int LONG[4] = { 6, 5, 3, 1 }, SHORT[4] = { 3, 2, 1, 0 };
                int len = (k & 1) ? SHORT[m] : LONG[m];
                if (len == 0) continue;
                float ux = (float)DX[k] * 0.5f, uy = (float)DY[k] * 0.5f;
                if (DX[k] != 0 && DY[k] != 0) { ux *= 1.41f; uy *= 1.41f; }
                ic_line(21.0f + ux * 12.0f, 20.0f + uy * 12.0f, 21.0f + ux * (12.0f + (float)len), 20.0f + uy * (12.0f + (float)len),
                        2.0f, ON(COL_GOLD));
            }
        }
        ic_disc(21, 20, 10, 10, ON(BODY[m]));
        ic_disc(21, 20, 9, 9, ON(FACE[m]));
        if (CUT[m] > 0) ic_disc(21 + CUT[m], 18, 9, 10, card_bg);   /* bite out a crescent */
        if (m < 6) {                                             /* the sun's face */
            ic_box(17, 17, 2, 2, dark);
            if (21 + CUT[m] - 9 > 25 || CUT[m] == 0) ic_box(24, 17, 2, 2, dark);
            if (is_on && m < 2) { ic_px(15, 22, COL_APPLE_HI); ic_px(16, 22, COL_APPLE_HI); ic_px(26, 22, COL_APPLE_HI); ic_px(27, 22, COL_APPLE_HI); }
            for (int x = 17; x <= 25; x++) {                     /* smile */
                if (CUT[m] > 0 && x > 21 + CUT[m] - 10) break;
                ic_px(x, 23 + (((x - 21) * (x - 21)) <= 4 ? 1 : 0), dark);
            }
        } else {                                                 /* the moon turns to the side: a smiling profile */
            /* find the crescent's inner edge on each row, then grow his
             * profile out of it, facing right: brow, a long nose, lips, chin */
            int edge[16];
            for (int k = 0; k < 16; k++) {
                int x = 16, y = 11 + k;
                while (x < IC_W && g_blit_buf[y * IC_W + x] != card_bg) x++;
                edge[k] = x - 1;
            }
            static const int8_t PROF_D[16] = { 0, 0, 1, 0, 0, 1, 2, 3, 4, 1, 1, 0, 1, 1, 2, 1 };   /* rows 11-26 */
            for (int k = 0; k < 16; k++)
                if (PROF_D[k] > 0) ic_box(edge[k] + 1, 11 + k, PROF_D[k], 1, ON(COL_CREAM));
            int e15 = edge[4], e22 = edge[11];
            ic_px(e15 - 3, 15, dark); ic_px(e15 - 2, 14, dark); ic_px(e15 - 1, 15, dark);   /* closed, happy eye */
            ic_px(e22, 22, dark); ic_px(e22 - 1, 22, dark); ic_px(e22 - 2, 21, dark);       /* smiling mouth */
            if (is_on) { ic_px(e22 - 4, 19, COL_APPLE_HI); ic_px(e22 - 5, 19, COL_APPLE_HI); }   /* blush */
            if (is_on) {                                         /* twinkling stars */
                static const int8_t SX[3][2] = { { 33, 8 }, { 36, 22 }, { 29, 31 } };
                for (int k = 0; k < 3; k++) {
                    int sx = SX[k][0], sy = SX[k][1];
                    uint16_t c = ((k + step) & 1) ? COL_GOLD_HI : COL_GOLD;
                    ic_px(sx, sy, c);
                    if (((k + step) & 1) || m < 8) continue;          /* stars grow as night falls */
                    ic_px(sx - 1, sy, c); ic_px(sx + 1, sy, c); ic_px(sx, sy - 1, c); ic_px(sx, sy + 1, c);
                }
            }
        }
    } else if (v == VB_PS20) {                  /* three rolling waves */
        static const uint16_t WC_ON[3] = { COL_BUBBLE_HI, COL_BUBBLE_MD, COL_BUBBLE_SH };
        static const int8_t SINE14[14] = { 0, 1, 2, 2, 2, 1, 0, 0, -1, -2, -2, -2, -1, 0 };
        for (int w = 0; w < 3; w++) {
            int base = 11 + w * 9;
            int shift = (step * 3 + w * 4) % 14;
            for (int x = 3; x <= 38; x++) {
                int y = base + SINE14[(x + shift) % 14];
                ic_box(x, y, 1, 3, is_on ? WC_ON[w] : (w == 1 ? COL_BORDER : muted));
                if (is_on && SINE14[(x + shift) % 14] == -2 && ((x + shift) % 14) == 10) ic_px(x, y - 1, COL_TEXT);   /* foam */
            }
        }
    } else if (v == VB_MBOX) {               /* a rose shedding its petals */
        ic_line(21.0f, 18.0f, 21.0f, 37.0f, 2.0f, ON(COL_LEAF_MD));
        ic_disc(15, 27, 4, 2, ON(COL_LEAF_HI));
        ic_disc(27, 31, 4, 2, ON(COL_LEAF_MD));
        ic_px(22, 24, ON(COL_LEAF_HI));
        /* the bloom gets thinner as it sheds */
        static const int BR[4] = { 8, 7, 6, 5 };
        int br = BR[step];
        ic_disc(21, 12, br, br - 1, ON(COL_APPLE_SH));
        ic_disc(21, 11, br - 1, br - 2, ON(COL_APPLE_MD));
        ic_disc(21, 10, 4, 3, ON(COL_APPLE_SH));
        ic_disc(20, 9, 2, 2, ON(COL_APPLE_MD));
        ic_px(17, 8, ON(COL_APPLE_HI));
        if (is_on && step > 0) {
            /* petals: x, y for steps 1..3 (tumbling wide, landing in a pile) */
            static const int8_t PX[4][3] = { { 30, 33, 35 }, { 9, 6, 4 }, { 27, 31, 30 }, { 13, 10, 12 } };
            static const int8_t PY[4][3] = { { 10, 20, 33 }, { 12, 22, 34 }, { 6, 15, 26 }, { 7, 17, 28 } };
            for (int k = 0; k < 4; k++) {
                if (k >= 2 && step == 1) continue;               /* more let go each beat */
                int px = PX[k][step - 1], py = PY[k][step - 1];
                int flip = (step + k) & 1;                       /* tumbling */
                if (flip) {
                    ic_box(px, py, 4, 2, ON(COL_APPLE_MD));
                    ic_px(px + 1, py - 1, ON(COL_APPLE_HI));
                    ic_px(px + 3, py + 2, ON(COL_APPLE_SH));
                } else {
                    ic_box(px + 1, py - 1, 2, 4, ON(COL_APPLE_MD));
                    ic_px(px, py, ON(COL_APPLE_HI));
                    ic_px(px + 3, py + 1, ON(COL_APPLE_SH));
                }
            }
            if (step == 3) {                                     /* fallen petals at the foot */
                ic_box(15, 37, 3, 2, ON(COL_APPLE_SH));
                ic_box(25, 37, 3, 2, ON(COL_APPLE_MD));
            }
        }
    } else if (v == VB_PHONE) {                 /* a rotary phone; the handset gets picked up */
        uint16_t body = ON(COL_APPLE_MD), body_sh = ON(COL_APPLE_SH), body_hi = ON(COL_APPLE_HI);
        /* body: a sloped box */
        ic_box(7, 22, 28, 14, body_sh);
        ic_box(9, 19, 24, 15, body);
        ic_box(10, 19, 22, 2, body_hi);
        ic_box(6, 35, 30, 2, body_sh);                           /* feet */
        /* dial: a cream disc with finger holes */
        ic_disc(21, 27, 7, 6, ON(COL_CREAM));
        ic_disc(21, 27, 2, 2, body_sh);
        static const int8_t HX[8] = { 0, 4, 5, 4, 0, -4, -5, -4 };
        static const int8_t HY[8] = { -4, -3, 0, 3, 4, 3, 0, -3 };
        for (int k = 0; k < 8; k++) ic_px(21 + HX[k], 27 + HY[k], dark);
        /* cradle prongs */
        ic_box(9, 15, 3, 4, body_sh);
        ic_box(30, 15, 3, 4, body_sh);
        /* handset: rests on the cradle, then lifts and tilts */
        static const int LIFT[4] = { 0, 3, 7, 9 };
        static const int TILT[4] = { 0, 1, 3, 4 };
        int lift = is_on ? LIFT[step] : 0, tilt = is_on ? TILT[step] : 0;
        float y0 = 13.0f - (float)lift, y1 = 13.0f - (float)lift + (float)tilt;
        ic_line(10.0f, y0, 32.0f, y1 - (float)tilt * 2.0f, 4.0f, body_sh);        /* handle */
        ic_disc(9, (int)y0 + 1, 4, 3, body);                                     /* earpiece */
        ic_disc(33, (int)(y1 - (float)tilt * 2.0f) + 1, 4, 3, body);            /* mouthpiece */
        ic_line(10.0f, y0 - 1.0f, 32.0f, y1 - (float)tilt * 2.0f - 1.0f, 1.5f, body_hi);
        /* the curly cord, stretching as it lifts */
        for (int k = 0; k < 4; k++) ic_px(6 - (k & 1), 18 + k * 3 - lift / 2, ON(COL_DIM));
        if (is_on && step >= 2) {                                /* a little sound from the earpiece */
            ic_px(3, (int)y0 - 3, COL_TEXT);
            ic_px(2, (int)y0 - 5, COL_TEXT);
            ic_px(5, (int)y0 - 6, COL_TEXT);
        }
    } else if (v == VB_WARM) {                  /* a police car that slowly catches fire while you play */
        const uint16_t blk = ON(0x2945u), wht = ON(COL_TEXT);
        int f = is_on ? g_fire : 0;                              /* 0 = calm ... 8 = ablaze */
        int burn = (f >= 3);
        ic_box(4, 25, 34, 7, blk);                               /* body: black hood and trunk */
        ic_box(5, 24, 32, 1, blk);
        ic_box(13, 25, 15, 6, wht);                              /* white doors */
        ic_box(12, 18, 17, 7, blk);                              /* cabin */
        ic_box(14, 19, 6, 5, ON(COL_BUBBLE_SH));                 /* windows */
        ic_box(21, 19, 6, 5, ON(COL_BUBBLE_SH));
        ic_px(20, 27, ON(COL_GOLD)); ic_px(19, 28, ON(COL_GOLD)); ic_px(21, 28, ON(COL_GOLD));   /* star badge */
        ic_box(36, 26, 2, 2, ON(COL_GOLD_HI));                   /* headlight */
        ic_box(4, 26, 1, 2, ON(COL_APPLE_MD));                   /* tail light */
        for (int k = 0; k < 2; k++) {                            /* wheels */
            int wx = k ? 30 : 11;
            ic_disc(wx, 32, 4, 4, ON(COL_INK));
            ic_disc(wx, 32, 2, 2, ON(COL_DIM));
        }
        /* first a little puff of smoke from under the hood ... */
        if (f == 1) ic_disc(32, 21, 2, 2, 0x8C51u);
        if (f == 2) { ic_disc(33, 17, 3, 2, 0x8C51u); ic_disc(31, 21, 1, 1, COL_SMOKE); ic_px(33, 24, COL_GOLD); }
        if (f >= 3) {                                            /* ... then smoke rolling up above the fire */
            int sm = f >= 6 ? 4 : 3;
            ic_disc(24 + step, 5 - (step & 1) + (8 - f), sm, sm - 1, COL_SMOKE);
            if (f >= 5) ic_disc(16 - step, 8 + (8 - f), 3, 2, 0x8C51u);
        }
        /* ... and the flames grow: the hood first, then the cabin */
        static const int TOP[4] = { 6, 3, 7, 2 };
        static const int TIP[4] = { 0, 2, -1, 1 };
        for (int fl = 0; fl < 2; fl++) {
            float g = fl ? (float)(f - 2) / 4.0f : (float)(f - 4) / 4.0f;
            if (g <= 0.0f) continue;
            if (g > 1.0f) g = 1.0f;
            int cx0 = fl ? 32 : 20, base = fl ? 25 : 21, wmax = fl ? 5 : 7;
            int top_full = TOP[(step + fl) & 3] + fl * 6;
            int top = base - (int)((float)(base - top_full) * g + 0.5f);
            int tip = (g >= 1.0f) ? TIP[(step + fl * 2) & 3] : 0;
            float wsc = 0.4f + 0.6f * g;
            for (int layer = 0; layer < 3; layer++) {
                uint16_t c = layer == 0 ? ON(COL_APPLE_MD) : (layer == 1 ? ON(COL_GOLD) : ON(COL_GOLD_HI));
                int t = top + layer * 3, w = (int)((float)(wmax - layer * 2) * wsc + 0.5f);
                if (w < 1) w = 1;
                if (t > base - layer) break;
                for (int y = t; y <= base - layer; y++) {
                    float fr = (float)(y - t) / (float)(base - layer - t + 1);
                    int hw = (int)((float)w * (fr < 0.7f ? fr / 0.7f : 1.0f) + 0.5f);
                    int cx = cx0 + (int)((float)tip * (1.0f - fr) + 0.5f);
                    ic_box(cx - hw, y, 2 * hw + 1, 1, c);
                }
            }
        }
        if (f >= 7 && (step & 1)) { ic_px(11, 9 + step, COL_GOLD_HI); ic_px(37, 12 + step, COL_GOLD); }   /* sparks */
        int flash = burn ? (step & 1) : 0;                       /* the light bar, flashing once it burns */
        ic_box(16, 16, 4, 2, flash ? ON(COL_APPLE_HI) : ON(COL_APPLE_SH));
        ic_box(21, 16, 4, 2, flash ? ON(0x2B5Fu) : ON(0x65FFu));
        if (burn) ic_px(flash ? 17 : 23, 15, flash ? COL_APPLE_HI : 0x65FFu);
    } else {                                    /* VB_HIGH: a tipped-over bottle, spilling */
        ic_disc(9, 10, 6, 6, ON(COL_LEAF_SH));                             /* round bottom */
        ic_line(9.0f, 10.0f, 20.0f, 19.0f, 12.0f, ON(COL_LEAF_SH));        /* body */
        ic_line(9.0f, 10.0f, 20.0f, 19.0f, 9.0f, ON(COL_LEAF_MD));
        ic_line(19.0f, 18.0f, 24.0f, 22.0f, 7.0f, ON(COL_LEAF_MD));        /* shoulder */
        ic_line(23.0f, 22.0f, 29.0f, 27.0f, 4.0f, ON(COL_LEAF_MD));        /* neck */
        ic_line(27.5f, 29.5f, 31.0f, 25.5f, 2.5f, ON(COL_LEAF_HI));        /* lip */
        ic_line(7.0f, 6.0f, 19.0f, 15.0f, 1.5f, ON(COL_LEAF_HI));          /* glint */
        ic_line(16.2f, 11.3f, 11.8f, 16.7f, 4.0f, ON(COL_CREAM));          /* label */
        if (is_on && step > 0) {
            int wob = (step & 1);
            ic_line(31.0f, 29.0f, 32.0f + (float)wob, 35.0f, 2.0f, COL_BUBBLE_HI);   /* the pour */
            ic_px(29 + step, 33 - step, COL_BUBBLE_HI);                              /* splashes */
            ic_px(35 - wob, 34 - step, COL_BUBBLE_HI);
        }
        int pr = is_on ? 4 + step * 2 : 4;                                 /* puddle */
        ic_disc(33, 37, pr, 2, is_on ? COL_BUBBLE_MD : COL_BORDER);
        ic_disc(33, 37, pr - 1, 1, is_on ? COL_BUBBLE_HI : muted);
    }
    (void)card_bg;
    #undef ON
}

/* Updates ONLY the 42x40 icon bitmap inside the card via a single atomic SPI blit (no border/label flash) */
static void draw_voice_card_icon(uint32_t v, uint32_t x, uint32_t y,
                                 int is_on, int anim_phase) {
    uint16_t card_bg = is_on ? COL_BTN_ON : COL_BTN_OFF;
    uint16_t muted = 0x8410u;

    const uint32_t IW = 42u;
    const uint32_t IH = 40u;
    for (uint32_t i = 0u; i < IW * IH; i++) g_blit_buf[i] = card_bg;

    #define PUT_PX(px, py, c) do { \
        int _x = (px), _y = (py); \
        if (_x >= 0 && _x < (int)IW && _y >= 0 && _y < (int)IH) \
            g_blit_buf[(uint32_t)_y * IW + (uint32_t)_x] = (c); \
    } while (0)

    #define FILL_BOX(bx, by, bw, bh, c) do { \
        for (int _yy = (by); _yy < (by) + (bh); _yy++) \
            for (int _xx = (bx); _xx < (bx) + (bw); _xx++) \
                PUT_PX(_xx, _yy, (c)); \
    } while (0)

    int play_anim = (is_on && anim_phase > 0);
    int step = play_anim ? ((anim_phase - 1) % 4) : 0;

    if (g_bank) {
        draw_bank_b_icon(v, is_on, step, card_bg);
    } else if (v == VOICE_FLUTE) {
        uint16_t m_hi = is_on ? COL_CREAM   : muted;
        uint16_t m_md = is_on ? COL_GOLD    : COL_BORDER;
        uint16_t m_sh = is_on ? COL_GOLD_SH : COL_INK;

        FILL_BOX(8, 6, 16, 26, m_md);
        FILL_BOX(10, 8, 5, 22, m_hi);
        FILL_BOX(16, 10, 6, 18, m_sh);
        FILL_BOX(16, 9, 10, 20, card_bg);
        FILL_BOX(14, 12, 3, 14, card_bg);

        if (!is_on) {
            PUT_PX(32, 12, muted);
        } else if (step == 0) {
            PUT_PX(32, 12, COL_GOLD);
        } else if (step == 1 || step == 3) {
            FILL_BOX(31, 12, 3, 2, COL_GOLD_HI);
            FILL_BOX(32, 11, 2, 4, COL_GOLD_HI);
        } else {
            FILL_BOX(30, 12, 6, 2, COL_GOLD_HI);
            FILL_BOX(32, 10, 2, 6, COL_GOLD_HI);
            PUT_PX(30, 10, COL_CREAM); PUT_PX(35, 15, COL_CREAM);
        }
    } else if (v == VOICE_HARPSI) {
        uint16_t pole = is_on ? COL_TEXT : muted;
        uint16_t col_blk = is_on ? COL_INK : COL_BORDER;
        uint16_t col_wht = is_on ? COL_TEXT : muted;
        uint16_t col_grn = is_on ? COL_LEAF_MD : COL_BORDER;
        uint16_t col_red = is_on ? COL_APPLE_MD : muted;
        int ripple = play_anim ? (step % 2) : 0;

        FILL_BOX(8, 5, 2, 30, pole);
        PUT_PX(8, 4, is_on ? COL_GOLD_HI : muted);

        int fy = 8 + ripple;
        FILL_BOX(10, fy, 18, 5, col_blk);
        FILL_BOX(10, fy + 5, 18, 5, col_wht);
        FILL_BOX(10, fy + 10, 18, 5, col_grn);

        for (int row = 0; row < 15; row++) {
            int dist_from_mid = (row < 7) ? (7 - row) : (row - 7);
            int tri_w = 8 - (dist_from_mid * 8) / 7;
            if (tri_w < 1) tri_w = 1;
            FILL_BOX(10, fy + row, tri_w, 1, col_red);
        }
    } else if (v == VOICE_HORN) {
        uint16_t stem = is_on ? COL_LEAF_MD : muted;
        uint16_t leaf = is_on ? COL_LEAF_HI : COL_BORDER;
        uint16_t petal = is_on ? COL_TEXT : muted;
        uint16_t center = is_on ? COL_GOLD : COL_BORDER;

        FILL_BOX(19, 20, 3, 15, stem);
        FILL_BOX(13, 25, 6, 4, leaf);
        FILL_BOX(22, 23, 6, 4, stem);

        FILL_BOX(17, 5, 7, 7, petal);
        FILL_BOX(11, 11, 7, 7, petal);
        FILL_BOX(23, 11, 7, 7, petal);

        if (step == 0) {
            FILL_BOX(17, 16, 7, 6, petal);
        } else {
            int fy = 17 + step * 4;
            int fx = 18 + (step % 2) * 2;
            FILL_BOX(fx, fy, 5, 4, petal);
        }

        FILL_BOX(17, 11, 7, 6, center);
        PUT_PX(18, 12, is_on ? COL_GOLD_HI : muted);
    } else if (v == VOICE_REED) {
        uint16_t rim = is_on ? COL_BUBBLE_HI : muted;
        uint16_t fill = is_on ? COL_BUBBLE_MD : COL_BORDER;
        uint16_t shade = is_on ? COL_BUBBLE_SH : COL_INK;

        if (step == 3) {
            FILL_BOX(14, 9, 3, 3, rim);
            FILL_BOX(25, 9, 3, 3, rim);
            FILL_BOX(10, 18, 3, 3, fill);
            FILL_BOX(29, 18, 3, 3, fill);
            FILL_BOX(14, 27, 3, 3, shade);
            FILL_BOX(25, 27, 3, 3, shade);
            PUT_PX(21, 7, is_on ? COL_TEXT : muted);
            PUT_PX(21, 31, is_on ? COL_TEXT : muted);
        } else {
            int rad = (step == 0) ? 7 : ((step == 1) ? 9 : 11);
            int cx = 21;
            int cy = 19;
            FILL_BOX(cx - rad, cy - rad + 2, rad * 2 + 1, rad * 2 - 3, rim);
            FILL_BOX(cx - rad + 2, cy - rad, rad * 2 - 3, rad * 2 + 1, rim);
            FILL_BOX(cx - rad + 2, cy - rad + 2, rad * 2 - 3, rad * 2 - 3, fill);
            FILL_BOX(cx, cy, (rad > 2) ? (rad - 1) : 1, (rad > 2) ? (rad - 1) : 1, shade);
            FILL_BOX(cx - rad + 3, cy - rad + 3, 3, 3, is_on ? COL_TEXT : muted);
        }
    } else if (v == VOICE_STRINGS) {
        /* Burning cigarette that shortens & smokes when playing a note */
        uint16_t filter_col = is_on ? COL_TRUCK_HI : muted;
        uint16_t paper_col  = is_on ? COL_TEXT     : muted;
        uint16_t ash_col    = is_on ? COL_DIM      : COL_BORDER;
        uint16_t ember_col  = (play_anim && (step % 2 == 1)) ? COL_GOLD_HI : (is_on ? COL_APPLE_MD : muted);
        uint16_t smoke_col  = is_on ? COL_SMOKE    : muted;

        int burn = play_anim ? step : 0;
        int cig_y = 26;
        int filter_x = 6;
        int filter_w = 7;
        int paper_len = 18 - burn * 2;
        if (paper_len < 8) paper_len = 8;
        int paper_x = filter_x + filter_w;
        int ash_x = paper_x + paper_len;

        FILL_BOX(filter_x, cig_y, filter_w, 4, filter_col);
        FILL_BOX(paper_x, cig_y, paper_len, 4, paper_col);
        FILL_BOX(paper_x, cig_y + 3, paper_len, 1, is_on ? COL_DIM : COL_BORDER);

        FILL_BOX(ash_x, cig_y, 2, 4, ash_col);
        FILL_BOX(ash_x + 2, cig_y, 2, 4, ember_col);
        if (play_anim) {
            PUT_PX(ash_x + 3, cig_y + 1, COL_GOLD_HI);
        }

        if (play_anim) {
            int sx = ash_x + 2 + ((step % 2) ? 1 : -1);
            int sy = cig_y - 5 - step * 3;
            FILL_BOX(sx, sy, 3, 2, smoke_col);
            FILL_BOX(sx + 1, sy - 3, 2, 2, smoke_col);
            if (step >= 2) {
                PUT_PX(sx, sy - 6, COL_TEXT);
            }
        }
    } else if (v == VOICE_VOX) {
        uint16_t a_hi = is_on ? COL_APPLE_HI : muted;
        uint16_t a_md = is_on ? COL_APPLE_MD : COL_BORDER;
        uint16_t a_sh = is_on ? COL_APPLE_SH : COL_INK;

        FILL_BOX(19, 5, 2, 5, is_on ? COL_TRUCK_MD : muted);
        FILL_BOX(21, 6, 7, 4, is_on ? COL_LEAF_MD : muted);
        PUT_PX(22, 7, is_on ? COL_LEAF_HI : muted);

        FILL_BOX(10, 10, 20, 19, a_md);
        FILL_BOX(12, 12, 6, 7, a_hi);
        FILL_BOX(21, 20, 8, 8, a_sh);
        FILL_BOX(18, 10, 4, 3, card_bg);

        if (step >= 1) {
            /* Bite, then a wiggly striped worm that crawls well out past the apple */
            FILL_BOX(25, 14, 5, 6, card_bg);

            uint16_t w_col = is_on ? COL_WORM : muted;
            uint16_t w_stripe = is_on ? COL_APPLE_HI : COL_BORDER;
            int worm_len = (step == 1) ? 5 : ((step == 2) ? 10 : 15);
            int body_len = worm_len - 3;

            for (int i = 0; i < body_len; i++) {
                int wy = 16 - (((i / 3) + step) % 2);   /* gentle wiggle */
                FILL_BOX(25 + i, wy, 1, 3, ((i % 3) == 2) ? w_stripe : w_col);
            }
            int hx = 25 + body_len;
            FILL_BOX(hx, 15, 3, 4, w_col);              /* head */
            PUT_PX(hx + 1, 16, COL_INK);                /* eye */
            PUT_PX(hx + 2, 18, is_on ? COL_APPLE_SH : COL_INK); /* smile */
        }
    } else if (v == VOICE_VIBES) {
        static const uint16_t CHIME_PALS[4][3] = {
            { COL_GOLD_HI,   COL_GOLD,      COL_GOLD_SH },
            { COL_BUBBLE_HI, COL_BUBBLE_MD, COL_BUBBLE_SH },
            { COL_LEAF_HI,   COL_LEAF_MD,   COL_LEAF_SH },
            { COL_APPLE_HI,  COL_APPLE_MD,  COL_APPLE_SH }
        };
        uint32_t pal_idx = (uint32_t)step;
        uint16_t b_hi = is_on ? CHIME_PALS[pal_idx][0] : muted;
        uint16_t b_md = is_on ? CHIME_PALS[pal_idx][1] : COL_BORDER;
        uint16_t b_sh = is_on ? CHIME_PALS[pal_idx][2] : COL_INK;

        FILL_BOX(9, 12, 4, 16, b_sh);
        FILL_BOX(15, 9, 4, 19, b_md);
        FILL_BOX(21, 7, 4, 21, b_hi);
        FILL_BOX(27, 10, 4, 18, b_md);
        PUT_PX(10, 13, COL_INK); PUT_PX(16, 10, COL_INK);
        PUT_PX(22, 8, COL_INK);  PUT_PX(28, 11, COL_INK);
    } else {
        uint16_t t_hi = is_on ? COL_TRUCK_HI : muted;
        uint16_t t_md = is_on ? COL_TRUCK_MD : COL_BORDER;
        uint16_t t_sh = is_on ? COL_TRUCK_SH : COL_INK;

        FILL_BOX(22, 11, 10, 8, t_md);
        FILL_BOX(24, 13, 6, 4, is_on ? COL_TEXT : muted);
        FILL_BOX(10, 18, 24, 9, t_md);
        FILL_BOX(11, 19, 10, 3, t_hi);
        FILL_BOX(23, 20, 10, 5, t_sh);
        FILL_BOX(34, 21, 2, 3, is_on ? COL_GOLD_HI : muted);
        FILL_BOX(13, 26, 6, 6, COL_INK);
        FILL_BOX(15, 28, 2, 2, is_on ? COL_TEXT : muted);
        FILL_BOX(26, 26, 6, 6, COL_INK);
        FILL_BOX(28, 28, 2, 2, is_on ? COL_TEXT : muted);

        if (step == 1) {
            FILL_BOX(6, 19, 3, 3, COL_SMOKE);
        } else if (step == 2) {
            FILL_BOX(4, 16, 4, 4, COL_SMOKE);
            PUT_PX(7, 14, COL_TEXT);
        } else if (step == 3) {
            FILL_BOX(3, 12, 4, 4, COL_SMOKE);
            PUT_PX(2, 10, COL_TEXT);
        }
    }

    #undef FILL_BOX
    #undef PUT_PX

    fm1_lcd_blit(x + 8u, y + 3u, IW, IH, g_blit_buf, IW * IH);
}

static void draw_bar_meter(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                           float val, uint16_t fill_col) {
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    fm1_lcd_fill_rect(x, y, w, h, COL_BORDER);
    if (w > 4u && h > 4u) {
        fm1_lcd_fill_rect(x + 2u, y + 2u, w - 4u, h - 4u, COL_BG);
        uint32_t inner_w = w - 4u;
        uint32_t fw = (uint32_t)(val * (float)inner_w + 0.5f);
        if (fw > 0u) {
            fm1_lcd_fill_rect(x + 2u, y + 2u, fw, h - 4u, fill_col);
        }
    }
}

static void format_pct(char *buf, float v) {
    int p = (int)(v * 100.0f + 0.5f);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    buf[0] = (char)('0' + (p / 100) % 10);
    buf[1] = (char)('0' + (p / 10) % 10);
    buf[2] = (char)('0' + (p % 10));
    buf[3] = '%';
    buf[4] = '\0';
}

static void format_oct(char *buf, int oct) {
    buf[0] = 'O';
    buf[1] = 'C';
    buf[2] = 'T';
    buf[3] = ':';
    if (oct < 0) {
        buf[4] = '-';
        buf[5] = (char)('0' + (-oct) % 10);
        buf[6] = '\0';
    } else if (oct > 0) {
        buf[4] = '+';
        buf[5] = (char)('0' + oct % 10);
        buf[6] = '\0';
    } else {
        buf[4] = '0';
        buf[5] = '\0';
    }
}

static int g_need_full_clear = 1;
static int g_splash_bg_drawn = 0;
static int g_prev_view = -99;
static int g_prev_voice_on[NUM_VOICES];
static int g_prev_anim_phase = -1;
static int g_prev_oct = 99;
static int g_prev_hold = -1;
static float g_prev_rev_mix = -1.0f;
static float g_prev_rev_decay = -1.0f;
static float g_prev_dly_mix = -1.0f;
static float g_prev_dly_time = -1.0f;
static float g_prev_dly_feedback = -1.0f;
static float g_prev_attack = -1.0f;
static float g_prev_decay = -1.0f;
static float g_prev_filter = -1.0f;
static float g_prev_mel = -1.0f;
static int g_prev_mello_key = -99;   /* home "MELLO ON" strip: last drawn state */
static uint32_t g_prev_master = 99999u;

/* Home screen "MELLO ON" strip under the parameter bars: two little tape
 * reels (spinning while notes play) and the words. Blank when the flute is off. */
#define MELLO_X 14u
#define MELLO_Y 187u
static void draw_mello_strip(int on, int frame) {
    if (!on) {
        fm1_lcd_fill_rect(MELLO_X, MELLO_Y, 110u, 12u, COL_STAGE_BG);
        return;
    }
    const uint32_t W = 34u, H = 12u;
    for (uint32_t i = 0u; i < W * H; i++) g_blit_buf[i] = COL_STAGE_BG;
    const int cx[2] = { 6, 27 };
    /* spokes: 4 directions, turning one step per frame */
    static const int8_t SPK[4][2] = { {1, 0}, {1, 1}, {0, 1}, {-1, 1} };
    for (int r = 0; r < 2; r++) {
        for (int y = 0; y < 12; y++) {
            for (int x = cx[r] - 6; x <= cx[r] + 6; x++) {
                int dx = x - cx[r], dy = y - 6, d2 = dx * dx + dy * dy;
                if (x < 0 || x >= (int)W) continue;
                if (d2 <= 30 && d2 >= 17) g_blit_buf[(uint32_t)y * W + (uint32_t)x] = COL_TEXT;      /* rim */
                else if (d2 < 17) g_blit_buf[(uint32_t)y * W + (uint32_t)x] = COL_INK;              /* dark reel */
            }
        }
        int k = (frame + r) & 3;
        for (int t = 1; t <= 3; t++) {
            int sx = SPK[k][0] * t, sy = SPK[k][1] * t;
            g_blit_buf[(uint32_t)(6 + sy) * W + (uint32_t)(cx[r] + sx)] = COL_GOLD_HI;
            g_blit_buf[(uint32_t)(6 - sy) * W + (uint32_t)(cx[r] - sx)] = COL_GOLD_HI;
        }
        g_blit_buf[6u * W + (uint32_t)cx[r]] = COL_GOLD;                                    /* hub */
    }
    for (int x = cx[0]; x <= cx[1]; x++) g_blit_buf[11u * W + (uint32_t)x] = COL_TRUCK_HI;   /* the tape */
    fm1_lcd_blit(MELLO_X, MELLO_Y, W, H, g_blit_buf, W * H);
    draw_text_blit(MELLO_X + 40u, MELLO_Y + 2u, "MELLO ON", COL_BUBBLE_HI, COL_STAGE_BG, 1u);
}

/* Slim header used by the scope page */
static void draw_page_header(const char *title, uint32_t m_raw, int sounding) {
    fm1_lcd_fill_rect(0u, 0u, 240u, 18u, COL_STAGE_BG);
    draw_retro_star(4u, 6u, sounding ? COL_GOLD_HI : COL_GOLD, COL_STAGE_BG);
    draw_text_blit(13u, 5u, title, COL_GOLD_HI, COL_STAGE_BG, 1u);

    char obuf[7];
    format_oct(obuf, g_octave_shift);
    draw_text_blit(96u, 5u, obuf, COL_LEAF_HI, COL_STAGE_BG, 1u);

    int vol_pct = (int)(((float)m_raw / (float)FM1_MASTER_MAX) * 100.0f + 0.5f);
    char vbuf[9] = "VOL:000%";
    vbuf[4] = (char)('0' + (vol_pct / 100) % 10);
    vbuf[5] = (char)('0' + (vol_pct / 10) % 10);
    vbuf[6] = (char)('0' + (vol_pct % 10));
    draw_text_blit(152u, 5u, vbuf, COL_TEXT, COL_STAGE_BG, 1u);
    draw_retro_star(230u, 6u, sounding ? COL_GOLD_HI : COL_GOLD, COL_STAGE_BG);
}

/* Oscilloscope screen area (inside a themed frame) */
#define SCOPE_X       12u
#define SCOPE_Y       30u
#define SCOPE_W       216u
#define SCOPE_H       120u
#define SCOPE_STRIP   12u          /* rows per blit: 216 x 12 = 2592 px <= blit buffer */
#define SCOPE_STRIDE  2u           /* audio samples per screen column (~9.8 ms shown) */
#define COL_SCOPE_BG    0x0100u    /* near-black green CRT */
#define COL_SCOPE_GRID  0x0280u    /* dim green graticule */
#define COL_SCOPE_TRACE 0x87F0u    /* bright phosphor green */

static void draw_scope_frame(void) {
    fm1_lcd_fill_rect(4u, 22u, 232u, 214u, COL_STAGE_BG);
    fm1_lcd_fill_rect(SCOPE_X - 2u, SCOPE_Y - 2u, SCOPE_W + 4u, SCOPE_H + 4u, COL_BORDER);
    draw_text_blit(12u, 196u, "SELECT: PAGE", COL_GOLD_HI, COL_STAGE_BG, 1u);
    draw_text_blit(12u, 212u, "PRESET: BUTTONS  ALGO: SCREEN", COL_DIM, COL_STAGE_BG, 1u);
}

static void draw_scope_trace(void) {
    static int16_t ys[SCOPE_W];
    const uint32_t mask = SCOPE_BUF_LEN - 1u;
    const uint32_t span = SCOPE_W * SCOPE_STRIDE;
    uint32_t w = g_scope_w;

    /* Trigger on the most recent rising zero crossing so the wave stands still */
    uint32_t start = w - span - 1u;
    uint32_t trig = start;
    for (uint32_t k = 0u; k < 700u; k++) {
        uint32_t i = start - k;
        if (g_scope_buf[(i - 1u) & mask] <= 0.0f && g_scope_buf[i & mask] > 0.0f) {
            trig = i;
            break;
        }
    }

    /* Auto-scale so quiet and loud patches both fill the screen */
    float peak = 0.0f;
    for (uint32_t x = 0u; x < SCOPE_W; x++) {
        float a = fabsf(g_scope_buf[(trig + x * SCOPE_STRIDE) & mask]);
        if (a > peak) peak = a;
    }
    if (peak < 0.02f) peak = 0.02f;
    float gain = ((float)SCOPE_H * 0.42f) / peak;
    float mid = (float)(SCOPE_H / 2u);

    for (uint32_t x = 0u; x < SCOPE_W; x++) {
        float v = g_scope_buf[(trig + x * SCOPE_STRIDE) & mask];
        int y = (int)(mid - v * gain);
        if (y < 1) y = 1;
        if (y > (int)SCOPE_H - 2) y = (int)SCOPE_H - 2;
        ys[x] = (int16_t)y;
    }

    /* Draw in horizontal strips: every pixel written once, so no flicker */
    for (uint32_t r0 = 0u; r0 < SCOPE_H; r0 += SCOPE_STRIP) {
        uint32_t rows = (r0 + SCOPE_STRIP <= SCOPE_H) ? SCOPE_STRIP : (SCOPE_H - r0);
        for (uint32_t ry = 0u; ry < rows; ry++) {
            uint32_t yy = r0 + ry;
            uint16_t *row = &g_blit_buf[ry * SCOPE_W];
            int h_grid = (yy % 20u) == 0u || yy == SCOPE_H / 2u;
            for (uint32_t x = 0u; x < SCOPE_W; x++) {
                int v_grid = (x % 27u) == 0u;
                row[x] = ((h_grid && (x % 3u) == 0u) || (v_grid && (yy % 3u) == 0u))
                         ? COL_SCOPE_GRID : COL_SCOPE_BG;
            }
        }
        for (uint32_t x = 0u; x < SCOPE_W; x++) {
            int y0 = ys[x];
            int y1 = (x > 0u) ? ys[x - 1u] : y0;
            int lo = (y0 < y1) ? y0 : y1;
            int hi = ((y0 > y1) ? y0 : y1) + 1;   /* 2 px thick */
            if (lo < (int)r0) lo = (int)r0;
            if (hi > (int)(r0 + rows) - 1) hi = (int)(r0 + rows) - 1;
            for (int y = lo; y <= hi; y++) {
                g_blit_buf[(uint32_t)(y - (int)r0) * SCOPE_W + x] = COL_SCOPE_TRACE;
            }
        }
        fm1_lcd_blit(SCOPE_X, SCOPE_Y + r0, SCOPE_W, rows, g_blit_buf, SCOPE_W * rows);
    }
}

static void render_screen(void) {
    uint32_t m_raw = fm1_master();
    int sounding = (g_any_note_sounding != 0u);

    if (g_need_full_clear || g_prev_view != g_view) {
        g_need_full_clear = 0;
        g_prev_view = g_view;
        g_splash_bg_drawn = 0;
        for (uint32_t i = 0u; i < NUM_VOICES; i++) g_prev_voice_on[i] = -1;
        g_prev_anim_phase = -1;
        g_prev_oct = 99;
        g_prev_hold = -1;
        g_prev_rev_mix = -1.0f;
        g_prev_rev_decay = -1.0f;
        g_prev_dly_mix = -1.0f;
        g_prev_dly_time = -1.0f;
        g_prev_dly_feedback = -1.0f;
        g_prev_attack = -1.0f;
        g_prev_decay = -1.0f;
        g_prev_filter = -1.0f;
        g_prev_mel = -1.0f;
        g_prev_mello_key = -99;
        g_prev_master = 99999u;

        /* Splash & truck pages paint the whole scene themselves */
        if (g_view != VIEW_SPLASH && g_view != VIEW_TRUCK) {
            fm1_lcd_fill_rect(0u, 0u, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, COL_BG);
        }
        if (g_view == VIEW_SCOPE) {
            draw_scope_frame();
        }
    }

    if (g_view == VIEW_SPLASH) {
        uint32_t elapsed = g_tick_ms - g_splash_start_ms;
        if (elapsed >= SPLASH_DURATION_MS) {
            g_view = VIEW_HOME;
            g_need_full_clear = 1;
            render_screen();
            return;
        }
        if (!g_splash_bg_drawn) {
            draw_splash_static_bg();
            g_splash_bg_drawn = 1;
        }
        /* Update at ~12.5 Hz (every 80ms) so SPI LCD transfers stay crisp & tear-free */
        uint32_t total_steps = SPLASH_DURATION_MS / 80u;
        uint32_t splash_step = elapsed / 80u;
        if (g_prev_anim_phase != (int)splash_step) {
            g_prev_anim_phase = (int)splash_step;
            draw_splash_dynamic(splash_step, splash_step, total_steps);
        }
        return;
    }

    if (g_view == VIEW_TRUCK) {
        /* Same scene as power-on; the truck only drives while notes are sounding */
        if (!g_splash_bg_drawn) {
            draw_splash_static_bg();
            g_splash_bg_drawn = 1;
            g_bridge_amp = 0.0f;
            g_bridge_flat = 1;
        }
        uint32_t total_steps = SPLASH_DURATION_MS / 80u;
        uint32_t step = g_tick_ms / 80u;
        if (g_prev_anim_phase != (int)step) {
            /* Held note: full speed. Released but still ringing: slow.
             * Silent: the truck parks and the bridge settles flat. */
            int held = (g_any_note_held != 0u);
            float speed = held ? 1.0f : (sounding ? 0.35f : 0.0f);
            if (g_prev_anim_phase >= 0) {
                g_truck_pos += speed;
                if (g_truck_pos >= (float)total_steps) g_truck_pos -= (float)total_steps;
                g_truck_step = (uint32_t)g_truck_pos;
            }
            g_prev_anim_phase = (int)step;

            float target = held ? 7.0f : (sounding ? 4.0f : 0.0f);
            g_bridge_amp += (target - g_bridge_amp) * (held ? 0.30f : 0.12f);
            g_bridge_ph += 0.08f + 0.50f * speed;
            if (g_bridge_ph > 6.2831853f) g_bridge_ph -= 6.2831853f;
            if (g_bridge_amp > 0.15f) {
                draw_bridge_band(g_bridge_amp, g_bridge_ph);
                g_bridge_flat = 0;
            } else if (!g_bridge_flat) {
                g_bridge_amp = 0.0f;
                draw_bridge_band(0.0f, 0.0f);
                g_bridge_flat = 1;
            }
            draw_splash_dynamic(step, g_truck_step, total_steps);
        }
        return;
    }

    if (g_view == VIEW_SCOPE) {
        if (g_prev_master != m_raw || g_prev_oct != g_octave_shift || g_prev_hold != sounding) {
            g_prev_master = m_raw;
            g_prev_oct = g_octave_shift;
            g_prev_hold = sounding;
            draw_page_header("SCOPE", m_raw, sounding);
        }
        /* ~10 frames per second: the LCD is slow (full screen ~77 ms) */
        int frame = (int)(g_tick_ms / 100u);
        if (g_prev_anim_phase != frame) {
            g_prev_anim_phase = frame;
            draw_scope_trace();
        }
        return;
    }

    /* Unified 4-step animation phase (0 = idle, 1..4 = active slow cycle every 560ms) */
    int anim_phase = sounding ? (1 + (int)((g_tick_ms / 560u) % 4u)) : 0;

    if (g_view == VIEW_HOME) {
        if (g_prev_master != m_raw || g_prev_oct != g_octave_shift ||
            g_prev_hold != g_hold_on || g_prev_anim_phase != anim_phase) {
            g_prev_master = m_raw;
            g_prev_oct = g_octave_shift;
            g_prev_hold = g_hold_on;

            fm1_lcd_fill_rect(0u, 0u, 240u, 18u, COL_STAGE_BG);
            draw_retro_star(4u, 6u, sounding ? COL_GOLD_HI : COL_GOLD, COL_STAGE_BG);
            draw_text_blit(13u, 5u, g_bank ? "BANK B" : "LOWREY", g_bank ? COL_BUBBLE_HI : COL_GOLD_HI, COL_STAGE_BG, 1u);

            char obuf[7];
            format_oct(obuf, g_octave_shift);
            draw_text_blit(53u, 5u, obuf, COL_LEAF_HI, COL_STAGE_BG, 1u);

            draw_text_blit(96u, 5u, g_hold_on ? "HOLD:ON" : "HOLD:--",
                           g_hold_on ? COL_APPLE_HI : COL_DIM, COL_STAGE_BG, 1u);

            int vol_pct = (int)(((float)m_raw / (float)FM1_MASTER_MAX) * 100.0f + 0.5f);
            char vbuf[9] = "VOL:000%";
            vbuf[4] = (char)('0' + (vol_pct / 100) % 10);
            vbuf[5] = (char)('0' + (vol_pct / 10) % 10);
            vbuf[6] = (char)('0' + (vol_pct % 10));
            draw_text_blit(152u, 5u, vbuf, COL_TEXT, COL_STAGE_BG, 1u);
            draw_retro_star(230u, 6u, sounding ? COL_GOLD_HI : COL_GOLD, COL_STAGE_BG);
        }

        const uint32_t col_x[4] = { 4u, 62u, 120u, 178u };
        const uint32_t row_y[2] = { 20u, 82u };

        for (uint32_t v = 0u; v < NUM_VOICES; v++) {
            int on = g_voice_on[v];
            uint32_t cx = col_x[v % 4u];
            uint32_t cy = row_y[v / 4u];
            if (g_prev_voice_on[v] != on) {
                /* Redraw card border & label ONLY when voice is toggled on/off */
                draw_voice_card_frame(v, cx, cy, on);
                draw_voice_card_icon(v, cx, cy, on, anim_phase);
                g_prev_voice_on[v] = on;
            } else if (on && g_prev_anim_phase != anim_phase) {
                /* Only blit the 42x40 icon interior when animation phase advances */
                draw_voice_card_icon(v, cx, cy, on, anim_phase);
            }
        }
        g_prev_anim_phase = anim_phase;

        /* POCKET's sun sets into a moon while you play, and rises back after */
        if (g_bank && g_voice_on[VB_POCKET]) {
            int tgt = sounding ? 8 : 0;
            if (g_pk_morph != tgt && g_tick_ms - g_pk_ms >= 260u) {   /* ~2 s each way */
                g_pk_morph += (tgt > g_pk_morph) ? 1 : -1;
                g_pk_ms = g_tick_ms;
                draw_voice_card_icon(VB_POCKET, col_x[VB_POCKET % 4u], row_y[VB_POCKET / 4u], 1, anim_phase);
            }
        } else {
            g_pk_morph = 0;
        }
        /* WARM's police car catches fire slowly while you play, and burns out after */
        if (g_bank && g_voice_on[VB_WARM]) {
            int tgt = sounding ? 8 : 0;
            if (g_fire != tgt && g_tick_ms - g_fire_ms >= 280u) {
                g_fire += (tgt > g_fire) ? 1 : -1;
                g_fire_ms = g_tick_ms;
                draw_voice_card_icon(VB_WARM, col_x[VB_WARM % 4u], row_y[VB_WARM / 4u], 1, anim_phase);
            }
        } else {
            g_fire = 0;
        }

        if (g_prev_rev_mix != g_rev_mix || g_prev_dly_mix != g_dly_mix ||
            g_prev_decay != g_decay || g_prev_filter != g_filter_cutoff) {
            g_prev_rev_mix = g_rev_mix;
            g_prev_dly_mix = g_dly_mix;
            g_prev_decay = g_decay;
            g_prev_filter = g_filter_cutoff;

            fm1_lcd_fill_rect(4u, 144u, 232u, 92u, COL_STAGE_BG);
            char pbuf[6];

            draw_text_blit(12u, 151u, "REV", COL_BUBBLE_HI, COL_STAGE_BG, 1u);
            draw_bar_meter(38u, 149u, 54u, 11u, g_rev_mix, COL_BUBBLE_MD);
            format_pct(pbuf, g_rev_mix);
            draw_text_blit(96u, 151u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(136u, 151u, "DEC", COL_GOLD_HI, COL_STAGE_BG, 1u);
            draw_bar_meter(162u, 149u, 36u, 11u, g_decay, COL_GOLD);
            format_pct(pbuf, g_decay);
            draw_text_blit(202u, 151u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(12u, 173u, "DLY", COL_APPLE_HI, COL_STAGE_BG, 1u);
            draw_bar_meter(38u, 171u, 54u, 11u, g_dly_mix, COL_APPLE_MD);
            format_pct(pbuf, g_dly_mix);
            draw_text_blit(96u, 173u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(136u, 173u, "FLT", COL_LEAF_HI, COL_STAGE_BG, 1u);
            draw_bar_meter(162u, 171u, 36u, 11u, g_filter_cutoff, COL_LEAF_MD);
            format_pct(pbuf, g_filter_cutoff);
            draw_text_blit(202u, 173u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(12u, 204u, "SEL:HLD SAVE:AD K3:DEC K4:FLT", COL_GOLD, COL_STAGE_BG, 1u);
            draw_text_blit(12u, 218u, "HOLD SAVE: MEL FLUTE ON/OFF", COL_DIM, COL_STAGE_BG, 1u);
            g_prev_mello_key = -99;   /* the panel was repainted: redraw the strip */
        }

        {
            int mel_on = (g_mel_level > 0.005f);
            int frame = sounding ? (int)((g_tick_ms / 140u) & 3u) : 0;
            int key = mel_on ? (sounding ? frame : 10) : -1;
            if (key != g_prev_mello_key) {
                g_prev_mello_key = key;
                draw_mello_strip(mel_on, frame);
            }
        }
    } else if (g_view == VIEW_ENV_AD) {
        if (g_prev_attack != g_attack || g_prev_decay != g_decay || g_prev_filter != g_filter_cutoff ||
            g_prev_mel != g_mel_level) {
            g_prev_attack = g_attack;
            g_prev_decay = g_decay;
            g_prev_filter = g_filter_cutoff;
            g_prev_mel = g_mel_level;

            fm1_lcd_fill_rect(8u, 8u, 224u, 224u, COL_STAGE_BG);
            draw_text_blit(16u, 18u, "ENVELOPE + MEL", COL_GOLD_HI, COL_STAGE_BG, 2u);

            char pbuf[6];
            /* rows in knob order: K1 MEL, K2 ATK, K3 DEC, K4 FLT */
            draw_text_blit(16u, 58u, "MEL", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 56u, 104u, 12u, g_mel_level, COL_BUBBLE_MD);
            format_pct(pbuf, g_mel_level);
            draw_text_blit(164u, 58u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 84u, "ATK", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 82u, 104u, 12u, g_attack, COL_GOLD);
            format_pct(pbuf, g_attack);
            draw_text_blit(164u, 84u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 110u, "DEC", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 108u, 104u, 12u, g_decay, COL_APPLE_MD);
            format_pct(pbuf, g_decay);
            draw_text_blit(164u, 110u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 136u, "FLT", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 134u, 104u, 12u, g_filter_cutoff, COL_LEAF_MD);
            format_pct(pbuf, g_filter_cutoff);
            draw_text_blit(164u, 136u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
            draw_text_blit(16u, 154u, "MEL = MELLOTRON FLUTE LAYER", COL_DIM, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 176u, "K1:MEL K2:ATK K3:DEC K4:FLT", COL_GOLD_HI, COL_STAGE_BG, 1u);
            draw_text_blit(16u, 198u, "SAVE OR HOME -> STAGE", COL_LEAF_HI, COL_STAGE_BG, 1u);
        }
    } else if (g_view == VIEW_FX_REVERB) {
        if (g_prev_rev_mix != g_rev_mix || g_prev_rev_decay != g_rev_decay || g_prev_filter != g_filter_cutoff) {
            g_prev_rev_mix = g_rev_mix;
            g_prev_rev_decay = g_rev_decay;
            g_prev_filter = g_filter_cutoff;

            fm1_lcd_fill_rect(8u, 8u, 224u, 224u, COL_STAGE_BG);
            draw_text_blit(16u, 18u, "FX: REVERB", COL_BUBBLE_HI, COL_STAGE_BG, 2u);

            char pbuf[6];
            draw_text_blit(16u, 56u, "MIX", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 54u, 104u, 12u, g_rev_mix, COL_BUBBLE_MD);
            format_pct(pbuf, g_rev_mix);
            draw_text_blit(164u, 56u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 82u, "DEC", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 80u, 104u, 12u, g_rev_decay, COL_BUBBLE_MD);
            format_pct(pbuf, g_rev_decay);
            draw_text_blit(164u, 82u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 108u, "FLT", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 106u, 104u, 12u, g_filter_cutoff, COL_LEAF_MD);
            format_pct(pbuf, g_filter_cutoff);
            draw_text_blit(164u, 108u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 142u, "K1:MIX  K2:DEC  K4:FLT", COL_GOLD_HI, COL_STAGE_BG, 1u);
            draw_text_blit(16u, 168u, "FX->DELAY  HOME->STAGE", COL_LEAF_HI, COL_STAGE_BG, 1u);
        }
    } else {
        if (g_prev_dly_mix != g_dly_mix || g_prev_dly_time != g_dly_time ||
            g_prev_dly_feedback != g_dly_feedback || g_prev_filter != g_filter_cutoff) {
            g_prev_dly_mix = g_dly_mix;
            g_prev_dly_time = g_dly_time;
            g_prev_dly_feedback = g_dly_feedback;
            g_prev_filter = g_filter_cutoff;

            fm1_lcd_fill_rect(8u, 8u, 224u, 224u, COL_STAGE_BG);
            draw_text_blit(16u, 18u, "FX: DELAY", COL_APPLE_HI, COL_STAGE_BG, 2u);

            char pbuf[6];
            draw_text_blit(16u, 54u, "MIX", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 52u, 104u, 12u, g_dly_mix, COL_APPLE_MD);
            format_pct(pbuf, g_dly_mix);
            draw_text_blit(164u, 54u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 78u, "TIM", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 76u, 104u, 12u, g_dly_time, COL_APPLE_MD);
            format_pct(pbuf, g_dly_time);
            draw_text_blit(164u, 78u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 102u, "FBK", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 100u, 104u, 12u, g_dly_feedback, COL_APPLE_MD);
            format_pct(pbuf, g_dly_feedback);
            draw_text_blit(164u, 102u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 126u, "FLT", COL_TEXT, COL_STAGE_BG, 1u);
            draw_bar_meter(54u, 124u, 104u, 12u, g_filter_cutoff, COL_LEAF_MD);
            format_pct(pbuf, g_filter_cutoff);
            draw_text_blit(164u, 126u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);

            draw_text_blit(16u, 154u, "K1:MIX K2:TIM K3:FBK K4:FLT", COL_GOLD_HI, COL_STAGE_BG, 1u);
            draw_text_blit(16u, 178u, "PRESS FX -> HOME STAGE", COL_LEAF_HI, COL_STAGE_BG, 1u);
        }
    }
}

/* ---- Mellotron M400S flute tapes ----
 * Samples: Taijiguy's (Bernie Kornowicz) Mellotron M400S recordings, "free to use, but cannot be used
 * in any commercial, for profit software". Prepared (12 tapes, 11025 Hz, IMA ADPCM, loops) by
 * AntonioAltea's FMEL-1 (github.com/AntonioAltea/fmel-1). Data only below: do not edit. */
#define MF_NZONES 12u
#define MF_CK 256u
#define MF_BYTES 113664u
typedef struct { uint32_t off, n, ls, ck; uint8_t root, lo, hi; } mf_zone_t;
static const mf_zone_t MF_ZONE[MF_NZONES] = {
    {0u, 18944u, 6400u, 0u, 44, 0, 45},
    {9472u, 18944u, 6400u, 74u, 47, 46, 48},
    {18944u, 18944u, 6656u, 148u, 50, 49, 51},
    {28416u, 18944u, 7424u, 222u, 53, 52, 54},
    {37888u, 18944u, 7168u, 296u, 56, 55, 57},
    {47360u, 18944u, 5888u, 370u, 59, 58, 60},
    {56832u, 18944u, 6400u, 444u, 62, 61, 63},
    {66304u, 18944u, 6656u, 518u, 65, 64, 66},
    {75776u, 18944u, 7168u, 592u, 68, 67, 69},
    {85248u, 18944u, 5888u, 666u, 71, 70, 72},
    {94720u, 18944u, 6912u, 740u, 74, 73, 75},
    {104192u, 18944u, 7424u, 814u, 77, 76, 127},
};
/* decoder state every 256 samples: predictor (low 16 bits) | step index << 16 */
static const uint32_t MF_CKS[888] = {
    0x00000000u, 0x001A046Cu, 0x001E0480u, 0x00270449u, 0x0029FC8Cu, 0x002BF4A5u, 0x0032FD7Du, 0x00300F78u,
    0x00300309u, 0x0033019Cu, 0x003902C7u, 0x0037F719u, 0x0035FB05u, 0x00321539u, 0x002D0591u, 0x002BFD93u,
    0x0033FE9Cu, 0x0038E766u, 0x0037FFD1u, 0x003DF3CDu, 0x003C2143u, 0x0039EDE1u, 0x00370967u, 0x0033F145u,
    0x0035F46Cu, 0x003C03F1u, 0x003A13AFu, 0x003CF168u, 0x00370A40u, 0x002EEA70u, 0x0031F5EDu, 0x00360359u,
    0x003516BFu, 0x0030FB4Fu, 0x0038098Au, 0x0040FF44u, 0x0031FEFAu, 0x0036FE8Du, 0x0037183Bu, 0x002CFE19u,
    0x00310314u, 0x003D033Au, 0x0040F05Du, 0x0039F593u, 0x003E054Fu, 0x003D0F0Eu, 0x003AF5ABu, 0x003A06A9u,
    0x0037EE94u, 0x0036F085u, 0x003E08C4u, 0x004212BDu, 0x0046E7CFu, 0x003C0D70u, 0x0034EDEBu, 0x0034F902u,
    0x00370FE6u, 0x003B1548u, 0x0039F131u, 0x003D196Cu, 0x0039EBFAu, 0x0032FB84u, 0x0039053Du, 0x003719E1u,
    0x0037F379u, 0x003A0853u, 0x003ED6FBu, 0x003EFDC4u, 0x0040F468u, 0x003F2962u, 0x0038ED42u, 0x0035094Du,
    0x0030ED25u, 0x0033F4C1u, 0x00000000u, 0x0036F4D5u, 0x003103CFu, 0x0031FDA2u, 0x0033F036u, 0x003AEEE2u,
    0x003B09DDu, 0x0033EC60u, 0x0038F05Cu, 0x0044FF69u, 0x003E2496u, 0x0035EED7u, 0x003800E6u, 0x003A127Eu,
    0x003C1E9Bu, 0x0030F189u, 0x0039FF07u, 0x0042FE7Au, 0x003A1D65u, 0x0034EC88u, 0x003AFB2Fu, 0x0045DD9Eu,
    0x003823BAu, 0x0037E994u, 0x003EFFC0u, 0x0041FBAAu, 0x003A2146u, 0x002AF2C8u, 0x0034FA99u, 0x0041E284u,
    0x0036196Eu, 0x0031FEC6u, 0x0036FF55u, 0x003BED34u, 0x003715C8u, 0x002EF8C8u, 0x002CF747u, 0x003DEA09u,
    0x00351760u, 0x0033EF34u, 0x003801F7u, 0x0042DF83u, 0x00381D2Fu, 0x0032F85Eu, 0x0034F7D8u, 0x003BFB03u,
    0x00331341u, 0x003B1289u, 0x0037EE0Au, 0x003FF26Fu, 0x00390AA4u, 0x0038018Bu, 0x0039F4E2u, 0x003F0021u,
    0x00321649u, 0x003E07B9u, 0x0033F68Eu, 0x0040F6E5u, 0x0044E717u, 0x003B2271u, 0x0032ED9Au, 0x003AFC2Au,
    0x0036114Au, 0x003E1963u, 0x0036F188u, 0x0041F6A5u, 0x004308CDu, 0x003C1F81u, 0x0039E73Cu, 0x0035F7D9u,
    0x0042E6E3u, 0x003C2787u, 0x0037E73Bu, 0x004001D8u, 0x00000000u, 0x00230216u, 0x002D05D6u, 0x003604F2u,
    0x0039F1BBu, 0x003AF043u, 0x0037070Eu, 0x0036FADBu, 0x0038FCC6u, 0x00390B62u, 0x0039077Au, 0x003F022Fu,
    0x003DF3D3u, 0x003D0B9Fu, 0x00370097u, 0x003C06FDu, 0x003E04A7u, 0x0041FB23u, 0x003EEC08u, 0x0042F1C2u,
    0x0042206Eu, 0x003BF2F1u, 0x003BFCCCu, 0x003A032Fu, 0x0041FD03u, 0x0044F77Eu, 0x0043EE44u, 0x00481280u,
    0x003C0A2Fu, 0x003913F6u, 0x003C05E2u, 0x003BFE15u, 0x0040EE92u, 0x0041104Du, 0x003CF4B7u, 0x003C038Bu,
    0x003B13A6u, 0x003A1072u, 0x003DFA7Fu, 0x003CE56Fu, 0x0044F35Au, 0x00450F58u, 0x003D0709u, 0x003F1846u,
    0x0043129Au, 0x0044098Eu, 0x003FECD8u, 0x004005F0u, 0x003CF4E3u, 0x00420DE3u, 0x0040FF37u, 0x004109A4u,
    0x0044F48Au, 0x0035EAD1u, 0x003AFE7Fu, 0x0032FCF5u, 0x003D0625u, 0x003F0286u, 0x0044FCBDu, 0x003FE0B7u,
    0x0044ECDDu, 0x00471917u, 0x003F0754u, 0x004207F9u, 0x003F04E0u, 0x00410275u, 0x00470798u, 0x0041DB1Bu,
    0x003F11CCu, 0x0041EBA7u, 0x003EFD75u, 0x003B0720u, 0x0041FCB7u, 0x0041F2F8u, 0x00000000u, 0x002907D6u,
    0x003707DFu, 0x0035F7A8u, 0x0033E766u, 0x003BF64Fu, 0x003D0EABu, 0x003715C4u, 0x003C134Au, 0x003FF218u,
    0x0038E8AFu, 0x003AFC3Bu, 0x003E0B71u, 0x003B1F61u, 0x003E0D59u, 0x0042F3D2u, 0x003AEC5Cu, 0x0036F5EDu,
    0x00390A3Cu, 0x003A1C86u, 0x00400764u, 0x003DE986u, 0x003FF4C1u, 0x003D0257u, 0x003411BCu, 0x003812E6u,
    0x003C00E2u, 0x003CDFD9u, 0x003FF15Au, 0x003E0CD4u, 0x003C2259u, 0x003B0750u, 0x003DF3BAu, 0x0038E48Eu,
    0x0037F2C0u, 0x00390CD5u, 0x003E0F84u, 0x003C1195u, 0x003EF06Du, 0x0039E226u, 0x0038F24Du, 0x003B0C0Fu,
    0x003F0809u, 0x003DF812u, 0x0040F83Du, 0x003D0556u, 0x00391FF6u, 0x003B157Du, 0x003BF0ECu, 0x0038E583u,
    0x003CFC48u, 0x00390F2Fu, 0x003E0E2Bu, 0x003B13C2u, 0x003EF65Eu, 0x003BE2C1u, 0x003AF5FAu, 0x003B0DDFu,
    0x00351AC5u, 0x003F1303u, 0x003BEFBDu, 0x003FECB8u, 0x003BF175u, 0x003C0EE6u, 0x003E13BBu, 0x004308ADu,
    0x003FDC05u, 0x0042FB11u, 0x00420365u, 0x00391BA6u, 0x003E15EEu, 0x003A061Au, 0x003BDE6Du, 0x003EEFEBu,
    0x00000000u, 0x002FF954u, 0x003BFB2Bu, 0x003A1361u, 0x0035F921u, 0x0038F5F1u, 0x003817B6u, 0x0038F682u,
    0x0043F55Au, 0x003D0280u, 0x003D1175u, 0x003DEF8Du, 0x003DFBD6u, 0x003A15D7u, 0x003E02EEu, 0x003DF345u,
    0x0040EA19u, 0x00432AAFu, 0x003DF242u, 0x003AE9CEu, 0x00401181u, 0x003D0B2Du, 0x0041F4F1u, 0x003CFBE8u,
    0x003E1251u, 0x003AEB16u, 0x003CF3A9u, 0x003A20FBu, 0x00410649u, 0x003EF4C0u, 0x003EED90u, 0x003F279Au,
    0x003DF234u, 0x003CF55Eu, 0x004110EFu, 0x003E0E1Fu, 0x003BF21Bu, 0x003BEAD2u, 0x003A21A6u, 0x0040045Cu,
    0x0041F830u, 0x0041F60Cu, 0x003D0E22u, 0x0037F2D1u, 0x0040E8D1u, 0x003D1B11u, 0x003E03E6u, 0x0045F2C0u,
    0x0043F642u, 0x003C0D64u, 0x0039F44Au, 0x003CF967u, 0x003C15FEu, 0x003DEBFBu, 0x0048FFCFu, 0x00420A24u,
    0x003B0947u, 0x0039EEF9u, 0x003EF753u, 0x00381E74u, 0x003F0CC7u, 0x0042F2FCu, 0x003EEAC7u, 0x004535C4u,
    0x003CEB9Au, 0x003AE8ABu, 0x003A2155u, 0x00421008u, 0x0043F365u, 0x003EEDFEu, 0x003F1C6Au, 0x0039EF15u,
    0x003CF00Au, 0x00381F16u, 0x00000000u, 0x00330083u, 0x0034067Fu, 0x0039FE02u, 0x003C0CF1u, 0x0044E39Cu,
    0x004120BAu, 0x0043D66Bu, 0x003D0E92u, 0x0043E5A1u, 0x003A0A4Cu, 0x003FFF1Cu, 0x0043F6C5u, 0x00411B90u,
    0x0041DA9Au, 0x003D0B16u, 0x0045D706u, 0x003C1A45u, 0x003DE43Fu, 0x003D18ECu, 0x0042DF2Cu, 0x003E1871u,
    0x0041E45Eu, 0x00390C7Du, 0x003CFFCCu, 0x003003B9u, 0x00380942u, 0x0034FEEDu, 0x003C1077u, 0x0041E5B1u,
    0x003F1814u, 0x0042E376u, 0x003E1493u, 0x003CF5E5u, 0x00390903u, 0x003FEBDDu, 0x003B1525u, 0x003EE073u,
    0x003C0E91u, 0x003BF8BBu, 0x003709ECu, 0x003FF93Fu, 0x00370665u, 0x003D02BBu, 0x00360D0Eu, 0x003C0793u,
    0x0044F36Au, 0x003C07AAu, 0x00380138u, 0x0037087Du, 0x003EF10Eu, 0x003B15D1u, 0x003EE806u, 0x00390F75u,
    0x003DFAE1u, 0x00390AD2u, 0x003DFA66u, 0x0035062Du, 0x0038EDBFu, 0x00360800u, 0x003CFC6Bu, 0x003814D5u,
    0x0040F93Du, 0x003B08FBu, 0x003E0BE5u, 0x003FEE87u, 0x003A0929u, 0x003FE180u, 0x004124CCu, 0x0041D643u,
    0x003D1CA9u, 0x0040D8CEu, 0x00401AE5u, 0x0042DF6Eu, 0x00000000u, 0x0032F4E1u, 0x0035072Eu, 0x0039086Fu,
    0x0042F9AFu, 0x003DFB8Au, 0x003C10C1u, 0x003EF39Eu, 0x0044E08Au, 0x003E0716u, 0x003C057Du, 0x003BE9F7u,
    0x00401266u, 0x00400398u, 0x0040E6E2u, 0x00430C71u, 0x00421826u, 0x004401D8u, 0x0041FEE7u, 0x003C10DAu,
    0x0041F48Du, 0x0041DF0Du, 0x0043098Au, 0x003F0C54u, 0x0045038Au, 0x0040FA4Du, 0x00381170u, 0x0040F60Eu,
    0x003F00C5u, 0x003A1037u, 0x0041F187u, 0x0042EB2Au, 0x00402246u, 0x003FF430u, 0x003BEFAFu, 0x003B1A66u,
    0x003E01ECu, 0x0042F976u, 0x00430036u, 0x00401A2Eu, 0x003EF547u, 0x003EFBD2u, 0x003A1231u, 0x0041F90Bu,
    0x0042E632u, 0x003E1CE1u, 0x003D0328u, 0x0045FC85u, 0x003D0186u, 0x003912B8u, 0x003BFADBu, 0x003DF2D6u,
    0x003C1D22u, 0x003EF58Au, 0x0042E60Du, 0x003D14CAu, 0x003CF6C8u, 0x003CEF94u, 0x00381569u, 0x0041FB7Au,
    0x003FECB0u, 0x003B20B7u, 0x003EFE2Fu, 0x0042F170u, 0x0042061Cu, 0x00401DBDu, 0x0042FE1Fu, 0x0040EFB6u,
    0x003E1A24u, 0x003FF300u, 0x0046D914u, 0x003F098Au, 0x003F0969u, 0x0044FEBCu, 0x00000000u, 0x00430233u,
    0x004C2534u, 0x004AF38Fu, 0x00481CC1u, 0x004B241Au, 0x004A049Bu, 0x00441BD4u, 0x00400C0Bu, 0x004BDDF5u,
    0x00450102u, 0x00410733u, 0x00461042u, 0x0049EA54u, 0x00440104u, 0x004303C7u, 0x0048E047u, 0x004700D4u,
    0x00430184u, 0x004CE83Au, 0x00480AF2u, 0x0045FCBAu, 0x004DE590u, 0x0047EA26u, 0x0043092Cu, 0x00420C55u,
    0x00460ED3u, 0x0045F33Du, 0x00451F5Cu, 0x00430E03u, 0x0041F192u, 0x004406BAu, 0x0040F535u, 0x0046EE41u,
    0x00441560u, 0x0043086Du, 0x0047F044u, 0x0045EDF0u, 0x004713DAu, 0x00440B88u, 0x0045EC99u, 0x004506ACu,
    0x003C0597u, 0x0048EBCAu, 0x004308B2u, 0x0045FD40u, 0x0048EABBu, 0x004300E1u, 0x003E0EDCu, 0x00430AE0u,
    0x0049EA7Fu, 0x00460575u, 0x0043050Bu, 0x004BE95Fu, 0x0043FA40u, 0x004211A5u, 0x004411C9u, 0x0047ECADu,
    0x0040122Au, 0x00460CE4u, 0x0048EE67u, 0x0047136Au, 0x0044FF27u, 0x0041EE34u, 0x0042F715u, 0x0046035Fu,
    0x00420067u, 0x004EDD75u, 0x004A0A1Cu, 0x004303E8u, 0x0049F64Eu, 0x004BE596u, 0x00450ED0u, 0x00440B23u,
    0x00000000u, 0x003E0877u, 0x003EE8C2u, 0x004116D3u, 0x003EFE7Bu, 0x0044007Eu, 0x003F05C8u, 0x003FE8A1u,
    0x004118FDu, 0x003E0500u, 0x003FE98Bu, 0x004015E1u, 0x0041E666u, 0x0042123Eu, 0x003F033Cu, 0x0041F203u,
    0x004416BCu, 0x0045E414u, 0x003AF577u, 0x004010B6u, 0x0040F1EAu, 0x003F06B7u, 0x003FFEF3u, 0x003EF9BEu,
    0x003F1925u, 0x003F00F0u, 0x003EED28u, 0x003F1BDDu, 0x0041F588u, 0x0040F5D0u, 0x0044140Fu, 0x0041EB09u,
    0x00401C3Fu, 0x003E06DCu, 0x003EEB15u, 0x003F149Cu, 0x003FE7A3u, 0x004312A2u, 0x003EFBF8u, 0x003EEEC0u,
    0x00421C96u, 0x003E0640u, 0x003DEC28u, 0x00400FAFu, 0x003F0A28u, 0x003EEC5Fu, 0x00411029u, 0x0041EAE3u,
    0x004215D8u, 0x0040EB8Bu, 0x003CF8FCu, 0x003F0D41u, 0x0041EE2Cu, 0x0042052Cu, 0x003D0DB3u, 0x0040EB74u,
    0x0044134Au, 0x003D0EE3u, 0x0040EA7Eu, 0x00430E10u, 0x003E0D4Au, 0x0040EA35u, 0x004014B5u, 0x0043E009u,
    0x00431268u, 0x004005C6u, 0x003EEBF2u, 0x00450DCAu, 0x0040025Bu, 0x003FF284u, 0x004220CCu, 0x003EFE99u,
    0x003CEA61u, 0x00401BDBu, 0x00000000u, 0x003EED59u, 0x003EF16Cu, 0x003BF946u, 0x004410F3u, 0x00451938u,
    0x00420C4Fu, 0x0042EF37u, 0x003FE89Cu, 0x0040F01Au, 0x00450BF7u, 0x00462207u, 0x00420E1Bu, 0x00430E4Bu,
    0x004210E1u, 0x003D0776u, 0x0040FC04u, 0x0041EBF0u, 0x003EEAA9u, 0x0042008Cu, 0x003F0EBDu, 0x003F07E4u,
    0x004209CAu, 0x0040FCBCu, 0x0040FC0Fu, 0x0040EE38u, 0x0040F610u, 0x003E0718u, 0x00431A65u, 0x00421862u,
    0x00430DF2u, 0x00420C12u, 0x00430DB4u, 0x0044F8B7u, 0x0042F048u, 0x0040F8B6u, 0x00411A6Fu, 0x0045105Fu,
    0x00430A54u, 0x00410F1Du, 0x004209FDu, 0x00400A8Du, 0x004101AFu, 0x0041E76Fu, 0x0041FA65u, 0x003EFCB6u,
    0x003F00D0u, 0x004304B0u, 0x003DFE1Fu, 0x00411270u, 0x003F0397u, 0x0043F071u, 0x0042F654u, 0x004510BCu,
    0x00421345u, 0x0044124Eu, 0x00470FA3u, 0x00430E5Cu, 0x0045F739u, 0x0044E6DBu, 0x0043F07Cu, 0x00450A2Eu,
    0x004318AFu, 0x004402BEu, 0x0044F9EBu, 0x0047F821u, 0x0045F53Fu, 0x0044EAF4u, 0x0040E697u, 0x003EE751u,
    0x004006BAu, 0x0043179Fu, 0x003F0B85u, 0x00430A82u, 0x00000000u, 0x003BFD67u, 0x004701D2u, 0x004AE3E7u,
    0x00450654u, 0x00450715u, 0x0044FA60u, 0x00440AFBu, 0x004B00D5u, 0x004EEADCu, 0x004EE66Bu, 0x004E1D61u,
    0x004BE475u, 0x00461328u, 0x004C0F05u, 0x004DFA6Bu, 0x004BEE71u, 0x004AF014u, 0x004E1262u, 0x004AFC17u,
    0x00460948u, 0x0045F054u, 0x004AEE55u, 0x0047FC2Bu, 0x004CF828u, 0x0046F596u, 0x0047F9C2u, 0x00460C9Eu,
    0x00430476u, 0x004200B8u, 0x00471234u, 0x0047FAE4u, 0x004B1ECBu, 0x004BDF62u, 0x0049F100u, 0x004B1502u,
    0x00490761u, 0x0044FA53u, 0x004C0EF5u, 0x0048093Fu, 0x0048F647u, 0x004DFD57u, 0x0046F0AAu, 0x0044EB01u,
    0x004716F0u, 0x0046F43Fu, 0x0047FF10u, 0x004604A0u, 0x0043F1FCu, 0x00480446u, 0x00440209u, 0x004B1548u,
    0x0044FED6u, 0x0043FAC2u, 0x003FEB34u, 0x0048128Bu, 0x0047F934u, 0x004F1298u, 0x004B045Cu, 0x0049E59Cu,
    0x004AFC0Au, 0x004500DCu, 0x0049E8D9u, 0x00481298u, 0x004BF0B2u, 0x00500EB7u, 0x004803B7u, 0x0041F87Fu,
    0x004203F4u, 0x0046F790u, 0x0047F80Fu, 0x004DF9D0u, 0x0047F29Fu, 0x0046F7D4u, 0x00000000u, 0x0044E700u,
    0x00400B46u, 0x0040F142u, 0x004416F5u, 0x0046E56Bu, 0x00441A84u, 0x0045E7E7u, 0x00430CFAu, 0x00460A6Du,
    0x0046E7F6u, 0x00451AC1u, 0x0044EE06u, 0x00451668u, 0x0044EC0Bu, 0x003F10B3u, 0x003FF7BCu, 0x0041073Fu,
    0x00410128u, 0x0045E7CDu, 0x0048137Fu, 0x0047FBAFu, 0x004603B5u, 0x0047E644u, 0x00421642u, 0x0042E939u,
    0x00441866u, 0x0045EB4Au, 0x0046179Du, 0x00460661u, 0x00450411u, 0x0042EC5Fu, 0x0043123Du, 0x0042EC2Cu,
    0x003F0FA8u, 0x003FF238u, 0x003C07F3u, 0x003FFA6Fu, 0x0041EB11u, 0x00410CBFu, 0x0041F516u, 0x00440C97u,
    0x0043E997u, 0x004518CDu, 0x0044E763u, 0x0043FC6Eu, 0x0043F3B0u, 0x00431271u, 0x0040F31Eu, 0x00440F3Du,
    0x0044ED5Du, 0x00430F13u, 0x00450018u, 0x00450255u, 0x00460830u, 0x0043FA04u, 0x004203E8u, 0x003E069Bu,
    0x0040FE56u, 0x0040EFF8u, 0x00421343u, 0x0041F327u, 0x00450039u, 0x004912A1u, 0x0047EE13u, 0x004A1481u,
    0x0048EFCFu, 0x00431255u, 0x004ADA59u, 0x00461E62u, 0x0047E4DBu, 0x00481E8Bu, 0x0045EA92u, 0x00471AC9u,
};
/* the tapes, base64 */
static const char MF_B64[] =
    "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"
    "CiJToaq8IEIVmcuLMlSRqswgMSWpy4wjUqK5vDgyF6jKmiNjkrq8KDMmqMuMIVOTuq0pMiaoypsxY4KpvSkiJqjJqyJzgprb"
    "GSI1uMmcIVKDqswZIkSguqwwUgSpyxsiRaGqvCAzF5nLihNUkaq8OEIGmckLInKAqcooMSWp2ooScpGZyhghJpm6qzJzk6nr"
    "GSI0qMqdIVGDmtsaIjWouZ8RQYOa6gkhNJi6niBRg6naCiI1oLquIVEDi+saEmOgqLwhQQSZ2goSY5CZrSAxFZrKCxJFoam8"
    "IEEGmbkMEmKRqcsoMhaZygshc5GZ2yghFpm5jBFjkqm8KDEnqamdElKSmespETSoup4RUpOZ6xkRNZmprSFBBJraGiFEoKme"
    "IDEEiusaEUSgmbwgQQWZuQ0RYpGZyyghFpi6jBFzgYrLKREmmLmdEVKTmcwZEjWoyZwhYYKpyhoSNpiqrRFRhJnJChJEkKq8"
    "IFIFiroMAjWhqb0gUQSZygsSZJCZyyBAFJrJiwNkkJi8IDEWmsmKElSQmbwgMReZugwCc5GZyxAhFpm5jBJikqnLKSEnmamc"
    "EWKCmtoZITWpuawhcpOZyxoSJ5iprBFigpnKChI2oKq8EWIEmroLElaQmcsoQRSa2QoRY5GpyygxFqjJixJykqnKKSEXmKqM"
    "AlOSucspQRaZuYwCc5GZyhghF4mqmxJzkqnKGSIXmKqrInOCmtspMCaZupsSc5Op2ykwF5i5myFzkpnbKREmmamcEWKCqcsZ"
    "MSeZuZwSYoKayyohJ5i5rCFig6q7GzJXmKmcEVGDqcsrITeoqZ0RUAOayxsSN5ipnRBQhJmqCyI2oLmtIFEEmbsMEkWQqZ0Q"
    "QBSauowiVJCpnBhAFpmqiyFkgKmrKUEWqLmMEWOSqawpMCeouashc4OavBkhN5jKqhFyg6nLGREnmLmrIXGEmasaETeYuawR"
    "YYSZugoCVoipuhBRBZmqiwJlgKm6KEAWmaqLEXOTubsqMWeJqZoQcpKouioRN5i6uzFxhZm6CRJVmKmbKGEEqboKEUaRuroo"
    "cQSZqosRZYCpqxhBB5iqihFzkqm7KTFHmbqaAnSRqLoYMDepqpsRZZGZuxgwR5mamxFzgprLKSAnmKqrEXODqrsaMWeYqaoR"
    "YpOpyhkQN5iquyFxhJmqGxE3mKq7IHIEmroKEjegqrsgcgSpuooiN5CryyhyA6m7DCFGkKrKEFEEqaqLIkaQmssoUQSZuowS"
    "dJCZuihRFJqqjBFFkKm7OVIGqKqLIVWRmssYQRapmZwCY5KayhkxF6iZuxJzg6rKGiE3mZqsEWKDqsobIjeomqwgUgSqyQsy"
    "RZiavBFSBKm6jCJFoarLIFEUqrmNEkShiusoIRWpqY0RY5GayhgyJbmpnhFTkprbKTElqameEWKBmskqISaZqawhYoKa2ioh"
    "NampzCFRhJq5DBM1qJrMEUEFmrkMEkSQmtsQQRSayQwhQ5Gb+xgxFqqwjRJSkZraKSIWqaiOAkKimtopMRapqI4hQZKb6Skh"
    "JLmonxJBkprpGSElqZmeAkKSmukZIRWomZ0RUpOb2SohJaiKrhFSgprZGiE1qZmtIFKDqtkbEjaombwgUgOqyQ0hU5CayyhB"
    "FZq5nCJUkZrLKUEVmbmdElOSmtsZMReYqawRY4KayhoRJ5CarBBShJnJCxJFkJrbEEEEmcmLAlWQibsZQSaZqpwRY4Kayxox"
    "J5i5nBBygpnJChE2mKmsEFEEmbqMAkaQmbspUQaYqpsSdJCYuikhJ6i5qxJ0gZnKGSAXmJmrEXKCmcoaATeYqcsgQQWKqowC"
    "VJGpyyhAFZm5mxFzk6nLGTEnmbicAXKCmbsaIjeYupwQYQSpugsTRpCprBBAFpm6ihF0kZm6KSEnmLqcEXKCqboqETeYqqwR"
    "cQKZuxsSVoi5uiBRFZq6ixJlkamrGTI3mcqLEXKTqKwaETeYuZwgUIWYq4oSVZC4nCgwF5m5ixF0kairGCEnibqbMHKDqa0I"
    "ETaYupwQcoOZrAkRRZDJmihBBYmsChFUkbmcGEEVmcuJEVSRqZ0AMRaZyokRY4G5rBAxJpnLihFzgpqsGSI1mNuLEWIDqq0J"
    "EjaYyZsgUQWZrAkhc5CpqxBCFpnLCRFjkamsKCEnmcoKIFKTubwZMjeoypogYgOqvAkxVZC5rCBCBZnLiSFUkaqsEDImmtoK"
    "EWOSqp0AIiWp2okRYoK6uxhDJpnLmhJzgrnLGDE2mdqKEHKCqbsZIjeoyZshYYSZyxkhNajJqyFSBoq7CiI2sbmtIEEVqdoJ"
    "EVOiua0QMReYyooSYpKpvCghJ6i5nCFigqnLGSE2qMmrMWKEmssZEjaouZ0RUoOavBoiRpiqrCFRhJnaCRJEmKm8IVEEmcsK"
    "AlWQmawoMQeJugsSZKGouyhBF5m5ixJzoqjLKUEWmbmMAmORmcsYMSaZyZsScpKJ2ykgJaipnRFhgoraGRE1qKmtEUEFiroM"
    "AmSgmMooMQaZqYwRcpGJuykxJ5mprBFykom6CxM3mKm8IFEFirqLEzegmbwoQQeZqIwCU6GJ2xghFpmpjBFjkZnLKTEXmamN"
    "EWKRmcoYISaaqJ0RUpKp2RkRJpiarBFik5rKGiEnmKmsEWKTisoKEjaYqrwhYgOq2goSRZCq2yBBBZq5DBJzkJnKKDEGmbmL"
    "EmSRqcopIieauZsiZKKa2ighFqmpjBJykYraECEFmbmMEnORissoMRaauYwTc5CKyyAiFarIDBJikYvaKCEWmqmMEnKRisoo"
    "IRW5qI4CU6GK6ighFKq4jhJSoQrrOCAUqqiOEmGhCtooIRS5qI8SQaGK6RgSJLqojhJCoov6GBIUqamdMmGii+k5ESS6qJ4j"
    "QaKL+jkRFLm4jSNRoovrKCIVm6mOE0KxiswwEgaayBwDUbGJvEARBZvQGgNSsInMMSEEq9gsEmGwCbxBIISa2CsDU7CKvTIx"
    "BavoGhNSsAq9QSAEq9AbBEKwCtwxEAWqsB4CQaEL6jgRFLqwjyMwsov7OBEkyqCOE0Cyiuo4ESS6qI8iMKOM2jkSNMqYnjIg"
    "lJvYKhNTuJmuQRGDq9krFFKomb0yMQaryBsUQsGJ2zASBaqpDRNCspv7KCIVqqmOIkGii+opEyS5qZ8iMZSb2iojRLmZriJB"
    "hJvJKyI1sKrNITIFqroNMmKQmss4IiaqqZ0iUpKb6SoSRKiarSFChKrYGyJUqJnLMEEFqrgNElOhmtsoIhaaqY0hUpKb6hgS"
    "FaipnRJCg6vqGiI1qKquIUGEitkKEmOQirwgQQWpyAsSc6GZ2iAgBpm4DBFikYraKCAVmamNEVKSmukZITSpmp8RQZOa6RkB"
    "NaipvCJghIrJKwFFmJmsEVCEmbkMEnOQmcoQMQaZuQwRc5CJyhgxBpi5jBFjgZrLGEAVqKmdAmORmcopMBeZqI0BUpKKyykR"
    "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"
    "cbAAGqgnm5EIS5YKmKFwsQgJmSaqkRk7BwuoonnDCAmpNrqSGSwHi5CheLEYiag1uoIKLAcLsYF7simKsFW5gRmKF4uygHui"
    "Oqmwc8AQCpgWi6IJW5UrqKBxwRAKqCWboxo7hyvAkHCwEAq4NpuzKiyHK6iRerEgi7A1qqQaGxccoJBpoSibsGS4khoKBwqh"
    "mWiiKaqoRaiRCxoXGsCYeaIpqqBUmZIMGgcaqJhokRibqFWZkYoqBxrAmGChEKuYJomhi0qHGamZcaEAm5g2irKMOYcZyJBQ"
    "oQGNiDSKsYs5FxrZkGCRGJyQNInBCjsXGsmYcJEIi5hUibGKOQcayIhQkRicmESYoYwoBgm4mXiSGLugdJiRixkXicCISJMZ"
    "y5BzoJEMiBaJsIlYlBnKgGGQgIwIJZmwiliFCbqYZKCADIglicCJSIQJuphzoYCMiDaasApZhhq5iHGhgIyAJZmwClmFGrqQ"
    "cqGAjAg1msEKOIcJuZByoZCLiCeZsAlqlAm5kHOggIuIJ4qwCVmVCaqQc6CRDAgliriJeYMKm6h0oJAKKheKqKBwooiKmTa4"
    "oYpKBwqpkHCRCYqZJ5mgCUqGCqigcaEImgknmpCJSYUKuKhzsgiLiTeaoIpphAq5oHOhiIqJN4uxmWmECqmYcrEAiwonmaCJ"
    "a4UKmbBzoICZCieKoJlooxmauWWogYorF4qgmHmSGZq5RpmRihoni7GweLMYmqlVmZGZOxcLoKh4ogiJqkaZkJg7BwqYwGCh"
    "GAqrRpmQiDyGGpi4cKEYCqpFqZKoPIcamLBgoRiJq0aakpgshxqIsGmiKQq6RamSiB2GCZCweaEYCaolmZGYLIcakLhwoRga"
    "uyeKkaA8hhqYwGCgEImbJpqjoD2WKpjAYKAAGZwWipGQTKMqiOhRqAEJjAYKkaFMszkK6FGwEBmcFguhkUyjKgnwQbgRGZwl"
    "m5KRPqQqiOBRuAEYqxaKkZE9pDoK4VCogRibBwqQkVuzKhnxUKgAKasXC6GCLqMqCeJQqAApuhaKkII9oysI8UCwACisFoqQ"
    "gi2lCQjBaKCAKLsni5ACHoQbiNJosAA5yjSboRIflBqYs3qxGBnQQpqhIY0FCpiSa7IJGOBCqZEojAeKkJJbsyoK4lG4gTmc"
    "FpqhATylGgnSYKgAKqkmm6EBPKYqicFwqIEquCWasREtlRqZs3qygDvYJYugES2FG4qzebEAO7kni6ABLJcJibJ4oAAruDWb"
    "sSAthgqZonixgCrIRJqgECuHCqiieKEIG7BUmZgZCRcLuIF6sxmL0mKokCmKF4qwgWuTCprCcaCAK7gnmqEIS4YKqaJ4oQgb"
    "wFSpkSqKF4qogXuiGZqhcrCRKqkni7GAa5QKmcNwoAgJqCaasRg7BwuokXixABu4NrmhKS2HCqCRabIZCrhGqYAKGReLoJFq"
    "symbsHWpgQmJB4qhCFqjGrigcrEAG5o3m6KKOQcbuJB4sxmLsEapkRsqF4uxkHmzGZqwdKiBGwoXi7KAWqQqmqBysAELmSea"
    "oolblRqwmHGxKIuoRZqSiykHGsCYeKEomqlFuAGLGhcKsal5oymq0FOoAo2IJYuimlmUObvAcqABmponmZKbSoYasapxkSir"
    "mVSoAp0ZFgqxilmTOcupZJiCnAgVicKZSYQp2YlSkIGcCSWJspw4hwjImEGSGMuJNZChjhkWCbiKaJMp24hikJGMGRUJwIpI"
    "hRnKkGGRCJwINInAikkFCdiIWJIIuolkkJCMKSWKsItohAnKkHKQgJsIJpnBCkiECcqIcpCAmwgmmbGLOBcK2IhRogiciESY"
    "kA0YFYrACViTCbuIVaCQjBgXirAJWIMKu4h0kIiMGBaZsIlogwq6iVWgkJsYJ5nAiUCVCbmIcqGAjAg1qrGKaYUaupByoQib"
    "CDapsYtIBwqpiXGiCaoIRZmQm0kHCriIcJEZm5hFqJEMGReKsIhpkxq6oHSgkAoJF4qgmHmSGaqgc6CACxoXCrCJaZMJqqhz"
    "soAMiSeKsIhqkyq6qHOykZsaN4rAiHuTGqmocqGAi4k3mrEJTIUaqbBxkQiLmTaZsYk8BxqpsHCiCIqqN5mxCTyHCaigeJEI"
    "irhFmaEJGxcKsLB5owiKyUWZkQobFwqwoHmiGJq4VZmRGgwWCqCoeaIYishjmYEKCxcKoKBqkhiLyVSZogkcBhqowWihAAq5"
    "RYqRiSyGGZmweKEACas2mqKILoQZmdBgoAEKqiaKoqhLlimZ0FCQAAqqNpuTmS2GKqjRUKAQGrs2mpKJLYUZqNFooAEaujWa"
    "o4gfBCuY0VixECrLRJqSgB6EGpDBabEQGco0qpOIH4QakNFYsRAZ2iSaowAfgxqQ4kigEBnLFoqigR2VKpDRWKAQGbsmi5IA"
    "H5QakbB5sRAZujWrk4AflRqAwVigGBnJNKuDCA+FCpGharEoGekzuoMpDgUckLJ6oCgJyEOqkhAehBuQwmmxKBrgQqqSEB2E"
    "G5CyerEQK+Azq6M4H5QamMNpoBgZuSabojAflCuYsnmxACrINKuiIB+VGpiierEAKsg0q6IRHpUamLN6sQAqyCWbsiEehBuo"
    "pHqgGBqwNLqyQA2GCpiSarEAO+AzqrEhDYcKmJJ6sQAqyFOaoDAdhRuZo3qhCBrANJuxMB2GG6ijeqEIK7g2q7IwHoUbqJJ5"
    "sRgrwFOqoigMhwmogWqhCArSUqmROpoXC7CCa7MZC+JSuJE5qyeLsINdohkLsXGwkSmaJwygAEqkGoqycbCRKpkni7CBe7MZ"
    "C9JymZAYigcKqIFqoioL0lKpkSkLFwuogXqyGQrBYqiAKgsXi6CSe6IZisFjuJE6CwcKqKJ6ohkLwWOpkSkLB4qQkWqiGYvB"
    "VKmAKooXC7CSepEZC7BkuIApqieLoAFNkwuJsXGxgBqpN6qQCDsHC6iyeLMJGrlGqZAYCyeboJF6sxkL0WOpgSqLF5qhkWuT"
    "G4nAcrAAGponi5CATJQKmLFxoBgKuTebkQg9hRuYwXihGQm4RKmRiCwHC5CgeaEpibhUqYGJCheLoqB6ojqoyGOoAImKF5qj"
    "mVqVK6CocLEQiqknmpKZKgcKoMB4kRmJuTWZgAkOBomQmGmiGYnJY5iAiQsXmZGoWoQKmMlykAiJizaakalbhRqxunKiKaqK"
    "RpoBq0oGG6GrcbIouYpVqYOcKQYbspthojnpiFKZAo0IFBvCikikOeiIQbASrAglirMOKJQp4IhAoSC8gCWZo44QBCrgCTCy"
    "Mc2AQ5mTjwAEGtEJSJEg2whCmIKPGAMp4AowgiDsCDKIkp8AIxnhC0gCGPkJMYGBnwgziMGNIBMY+hoxAQCvGCIC0I0gEoL7"
    "CRISob4QIiPpnCEigvsKElKoyyghFdmLAlGSyyuCZKm5KBEXm6sicJGqG4J0qLk4GAeamoJwoZkrkDequCg5B4uqk3DDmCqY"
    "J4u4AWuVCpmhcaCIKpkni7CBeqIJirFkqJgYChcLqIB5ogkKwESpkBgcB4qYkWihCQmpNqqhCDwHC5ixcLEAGqonmqGAOwcb"
    "mcFxoIAJmheJmJBakwqJ0HKokRmKBxqogFqjKovAc6iRCQoHGqiQerIAibk2mZAJHYcJmKBgkIAJmyaJoAlLlRmYyXGQGIqp"
    "JgqgCSyHGZmweKGBCskmiqEIHIcqqKB4oAEKuSaKshkslympoXigARu5JoqhCC2WKZqxeKEYCrg1i7IIHoYJmLF5oQAKuDWa"
    "shgfhRqYsXmxEQvINJqiCB6GGpixebEBGrk1qrMYH4UamLF5oRgayTSashAfhBqYwmmxEBrJNKqzEB+EGpixerIoGspEmqIA"
    "HYUamLF5oRgayWOpohANBRuYsnqhGAnIYriBGIwGCpChWqMaCeBRsAAZmxeKoZFMlBqJwGGogRmbF4qhgFuTCgnQYaAIGasn"
    "iqCAS5aJCLB4oQgJmiapkAAthgoJsHChCRmaJpqYkjyGCgmocKGYGJommZmBPAaKCqB4oqkQmXOYmpIaJ6oaiECUqxCbZaCL"
    "ghsnqgqyWIWrKKlzsZoCCyeqGqFpk7o5mWSwmgILN5uLpEiUukmoYpC7Ayo0yQ2SOIW6OphzkMsSCUTZGpI5hbpKoGCinSKp"
    "U7kblCqHqkmgQJC6MotzqI2EGQK6SqFYo60yyXGwCoOKJcs4skuVq0LJUbCJA6s2uyjDS4SbINBQsZokjSKpKcIZFrtAwEmS"
    "myOdUqgKlAsVqjjIOIWcIrtyoAqkCiTJObEsB6oSqliziwS7c7ApsRwXmxG4SpULArtykBqRizW6OMAqB5ogu3CSm4SLQqA7"
    "sItHySCpKAWrk5l4oy6giEPYKIooFKyCiWmkHJiRYtAYiBkUu4KYaJQOgZhC4AAICRSrgZhoow0AqGO4iAAsB5sBuHChiQCp"
    "RaoIkS6GigDAULEJEKsnqgCRPIULCOFQoAkQukW5gAEdhooYwVixiCC7NrqAky+ECxjhUKiAEKsXmoCjPaQJKeBBqIAgjhSL"
    "gLNMpBop8EGpgRCMBQuAs02iCSjoUamBEIwFioDDWrIZOelCqYARjgWKALF7oQg52UKpgBEOBAuAsnuhGSjaQ6qBEQ4ECwiy"
    "e6EJKOhCqYARjQWKgLJqsgko6EKpkCKPBIoIs1uzGknoQamAEo0FixDCWrIJOOlCqZASDQSLEMFqoQkg2iSqAJMehIoo0Wmg"
    "iBG6JZoIky+TmjDxSKCIEaslqgiVHIWaIMBYsYkirCWqCJQtlJow0FigiSKsFpoIky6TmjDgUKiIIZwVmgiTLpSKKOFAoIkh"
    "qxaaCIItlYoowGCYiRGqFpqIkz2VihjRUKCYIJsXmoiTPZSKKdFgmJggqjWriIMvhJoZw3mgiCjIQ5qIEByHComSa6KJKdBi"
    "qZAgmgcKmZNrsggKwmGokCiqJ4uoAjyWComyeLGROroniqgRPKYZC9NgqJEpmhcLqBItpBkK0lGokSqZFguYAT2lGYrCcKiB"
    "Krg1jKEAK4caqZJ5oAAKsEWaoSkchxqZklrDGAvBY5qiKQsHG7CBarIYi8JymZEqihcLsAFbszmcsnGwgTuqJwyxgVqzGQvR"
    "cbCRKakWC7EATJQaqbJxwQAaqCaLsSgslxqYoXigAAqgNZyRGBsHG5mReaGIGbhUqZEqiieboAFMpRmKsXGgkCqoJ5uhGCoH"
    "C6mTerIIG7FzuKEpiSeMmIJrsxoK0mK4gBmJF5qYATqXComhcLEIK6g2q6EoLAeLmJJ6ogoJsWS5gCgLB4qok3qyGgmxcrCI"
    "KYonjKCCWrOZE4StKybCvjBHuK9RI/qccxPuGTbAnlGDvTklyJtSgtoZM6HMKEOgvDglqKwoJbCsICWorSEVuatSA8qLU4LK"
    "G2KByhpCgtoKQpPKC1OC2gpDktoaQ5LbGlORuxpEktsJQ5LbCUOS2xlSkbsqRJG8KUSgyyg0mL04NKiuITO5rjEUuZ0yBMmL"
    "QgPaC1KC2hpCgcsqU6HLGDSgvDkmqMswJLitMSTJq0IEuZwyBbmMQQO6jUEDyptDBMoLQoPaClKByhpDgdsaU5HLGDORvSk0"
    "oa0pNKDMKDSovDgWoKw4JamsMRW5nDIEyYxCgsoKQ5PbCjSS2xpTkcspQ6G8KTWgvDg0qL0gJqm7QBWpnDEUyZtCE8qcUoK5"
    "jDOEygtDg9uKU4LaCkOS2gkzo8wqY5DKKUOgyyg0oL0gNKitOCSorTAkuLxAFLicMBWprDEVubtSBKmsQgPJm0IEyZtSEsmM"
    "MRS6jUGDyYtChMmKQoPaikOT2gpDgtsaQ4G8GjWQyypEkMspNKDrKDOgvSgloMsoNKC9KCWgyygloLw4Jai8MCWorTAkuZ0x"
    "FLmdMRS5nTEUuZ1BA7mdMgS5jTEEyYtChMmKQgLKi2KCyQtSgsoKQpPKC2OBuhxSgcoaUpHKGVKRyglTkcoaU5HKGlOR2hkz"
    "kcwZNKHbKTSYvCgloK0gJKitISSpnTAjuZ4wJLmdMRS6nEEEuY0yA9qLQ4TKilOByhpSgdoZQoDaCEKRyxk0oesYJJjLICSo"
    "vDAWqKwxFKmeIhO6jjEEuoxCg7qNM4TKC1OSyhtTktoaQ5HqGUOQuyo1kOsYM6HMGDSgzBAkoLwgJZitICSorSEzub1BFKmd"
    "IRSpnTEEqY0hBKmNIQS5jEKDyotDhMobUpLZGkKB2glDkdoZQpHLKVOg2igzsMwgJKi8MCWorTAkuaxBBKmsMgWpnEEDyYwy"
    "hMkLQoPaC0OD2wpSktkaQpLaGkOR2hlSkLoZRJDLKTSQzBg0mLwoRJisKDSovDglqLwwJaitISS5nTEUuZ0yBLqNQoLJC1KC"
    "yhtigcoJM5LrCTSA6xgyoesYQ6DLOEOovDAWqKwxFKmeMQS5jDEUup0yBbmMQQK5jUGDuY1Bg8mLUgK6DUECygtSgsobYoHK"
    "CUORyhpTkcsZNKHMKEOgvDglqLwwFqicIBWpq0EEuI0hBLmMQoPKDEKCyhtSktkaQoHaGkOR2xhDkNsYQ6DbECSYvDAkqMwh"
    "FKi8MRWorTEUuY0xBLmNQQK6jEOTygtTgtoKQ5LaGlKBywlTkcoqQ5HrKUKQyxglmKwgJKitMCSprTEUqZ0hFKmdMQS5nEID"
    "yotSg8oLQ4LaCkOC2xpDgfsIM5DMKCSgvCAWmKwwFLidMQS4nTITyo1BAsmLQwLaG0KC2gpTkcoZQ5DbGCSQvCA0qK04JKit"
    "MCSprSEVuKwyBamNMQPJnEID2gtSgsoKUoHZCUKByxlDkdsZNJDMKEOYvCAVmKwgFaisMRS4nTEEuI4hBLmMMRTJjEECuY0y"
    "A8oNMQPaC1KCygtTgtoaQoHaCVKA2RkygesZM5H7CDOAzBhDkLwoJZi8ICWonSAUqJ0hBLicQQPJDDGEyQtSgtkKMoPqClKB"
    "yhpSkcoZQ5HrGEKQyxBDmLwgJKDMICSovCElqJ4RBKicMQS4jTETyo1BAskLQgLaClKB2hlCgMsINJC8GDWYrRAVmKwhFLid"
    "MRS5jiEEuYxCgskLQ4LqCUKRyhlDkdsZU5DLGDSQvRA0mK0oJaisIBWouzEWqJ0hI8mcMQW4nDEFuYxBA8kMMQPaC2EBugtT"
    "guoJQoHaCUORyxk0kMwQQ5i8IDSovSElma0hFKidIAWonDEEyItBA8mMQQPKi1KDygtSgsobYoHKCVKR2QgykdsINJDrGEOY"
    "rBAVmKwRFZisMBS4nSEUqI4gE7iOIAS4jDEEuY0xhbmLUoPZC1KSyQpTgMoZQ5DqGEKQuxhEkLwQNJitKBWYrDAkuZ0xFLmO"
    "IQS5jEECyItBA8mMQYO5jTKEyQtCg9qKUoLZCkKC2hpCgdoJQpLaCUORyhpTkdoZQ4DbGUOAvCk0kOsYQ5C8GDShzBglmKw4"
    "JKitICWprDEkuZ0wBaicMRS6jTEEuY0xFMqLQoTJC0KD2htCgukKQgHKClKByhpSgcoaUoHLCUOS2xlSkcoZUoDaCDOR6xlD"
    "kMsYNKDrKDOgvRAloNsQQ6C8IDSYvSgloKwoJajLIDSovTAksK0gNLitMCS4rSEVqKwwFaidMBS5nEEDyZtDA9oLQoPaG1KB"
    "yhpSgcoKU4HaCTOS+xlCkdoZM5HMKVOg2igzsMwgQ6jbIDOwvSAloL0hFaidIRS5nDIUyptTAsoLUoLKCzSC6xpDkcsqNJG9"
    "OTSovDglsLwwFqicMBSpnTEEyJtCE9qLQgPLi1ODywtTk9oaQpLLGjSSvRklkcwoQ6jLMCS4rTEkyatBFMmLQQS6i1OCygs0"
    "gswaJJLbGUOR2xk0kLwpJaG8OSaorDAkqZ04JLitMSTJq0Ij2ptSEsqLQhPLjEKDygtSAsuKQ4PLDEOC2gpCg7wKU5PbGkOS"
    "vCpEobwoNKC9MCS4vFAjyatBFLmdMhTKm0OEygpSgssaQ5K8GkSRyxk0ocwoQ6C8ODSwvTAluJ0wFLmsQhPJnEID2opCAsqL"
    "QwPbC1OCywpTkssaU4HLGjSivCo1kLwpRKDLKDSw2ygkkMwoQ6CsOTSozCAkqLxAI8isQRO5nTEUyatDBMmbUgPKm0MUy4tT"
    "AssLY4HKGkORyyo0kcwZNJC8KTWgvDg0sK4gJLisQCPIrEETyatCFLqcQhPai0IDyoxCAsoLQgPLDEICywtTgtoKQ4LbCkOC"
    "2xpDktsqQ5G8OjShvSg0sa05NLCtKCWoyzBDuLxAJLmsMRW4rDEVuawyFbmsQhPJnDIUyptSA8qbU4PKi2OCuwtjgsoLNJPM"
    "CVORyipDodspNKC8OTWovDg1uLwwJbisMCWprTEUqZ0xFLqcQgPKm1ODyotTg9saQoLbGkOR2hlDkNsoM6C9OCWwrTAkqa0x"
    "JLmtIhW5nDIUyptDhMkLUoLaGkKB2glSkboaRKHaGDOhvShEmK0gJLisMSS5rTEVqZ0xBLmMQQPKi0IEuo0yBMoLQoPaC0OT"
    "2gpDgtsaUoHaGTKSzClDkK0oJLC8MTS5niAVmZ0hE8iMMRPZjEESyotDg+oKQoLaGlKRyhlSkcsYQ5DbGEOQvCglmLwgJais"
    "ICW4rCEVqJ0hFLmcMQW4nDEFuI0xA8mMMQW5jEGDyYtSAsoLQoTKCkKD6gpCgtoJMpPrCUKS2glDkdoIM5HMGFOQvBA0mL0g"
    "JaisICWprCEVqJ0hBKiNIBS5jTEEyQtBg9kKQQLaCkKC2gpSgcoJUoHKCVKByxlSkcoJU5HLGEOQ2xhDkLwYNZi8ECWgvCA0"
    "oK4QJKC8IDSgrhAkoLwgJZitECSgvCAlmK0gJLisIRWonREFqJsxFbmNIQS4jTAFuQwxBMoLUQLKilKC2QpCgtoJQYLaCUKB"
    "yxlDkcwYJJC8ECWgvCAlqKwwFbicMBWpjSAFuIwxBMkLQYPZilKByQpSgbobY4HLCVOB2whDkMsYU5DLGDSgvCg1qK0gJKit"
    "IRWpnCEFqJwhBbibMRa5jEADuY1AA8mLQQTJi1GDyQtRAsoLUoLKCkOC6xlCkcsZU4DbGDOhzBg0oLwoNaitICSorREVqJwg"
    "FaiNICO5njETyQ0wE8qMQQPaClGCygpSgcoJQ4G8GUSQywhEkKwYNJitEBWYrCEFqIwwBLkNQALJC0IC2gpCguoJQpHKGUOR"
    "2wgkgLwYRJjLEDSovCAlmK0gJKieIBS4jCAUuI0wBLiNIQS5jEED2QtBA8oMQQLZikIC2gpRgsoKUoHZCUKRyhlDkdsINKDb"
    "EEOYvCA0qJ4QJKmsMRW5jEAD2ItCg9oKUoHKCUOR2xhDkLwoNKC9ICWonSAUuJwxBamNMQPJjUECyQtRgskKUZLJGkKB2xlD"
    "gNspM6HcGEOQvCglmLwhJKitMCSprTEUqZ4xE8mcMhS6nTIUuo4ihLmMQoPZikKCyQtSgsqKQ4PqCkKC2hpCktoJQ5HaGUKR"
    "2hkzoswpU5DLKTSgzCgkoLwwNLi9ISWorSEzua4hFaidIRS5nDIEuZ0yBMmbQxPLjEKDygwyhMoLU4LLGlKSywk0kdsZNJC8"
    "KTWYzDgzqK4gJKitMCSprTEzyZ0hFLidMRS5nTEUyZtCE8qMQQPKi0IEygtCg8oMQoLKC0OD2xtDk9saQ5LrGUKR2hkzkr0Z"
    "RKHLKTSQzCk0oLwoRKC8IDSovTA0uMwwM7iuMCS4rSEkuK0xJMmrQiPKjTEEyYtCA8qMQgLKi0MD2otDA9sLQ4PrGkKS2glD"
    "kcsZU5HLKlOR2ylCkbwYNKHMKEOgvCg1qLwgNbi8MCW4vEEzyZ0hFKmdIgS5jEEDyotCBMoLUoLKCkOC6wkzgtwZM6LMGTSh"
    "vCk1oMwgM7CuICS4rEAzup4xBMibUQO6jTKEyQtSgtoaQpLbGUORvBg0kL0QJaC8MCSoniAUqJ0wFLmcMQW4jTEDyYwyBMqL"
    "Q4PqCkKC2hpCkssJU4HbGUOR2whDkMsYNJC9ECWYvDA0qZ4gFLicIRWpjTATuZ4xFLqcMgW5jDEFuY0yA9oLQoPaG0KD6xpC"
    "ktoJQpLaGUKR2hlCkdoZQ5DaGEKR2xhCkbwYNJDMGDSY2xg0oLwoRKC8ECWQvCgloK0gJKi8ITSprSAWqJwRFKisIRWonREF"
    "mJwgFKidISPInCAWmZwgFKidIRSpnSEUqY0wE7mPIRO6jUADuY0xBbkNMITIC0GD2YpCAsoLUoLZCkGD2gpCgtoaQoHaCVKB"
    "yhpCkuoIQoDLGFKQyhhSkLsYRJC8EDSYrSg0qK0gJKitICWZnSAUqJ0gFamMIAWojCAUuYwxBMmLQoTJC0KD2gpRgsoKUpLK"
    "GlKS2hlCkcsZU4DLGTSQzBBDoLwgNKitICSoniAUqJ0hI7meIRS5nEETyYwxBMmLUYPZCkGD2gpCgtoaQpLaCVKRyhlSkcoZ"
    "Q5HMGCSQvBA0oK0oJaisIBWonCAVqYwwFMmLQQPZC0ED2opCg9oKUoHKGlOA2xhDmMsQNKi8ICWYnhAkqZwhBbiMIQWpjDEE"
    "yYtCA+oKMoPrCUKS6ghCkMoYQ5C8KDSYrSgkoLw4Jai8MBaonDAUuY0hBbmLUQPKi1KDygtSgtoaQoLbCVKRyhlDkbwYRJjL"
    "KDSYvSA0qK0gJLisIRapmzEFuYxBA7qNMgTKi1KDygtSg9sKUoLLCUOS6xkzoswpU6DLKDSgvSA0qK0gNKmeIBS4nDEUuY4x"
    "A9mLQgPaC1KC2glCgcsZQ5HbGVOQyxg0kMwoM6C9KCaYrBA0qawgJaidIBS4nDAGuJtBBLmcQgPJjEEDyotSg8oLUoLaGkKS"
    "2glDkdoZUpDKGEOQvCg0oL0gNKitIDSprTAkuK0wFaisIRW4nDAVqZwwFbmcIgW4nDIEuY0xBMmLQgPaikIC2gpCgtoKQ5La"
    "GkOB6xkzkcwpQ5C8KDSgvSAlqLwwJaitMCSpnSAVuJwhFamcMBWprDEUyJtBE8mcQhLKi0IEygtCg9qKU4HKCjSS6xlCkcsp"
    "Q5G9KFOgyyg0mL0gNLi8ITW5nTAkyZtBBMmKMgTKi1KDywpDgtsaQ5LrGUKR2hkzoswpU5C8KDSgrSglqKwwM7i+ISW4yzAl"
    "qbwxJbmsMiTKq0MEyZtCBKqMQQPKizMFy4pDg9sKQ4LbGlKByhpCgtsaQ5LMKTOivSo1ocwYNLDLODSwvTA0yKwxJLmdMCS5"
    "nTEUupxCE9qbQxPrCjID64pDg7wbY5K7GzWS3CkzocwoQqG8KCWgrSAkuMsxJMi7QRS4nTEj2atCFLqcQSPKnDIUyptDBMoL"
    "QgPbikMCy4s0g+sKM4PrCkOCvBpjkcoJNJG8KjSizBk0obwpNbGtKDSwrTg0uK0wJLitISS4rTEkyatCBLmcMwTKDDKEygtT"
    "ksoKJILbGkOS2wlDktsqQ5HbGVOhyyk0ocwYQ6G8KDSxvSA0sL0wNMCsMCS4rTEkyatCI9qbUgO7jTOE2gozg8wKNJK8GiWS"
    "vCpEobwoNLHMOEOovEgzyKwxJMicMBW5q0IUyZtCE9qLQgPaikKD2opDAtsKM4TbCkOTvBpEkdspQ7HbKEOgvDg0sK04NLit"
    "ICW4rEAjuZ4hFLmcQRPZizIUyptCFMqLQhPbi0OEuoxDg9sKQ4LrGUKRyylDobwpNaC8OUSgvDhEqKwgJamsIRW4nCEVuYwx"
    "FLqNMRTKi0KEugxCg9qKQ5PaCkOSyxpjkbsqRKHLKUSYyyg0qLwgJbCtIRWpnDEFuYxRAsqKUoLKCkOCvBpEkbwpNKHMGDSw"
    "vDA0uL0xFaidMBS5nEETyoxBg7mNMoS6DFKCyxpDktsaNJHMGEOQvCgloLw4Nam8IRaonDAUuY0xBLqMUoLKClKSyhpTkcsZ"
    "U6HKGTShzChDoLw4Jai8ISWorSEUuKwxBribQQS5jEEDyotSg9oKUoHKGlOA2hkzoesYQ6DLKDSovCAmmawwJLmsQSPJnTEU"
    "qo0xBLoNQYLJC1KCygpSksoaU5HLGVOAyxk0kLwpNZi8KDWorSAkqK0hFKidMBSpnSEUuZxBA9iLQgLJi1ICygtSgsqKU4LL"
    "ClOS6hlCgMsINKHrGEOgyyg0mL0gJZmsIBWorCEVqJ0hBKiNIBS5jDEEyYtChMkLQoPaClGC2QpCksoaUoHaGUKR2ghCkcsZ"
    "NIDMGEOQvCg0oMwoNKi8ICWYrSAkqJ4gFKmcMRS5nTEUuZ0yBMmLUYPJi1KCyYpSgsoKUpLZCUKBywljkMkIQ5DLGFOQ2wBD"
    "iLwQNKDMEDSZvCAlqKwgFqicMBSpnSEUuZxBE8mMIQWpjDEEuoxRAskLUYLJilKCygpSgcoaUoHaGUKR2hkzkcwZNKHrGEOY"
    "yxA0mMwQQ5i8ICWorCAlmZ0QJKidIBSonTAEqJ0hBamMMYS4DTCEuIxBg8kMQQLKC1KCyQtSkskKUpLKGlKB2hkykusJU4DL"
    "CFOAywg0kNsYQ5C8GDWYvBAlmKwoJaisIBWYnSAUuJxAE7mOMAS5jDEFuYxBg8kMQYPKC1OSygpTgdoJQ5HLGVOR2xhCocsY"
    "NJC9EDSYrSg0qLwgJZieEBSYnSAUqKwhFamrQBS4jSAFuJtBBLkNQIPJC1GCyQtSgtkKQoLKG2GByRpCgdoJQ5HLGVOA2wgk"
    "gLwYNJDMEEOYvCA0qK0QFpicIBSonSAUuJwwBqiMMAS5jDEFuQ0hBMkLQQPaC0KD2gpRgsoKQ5LaCUOR2hlSgLsZRJDbEEKQ"
    "vBA0oK0oNKitOCSorSAlqKwgFaicICS4nSAVuKtAFamcMBW5jDAFuJxBE8qbQgTJi0IDygxBAsoLUgLaClGCygpSgcoJUoHa"
    "GTKS6xlCkcsZNKHbGFOIrBgkkLwoRJi8ICWorCAVmJ0gM7meMBS5nDEVuY0hFMmLQRPKjEEDuo0xBaqMQYPJi0KEuYxCg8qL"
    "YoK5jEKD2QtCg9qKQ4LaCkOS2hpDkdoZUoC7GUSQ2ilCkdsZU5C7KUSQ2xhDkNsYNJi8KESYvCA0qK0gJKi8MCWorSAVqLsx"
    "JamtIRWonSEUuZwyBMmLQQTJC0EDygxBg8oLUoLKCkOBywlTkbsqc5HLGFKhyilToMsoNLCtICW4nSEUuZxBE7qOMQTJi0ID"
    "ywtSk8obUpPbGUKR2ylDodsYQ5C8KDSgvSA0qK0gNKmtICWZrSEkuaxBBKmNIQS5jDIEygtCA9uKQ4PbG1OByxpTkdoZQ6HL"
    "KSWgyzhDqLwwJbi8QRS5nDEFqY0xBLqMQgPLi1ODywtTk9saQ4HbGUOh2yhSoLs4RKjbICSovCElqZ0hFLmcMhTKm0MTyw1B"
    "gskLQ4LqCUKBywk0kesYQpDLGDSgvDg0sL0gNam8IRaorDEUuK0yFLqNMRTKi0KEyQtSgsoKQpPaGkKSyxpTgcsaNJHMGFOg"
    "uyg1sMwgNLi8QDPJrEETuZ0xFbqMQQPKi1KDygtSgsoKUoLLCmORuhpEkdsYQqHbKEOgvDg0wLtAM8isMCW5rEEj2ZsyBrmM"
    "MgPai0MD2wtTgtoKQ5LaGlKRyhlCktsZQ5G8KVOhyyk0kMwoQ6C8KDWorSAkqK0hJKmtMRS4nSEkuY4hE7qdMgW5jSIEuoxC"
    "A8qMMgTZijKEyYtSg8oLUoLKG1KCyxpDkusZM5HMGTSh6xgzocwYJZisOCSorSAVqLsxFqidMBSpnTEjyo0xBMmLQgPKDDEE"
    "yotTgsqKU4LaCkOB2glSkboaY5HKGVKRyxk0kcwYQ5C8KDSYvSAlqKwgFaisIRWorDAVuI0wBLiNIQS5jEED2QtRgsmKUoK6"
    "DFKByQpSgcoaQ4HrCEKRyylDkNsYJJC8KESYrCg0qK0gFaisIRWonSEEuIwhBamcMgTJi1GDyYtSgskLQ4PrGkKByxo0ousZ"
    "JIDbGEOIvBglkLwQJZi8IBaYnBAVmZwgBZicMBS5nEEDyZtSg8mLUoPKC2GCyRtCktoJQ4HbGUKR2hlDkdsZU5DKKUOQvBg1"
    "mK0QJKisMBWonSAUqJ0hFKmdIQWpmzIFyQtRgsmKUoLKClIBygpSgcoaUoHaCUOR6hgykLwYNZi8ECWYvCAlqKwgJZmdIBS4"
    "nCEVqY0wE8mMMQTJi1GDyYtSk9kaUYHKCVKRyghTkMsoQ5isGCWgrCglqKwgFZidIBS4nDEUyZtChMiLQoPZilECyopTgcoa"
    "UoHLGVOQ2hgzkL0YNZi8ECWYrCglqKwgFaicMBS4nTAVuatRE8mMQAPIjDEEyYtRg8kLUQLKC2KBugpjgcoaUpHKGUOR2xlT"
    "kLsYRJC8EDSYzBAkoLwQJpi8ICWorCAVmKwgFaicMBS5nDEFuJxBA8mbQgTJi1EDygxBg8oLUoLZClKByglSgMoIQ4DbCFOI"
    "yxBDmNsQJJi8ICWonSAUqJ0hBLiMMBTIjDAUuY0xE8qMMQW5jEED2opCg9oKUYLKCkOS2glDgesIQoDLGEOQ2xhDkLwoJaC8"
    "IBaonCEUqY0gBambQQS5jEEDyotSg8oLUoLZCkKC2hpCgdoJUoC6GkSRvBg0kK0YNJi9ICWorCAVqKwxJLmOIBS5jEATyotR"
    "g8mLQgS6DUGCyQtSgsoKUoHKCUOB2xlSgMsYQ5C8KDSgvRAloKwoJaisMCS4rSEVqJ0hFKmdIRSpjSAFqZsyBbmNMgPai0OD"
    "2gtSgtkKQoLLGlKSyxlDotsZNJC8KTWgvChEmLwgNKi9MDS4rSAluKwhJbmcMRS5nTEVuoxBA8mMMoTJC1ECygtDk9oKQ4K8"
    "G0SSvBlEkMspU5DLGDSgvCg1qLwgNamtISSprTEkqp0hBbiMMQTJi0ID2gtCA9sbUpLKGlKBywlTkdoIM6LMKUORzBhDkLwo"
    "NKC9IDSozCAksLwwNLitICWorSEkqZ0wI7meMCS5nSEVuZwyFLqdMhTKizIGuotSA9oLQoPaCkKCygtDA+sbQoLaClKSugtj"
    "gsobUpLKGlKSyypDotsZQ5HbGVOh2hhCocsoQ5C9IEOgrSg0qMwwM8isISW5rCIVqY0gFKmcMRS5nTEUyZtCBLqMMgW6jEID"
    "y4tTg9oKQoLbGlORyylDobw5RKC8IDSovTA0uK0gNLitMBWonSAkqZ0hFLmcMRWqnEETyowyBMoLQYTJilKCygpCgtoaQpLL"
    "GVOhyyk0sbwoNbC8ODW4rSAluKwxJLmtIhWpjTATyYwxFLqdMhTKi0IEygtCg+oKQoLKCkOC6wkzkrwaY4HbGUOhyylEoMso"
    "NLC8ODW4rTAkuK0hFbicMRS5nTIEuowyBcoLQgPbCkKD2wpSksoJQ4HbGVKRyxk0obwpNaC8KDWovCAlqLwhJbisISW5rEEU"
    "qp0yBMkMMYS6C1KDywtjksoaUpLLGUOR6xhCocsYNKC8OESovCE0ubwiFricMRS6nEITy5tjgroMQ5PbGkOSzClSkLspNaC8"
    "OESovDA0uJ4gJKmsMCW5nSEFuJtBFMqaQoTJikKD2gpCgtoaQpLaCVKRyhlDkbwpU6HLGDSgzCBDqLwwNLieICS5nDAVuJ0x"
    "I9mMMQTJi0ID2gtCg9oKUpLaGUKRyxk0obwpNaC8KDWovCAlqLwhJbisMCWprTEUuZwxFbmdMgS5jTEEyYtCA8qMQgLKC0ID"
    "2wpCg9sKQ5LaGkOR2ylDoesYQ6DLKCWorDAkuK0hFbicMQW5jEEDyotDhMoLU5LKGlKSyypigLspY5DLKEOgzCBDuKwhFaid"
    "ISPJnDEVqpwyBMmbUgPaikKDyxtDk9sKY5HKGUOR2ylDkLwpNZi8KESYrCg0qK0gNKmtMCSpniEUuZwxBqmMMYS5jEKD2gpC"
    "k9oaQpLqGUKAyxhCkdsYQ5C8KFOQvCglmLwgJaisICWprDAlqZ0hFLmNIQW5i1EDyotChMkLQoPKC1KT2hpCktoaQ4HbCVOA"
    "yxhDkNsYNJi8KCWgrSAkqK0wFamcIQW4m0EEyItBA9kLQQPailKCygpCgtoKQ5LaCVKByxlDkdsZNJC8GCWQrRAkoLwgJais"
    "OCW4rDAWqatBBLiNQBLJi1ECuoxSgsmKUgHKCkOB2glDkcsZNJG9GCWQrSgkmJ0oJKidOBS4nDAGqJwxBMibUQPZikGD2YpC"
    "AtoKUoHKCVKBuxpjkcoJNJHMGEOQvCg0oMwQJKC8ICWorCAWqKwxFLiOMBPJnEEDuY4xhLkMQYPKC1KCyQtSgsoLU4LaClKB"
    "yglDgdsZQpHaCEOAvBg0kL0oNKC9IDSorSA0qa0wFZmdMCO5niEUuY0xBMmLQgPKjEKD2gpCgtqJUoHJCkOByxpTgdsZQ4Db"
    "CEORvBhTocspNJC9KDSgrSg0qK0gNKmtIBWYnSAUuJwwFamcQBPJjDEEyYtChMmKQoPaClGCygpDktoJUoHLGVKRyxhDkLwY"
    "NaC8ECWgvCAlqKwgJbisMBapjDAUuY1Ag8iLQYS5i2EC2QpBgtkJQYLaCUGC2glCgdoJQoHKGkOS6whCkcsINKG8GDWgzBAk"
    "oKwoJZitEDSorSAkqKwgJZmdEBWonCAUqI0gFLmMMAW4jEADyZtSg8mLUgLKC1KCyhtigcoJQ4HbGUKRyxk0kb0YNJCtGCWY"
    "rBAVoKwwJKmeISO5njETyZxBE8qMMgTKi0MD2wtTgssKY5HKGUORvBhTgLwYNJC9ECWYrCglqKwgFaicIBW4nDEUuZ0xFLmd"
    "QQPJi0EEyYtCA9qKQoPaCkKC2gpDgtsaM4K9GkSRyxk0oa0YNKCtODOwrjgkwKtAM9mrUSLJm0Ej2otREsqLQwLaikMC24ok"
    "g7wKNIO9GjSSvSolkbwpNLK9OUSgvCA0sK04NLiuMCTIq0EjyZwxJMqbQgTJi0KEygpSgtoaQoHLCVORuypEkbwpRJC8KDSg"
    "vSA0qK04JKitMCS4nhEUqJ0hFKmdMRPJnDIUuo4xBLoMQYPKC1IC2gpCgtoKUoHKCVKByhpSkcoZUpHaGDKRzBhTmLsoNZiu"
    "IBSYrCAVqKwxFKmdIQW4jDEEyQxBgskLQ4LqCUKByxlDkbwpNKG9KESYvDAkuKwwJamdMBS5jTAUuY0xBMmLQoPZC0IC6gky"
    "gusJQ5HaGUKRyxk0kcwYQ5C8KDSgriAUoJ04M8isMBWonTAUqZ0xE7meMRTJm0IDyZxCA8qbQwTKi0MD2wtDg9sKQ4LqGjKT"
    "6xpSgcsZUpHKGUOhyylTkNsoM6C9KDWYrSgkoK0gJKitMCS5rDEluZ0xFLmOIRPJjDEUyotChMkLQoPaCkKC2gpDktoaQ5HL"
    "GTSh6ylDkLwoNKCuIDOoriAkqK0wFKidICS4nTAUuJ0xI8qcQRO6jiEFuZtSA8qLQgTKC0KD2htCg9sKQ4LbGlKB2hkykswZ"
    "Q6LbGVOh2hgzocwYNKDMECSgvCAlqMsgNLi8MDWprTAkuJ4hI8mcMQW4nEEDyZtCBMmLQgPaijIFygtCg9qKQ4PbCkOC6wkz"
    "kusZQoHbGVKRuypEkMspU5DLKTSQvShEmLwgNKitICSorTAkqa0xFKmdMQTImzIFuY0yE9uLUoPKC1OCyxtTktoaQ5HLGVOR"
    "2xgzob0oNLG9ODSwvTAluKwwFbicQBPJm1ED2QtCAtoKQoLbCUOS2xlDkbwZNaC8KCWgvDA0uK0gJbisMBapq0EEuJwxBciL"
    "QQPZilGCyQpRgsoKQoLaClOByxlSkcsYQ6G8GDWgvCgloLwgJaidICSpnTAUuJ0hFKmdIQW4m0EEuYwxBbkNMQPaC0KD6gpC"
    "gsobUoLLGlKS2glDkdoZQpHaGTOSvSpEkNopQoC8GDSQzBg0oNsYNKC8KESgvBA1qLwgJZitESSZnSAUqJ0gFKidIRSpnSEU"
    "qZwwFamNIBSpnDAVqZwwFbmMMBTIjCEUuY0hFLmNIQS5jEEDyYxBg7kNMYTJi1ICygtSgtkKQoLKG0KT2gpSksoaUoHKGlKB"
    "yxlSkdoIUpDJCEOQyxhTkMsYNJi8KESYrCg0qK0gJKitICWZnSAUqJ0gFamMIAWo"
;

/* Each tape's tuning correction: the recordings run 5..15 cents sharp
 * (measured: +5.9, +6.6, +8.9, +15.3, +13.2, +9.2, +7.3, +8.4, +7.6, +13.5, +5.2, +5.0 cents), so they are pulled back into tune with the voices */
static const float MF_TUNE[MF_NZONES] = { 0.996598f, 0.996195f, 0.994872f, 0.991201f, 0.992404f, 0.994700f, 0.995792f, 0.995160f, 0.995620f, 0.992232f, 0.997001f, 0.997116f };

/* ---- Mellotron flute playback (own code: standard IMA ADPCM + linear interpolation) ---- */
static const int16_t IMA_STEP_TAB[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t IMA_IDX_TAB[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static uint8_t g_b64_val[128];

static void init_mel_flute(void) {
    const char *abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (uint32_t i = 0u; i < 64u; i++) g_b64_val[(uint8_t)abc[i]] = (uint8_t)i;
}

/* Byte i of the tapes, read straight from the base64 text (no RAM copy) */
static inline __attribute__((always_inline)) uint32_t mf_byte(uint32_t i) {
    const char *c = &MF_B64[(i / 3u) * 4u];
    uint32_t v = ((uint32_t)g_b64_val[c[0] & 127] << 18) | ((uint32_t)g_b64_val[c[1] & 127] << 12) |
                 ((uint32_t)g_b64_val[c[2] & 127] << 6) | (uint32_t)g_b64_val[c[3] & 127];
    return (v >> (16u - 8u * (i % 3u))) & 0xFFu;
}

static int32_t mf_next(NoteVoice *nv) {
    const mf_zone_t *z = &MF_ZONE[nv->mf_zone];
    uint32_t code;
    if ((nv->mf_pos & 1u) == 0u) {               /* a new byte: decode it once */
        nv->mf_byte = mf_byte(z->off + (nv->mf_pos >> 1));
        code = nv->mf_byte & 15u;
    } else {
        code = nv->mf_byte >> 4;
    }
    int32_t step = IMA_STEP_TAB[nv->mf_idx];
    int32_t vd = step >> 3;
    if (code & 4u) vd += step;
    if (code & 2u) vd += step >> 1;
    if (code & 1u) vd += step >> 2;
    int32_t p = nv->mf_pred + ((code & 8u) ? -vd : vd);
    nv->mf_pred = (p > 32767) ? 32767 : ((p < -32768) ? -32768 : p);
    int32_t ix = nv->mf_idx + IMA_IDX_TAB[code & 7u];
    nv->mf_idx = (ix < 0) ? 0 : ((ix > 88) ? 88 : ix);
    if (++nv->mf_pos >= z->n) {              /* end of tape: jump to its loop */
        uint32_t ck = MF_CKS[z->ck + z->ls / MF_CK];
        nv->mf_pos = z->ls;
        nv->mf_pred = (int16_t)(ck & 0xFFFFu);
        nv->mf_idx = (int32_t)(ck >> 16);
    }
    return nv->mf_pred;
}

/* ---------------- Lowrey Combo Organ Synth Engine ---------------- */
/* CPU-saving lookup tables (filled once in setup):
 * - g_note_inc_base: phase increment per key at octave 0 (no powf in the audio loop)
 * - g_sine_table:    1024-point sine with a guard point for linear interpolation
 * Calling powf()/sinf() per note per sample was overloading the CPU once
 * ~4 notes x several layers were sounding, causing buffer underrun glitches. */
#define SINE_TABLE_SIZE 1024u
static float g_sine_table[SINE_TABLE_SIZE + SINE_TABLE_SIZE / 4u + 1u];  /* 1.25 cycles: cos = sin + 1/4 */
static float g_note_inc_base[FM1_NOTE_KEYS];
static float g_note_inc[FM1_NOTE_KEYS]; /* per-block, includes octave shift */
static float g_midi_inc[128];           /* MIDI notes at true pitch */

static void init_dsp_tables(void) {
    for (uint32_t i = 0u; i <= SINE_TABLE_SIZE + SINE_TABLE_SIZE / 4u; i++) {
        g_sine_table[i] = sinf(6.2831853f * (float)i / (float)SINE_TABLE_SIZE);
    }
    for (uint32_t n = 0u; n < FM1_NOTE_KEYS; n++) {
        float midi_note = 53.0f + (float)n;
        float f = 440.0f * powf(2.0f, (midi_note - 69.0f) / 12.0f);
        g_note_inc_base[n] = f / (float)FM1_AUDIO_RATE;
        g_note_inc[n] = g_note_inc_base[n];
    }
    for (uint32_t m = 0u; m < 128u; m++) {
        float f = 440.0f * powf(2.0f, ((float)m - 69.0f) / 12.0f);
        g_midi_inc[m] = f / (float)FM1_AUDIO_RATE;
    }
}

/* Sine and cosine of a phase in cycles (0..1), from one table position.
 * The higher harmonics then come from identities (sin 2x = 2 sin x cos x ...)
 * instead of more table lookups */
static inline __attribute__((always_inline)) void fast_sincos(float cycles, float *s, float *c) {
    float pos = cycles * (float)SINE_TABLE_SIZE;
    uint32_t idx = (uint32_t)pos;
    if (idx >= SINE_TABLE_SIZE) idx = SINE_TABLE_SIZE - 1u;
    float t = pos - (float)idx;
    float a = g_sine_table[idx];
    float b = g_sine_table[idx + SINE_TABLE_SIZE / 4u];
    *s = a + (g_sine_table[idx + 1u] - a) * t;
    *c = b + (g_sine_table[idx + SINE_TABLE_SIZE / 4u + 1u] - b) * t;
}

/* Sine of a phase given in cycles (any positive value, e.g. p * 3.0f) */
static inline __attribute__((always_inline)) float fast_sin(float cycles) {
    float frac = cycles - (float)(int32_t)cycles;
    float pos = frac * (float)SINE_TABLE_SIZE;
    uint32_t idx = (uint32_t)pos;
    if (idx >= SINE_TABLE_SIZE) idx = SINE_TABLE_SIZE - 1u;
    float t = pos - (float)idx;
    float a = g_sine_table[idx];
    return a + (g_sine_table[idx + 1u] - a) * t;
}

/* ---- Safety cap on notes ----
 * Each voice's cost per note (instructions per sample, measured), so the synth
 * can work out how many notes fit in the audio deadline. Above that it takes
 * over the oldest note instead of breaking up. Budget: between what played
 * cleanly on the FM-1 (~2750) and what broke up (~3180). */
static const uint16_t NOTE_COST_A[NUM_VOICES] = { 49, 76, 54, 59, 81, 61, 58, 40 };
static const uint16_t NOTE_COST_B[NUM_VOICES] = { 117, 60, 30, 77, 95, 69, 60, 39 };
#ifndef CPU_BUDGET
#define CPU_BUDGET   2800
#endif
#define CPU_BASE     525
static volatile int g_max_notes = (int)MAX_NOTES;

static void update_note_cap(void) {
    const uint16_t *cost = g_bank ? NOTE_COST_B : NOTE_COST_A;
    int sum = 0, top = 0, nv = 0, nh = 0;
    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        if (g_voice_on[v]) {
            sum += cost[v];
            if (cost[v] > top) top = cost[v];
            nv++;
            if (v == VB_LOWREY || v == VB_MBOX || v == VB_WARM) nh++;
        }
    }
    int per_note;
    if (g_bank) {
        /* bank B: voices share a little per-note work; LOWREY, M.BOX and WARM
         * also share one harmonic series (fitted to measured stacks) */
        per_note = 68 + sum - (nh > 1 ? 27 * (nh - 1) : 0) - (nv > 1 ? 11 * (nv - 1) : 0);
    } else {
        /* bank A: stacked voices share the same oscillators heavily */
        per_note = 41 + top + ((sum - top) * 42) / 100;
    }
    if (g_mel_level > 0.005f) per_note += 78;
    int cap = (CPU_BUDGET - CPU_BASE) / (per_note > 1 ? per_note : 1);
    if (cap < 2) cap = 2;
    if (cap > (int)MAX_NOTES) cap = (int)MAX_NOTES;
    g_max_notes = cap;
}

static int note_busy(const NoteVoice *nv) {
    return nv->active || nv->sustained || nv->env > 0.03f;
}

/* is_midi = 0: note_idx is an FM-1 key (follows the octave buttons)
 * is_midi = 1: note_idx is a MIDI note number (plays at its true pitch) */
static void note_on(int note_idx, int is_midi) {
    if (note_idx < 0) note_idx = 0;
    if (is_midi) {
        if (note_idx > 127) note_idx = 127;
    } else if (note_idx >= (int)FM1_NOTE_KEYS) {
        note_idx = (int)FM1_NOTE_KEYS - 1;
    }

    uint32_t chosen = 0u;
    float lowest = 999.0f;
    int busy = 0;
    for (uint32_t s = 0u; s < MAX_NOTES; s++) busy += note_busy(&g_pool[s]);
    if (busy >= g_max_notes) {
        /* at the cap: take over the quietest sounding note */
        for (uint32_t s = 0u; s < MAX_NOTES; s++) {
            if (note_busy(&g_pool[s]) && g_pool[s].env < lowest) {
                lowest = g_pool[s].env;
                chosen = s;
            }
        }
    } else {
        for (uint32_t s = 0u; s < MAX_NOTES; s++) {
            if (!g_pool[s].active && !g_pool[s].sustained && g_pool[s].env < 0.05f) {
                chosen = s;
                break;
            }
            if (g_pool[s].env < lowest) {
                lowest = g_pool[s].env;
                chosen = s;
            }
        }
    }
    NoteVoice *nv = &g_pool[chosen];
    nv->active = 1;
    nv->sustained = g_hold_on ? 1 : 0;
    nv->note_idx = note_idx;
    nv->is_midi = is_midi;
    nv->phase = 0.0f;
    nv->phase_vib = 0.0f;
    nv->phase_sub = 0.0f;
    nv->env = (g_attack < 0.02f) ? 1.0f : 0.02f;
    nv->stage = (g_attack < 0.02f) ? 1 : 0;
    nv->pluck_state = 1.0f;
    nv->strike = nv->strike3 = 1.0f;
    nv->strike2 = 0.0f;
    nv->bph[0] = nv->bph[1] = nv->bph[2] = 0.0f;
    nv->pwm_ph = 0.0f;
    nv->hold_l = nv->hold_r = 0.0f;
    nv->hold_n = 0;
    nv->lfsr = 0x9E3779B9u ^ ((uint32_t)note_idx * 2654435761u) ^ g_scope_w;
    if (nv->lfsr == 0u) nv->lfsr = 1u;
    nv->age = 0.0f;
    nv->mph = 0.0f;
    nv->hop_left = 0;
    nv->hop_ratio = 2.0f;
    nv->chip_seed = nv->lfsr * 747796405u + 2891336453u;
    nv->burst = 0;
    nv->pha = nv->phb = 0.0f;
    nv->aol_lp = nv->aol_hp = nv->aol_nlp = 0.0f;
    nv->aol_cc = 0;
    nv->aol_held = 0.0f;
    nv->aol_ga = nv->aol_gb = AOL_FLOOR;
    nv->aol_fl = 0;
    nv->aol_chop = 0;
    nv->aol_chlv = 1.0f;
    nv->phfar = 0.0f;
    nv->click = 1.0f;
    nv->nlp = 0.0f;
    nv->ccount = 0;
    nv->cheld_l = 0.0f;

    /* Mellotron flute: the M400 flute tapes sound an octave above their key, so
     * use the tape recorded an octave below this note, at natural speed (full
     * tape quality, as FMEL-1 plays it), and it sounds at this note's pitch */
    int m = is_midi ? note_idx : (53 + note_idx + 12 * g_octave_shift);
    if (m < 0) m = 0;
    if (m > 127) m = 127;
    int t = m - 12;
    if (t < 0) t = 0;
    uint32_t zi = 0u;
    while (zi < MF_NZONES - 1u && t > (int)MF_ZONE[zi].hi) zi++;
    const mf_zone_t *z = &MF_ZONE[zi];
    uint32_t ck = MF_CKS[z->ck];
    nv->mf_zone = (int)zi;
    nv->mf_pos = 0u;
    nv->mf_pred = (int16_t)(ck & 0xFFFFu);
    nv->mf_idx = (int32_t)(ck >> 16);
    nv->mf_rate = 0.25f * g_midi_inc[t] / g_midi_inc[z->root] * MF_TUNE[zi];   /* 11025 Hz tape at 44100 Hz */
    nv->mf_s[0] = nv->mf_s[1] = (float)mf_next(nv) * (1.0f / 32768.0f);
    nv->mf_s[2] = (float)mf_next(nv) * (1.0f / 32768.0f);
    nv->mf_s[3] = (float)mf_next(nv) * (1.0f / 32768.0f);
    nv->mf_frac = 0.0f;
}

static void note_off(int note_idx, int is_midi) {
    for (uint32_t s = 0u; s < MAX_NOTES; s++) {
        if (g_pool[s].active && g_pool[s].is_midi == is_midi &&
            g_pool[s].note_idx == note_idx) {
            g_pool[s].active = 0;
            g_pool[s].sustained = g_hold_on ? 1 : 0;
            if (!g_hold_on) {
                g_pool[s].stage = 2;
            }
        }
    }
}

static void apply_hold_change(int hold_enabled) {
    g_hold_on = hold_enabled;
    if (!g_hold_on) {
        for (uint32_t s = 0u; s < MAX_NOTES; s++) {
            g_pool[s].sustained = 0;
            if (!g_pool[s].active) {
                g_pool[s].stage = 2;
            }
        }
    }
}

static void handle_midi_packet(uint32_t pkt) {
    uint8_t status = FM1_MIDI_STATUS(pkt);
    uint8_t msg_type = status & 0xF0u;
    uint8_t d1 = FM1_MIDI_DATA1(pkt);
    uint8_t d2 = FM1_MIDI_DATA2(pkt);

    /* MIDI notes play at their real pitch: no octave folding, and the
     * FM-1's octave buttons don't move them */
    if (msg_type == 0x90u) {
        if (d2 > 0u) {
            note_on((int)(d1 & 0x7Fu), 1);
        } else {
            note_off((int)(d1 & 0x7Fu), 1);
        }
    } else if (msg_type == 0x80u) {
        note_off((int)(d1 & 0x7Fu), 1);
    }
}

/* ---------------- Controls & Input Handling ---------------- */
static float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static void process_controls(void) {
    uint32_t released = 0u;
    uint32_t pressed = fm1_input_edges(&released);

    /* SAVE: a short press opens/closes its screen (on release); holding it
     * for 0.6 s switches the Mellotron flute between 100% and 0% */
    static int save_down = 0, save_long = 0;
    static uint32_t save_t0 = 0u;
    if ((pressed & (1u << FM1_BTN_SAVE)) != 0u) {
        save_down = 1;
        save_long = 0;
        save_t0 = g_tick_ms;
    }
    if (save_down && !save_long && (g_tick_ms - save_t0) >= 600u) {
        save_long = 1;
        g_mel_level = (g_mel_level > 0.005f) ? 0.0f : 1.0f;
    }
    int save_short = 0;
    if ((released & (1u << FM1_BTN_SAVE)) != 0u) {
        save_short = save_down && !save_long;
        save_down = 0;
    }

    if (g_view == VIEW_SPLASH && (pressed != 0u || fm1_in.notes != 0u)) {
        g_view = VIEW_HOME;
        g_need_full_clear = 1;
    }

    if ((pressed & (1u << FM1_BTN_OCT_DOWN)) != 0u) {
        if (g_octave_shift > -2) g_octave_shift--;
    }

    if ((pressed & (1u << FM1_BTN_OCT_UP)) != 0u) {
        if (g_octave_shift < 2) g_octave_shift++;
    }

    /* HOME: a tap goes home (as before); holding it 0.6 s switches between
     * bank A and bank B. Each bank keeps its own voice buttons. */
    static int home_down = 0, home_long = 0;
    static uint32_t home_t0 = 0u;
    if ((pressed & (1u << FM1_BTN_HOME)) != 0u) {
        g_view = VIEW_HOME;
        home_down = 1;
        home_long = 0;
        home_t0 = g_tick_ms;
    }
    if (home_down && !home_long && (g_tick_ms - home_t0) >= 600u) {
        home_long = 1;
        for (uint32_t v = 0u; v < NUM_VOICES; v++) g_bank_voices[g_bank][v] = g_voice_on[v];
        g_bank ^= 1;
        for (uint32_t v = 0u; v < NUM_VOICES; v++) g_voice_on[v] = g_bank_voices[g_bank][v];
        g_view = VIEW_HOME;
        g_need_full_clear = 1;
    }
    if ((released & (1u << FM1_BTN_HOME)) != 0u) {
        home_down = 0;
    }

    if ((pressed & (1u << FM1_BTN_SCL)) != 0u) {
        apply_hold_change(!g_hold_on);
    }

    if (save_short) {
        g_view = (g_view == VIEW_ENV_AD) ? VIEW_HOME : VIEW_ENV_AD;
    }

    if ((pressed & (1u << FM1_BTN_FX)) != 0u) {
        if (g_view == VIEW_FX_REVERB) {
            g_view = VIEW_FX_DELAY;
        } else if (g_view == VIEW_FX_DELAY) {
            g_view = VIEW_HOME;
        } else {
            g_view = VIEW_FX_REVERB;
        }
    }

    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        uint32_t btn = VOICE_BTN_MAP[v];
        if ((pressed & (1u << btn)) != 0u) {
            g_voice_on[v] = !g_voice_on[v];
        }
    }

    int32_t d_sel = fm1_enc_take(FM1_ENC_SELECT);
    int32_t d_k1  = fm1_enc_take(FM1_ENC_KNOB1);
    int32_t d_k2  = fm1_enc_take(FM1_ENC_KNOB2);
    int32_t d_k3  = fm1_enc_take(FM1_ENC_KNOB3);
    int32_t d_k4  = fm1_enc_take(FM1_ENC_KNOB4);
    int32_t d_alg = fm1_enc_take(FM1_ENC_ALGORITHM);
    int32_t d_pre = fm1_enc_take(FM1_ENC_PRESETS);
    /* SELECT: step through home pages (cards -> scope -> truck) */
    if (d_sel != 0 && g_view != VIEW_SPLASH) {
        if (!is_home_page(g_view)) {
            g_view = VIEW_HOME;
        } else {
            static const int PAGES[3] = { VIEW_HOME, VIEW_SCOPE, VIEW_TRUCK };
            int cur = (g_view == VIEW_SCOPE) ? 1 : ((g_view == VIEW_TRUCK) ? 2 : 0);
            cur = (cur + ((d_sel > 0) ? 1 : 2)) % 3;
            g_view = PAGES[cur];
        }
    }

    /* PRESET: button colors, ALGORITHM: main screen colors */
    if (d_pre != 0) {
        g_btn_theme = (g_btn_theme + ((d_pre > 0) ? 1u : (uint32_t)NUM_THEMES - 1u)) % (uint32_t)NUM_THEMES;
        apply_themes();
        g_need_full_clear = 1;
    }
    if (d_alg != 0) {
        g_main_theme = (g_main_theme + ((d_alg > 0) ? 1u : (uint32_t)NUM_THEMES - 1u)) % (uint32_t)NUM_THEMES;
        apply_themes();
        g_need_full_clear = 1;
    }

    /* K3: delay feedback on the delay screen (as its label says), decay elsewhere */
    if (g_view == VIEW_FX_DELAY) {
        g_dly_feedback = clamp01(g_dly_feedback + (float)d_k3 * 0.04f);
    } else {
        g_decay = clamp01(g_decay + (float)d_k3 * 0.04f);
    }
    g_filter_cutoff = clamp01(g_filter_cutoff + (float)d_k4 * 0.04f);

    float s1 = (float)d_k1 * 0.05f;
    float s2 = (float)d_k2 * 0.05f;

    if (g_view == VIEW_ENV_AD) {
        /* SAVE screen: K1 Mellotron flute level, K2 attack (K3 decay, K4 filter) */
        g_mel_level = clamp01(g_mel_level + s1);
        g_attack = clamp01(g_attack + s2);
    } else if (g_view == VIEW_FX_REVERB) {
        g_rev_mix = clamp01(g_rev_mix + s1);
        g_rev_decay = clamp01(g_rev_decay + s2);
    } else if (g_view == VIEW_FX_DELAY) {
        g_dly_mix = clamp01(g_dly_mix + s1);
        g_dly_time = clamp01(g_dly_time + s2);
    } else {
        g_rev_mix = clamp01(g_rev_mix + s1);
        g_dly_mix = clamp01(g_dly_mix + s2);
    }

}

static void sync_keyboard_notes(void) {
    uint32_t cur = fm1_in.notes;
    for (int n = 0; n < (int)FM1_NOTE_KEYS; n++) {
        uint32_t bit = (1u << (uint32_t)n);
        int was_down = (g_prev_notes_mask & bit) != 0u;
        int is_down = (cur & bit) != 0u;
        if (is_down && !was_down) {
            note_on(n, 0);
        } else if (!is_down && was_down) {
            note_off(n, 0);
        }
    }
    g_prev_notes_mask = cur;
}

static inline __attribute__((always_inline)) float poly_blep(float t, float dt) {
    if (dt <= 0.0f) return 0.0f;
    if (t < dt) {
        float x = t / dt;
        return x + x - x * x - 1.0f;
    }
    if (t > 1.0f - dt) {
        float x = (t - 1.0f) / dt;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}

#define LAYER_BIT(v)  (1u << (uint32_t)(v))
#define LAYERS_MAIN   (LAYER_BIT(VOICE_FLUTE) | LAYER_BIT(VOICE_HARPSI) | LAYER_BIT(VOICE_HORN) | \
                       LAYER_BIT(VOICE_REED) | LAYER_BIT(VOICE_STRINGS))
#define LAYERS_VIB    (LAYER_BIT(VOICE_VOX) | LAYER_BIT(VOICE_VIBES))
#define LAYER_MEL     (1u << 8)     /* Mellotron flute layer (not a voice button) */
static float g_mel_gain = 0.0f;     /* per block, from g_mel_level */

/* Renders one note for every enabled layer at once and adds it (panned) into
 * acc_l / acc_r. Sine lookups are shared: at most 3 for the main oscillator,
 * 3 for vibrato and 2 for bass, however many layers are on. */
/* A rotating sine oscillator: sin and cos advanced by an exact rotation each
 * sample (4 multiplies), set from the sine table at the start of every block.
 * The same waveform as a table lookup per sample, for a fraction of the work. */
typedef struct { float s, c, cr, sr; } RotOsc;

static inline __attribute__((always_inline)) void rot_step(RotOsc *o) {
    float s = o->s * o->cr + o->c * o->sr;
    float c = o->c * o->cr - o->s * o->sr;
    o->s = s;
    o->c = c;
}

static void rot_init(RotOsc *o, float phase, float inc) {
    fast_sincos(phase, &o->s, &o->c);
    o->cr = cosf(6.2831853f * inc);
    o->sr = sinf(6.2831853f * inc);
}

/* AOL: give a just-set oscillator a level that glides from *cur toward
 * target over this block (at most ~22 dB per block, so a beep fades in or
 * out over ~3 blocks), by scaling its start and its rotation */
static void aol_glide(RotOsc *o, float *cur, float target, int n) {
    float g0 = *cur;
    float g1 = target < AOL_FLOOR ? AOL_FLOOR : target;
    float r = g1 / g0;
    if (r > 12.0f) r = 12.0f;
    if (r < (1.0f / 12.0f)) r = 1.0f / 12.0f;
    g1 = g0 * r;
    float k = expf(logf(r) / (float)n);
    o->s *= g0; o->c *= g0;
    o->cr *= k; o->sr *= k;
    *cur = g1;
}

/* Shared by both banks: the Mellotron layer, the envelope, and the mix-in */
static inline __attribute__((always_inline)) void note_mel_env(NoteVoice *nv, uint32_t layers,
                                                                float atk_step, float dec_coeff,
                                                                float l, float r, float *acc_l, float *acc_r) {
    float x;
    /* The Mellotron flute, at full rate (its tape is only 11 kHz: halving
     * its rate again would add faint false high tones) */
    if (layers & LAYER_MEL) {
        nv->mf_frac += nv->mf_rate;
        while (nv->mf_frac >= 1.0f) {
            nv->mf_s[0] = nv->mf_s[1];
            nv->mf_s[1] = nv->mf_s[2];
            nv->mf_s[2] = nv->mf_s[3];
            nv->mf_s[3] = (float)mf_next(nv) * (1.0f / 32768.0f);
            nv->mf_frac -= 1.0f;
        }
        /* 4-point Hermite between tape samples */
        float y0 = nv->mf_s[0], y1 = nv->mf_s[1], y2 = nv->mf_s[2], y3 = nv->mf_s[3];
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        float tt = nv->mf_frac;
        x = (((c3 * tt + c2) * tt + c1) * tt + y1) * g_mel_gain * 0.7071f;
        l += x;
        r += x;
    }

    /* Envelope (once per note, shared by all layers) */
    if (nv->stage == 0) {
        nv->env += atk_step;
        if (nv->env >= 1.0f) {
            nv->env = 1.0f;
            nv->stage = 1;
        }
    } else if (nv->stage == 1) {
        if (!nv->active && !nv->sustained) {
            nv->stage = 2;
        } else {
            nv->env = 0.68f + (nv->env - 0.68f) * dec_coeff;
        }
    } else {
        nv->env *= dec_coeff;
        if (nv->env < 0.03f) nv->env = 0.0f;
    }

    *acc_l += l * nv->env;
    *acc_r += r * nv->env;
}

/* Pitches of the notes being held right now (AOL only uses these) */
static float g_held_inc[MAX_NOTES];
static int g_held_n = 0;

/* ======================= BANK B voices =======================
 * Eight more voices, chosen with HOLD HOME. They share the note pool,
 * envelope, Mellotron layer and effects with bank A. Everything per block
 * (pitch tricks, oscillator set-up) is done once in render_note_block. */

typedef struct {
    RotOsc om;        /* fundamental (harmonics come from it) */
    RotOsc ov;        /* PS-20 vibrato oscillator */
    RotOsc ob;        /* slightly sharp twin (PS-20 chorus, celesta shimmer) */
    RotOsc oi[3];     /* inharmonic partials: celesta 2.756x, 5.404x; steel 4.27x */
    float inc;        /* this block's pitch step */
    float fl_amp[10]; /* LOWREY harmonic levels for this note */
    float buzz;       /* LOWREY top-end buzz level */
    float buzz_w;     /* its pulse width (follows the note so it reaches ~20 kHz) */
    RotOsc om2;       /* SID modem tone */
    float m_tone, m_scr, m_flip;   /* modem: tone level, screech level, phase-reversal sign */
    float m_nbw;                   /* modem: noise width (bigger = hissier) */
    int m_crush;                   /* modem: crunch (hold every N samples; 1 = off) */
    RotOsc pa, pb;    /* PHONE: its two tones */
    float pn_amp;                   /* AOL: data hiss level */
    int aol_two;                    /* AOL: second tone in use this block */
    float crackle;                  /* AOL: crackle in this block (0 = none) */
    int aol_crush;                  /* AOL: crunch (hold every N samples; 1 = off) */

    RotOsc oh;        /* HIGH: its tone */
    float mb_tine;    /* music box: tine overtone on/off (below Nyquist) */
    float d1, d2, d3; /* struck envelopes' per-sample decay */
    float wl[10], wr[10]; /* all harmonic voices combined, per channel */
    int nh;               /* how many harmonics are needed */
    float st1, st2;       /* struck envelopes for this block */
    float trem;           /* celesta tremolo for this block */
} BankBBlock;

/* LOWREY flute harmonic levels (dB below the fundamental), read off the
 * spectrum of the real organ at low C (C3) and mid C (C4) */
#ifndef BUZZ_GAIN
#define BUZZ_GAIN 1.6f   /* matched to the peak-hold spectrum of the real organ */
#endif
static const float LOWREY_DB_LOW[10] = { 0.0f, -21.0f, -18.0f, -28.0f, -28.0f, -36.0f, -35.0f, -41.0f, -43.0f, -45.0f };
static const float LOWREY_DB_MID[10] = { 0.0f, -17.0f, -19.0f, -29.0f, -32.0f, -37.5f, -40.0f, -42.5f, -44.0f, -46.0f };

static inline __attribute__((always_inline)) float pb_saw(float p, float inc) {
    return (2.0f * p - 1.0f) - poly_blep(p, inc);
}

#ifndef PHONE_LEVEL
#define PHONE_LEVEL 1.2f   /* AOL: about 12 dB under the other voices */
#endif
#ifndef HIGH_LEVEL
#define HIGH_LEVEL 0.36f /* about 9 dB under the other voices */
#endif
/* cheap rounding to 1/steps (x within -1..1): avoids a floorf call per sample */
static inline __attribute__((always_inline)) float quant(float x, float steps) {
    return (float)((int)(x * steps + steps + 0.5f) - (int)steps) / steps;
}

static inline __attribute__((always_inline)) void render_bank_b(NoteVoice *nv, uint32_t layers, float lfo_val,
                                                                 BankBBlock *bb, float *pl, float *pr) {
    float l = 0.0f, r = 0.0f, x;
    float inc = bb->inc;
    (void)lfo_val;

    nv->phase += inc;
    if (nv->phase >= 1.0f) nv->phase -= 1.0f;
    float p = nv->phase;
    float h2s = 0.0f;                             /* the shared 2nd harmonic (MBOX's note) */

    /* LOWREY, WARM and the harmonic parts of CELESTA and STEEL: one shared
     * harmonic series, mixed with weights set once per block */
    if (bb->nh > 0) {
        if (bb->nh < 2) { bb->wl[1] = 0.0f; bb->wr[1] = 0.0f; }
        rot_step(&bb->om);
        float tc = 2.0f * bb->om.c;
        float h1 = bb->om.s;
        float h2 = tc * h1;                       /* sin(nx) = 2 cos x sin((n-1)x) - sin((n-2)x) */
        h2s = h2;
        l += bb->wl[0] * h1 + bb->wl[1] * h2;
        r += bb->wr[0] * h1 + bb->wr[1] * h2;
        if (bb->nh > 2) {
            float h3 = tc * h2 - h1;
            l += bb->wl[2] * h3;
            r += bb->wr[2] * h3;
            if (bb->nh > 3) {
                float h4 = tc * h3 - h2, h5 = tc * h4 - h3, h6 = tc * h5 - h4;
                float h7 = tc * h6 - h5, h8 = tc * h7 - h6;
                l += bb->wl[3] * h4 + bb->wl[4] * h5 + bb->wl[5] * h6 + bb->wl[6] * h7 + bb->wl[7] * h8;
                r += bb->wr[3] * h4 + bb->wr[4] * h5 + bb->wr[5] * h6 + bb->wr[6] * h7 + bb->wr[7] * h8;
            }
        }
    }
    if (layers & (LAYER_BIT(VB_PS20) | LAYER_BIT(VB_WARM))) rot_step(&bb->ob);   /* shared twin */

    /* one noise source per note (modem screech, clicks) */
    float nz = 0.0f;
    if (layers & (LAYER_BIT(VB_SID) | LAYER_BIT(VB_MBOX) | LAYER_BIT(VB_HIGH) | LAYER_BIT(VB_PHONE))) {
        nv->lfsr ^= nv->lfsr << 13; nv->lfsr ^= nv->lfsr >> 17; nv->lfsr ^= nv->lfsr << 5;
        nz = (float)(int32_t)nv->lfsr * 4.6566e-10f;
        nv->click *= 0.9925f;                       /* ~3 ms */
    }

    if (layers & LAYER_BIT(VB_LOWREY)) {
        float pb = p + (1.0f - bb->buzz_w);       /* narrow pulse: the organ's buzz, flat to ~20 kHz */
        if (pb >= 1.0f) pb -= 1.0f;
        x = bb->buzz * (pb_saw(p, inc) - pb_saw(pb, inc));
        l += x * g_pan_l[0]; r += x * g_pan_r[0];
    }
    if (layers & LAYER_BIT(VB_SID)) {
        /* dial-up modem: a tone a step above the note that flips phase, hops
         * between the octave and the fifth above it, and gets screech bursts:
         * noise ring-modulated by the tone, so it stays in key */
        rot_step(&bb->om2);
        float t = bb->om2.s;
        nv->nlp += bb->m_nbw * (nz - nv->nlp);       /* narrow the noise ... */
        x = bb->m_tone * bb->m_flip * (t + 0.18f * t * (3.0f - 4.0f * t * t))
          + bb->m_scr * 2.35f * nv->nlp * t;         /* ... then ring it around the tone: the screech (-1.5 dB) */
        if (bb->m_crush > 1) {                       /* crunch: hold every N samples (aliased buzz) */
            if (++nv->ccount >= bb->m_crush) { nv->ccount = 0; nv->cheld_l = x; }
            x = nv->cheld_l;
        }
        l += x * g_pan_l[1]; r += x * g_pan_r[1];
    }
    if (layers & LAYER_BIT(VB_POCKET)) {
        /* sample-and-hold at half rate + 5-bit steps: a lo-fi chip (plain squares) */
        if (nv->hold_n == 0) {
            float p2 = p * 2.0f;
            if (p2 >= 1.0f) p2 -= 1.0f;
            float sq1 = (p < 0.5f) ? 1.0f : -1.0f;
            float sq2 = (p2 < 0.5f) ? 1.0f : -1.0f;
            nv->hold_l = quant(0.21f * sq1 + 0.093f * sq2, 32.0f);
        }
        nv->hold_n ^= 1;
        x = nv->hold_l;
        l += x * g_pan_l[2]; r += x * g_pan_r[2];
    }
    if (layers & LAYER_BIT(VB_PS20)) {
        rot_step(&bb->ov);
        float v1 = bb->ov.s, vt = 2.0f * bb->ov.c;
        float v2 = vt * v1, v3 = vt * v2 - v1, v4 = vt * v3 - v2;
        float v5 = vt * v4 - v3, v6 = vt * v5 - v4, v7 = vt * v6 - v5, v8 = vt * v7 - v6;
        x = 0.30f * v1 + 0.17f * v2 + 0.08f * v3 + 0.10f * v4 + 0.025f * v5 + 0.04f * v6 + 0.01f * v7 + 0.03f * v8
          + 0.16f * bb->ob.s;                                    /* the wobbly twin */
        l += x * g_pan_l[3]; r += x * g_pan_r[3];
    }
    if (layers & LAYER_BIT(VB_MBOX)) {
        /* music box: the 2nd harmonic of the shared series is its fundamental
         * (an octave up, in the weights); here the wooden-box twin, the tine's
         * own overtone (6.27x) and the pin's soft pluck */
        rot_step(&bb->oi[0]);
        rot_step(&bb->oi[1]);
        nv->strike3 *= bb->d3;
        if (nv->strike3 < 1.0e-5f) nv->strike3 = 0.0f;
        nv->strike2 += 0.0016f * (1.0f - nv->strike2);       /* soft ~14 ms swell, no hard pluck */
        x = nv->strike2 * (bb->st1 * (0.42f * h2s + 0.09f * bb->oi[0].s)
                           + 0.010f * bb->mb_tine * bb->oi[1].s * nv->strike3);
        l += x * g_pan_l[4]; r += x * g_pan_r[4];
    }
    if (layers & LAYER_BIT(VB_PHONE)) {
        /* AOL: a whole dial-up connection: two tones set per block, plus the
         * smooth "kshhh" of the data, all through a telephone line (~0.3-3.4 kHz) */
        /* the tone levels glide (~3 ms) instead of jumping: hard on/off edges
         * click, and clicks smear the pitch so the tones sound out of tune */
        rot_step(&bb->pa);                                         /* (their levels ride on the */
        x = bb->pa.s;                                              /*  oscillators themselves) */
        if (bb->aol_two) {                                         /* dial tone and dialing only */
            rot_step(&bb->pb);
            x += bb->pb.s;
        }
        if (bb->pn_amp > 0.0f) {                                   /* the data static only */
            nv->aol_nlp += 0.45f * (nz - nv->aol_nlp);
            x += bb->pn_amp * nv->aol_nlp;
        }
        if (bb->crackle > 0.0f && (nv->lfsr & 0x7Fu) == 0u) x += bb->crackle * nz;   /* a crackle pop */
        if (bb->aol_crush > 1) {                                   /* a hint of crunch */
            if (++nv->aol_cc >= bb->aol_crush) { nv->aol_cc = 0; nv->aol_held = x; }
            x = nv->aol_held;
        }
        nv->aol_lp += 0.39f * (x - nv->aol_lp);                 /* line: top ~3.4 kHz */
        nv->aol_hp += 0.043f * (nv->aol_lp - nv->aol_hp);       /* line: bottom ~300 Hz */
        x = nv->aol_lp - nv->aol_hp;
        l += x * g_pan_l[5]; r += x * g_pan_r[5];
    }
    if (layers & LAYER_BIT(VB_WARM)) {
        /* fat, mellow sine: (sine + a whisper of 2nd harmonic, in the weights)
         * + a slightly sharp twin + a sub-octave */
        rot_step(&bb->oi[2]);
        x = 0.28f * bb->ob.s + 0.24f * bb->oi[2].s;
        l += x * g_pan_l[6]; r += x * g_pan_r[6];
    }
    if (layers & LAYER_BIT(VB_HIGH)) {
        /* HIGH: a soft, pure sine an octave up, held as long as the key is */
        rot_step(&bb->oh);
        x = HIGH_LEVEL * bb->oh.s;                                  /* a pure, mellow sine */
        l += x * g_pan_l[7]; r += x * g_pan_r[7];
    }
    *pl += l;
    *pr += r;
}

/* Bank B set-up for one note, once per block */
#define CHIP_LOOP_S 1.3f

static void bank_b_prepare(NoteVoice *nv, uint32_t layers, float base_inc, float lfo_mid, int n, BankBBlock *bb) {
    float inc = base_inc;
    int midi = nv->is_midi ? nv->note_idx : (53 + nv->note_idx + 12 * g_octave_shift);
    float blk_s = (float)n / (float)FM1_AUDIO_RATE;

    if (layers & LAYER_BIT(VB_SID)) {
        /* modem noise, evolving while the note is held (the tone is HIGH's job now):
         *   0-0.35 s   a steady pitched screech
         *   0.35 s+    it breaks up: bursts, dropouts, crunch, jumps to off-key bands
         *   1.0 s+     it cycles over 1.3 s: thin hiss ... the screech swells in ... full
         *              screech; then the same pattern backwards, then forwards again ...
         * The screech is noise rung around a carrier an octave or a twelfth
         * above the note, so it stays in key; the carrier itself isn't heard. */
        float age = nv->age;
        nv->lfsr ^= nv->lfsr << 13; nv->lfsr ^= nv->lfsr >> 17; nv->lfsr ^= nv->lfsr << 5;
        uint32_t dice = nv->lfsr;
        float ratio;
        if (age < 0.15f) {
            nv->hop_ratio = 2.0f;
        } else if (--nv->hop_left <= 0) {
            nv->hop_left = 1 + (int)(dice % 3u);                    /* 6-18 ms per band */
            nv->hop_ratio = (nv->hop_ratio == 2.0f) ? 3.0f : 2.0f;
        }
        ratio = nv->hop_ratio;
        bb->m_flip = 1.0f;
        float tone = 0.0f;
        float scr = 0.44f;
        bb->m_nbw = 0.12f;
        bb->m_crush = 1;
        int screech_phase = 1;
        float swell = 1.0f;
        if (age >= 1.0f) {
            /* the loop runs forward, then the same pattern backwards (a palindrome),
             * so it doesn't sound like a short loop repeating */
            float cyc = (age - 1.0f) * (1.0f / CHIP_LOOP_S);
            int pass = (int)cyc;
            float u = cyc - (float)pass;
            if (pass & 1) u = 1.0f - u;                            /* reverse pass */
            screech_phase = (u >= 0.28f);
            if (u < 0.45f && screech_phase) swell = (u - 0.28f) * (1.0f / 0.17f);  /* screech swells in */
            /* the same block in the pattern always gets the same dice, so the
             * reverse pass really is the forward pass played backwards */
            uint32_t k = (uint32_t)(u * (CHIP_LOOP_S * (float)FM1_AUDIO_RATE / 256.0f)) + 1u;
            uint32_t h = k * 2654435761u + nv->chip_seed;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            dice = (dice & 0x7Fu) | (h & ~0x7Fu);                  /* keep the band hops free */
        }
        if (age >= 0.35f) {
            if (screech_phase) {
                nv->burst = ((dice >> 7) % 10u) < 6u;               /* choppy: on ~60% of blocks */
                scr = nv->burst ? 0.62f * swell : 0.0f;
                bb->m_nbw = 0.05f + 0.11f * swell;                 /* wider, hissier */
                if (scr < 0.22f && nv->burst) scr = 0.22f;
            } else {
                scr = 0.22f;                                       /* thin hiss between screeches */
                bb->m_nbw = 0.05f;
            }
            uint32_t g = (dice >> 12) % 100u;                       /* glitches */
            if (g < 12u) {                                          /* dropout */
                scr = 0.0f;
            } else if (g < 24u) {                                   /* crunch burst */
                bb->m_crush = 4 + (int)((dice >> 20) % 9u);
            } else if (g < 34u) {                                   /* jump to an off-key band */
                static const float DATA[4] = { 1.41f, 2.37f, 3.71f, 1.83f };
                ratio = DATA[(dice >> 22) & 3u];
            }
        }
        bb->m_tone = tone;
        bb->m_scr = scr;
        float im = base_inc * ratio;
        if (im > 0.45f) im = 0.45f;
        rot_init(&bb->om2, nv->mph, im);
        nv->mph += im * (float)n;
        nv->mph -= floorf(nv->mph);
    }
    if (layers & (LAYER_BIT(VB_SID) | LAYER_BIT(VB_MBOX) | LAYER_BIT(VB_HIGH) | LAYER_BIT(VB_PHONE))) nv->age += blk_s;

    if (layers & LAYER_BIT(VB_PHONE)) {
        /* a dial-up connection that only uses the notes being held (in any
         * octave), so it is always in tune:
         *   0-0.4 s    dial tone: the note, in two octaves
         *   0.4-1.4 s  dialing: 8 beeps, each one held note (now and then its 5th) in two octaves, 75 ms on / 50 ms off
         *   1.4-1.9 s  answer tone: the note two octaves up, with phase flips
         *   1.9-2.4 s  bong-bong: the note, then a held pitch an octave up (or its 5th)
         *   2.4-2.8 s  chirps: flipping between octaves of the note
         *   2.8 s+     the data static, held, gently choppy, with a faint carrier and the
         *              slightest glitches (rare dropouts, crackle, crunch) */
        float f = 1.0f;                                   /* ratios below are x base_inc */
        int nh = g_held_n;
        float own = base_inc;
        #define HELD(k) (nh > 0 ? g_held_inc[(uint32_t)(k) % (uint32_t)nh] : own)
        #define FIFTH 1.4983071f                            /* a 5th, tuned like the keys */
        float ia_abs = 0.0f, ib_abs = 0.0f;
        float aa = 0.0f, ab = 0.0f, flip = 1.0f, pn = 0.0f;
        float age = nv->age;
        nv->lfsr ^= nv->lfsr << 13; nv->lfsr ^= nv->lfsr >> 17; nv->lfsr ^= nv->lfsr << 5;
        uint32_t dice = nv->lfsr;
        bb->crackle = 0.0f;
        bb->aol_crush = 1;
        if (age < 0.4f) {
            ia_abs = 2.0f * own;                            /* the note, as a clean octave pair */
            ib_abs = 4.0f * own;
            aa = 0.16f; ab = 0.13f;
        } else if (age < 1.4f) {
            float tt = (age - 0.4f) / 0.125f;
            int digit = (int)tt;
            float in_digit = tt - (float)digit;
            uint32_t d = (uint32_t)(digit * 2654435761u) ^ (uint32_t)(nv->note_idx * 40503u);
            ia_abs = 2.0f * HELD(d >> 4);                   /* each digit: one held note, */
            uint32_t h5 = d ^ (d >> 15); h5 *= 0x2C1B3C6Du; h5 ^= h5 >> 12;
            if ((h5 >> 30) == 0u) ia_abs *= FIFTH;          /* ... now and then (1 in 4) its 5th */
            ib_abs = 2.0f * ia_abs;                         /* doubled an octave up */
            if (in_digit < 0.6f) { aa = 0.15f; ab = 0.12f; }
        } else if (age < 1.9f) {
            ia_abs = 4.0f * own; aa = 0.18f;
            flip = ((int)((age - 1.4f) / 0.225f) & 1) ? -1.0f : 1.0f;   /* phase reversals (each a quick dip) */
        } else if (age < 2.4f) {                            /* bong-bong */
            float bt = (age - 1.9f) / 0.25f;
            float ph = bt - floorf(bt);
            ia_abs = (bt < 1.0f) ? own : 2.0f * HELD(nv->note_idx + 2);
            if (bt >= 1.0f && (nv->chip_seed & 0x100u)) ia_abs = own * FIFTH;   /* sometimes up a 5th */
            aa = 0.24f * (1.0f - ph);
        } else if (age < 2.8f) {                            /* chirps */
            int hop = (int)((age - 2.4f) / 0.035f);   /* hops slow enough to hear the pitch */
            ia_abs = (hop & 1) ? 4.0f * own : 2.0f * own;   /* octave flips of the note */
            aa = 0.14f;
        } else {                                            /* the static, held */
            /* a gentle chop: the static comes in short bursts (12-35 ms) at
             * slightly different levels, now and then a quick dip, like data
             * packets going by */
            if (--nv->aol_chop <= 0) {
                nv->aol_chop = 2 + (int)((dice >> 3) % 5u);
                uint32_t c = (dice >> 24) % 100u;
                nv->aol_chlv = (c < 10u) ? 0.45f : 0.80f + 0.004f * (float)(c % 50u);
            }
            pn = 0.61f * nv->aol_chlv;                      /* (same overall level as before) */
            ia_abs = 4.0f * HELD(nv->note_idx); aa = 0.035f;  /* faint carrier, in tune */
            uint32_t g = (dice >> 11) % 100u;
            if (g < 3u) {                                   /* rare instant dropout */
                pn = 0.0f; aa = 0.0f;
            } else if (g < 9u) {                            /* a little crackle */
                bb->crackle = 0.9f;
            } else if (g < 12u) {                           /* a hint of crunch */
                bb->aol_crush = 2 + (int)((dice >> 20) % 3u);
            }
        }
        #undef HELD
        #undef FIFTH
        float ra = ia_abs, rb = ib_abs;
        float ia = f * ra, ib = f * rb;     /* already absolute pitch steps */
        if (ia > 0.45f) { ia = 0.45f; aa = 0.0f; }
        if (ib > 0.45f) { ib = 0.45f; ab = 0.0f; }
        /* The tones fade in and out over a few ms instead of switching on and
         * off: hard edges click, and clicks smear the pitch so the beeps sound
         * out of tune. The fade rides on the oscillator itself (a rotation a
         * hair bigger or smaller than 1), so it costs nothing per sample. The
         * answer tone's phase flips happen while it is faded right down. */
        float ta = PHONE_LEVEL * aa, tb = PHONE_LEVEL * ab;
        int want_fl = (flip < 0.0f);
        if (want_fl != nv->aol_fl) {
            if (nv->aol_ga <= 4.0f * AOL_FLOOR) nv->aol_fl = want_fl;   /* faded out: flip now */
            else ta = 0.0f;                                             /* fade out first */
        }
        float pha = nv->pha + (nv->aol_fl ? 0.5f : 0.0f);
        rot_init(&bb->pa, pha, ia);
        rot_init(&bb->pb, nv->phb, ib > 0.0f ? ib : 0.0f);
        aol_glide(&bb->pa, &nv->aol_ga, ta, n);
        aol_glide(&bb->pb, &nv->aol_gb, tb, n);
        nv->pha += ia * (float)n; nv->pha -= floorf(nv->pha);
        nv->phb += ib * (float)n; nv->phb -= floorf(nv->phb);
        bb->pn_amp = PHONE_LEVEL * pn;
        bb->aol_two = (age < 1.45f);                      /* lets the 2nd tone fade out first */
        if (!bb->aol_two) nv->aol_gb = AOL_FLOOR;
    }
    if (layers & LAYER_BIT(VB_HIGH)) {
        float ih = 2.0f * base_inc;                               /* an octave up */
        if (ih > 0.45f) ih = 0.45f;
        rot_init(&bb->oh, nv->phfar, ih);
        nv->phfar += ih * (float)n;
        nv->phfar -= floorf(nv->phfar);
    }
    bb->inc = inc;
    rot_init(&bb->om, nv->phase, inc);

    if (layers & LAYER_BIT(VB_LOWREY)) {
        /* blend the two measured recipes across the keyboard */
        float t = ((float)midi - 48.0f) / 12.0f;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.5f) t = 1.5f;
        for (int k = 0; k < 10; k++) {
            float db = LOWREY_DB_LOW[k] + (LOWREY_DB_MID[k] - LOWREY_DB_LOW[k]) * t;
            bb->fl_amp[k] = 0.40f * powf(10.0f, db * 0.05f);
        }
        /* the organ's buzz floor: about -50 dB at low C, -45 dB at mid C */
        /* a narrow pulse's harmonics are each ~4 x width: size it to land the
         * floor at the organ's level */
        bb->buzz_w = 2.5f * inc;
        if (bb->buzz_w > 0.05f) bb->buzz_w = 0.05f;
        bb->buzz = BUZZ_GAIN * 0.40f * powf(10.0f, (-50.0f + 5.0f * t) * 0.05f) / (4.0f * bb->buzz_w);
    }
    if (layers & LAYER_BIT(VB_PS20)) {
        rot_init(&bb->ov, nv->phase_vib, base_inc * (1.0f + lfo_mid * 0.0065f));
        nv->phase_vib += base_inc * (1.0f + lfo_mid * 0.0065f) * (float)n;
        nv->phase_vib -= floorf(nv->phase_vib);
    }
    if (layers & (LAYER_BIT(VB_PS20) | LAYER_BIT(VB_WARM))) {
        rot_init(&bb->ob, nv->phase_sub, base_inc * 1.0035f);
        nv->phase_sub += base_inc * 1.0035f * (float)n;
        nv->phase_sub -= floorf(nv->phase_sub);
    }
    /* music box (octave up): its twin at 2 x 1.004 and its tine overtone at
     * 2 x 6.27; WARM: its sub-octave */
    {
        static const float RATIO[3] = { 2.0f * 1.004f, 2.0f * 6.27f, 0.5f };
        for (int k = 0; k < 3; k++) {
            int used = (k < 2) ? ((layers & LAYER_BIT(VB_MBOX)) != 0u) : ((layers & LAYER_BIT(VB_WARM)) != 0u);
            if (!used) continue;
            float ik = base_inc * RATIO[k];
            if (k == 1) bb->mb_tine = (ik < 0.45f) ? 1.0f : 0.0f;
            if (ik >= 0.45f) ik = 0.45f;
            rot_init(&bb->oi[k], nv->bph[k], ik);
            nv->bph[k] += ik * (float)n;
            nv->bph[k] -= floorf(nv->bph[k]);
        }
    }
    /* music box ring: longer on low notes (~2 s), shorter on high (~0.6 s);
     * its tine overtone fades in ~0.12 s */
    if (layers & LAYER_BIT(VB_MBOX)) {
        float f2 = 2.0f * base_inc * (float)FM1_AUDIO_RATE;
        float tau = 1.8f * sqrtf(523.0f / f2);
        if (tau < 0.5f) tau = 0.5f;
        if (tau > 3.0f) tau = 3.0f;
        float half = expf(-0.5f * (float)n / (tau * (float)FM1_AUDIO_RATE));
        bb->st1 = nv->strike * half;
        nv->strike *= half * half;
        if (nv->strike < 1.0e-5f) nv->strike = 0.0f;
        bb->d3 = 0.99981f;
    }

    /* combine every harmonic voice into one weight per harmonic and channel */
    for (int k = 0; k < 10; k++) { bb->wl[k] = 0.0f; bb->wr[k] = 0.0f; }
    bb->nh = 0;
    #define ADDH(k, a, v) do { bb->wl[k] += (a) * g_pan_l[v]; bb->wr[k] += (a) * g_pan_r[v]; \
                               if ((k) + 1 > bb->nh) bb->nh = (k) + 1; } while (0)
    if (layers & LAYER_BIT(VB_LOWREY)) {
        /* harmonics 1-8; the organ's 9th and 10th sit at its buzz floor, which the buzz supplies */
        for (int k = 0; k < 8; k++) ADDH(k, bb->fl_amp[k], VB_LOWREY);
    }
    if (layers & LAYER_BIT(VB_WARM)) {
        ADDH(0, 0.50f, VB_WARM);                                /* the sine */
        ADDH(1, 0.031f, VB_WARM);                               /* a whisper of 2nd harmonic (-24 dB) */
    }
    if (layers & LAYER_BIT(VB_MBOX)) {
        ADDH(1, 0.0f, VB_MBOX);                                 /* its note (an octave up) is mixed in its own
                                                                    section, so it can swell in softly */
    }
    #undef ADDH
}

static inline __attribute__((always_inline)) void render_note(NoteVoice *nv, uint32_t layers, float lfo_val,
                        float inc, float inc_v, RotOsc *om, RotOsc *ov, RotOsc *ob,
                        BankBBlock *bb,
                        float atk_step, float dec_coeff,
                        float *acc_l, float *acc_r) {
    float l = 0.0f;
    float r = 0.0f;
    float x;

    (void)bb;

    /* Stop the pluck fade at a floor: letting it shrink forever drives it into
     * denormal floats (~3 s after a note starts), which are very slow to
     * process and cause audio dropouts. */
    if (nv->pluck_state > 0.0001f) {
        nv->pluck_state *= 0.9993f;
    } else {
        nv->pluck_state = 0.0f;
    }

    if (layers & LAYERS_MAIN) {
        nv->phase += inc;
        if (nv->phase >= 1.0f) nv->phase -= 1.0f;
        float p = nv->phase;
        rot_step(om);
        float s1 = om->s, c1 = om->c;
        float s2 = 2.0f * s1 * c1;

        if (layers & LAYER_BIT(VOICE_FLUTE)) {
            x = 0.45f * s1 + 0.10f * s2;
            l += x * g_pan_l[VOICE_FLUTE]; r += x * g_pan_r[VOICE_FLUTE];
        }
        if (layers & (LAYER_BIT(VOICE_HARPSI) | LAYER_BIT(VOICE_REED))) {
            float s3 = s1 * (3.0f - 4.0f * s1 * s1);
            if (layers & LAYER_BIT(VOICE_HARPSI)) {
                float saw = (2.0f * p - 1.0f) - poly_blep(p, inc);
                x = (0.20f * saw + 0.18f * s2 + 0.10f * s3) * (0.40f + 0.60f * nv->pluck_state);
                l += x * g_pan_l[VOICE_HARPSI]; r += x * g_pan_r[VOICE_HARPSI];
            }
            if (layers & LAYER_BIT(VOICE_REED)) {
                float sq = (p < 0.5f) ? 0.48f : -0.48f;
                x = 0.38f * sq + 0.20f * s3;
                l += x * g_pan_l[VOICE_REED]; r += x * g_pan_r[VOICE_REED];
            }
        }
        if (layers & LAYER_BIT(VOICE_HORN)) {
            float tri = 2.0f * fabsf(2.0f * p - 1.0f) - 1.0f;
            x = 0.42f * tri + 0.16f * s2;
            l += x * g_pan_l[VOICE_HORN]; r += x * g_pan_r[VOICE_HORN];
        }
        if (layers & LAYER_BIT(VOICE_STRINGS)) {
            float saw_a = (2.0f * p - 1.0f) - poly_blep(p, inc);
            float p_b = p + 0.008f;
            if (p_b >= 1.0f) p_b -= 1.0f;
            float saw_b = (2.0f * p_b - 1.0f) - poly_blep(p_b, inc);
            x = 0.20f * (saw_a + saw_b) + 0.10f * s2;
            l += x * g_pan_l[VOICE_STRINGS]; r += x * g_pan_r[VOICE_STRINGS];
        }
    }

    /* VOX and VIBES (vibrato: their pitch follows the LFO, set per block) */
    if (layers & LAYERS_VIB) {
        nv->phase_vib += inc_v;
        if (nv->phase_vib >= 1.0f) nv->phase_vib -= 1.0f;
        rot_step(ov);
        float s1 = ov->s, c1 = ov->c;
        float s2 = 2.0f * s1 * c1;
        float s4 = 2.0f * s2 * (1.0f - 2.0f * s1 * s1);
        if (layers & LAYER_BIT(VOICE_VOX)) {
            x = (0.30f * s1 + 0.20f * s2 + 0.10f * s4) * (0.92f + 0.08f * lfo_val);
            l += x * g_pan_l[VOICE_VOX]; r += x * g_pan_r[VOICE_VOX];
        }
        if (layers & LAYER_BIT(VOICE_VIBES)) {
            x = (0.48f * s1 + 0.10f * s2 + 0.06f * s4) * (0.88f + 0.12f * lfo_val);
            l += x * g_pan_l[VOICE_VIBES]; r += x * g_pan_r[VOICE_VIBES];
        }
    }

    /* BASS: a sub-octave and its octave */
    if (layers & LAYER_BIT(VOICE_BASS)) {
        nv->phase_sub += inc * 0.5f;
        if (nv->phase_sub >= 1.0f) nv->phase_sub -= 1.0f;
        rot_step(ob);
        x = 0.48f * ob->s + 0.14f * (2.0f * ob->s * ob->c);
        l += x * g_pan_l[VOICE_BASS]; r += x * g_pan_r[VOICE_BASS];
    }

    note_mel_env(nv, layers, atk_step, dec_coeff, l, r, acc_l, acc_r);
}

static inline __attribute__((always_inline)) void render_note_b(NoteVoice *nv, uint32_t layers, float lfo_val,
                                                                 BankBBlock *bb, float atk_step, float dec_coeff,
                                                                 float *acc_l, float *acc_r) {
    float l = 0.0f, r = 0.0f;
    render_bank_b(nv, layers, lfo_val, bb, &l, &r);
    note_mel_env(nv, layers, atk_step, dec_coeff, l, r, acc_l, acc_r);
}

/* Note-count scaling (as before): eases the level as notes and tails pile up */
static float note_norm(float w) {
    if (w <= 1.0f) return 1.0f;
    float note_scale = 0.5f + 0.5f / w;
    if (w > 1.5f) {
        note_scale *= 1.0f / (1.0f + 0.18f * g_decay * (w - 1.5f));
    }
    return note_scale;
}

/* One note for a whole audio block: the per-sample work stays in one tight
 * loop (no call per sample), mixing into the block buffers */
static void render_note_block(NoteVoice *nv, uint32_t layers, const float *lfo, int n,
                              float atk_step, float dec_coeff, float *bl, float *br) {
    float inc = nv->is_midi ? g_midi_inc[nv->note_idx] : g_note_inc[nv->note_idx];
    float inc_v = inc * (1.0f + lfo[n / 2] * 0.0085f);   /* vibrato, per block */
    if (g_bank) {
        static BankBBlock bb;
        bank_b_prepare(nv, layers, inc, lfo[n / 2], n, &bb);
        for (int i = 0; i < n; i++) {
            render_note_b(nv, layers, lfo[i], &bb, atk_step, dec_coeff, &bl[i], &br[i]);
        }
        return;
    }
    RotOsc om, ov, ob;
    if (layers & LAYERS_MAIN) rot_init(&om, nv->phase, inc);
    if (layers & LAYERS_VIB) rot_init(&ov, nv->phase_vib, inc_v);
    if (layers & LAYER_BIT(VOICE_BASS)) rot_init(&ob, nv->phase_sub, inc * 0.5f);
    for (int i = 0; i < n; i++) {
        render_note(nv, layers, lfo[i], inc, inc_v, &om, &ov, &ob, 0, atk_step, dec_coeff, &bl[i], &br[i]);
    }
}

/* Gentle soft-knee ceiling to prevent hard clipping without adding heavy harmonic grit */
/* Flush tiny values to zero so decaying filters never become denormal */
static inline float flush_denormal(float x) {
    return (fabsf(x) < 1.0e-15f) ? 0.0f : x;
}

/* Final output limiter: clean below 0.8 of full scale, then bends smoothly
 * toward 1.0 instead of clipping hard */
static inline float output_limit(float x) {
    float ax = fabsf(x);
    if (ax <= 0.8f) return x;
    float y = 0.8f + 0.2f * tanhf((ax - 0.8f) * 5.0f);
    return (x < 0.0f) ? -y : y;
}

static inline __attribute__((always_inline)) float smooth_limit(float x) {
    float ax = fabsf(x);
    if (ax < 0.0001f) return 0.0f;
    float u = 0.15f * ax;
    if (u < 0.2f) {
        /* x / (1 + u) without the (slow) division: series, error < 0.2% */
        return x * (1.0f - u + u * u - u * u * u);
    }
    return x / (1.0f + u);
}

/* ---------------- Public Entry Points ---------------- */
void setup(void) {
    /* Paint the screen night-sky dark before anything else so the panel
     * doesn't sit on white while the rest of setup runs */
    fm1_lcd_fill_rect(0u, 0u, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, COL_NIGHT_SKY);
    apply_themes();
    init_dsp_tables();
    init_mel_flute();
    reverb_init();
    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        g_voice_on[v] = (v == VOICE_FLUTE) ? 1 : 0;
        g_bank_voices[0][v] = g_voice_on[v];
        g_bank_voices[1][v] = (v == VB_LOWREY) ? 1 : 0;
        g_prev_voice_on[v] = -1;
        float pan = ((float)v - 3.5f) / 3.5f;
        g_pan_l[v] = 0.7071f * (1.0f - 0.35f * pan);
        g_pan_r[v] = 0.7071f * (1.0f + 0.35f * pan);
    }
    for (uint32_t s = 0u; s < MAX_NOTES; s++) {
        g_pool[s].active = 0;
        g_pool[s].sustained = 0;
        g_pool[s].env = 0.0f;
        g_pool[s].stage = 2;
        g_pool[s].is_midi = 0;
        g_pool[s].note_idx = 0;
        g_pool[s].mf_zone = 0;
        g_pool[s].mf_rate = 0.25f;
    }

    for (uint32_t i = 0u; i < FM1_NLED; i++) {
        g_led_state[i] = -1;
    }

    g_octave_shift = 0;
    g_hold_on = 0;
    g_view = VIEW_SPLASH;
    g_splash_start_ms = g_tick_ms;
    g_any_note_sounding = 0u;
    g_need_full_clear = 1;
    g_splash_bg_drawn = 0;
    update_leds();
    render_screen();
}

void audio(int32_t *out, int frames) {
    update_note_cap();
    sync_keyboard_notes();

    /* MIDI notes are handled here, in the audio interrupt, so they can never
     * change the note pool while it is being rendered (fm1.h: call
     * fm1_midi_take from one place only, audio() or loop()). */
    uint32_t pkt = 0u;
    while (fm1_midi_take(&pkt)) {
        handle_midi_packet(pkt);
    }
    g_audio_frames += (uint32_t)frames;

    /* more voices switched on while notes ring: stop the quietest extras */
    {
        int busy = 0;
        for (uint32_t s = 0u; s < MAX_NOTES; s++) busy += note_busy(&g_pool[s]);
        while (busy > g_max_notes) {
            uint32_t q = 0u;
            float lo = 999.0f;
            for (uint32_t s = 0u; s < MAX_NOTES; s++) {
                if (note_busy(&g_pool[s]) && g_pool[s].env < lo) { lo = g_pool[s].env; q = s; }
            }
            g_pool[q].active = 0;
            g_pool[q].sustained = 0;
            g_pool[q].stage = 2;
            g_pool[q].env = 0.0f;
            busy--;
        }
    }

    /* Audio-taper volume curve at a middle-ground ceiling across the full knob */
    uint32_t m_val = fm1_master();
    float m_norm = clamp01((float)m_val / (float)FM1_MASTER_MAX);
    float master_gain = (m_norm * m_norm) * MAX_OUTPUT_GAIN;

    /* Per-block pitch table (octave shift applied once, not per sample) */
    static const float OCT_MUL[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    int oct_i = g_octave_shift + 2;
    if (oct_i < 0) oct_i = 0;
    if (oct_i > 4) oct_i = 4;
    float oct_mul = OCT_MUL[oct_i];
    for (uint32_t n = 0u; n < FM1_NOTE_KEYS; n++) {
        g_note_inc[n] = g_note_inc_base[n] * oct_mul;
    }

    float atk_ms = 2.0f + g_attack * 440.0f;
    float atk_step = 1.0f / ((atk_ms * 0.001f) * (float)FM1_AUDIO_RATE);
    if (atk_step > 1.0f) atk_step = 1.0f;

    float dec_ms = 60.0f + g_decay * 3200.0f;
    float dec_coeff = powf(0.01f, 1.0f / ((dec_ms * 0.001f) * (float)FM1_AUDIO_RATE));

    float fc = 180.0f * powf(78.0f, g_filter_cutoff);
    float lp_a = 1.0f - expf(-6.2831853f * fc / (float)FM1_AUDIO_RATE);
    if (lp_a < 0.005f) lp_a = 0.005f;
    if (lp_a > 0.99f) lp_a = 0.99f;

    uint32_t dly_samples = 3300u + (uint32_t)(g_dly_time * 18000.0f);
    if (dly_samples >= DELAY_MAX_SAMPLES) dly_samples = DELAY_MAX_SAMPLES - 1u;

    /* Reverb: DECAY sets the room (0.70 small .. 0.98 huge, ~0.6 s to ~8 s tails),
     * with a little more high-cut on long settings so big tails stay smooth */
    float rv_room = 0.70f + 0.28f * g_rev_decay;
    float rv_damp = 0.20f + 0.15f * g_rev_decay;
    /* Longer rooms build up more energy: turn the wet level down to match,
     * so DECAY changes the length of the tail, not the volume */
    float rv_comp = powf((1.0f - rv_room * rv_room) / (1.0f - 0.70f * 0.70f), RV_COMP_EXP);
    float dly_fb = g_dly_feedback * 0.82f;       /* capped at 0.82 to prevent runaway buildup */
    int any_active_in_block = 0;
    int any_held_in_block = 0;

    /* Which voice layers are on (read once per block) */
    uint32_t layers = 0u;
    int active_layers = 0;
    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        if (g_voice_on[v]) {
            layers |= LAYER_BIT(v);
            active_layers++;
        }
    }
    if (g_mel_level > 0.005f) {
        layers |= LAYER_MEL;
        active_layers++;
    }
    g_mel_gain = g_mel_level * 2.40f;   /* 100% = about as loud as one synth voice */

    if (frames > FM1_AUDIO_BLOCK) frames = FM1_AUDIO_BLOCK;

    /* Vibrato LFO: a sine at 5.2 Hz, interpolated across the block */
    static float lfo_buf[FM1_AUDIO_BLOCK];
    {
        float lfo0 = fast_sin(g_lfo_phase);
        g_lfo_phase += 5.2f * (float)frames / (float)FM1_AUDIO_RATE;
        if (g_lfo_phase >= 1.0f) g_lfo_phase -= 1.0f;
        float lfo1 = fast_sin(g_lfo_phase);
        float d = (lfo1 - lfo0) / (float)frames;
        for (int i = 0; i < frames; i++) lfo_buf[i] = lfo0 + d * (float)i;
    }

    /* Which notes are held right now (for AOL) */
    g_held_n = 0;
    for (uint32_t n = 0u; n < MAX_NOTES; n++) {
        NoteVoice *nv = &g_pool[n];
        if (nv->active || nv->sustained) {
            g_held_inc[g_held_n++] = nv->is_midi ? g_midi_inc[nv->note_idx] : g_note_inc[nv->note_idx];
        }
    }

    /* All notes, a whole block at a time */
    static float blk_l[FM1_AUDIO_BLOCK], blk_r[FM1_AUDIO_BLOCK];
    for (int i = 0; i < frames; i++) { blk_l[i] = 0.0f; blk_r[i] = 0.0f; }
    float weight0 = 0.0f, weight1 = 0.0f;
    for (uint32_t n = 0u; n < MAX_NOTES; n++) {
        NoteVoice *nv = &g_pool[n];
        if (nv->active || nv->sustained || nv->env > 0.03f) {
            weight0 += nv->env;
            render_note_block(nv, layers, lfo_buf, frames, atk_step, dec_coeff, blk_l, blk_r);
            weight1 += nv->env;
            if (layers != 0u && (nv->active || nv->sustained || nv->env > 0.04f)) {
                any_active_in_block = 1;
            }
            if (layers != 0u && (nv->active || nv->sustained)) {
                any_held_in_block = 1;
            }
        }
    }

    /* Polyphony / layer scaling, from the notes' levels at the block's start
     * and end (the divisions happen twice a block, not every sample) */
    float layer_norm = 0.55f;
    if (active_layers > 1) layer_norm *= 0.5f + 0.5f / (float)active_layers;
    float norm0 = layer_norm * note_norm(weight0);
    float norm1 = layer_norm * note_norm(weight1);
    float norm_step = (norm1 - norm0) / (float)frames;

    /* Half-rate reverb state */
    static float rv_in = 0.0f, rv_prev_l = 0.0f, rv_prev_r = 0.0f, rv_cur_l = 0.0f, rv_cur_r = 0.0f;
    static int rv_odd = 0;
    float rv_damp2 = rv_damp * rv_damp;   /* the same tone at half rate */

    for (int i = 0; i < frames; i++) {
        float norm = norm0 + norm_step * (float)i;
        float mix_l = blk_l[i] * norm;
        float mix_r = blk_r[i] * norm;

        g_lp_l = flush_denormal(g_lp_l + lp_a * (mix_l - g_lp_l));
        g_lp_r = flush_denormal(g_lp_r + lp_a * (mix_r - g_lp_r));
        mix_l = g_lp_l;
        mix_r = g_lp_r;

        g_scope_buf[g_scope_w & (SCOPE_BUF_LEN - 1u)] = 0.5f * (mix_l + mix_r);
        g_scope_w++;

        uint32_t dly_read = g_dly_write + DELAY_MAX_SAMPLES - dly_samples;
        if (dly_read >= DELAY_MAX_SAMPLES) dly_read -= DELAY_MAX_SAMPLES;
        /* Mono delay: fed with L+R, echoes return to the centre */
        float dm = g_dly[dly_read];
        g_dly[g_dly_write] = smooth_limit(0.5f * (mix_l + mix_r) + dm * dly_fb);
        float dl = dm;
        float dr = dm;
        if (++g_dly_write >= DELAY_MAX_SAMPLES) g_dly_write = 0u;

        float dry_l = mix_l * (1.0f - 0.4f * g_dly_mix) + dl * g_dly_mix;
        float dry_r = mix_r * (1.0f - 0.4f * g_dly_mix) + dr * g_dly_mix;

        /* Reverb at half rate: one reverb step per two samples, fed with
         * their average; its output is interpolated back to full rate */
        float rl, rr;
        rv_in += dry_l + dry_r;
        if (rv_odd) {
            rv_prev_l = rv_cur_l;
            rv_prev_r = rv_cur_r;
            reverb_process(rv_in * 0.0075f, rv_room, rv_damp2, &rv_cur_l, &rv_cur_r);
            rv_in = 0.0f;
            rl = 0.5f * (rv_prev_l + rv_cur_l);
            rr = 0.5f * (rv_prev_r + rv_cur_r);
        } else {
            rl = rv_cur_l;
            rr = rv_cur_r;
        }
        rv_odd ^= 1;

        /* MIX: 0 = dry, 50% = a clear room, 100% = mostly reverb */
        float wet = g_rev_mix * RV_WET_GAIN * rv_comp;
        float dry_k = 1.0f - RV_DRY_DROP * g_rev_mix;
        float out_l = output_limit(smooth_limit(dry_l * dry_k + rl * wet) * master_gain);
        float out_r = output_limit(smooth_limit(dry_r * dry_k + rr * wet) * master_gain);

        float max_s = (float)FM1_AUDIO_MAX;
        int32_t s_l = (int32_t)(out_l * max_s);
        int32_t s_r = (int32_t)(out_r * max_s);
        if (s_l > FM1_AUDIO_MAX) s_l = FM1_AUDIO_MAX;
        if (s_l < -FM1_AUDIO_MAX) s_l = -FM1_AUDIO_MAX;
        if (s_r > FM1_AUDIO_MAX) s_r = FM1_AUDIO_MAX;
        if (s_r < -FM1_AUDIO_MAX) s_r = -FM1_AUDIO_MAX;

        out[i * 2]     = s_l;
        out[i * 2 + 1] = s_r;
    }

    g_any_note_sounding = any_active_in_block ? 1u : 0u;
    g_any_note_held = any_held_in_block ? 1u : 0u;
}

void loop(void) {
    /* loop() runs at an uneven rate (about once per audio block, slower while
     * drawing), so take the time from the audio sample count */
    g_tick_ms = (uint32_t)(((uint64_t)g_audio_frames * 1000u) / (uint64_t)FM1_AUDIO_RATE);
    process_controls();
    update_leds();
    render_screen();
}
