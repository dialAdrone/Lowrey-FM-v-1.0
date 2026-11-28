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
static uint32_t g_main_theme = 7u;   /* starts on CHOCOLATE */
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
    "LOWREY", "CHIP", "PIANO", "PS-20",
    "M.BOX", "AOL", "WARM", "HIGH"
};

/* Bank C voices (hold EDIT): glitchy, fuzzy and soft, after the feel of
 * Broadcast's "Tender Buttons" (sounds of our own) */
enum {
    VC_LOWREY = 0,  /* the eye: the same Lowrey flute as bank B */
    VC_FUZZ,        /* a smiley glitching out: a sputtering, wave-folded fuzz */
    VC_GLITCH,      /* old TV rolling: a stuttering, octave-jumping synth, always in tune */
    /* the next five are noise layers measured from four Broadcast songs */
    VC_FLICKER,     /* a hand holding a lantern: a bright pulse drone that keeps stuttering as it
                       holds ("Corporeal", the opening) */
    VC_BURST,       /* a wine glass that shatters: one burst of tuned noise per note, fading ("I Found The F") */
    VC_CRUSH,        /* a beer can getting crushed: a bit-crushed lead whose grit pulses, late and never settling */
    VC_WASH,        /* the moon behind clouds: reversed blips arpeggiating your held notes, and fifths */
    VC_STEPS        /* a white medieval horse: soft hiss with soft, fuzzy glints on fifths and octaves of the notes held ("Arc of a Journey", ~4:00) */
};

static const char *VOICE_NAMES_C[NUM_VOICES] = {
    "LOWREY", "FUZZ", "GLITCH", "FLICKER",
    "BURST", "CRUSH", "WASH", "STEPS"
};

/* WAVES voices (HOLD ARP): a wavetable synth, after TastyWaves (marksweiss,
 * GPL-3.0), whose wavetables these are. SAW, SQUARE and SINE are plain;
 * LUSH is three detuned saws; GROWL and FOLDS move through their tables;
 * MAXFOLD sits at the fold table's far end. 5TH+OCT is built as it plays. */
enum {
    VE_SAW = 0,     /* ENV: a plain saw */
    VE_LUSH,        /* LFO: three saws a little out of tune, spread wide: a lush ensemble */
    VE_SQUARE,      /* EDIT: a plain square */
    VE_SINE,        /* GLO: a plain sine */
    VE_GROWL,       /* ARP: a wavefolder that gets meaner the longer you hold */
    VE_FOLDS,       /* SEQ: the wavefolder, wobbling quickly */
    VE_MAXFOLD,     /* PLAY: the wavefolder held at its far end - where GROWL ends up */
    VE_FIFTH        /* REC: the fifth and the octave above the note (with a little of the note): layer it on */
};
#define VE_FIRST_ADD VE_FIFTH

static const char *VOICE_NAMES_E[NUM_VOICES] = {
    "SAW", "LUSH", "SQUARE", "SINE",
    "GROWL", "FOLDS", "MAXFOLD", "5TH+OCT"
};

static const char *VOICE_NAMES_D[NUM_VOICES] = {
    "ROCK", "SWING", "WALTZ", "BOSSA",
    "RHUMBA", "MARCH", "SLOW RK", "COUNTRY"
};
static const uint16_t VOICE_COLORS_D[NUM_VOICES] = {
    0xFA8Au, 0x5E7Fu, 0xFFE0u, 0x07EFu, 0xFD20u, 0xF9E7u, 0xB71Fu, 0xFC9Fu
};
static const uint16_t VOICE_COLORS_E[NUM_VOICES] = {
    0xFD20u, 0xFD20u, 0xFD20u, 0x5E7Fu, 0xF9E7u, 0xF9E7u, 0xF9E7u, 0xFFE0u   /* orange (saw/square table), sky (sine), red (fold), yellow (intervals) */
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

static const uint16_t VOICE_COLORS_C[NUM_VOICES] = {
    COL_BUBBLE_HI, /* LOWREY (eye) */
    COL_APPLE_MD,  /* FUZZ (amp on fire) */
    COL_LEAF_HI,   /* GLITCH (TV) */
    COL_GOLD,      /* TAPE (cassette) */
    COL_APPLE_HI,  /* GLASS (wine glass) */
    COL_GOLD_HI,   /* CRUSH (beer can) */
    COL_BUBBLE_MD, /* HUSH (moon, clouds) */
    COL_TEXT       /* FEEDBACK (light through a cloud) */
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
    VIEW_TRUCK,   /* SELECT knob page 3: truck drives while notes play */
    VIEW_SEQ,     /* hold SEQ: the step sequencer */
    VIEW_FACE,    /* hold FX: one big picture - smiley, horse, eye or TV (then the intro scene) */
    VIEW_DSEQ,    /* hold SEQ on the RHYTHM bank: the drum sequencer */
    VIEW_GLOBAL   /* hold GLO: MIDI channels */
};

static int g_face_sel = 0;    /* the big picture: 0 smiley, 1 horse, 2 eye, 3 TV, 4 the intro scene */

static int is_home_page(int view) {
    return view == VIEW_HOME || view == VIEW_SCOPE || view == VIEW_TRUCK;
}

static int g_view = VIEW_SPLASH;
static int g_voice_on[NUM_VOICES];
static int g_bank = 0;                       /* 0 = bank A (organ), 1 = bank B (hold HOME), 2 = bank C (hold EDIT), 4 = WAVES; hold HOME/ENV/LFO/ARP for A/B/C/WAVES; 3 is unused */
static int g_bank_voices[6][NUM_VOICES];     /* each bank remembers its own voice buttons */

/* ---- RHYTHM (bank 3, hold PLAY): an old organ's rhythm unit ----
 * Twelve drums in the manner of a late-60s rhythm box like the Rhythm Ace:
 * pure tuned pings and soft filtered hiss, no samples, and eight built-in beats on the voice buttons. A beat
 * keeps going on the other banks, at the sequencer's tempo. */
#define BANK_RHYTHM 3
#define NUM_DRUMS   12
enum { DR_BD, DR_SD, DR_RIM, DR_HAT, DR_CYM, DR_CLAVE, DR_COWBELL, DR_BONGO_HI, DR_BONGO_LO, DR_CONGA, DR_MARACA, DR_GUIRO };
typedef struct { uint8_t len, spb; uint16_t hits[NUM_DRUMS]; uint16_t soft; } Beat;   /* bit n of hits[d]: drum d on step n; soft: quiet (ghost) snare steps */
static const Beat BEATS[NUM_VOICES] = {
    { 16, 2, { 0x1919u, 0xC4C4u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0xFFFFu, 0x0000u }, 0x0000u },   /* ROCK: as a real Lowrey's - kick, chh, kick-kick, chh-chh (the "chh" single, then double); a shaker driving it; in eighths, so it sits at the same tempo as the others */
    { 12, 3, { 0x0041u, 0x0000u, 0x0208u, 0x0208u, 0x0A69u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u }, 0x0000u },   /* SWING */
    { 12, 4, { 0x0001u, 0x0000u, 0x0110u, 0x0110u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u }, 0x0000u },   /* WALTZ */
    { 16, 4, { 0x9999u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x2449u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x5555u, 0x0000u }, 0x0000u },   /* BOSSA */
    { 16, 4, { 0x0101u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x1449u, 0x0000u, 0x0404u, 0x4000u, 0x00C0u, 0x5555u, 0x0000u }, 0x0000u },   /* RHUMBA */
    { 16, 4, { 0x0101u, 0xD010u, 0x0000u, 0x0000u, 0x0101u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u }, 0x0000u },   /* MARCH */
    { 12, 3, { 0x0061u, 0x0208u, 0x0000u, 0x0FFFu, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u }, 0x0000u },   /* SLOW RK */
    { 12, 3, { 0x0045u, 0x0A28u, 0x0000u, 0x096Du, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u }, 0x0820u },   /* COUNTRY: the train-beat shuffle - boom-ba chick-a, boom . chick-a, a hi-hat shuffle keeps it rolling */
};
/* two beats can play at once, layered: two slots */
static volatile int g_beat_sel[2] = { -1, -1 };   /* the beats picked (-1: none) */
static int g_beat_old = 0;                        /* which slot was picked first (a third tap replaces it) */
static volatile int g_beat[2] = { -1, -1 };       /* the beats playing (audio side) */
static volatile int g_beat_step[2] = { 0, 0 };    /* their steps, for the screen and lights */
static volatile int g_beat_sync = 0;          /* the sequencer started: back to the top of the bar */
static float g_beat_vol = 0.7f;               /* K3 on the rhythm bank */
static float g_beat_tone = 0.5f;              /* K4 on the rhythm bank: dark .. bright */
/* The RHYTHM bank sits on top of the bank you came from: that bank keeps
 * sounding (the note sequencer, a held chord), only the keys play drums */
static int g_snd_bank = 0;                    /* the bank under the RHYTHM bank */
static volatile int g_rhythm_ui = 0;          /* (audio side) the RHYTHM bank is showing */
/* the drum sequencer (hold SEQ on the RHYTHM bank): its own pattern, length,
 * PLAY and REC; one bit a drum on each step */
#define DSEQ_STEPS 32
static uint16_t g_dseq[DSEQ_STEPS];
static uint16_t g_dseq_skip[DSEQ_STEPS];      /* hits played early while recording: not again this time */
static int g_dseq_len = 16;
static volatile int g_dseq_play = 0, g_dseq_rec = 0;
static volatile int g_dseq_req_play = -1, g_dseq_req_clear = 0, g_dseq_sync = 0;
static volatile int g_dseq_step = -1;
static int g_dseq_cur = 0;
static volatile uint32_t g_dseq_ver = 0u;
static float g_dseq_acc = 0.0f;
static const uint16_t DRUM_LAMP[NUM_DRUMS] = {
    0xF8C3u, 0xFFE0u, 0xFEB0u, 0x5FFFu, 0x7EFFu, 0x07E8u,
    0xFCA0u, 0xFC10u, 0xD8A0u, 0xF81Fu, 0xAFE5u, 0xFD8Cu };
/* GLOBAL (hold GLO): MIDI in. SINGLE: the bank on screen plays every channel
 * (as before). MULTI: each bank has its own channel and they all play at once,
 * whatever is on screen - for sequencing them together from a computer.
 * Index by bank: A, B, C, RHYTHM, WAVES; 0 = off. */
static int g_midi_multi = 0;
static int8_t g_bank_ch[5] = { 1, 2, 3, 10, 4 };
static int g_glob_row = 0;                    /* the GLOBAL screen's cursor */
static int g_note_bank = -1;                  /* (audio side) the bank for the next note_on, -1: the one on screen */
static int g_note_ch = 0;

/* ---- MIDI OUT (fm1_midi_send): what goes to the computer, set in GLOBAL ----
 * KEYS: the keys you play (on the RHYTHM bank, as drums on channel 10);
 * SEQ: the note sequencer; DRUMS: the beats and the drum pattern, as General
 * MIDI drum notes on channel 10; CLOCK: MIDI clock, and start/stop. Keys and
 * the sequencer go on channel 1, or in MULTI on their bank's channel. */
static int g_mo_keys = 1, g_mo_seq = 1, g_mo_drums = 0, g_mo_clock = 1;
/* USB SPLIT (GLOBAL): the organ in mono on the left, the drums (dry, in mono)
 * on the right - to record them on two tracks */
static int g_usb_split = 0;
static int mo_send(uint32_t st, uint32_t d1, uint32_t d2) {
#ifdef FM1_MIDI_PACKET
    return fm1_midi_send(FM1_MIDI_PACKET(st, d1, d2));
#else
    (void)st; (void)d1; (void)d2;      /* (an FM1 Studio without MIDI out: nothing is sent) */
    return 0;
#endif
}
/* note-offs that could not go out yet wait here and are sent again, so no
 * note is left hanging in the computer */
static uint32_t g_mo_pend[16][4];
static void mo_off(int ch, int note) {
    if (ch < 1 || ch > 16 || note < 0 || note > 127) return;
    if (!mo_send(0x80u | (uint32_t)(ch - 1), (uint32_t)note, 0u))
        g_mo_pend[ch - 1][note >> 5] |= 1u << (note & 31);
}
static void mo_on(int ch, int note, int vel) {
    if (ch < 1 || ch > 16 || note < 0 || note > 127) return;
    g_mo_pend[ch - 1][note >> 5] &= ~(1u << (note & 31));
    mo_send(0x90u | (uint32_t)(ch - 1), (uint32_t)note, (uint32_t)vel);
}
static void mo_retry(void) {
    for (int c = 0; c < 16; c++)
        for (int w = 0; w < 4; w++)
            while (g_mo_pend[c][w]) {
                int b = __builtin_ctz(g_mo_pend[c][w]);
                if (!mo_send(0x80u | (uint32_t)c, (uint32_t)(w * 32 + b), 0u)) return;   /* still full: next time */
                g_mo_pend[c][w] &= ~(1u << b);
            }
}
/* MIDI start / stop: start when the first of the sequencers or a beat starts,
 * stop when the last one stops (then a clock straight after the start) */
static int g_mo_running = 0;
static float g_mo_clk_acc = 0.0f;
static int mo_running_now(void);
static void mo_transport(void) {
    int running = mo_running_now();
    if (g_mo_clock && running && !g_mo_running) {
        mo_send(0xFAu, 0u, 0u);
        mo_send(0xF8u, 0u, 0u);
        g_mo_clk_acc = 0.0f;
    }
    if (g_mo_clock && !running && g_mo_running) mo_send(0xFCu, 0u, 0u);
    g_mo_running = running;
}
/* the channel of the keys and the sequencer: 1, or in MULTI their bank's */
static int mo_ch(void) {
    int b = (g_bank == 3) ? g_snd_bank : g_bank;   /* (3: the RHYTHM bank: the bank under it) */
    if (g_midi_multi && b < 5 && g_bank_ch[b]) return g_bank_ch[b];
    return 1;
}
static const char *DRUM_SHORT[NUM_DRUMS] = { "BD", "SD", "RS", "HH", "CY", "CL", "CB", "BH", "BL", "CG", "MA", "GU" };
/* ---- POCKET (bank 5, hold SCL): after Critter & Guitari's Pocket Piano ----
 * Seven modes, one at a time, on the first seven voice buttons; the eighth
 * (REC) is its BUTTON: the waveform (sine, square, triangle, saw), or in FM
 * ARP an octave jump. PLAIN (in place of the original's Mono FM) plays the
 * waveform straight, with a decay and a tone knob. Up to 4 notes. K1 and K2 are the mode's two
 * knobs, K3 reverb, K4 volume; the octave buttons stand in for TUNE. Up
 * to 8 notes (the original: 4).
 * Arpeggios and cascades start on the first note pressed, every time. */
#define BANK_POCKET 5
enum { PP_VIBRATO, PP_SWEEPER, PP_ARP, PP_CASCADE, PP_PLAIN, PP_FMARP, PP_GLIDER, PP_MODES };
static int g_pp_mode = PP_VIBRATO;
static uint8_t g_pp_wave[PP_MODES] = { 0, 2, 1, 1, 1, 0, 3 };   /* waveform; FM ARP: octave jump 0/1 */
static float g_pp_p1[PP_MODES] = { 0.45f, 0.45f, 0.50f, 0.45f, 0.00f, 0.50f, 0.40f };
static float g_pp_p2[PP_MODES] = { 0.30f, 1.00f, 0.50f, 0.50f, 0.80f, 0.40f, 0.20f };
static float g_pp_vol = 0.80f;
static const char *PP_NAMES[PP_MODES] = { "VIBRATO", "SWEEPER", "OCT ARP", "CASCADE", "PLAIN", "FM ARP", "GLIDER" };
static const char *PP_P1[PP_MODES] = { "RATE", "SPEED", "SPEED", "SPEED", "DECAY", "SPEED", "GLIDE" };
static const char *PP_P2[PP_MODES] = { "DEPTH", "SPAN", "GATE", "GATE", "TONE", "DEPTH", "VIB" };
static const uint16_t PP_COLORS[NUM_VOICES] = { 0xFD20u, 0x5E7Fu, 0x07E8u, 0xB71Fu, 0xFFE0u, 0xF81Fu, 0x07FFu, 0xFFFFu };
static int pp_is_fm(int m) { return m == PP_FMARP; }

/* is this button lit / its card on? On the RHYTHM bank: the beat picked;
 * on POCKET: the mode, and the BUTTON when it is off its first setting */
static int voice_lit(uint32_t v) {
    if (g_bank == BANK_POCKET) return (v < PP_MODES) ? ((int)v == g_pp_mode) : (g_pp_wave[g_pp_mode] != 0);
    return (g_bank == BANK_RHYTHM) ? ((int)v == g_beat_sel[0] || (int)v == g_beat_sel[1]) : g_voice_on[v];
}
static float g_e_pos[NUM_VOICES];            /* bank E: where each voice is in its table (0..1), for its icon */
static int g_cig = 0;                        /* STRINGS: how far the cigarette has burned (0..7) */
static uint32_t g_cig_ms = 0u, g_cig_quiet_ms = 0u;
static uint32_t g_dt_ms = 0u;
static int g_octave_shift = 0; /* -2 .. +2 octaves */
static int g_hold_on = 0;

/* Envelope & Filter controls (0.0f .. 1.0f) */
static float g_attack = 0.04f;        /* maps to ~2ms .. 450ms */
static float g_decay = 0.55f;         /* maps to ~60ms .. 3260ms (KNOB3) */
static float g_filter_cutoff = 0.78f; /* low-pass cutoff (KNOB4) */
static float g_mel_level = 0.0f;      /* Mellotron flute layer (SAVE screen, KNOB2); 0 = off */
static float g_mel_last = 0.90f;      /* its last level above 0: what holding SAVE brings it back to (90% at start-up) */

/* ---- The step sequencer (after FMEL-1's, by AntonioAltea, GPL-3.0) ----
 * 16 steps of 1/16, up to 4 notes each (a chord). Hold SEQ for its screen:
 * PLAY starts/stops, REC records (stopped: step by step; playing: into the
 * step it falls on), REC held 1 s clears. K1 tempo, K2 length, K3 gate,
 * K4 swing, SELECT moves the step cursor (stopped). It keeps playing when you
 * leave the screen. The clock and the notes run in audio(); loop() only
 * sends requests. */
#define SEQ_STEPS 32                       /* up to 32; it starts at 16 */
static uint8_t g_seq[SEQ_STEPS][4];       /* MIDI notes, 0 = none */
static int g_seq_len = 16;
static int g_seq_bpm = 110;
static float g_seq_gate = 0.5f;           /* of a step */
static float g_seq_swing = 0.0f;          /* 0 straight .. 1 a triplet feel */
static volatile int g_seq_play = 0, g_seq_rec = 0;
static volatile int g_seq_step = 0;       /* the step playing */
static uint32_t g_seq_tick = 0u, g_seq_clk = 0u, g_seq_gate_left = 0u;
static volatile int g_seq_cur = 0;        /* the step-recording cursor */
static volatile uint32_t g_seq_ver = 0u;  /* bumped when the pattern changes (redraw) */
static volatile int g_seq_req_play = -1;  /* loop() -> audio(): 1 start, 0 stop, -1 none */
static volatile int g_seq_req_clear = 0;
static int g_seq_screen_gen = 0;           /* bumped on every full repaint (sequencer screen) */

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
    float cf[64];                    /* bank C state (CF_* below) */
    int ci[4];                       /* bank C counters (CI_* below) */
    int ab, ab_left, ab_off;         /* AUTO BASS: a "bum" on a kick (bass only), samples left held, start in the block */
    int ab_soft;                     /* AUTO BASS: joined late - swells in instead of striking */
    int ab_fade;                     /* AUTO BASS: the next "bum" came - fade out fast (it is one note at a time) */
    uint32_t t0;                     /* when it started (to find the oldest) */
    int bank;                        /* MULTI MIDI: the bank this note plays */
    int ch;                          /* ... and the MIDI channel it came on (0: not MIDI multi) */
    float cp_ph, cp_amp, cp_s1, cp_s2;   /* bank B PIANO (CP-30 style): oscillator, level, filter state */
} NoteVoice;
/* bank C state, per voice, so any of them can be on together */
enum { CF_FZ_PH, CF_FZ_K, CF_FZ_KT, CF_FZ_DC, CF_FZ_GATE,
       CF_GL_PH, CF_GL_HELD,
       CF_FL_PH, CF_FL_DR, CF_FL_DT, CF_FL_G0, CF_FL_T0 = CF_FL_G0 + 6, CF_FL_TM0 = CF_FL_T0 + 6,
       CF_FL_END = CF_FL_TM0 + 6,
       CF_BU_ENV, CF_BU_T, CF_BU_L1, CF_BU_B1, CF_BU_L2, CF_BU_B2, CF_BU_HP, CF_BU_LP, CF_BU_LP2,
       CF_CR_PH, CF_CR_HELD, CF_CR_HPH, CF_CR_D, CF_CR_DT, CF_CR_T,
       CF_WS_PH, CF_WS_LP,
       CF_ST_HP, CF_ST_LP, CF_ST_LP2, CF_ST_G0,              /* + 2 glints x (phase, inc, time, length, level, wait) */
       CF_ST_FZ = CF_ST_G0 + 12, CF_ST_FC, CF_ST_FL,         /* the glints' fuzz: held sample, count, softening */
       CF_N };
enum { CI_GL_CNT, CI_GL_SEG, CI_GL_MODE, CI_FL_CNT };

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

static int white_step(int k) {                    /* FM-1 key -> step 0..15 on the white keys, -1 for black */
    static const signed char W[12] = { 0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6 };
    if (k < 0 || k >= (int)FM1_NOTE_KEYS) return -1;
    int w = W[k % 12];
    return w < 0 ? -1 : 7 * (k / 12) + w;
}
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
        uint32_t b = VOICE_BTN_MAP[v];
        int lv = voice_lit(v) ? FM1_LED_ON : FM1_LED_OFF;
        for (int k = 0; k < 2; k++)                   /* a playing beat's light pulses on each beat */
            if (g_bank == BANK_RHYTHM && (int)v == g_beat[k] &&
                (g_beat_step[k] < 0 || (g_beat_step[k] % BEATS[v].spb) != 0)) lv = FM1_LED_MID;
        if (g_view == VIEW_SEQ) {                /* PLAY and REC show the sequencer */
            if (b == FM1_BTN_PLAY) lv = g_seq_play ? FM1_LED_ON : FM1_LED_OFF;
            if (b == FM1_BTN_REC) lv = g_seq_rec ? FM1_LED_ON : FM1_LED_OFF;
            if (b == FM1_BTN_SEQ) lv = FM1_LED_ON;
        } else if (g_view == VIEW_GLOBAL) {
            if (b == FM1_BTN_GLO) lv = FM1_LED_ON;
        } else if (g_view == VIEW_DSEQ) {         /* ... and on the drum sequencer, it */
            if (b == FM1_BTN_PLAY) lv = g_dseq_play ? FM1_LED_ON : FM1_LED_OFF;
            if (b == FM1_BTN_REC) lv = g_dseq_rec ? FM1_LED_ON : FM1_LED_OFF;
            if (b == FM1_BTN_SEQ) lv = FM1_LED_ON;
        }
        set_led_if_changed(b, lv);
    }

    /* PLAY's red LED: the sequencer recording */
    set_led_if_changed(FM1_LED_PLAY_RED, ((g_seq_rec && g_view == VIEW_SEQ) || (g_dseq_rec && g_view == VIEW_DSEQ)) ? FM1_LED_ON : FM1_LED_OFF);

    uint32_t notes = fm1_in.notes;
    for (uint32_t n = 0u; n < FM1_NOTE_KEYS; n++) {
        uint32_t key_id = FM1_KEY_NOTE0 + n;
        int is_down = ((notes >> n) & 1u) != 0u;
        int lv = is_down ? FM1_LED_ON : FM1_LED_OFF;
        if (g_view == VIEW_DSEQ) {                    /* the drum sequencer: the white keys are the steps */
            int st = white_step((int)n);
            if (st >= 0 && st < g_dseq_len) {
                int dd = (g_dseq_cur >= 0 && g_dseq_cur < NUM_DRUMS) ? g_dseq_cur : 0;
                if (lv == FM1_LED_OFF && (st & 3) == 0) lv = FM1_LED_MID;       /* steps 1, 5, 9, 13: half lit */
                if (!g_dseq_rec && ((g_dseq[st] >> dd) & 1u)) lv = FM1_LED_ON;  /* a step that's set: full */
                if (g_dseq_play && st == g_dseq_step && !is_down)               /* the playhead flips it: */
                    lv = (lv == FM1_LED_ON) ? FM1_LED_OFF : FM1_LED_ON;          /* a set step blinks dark, others light */
            }
        }
        set_led_if_changed(key_id, lv);
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

/* The battery, at the top right of the home screen (needs FM1 Studio's
 * newer fm1.h with fm1_battery(); without it, the star stays there) */
#ifdef FM1_BATTERY_FULL
static int g_batt = -2, g_prev_batt = -3;
static void draw_battery_icon(uint32_t x, uint32_t y, int level) {
    uint16_t frame = (level == FM1_BATTERY_USB) ? 0x07FFu : (level == 0 ? 0xF800u : (level < 0 ? COL_DIM : COL_TEXT));
    fm1_lcd_fill_rect(x, y, 16u, 8u, frame);                    /* the case */
    fm1_lcd_fill_rect(x + 1u, y + 1u, 14u, 6u, COL_STAGE_BG);
    fm1_lcd_fill_rect(x + 16u, y + 2u, 2u, 4u, frame);           /* its tip */
    if (level == FM1_BATTERY_USB) {                             /* on USB power: a cyan bolt */
        fm1_lcd_fill_rect(x + 6u, y + 2u, 3u, 2u, 0x07FFu);
        fm1_lcd_fill_rect(x + 8u, y + 4u, 3u, 2u, 0x07FFu);
        fm1_lcd_fill_rect(x + 4u, y + 3u, 3u, 1u, 0x07FFu);
        fm1_lcd_fill_rect(x + 9u, y + 4u, 3u, 1u, 0x07FFu);
    } else if (level > 0 && level <= FM1_BATTERY_FULL) {        /* bars: green, amber when low, red when nearly out */
        uint16_t bar = level >= 2 ? COL_LEAF_HI : (level == 1 ? 0xFD20u : 0xF800u);
        uint32_t bw = 12u / (uint32_t)FM1_BATTERY_FULL;
        for (int i = 0; i < level; i++) fm1_lcd_fill_rect(x + 2u + (uint32_t)i * bw, y + 2u, bw - 1u, 4u, bar);
    }
}
#endif

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
    draw_text_blit(30u, 194u, "Lowrey FM V 1.1", COL_GOLD_HI, COL_BG_DEF, 2u);
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
    uint16_t border = is_on ? (g_bank == BANK_POCKET ? PP_COLORS[v] : g_bank == BANK_RHYTHM ? VOICE_COLORS_D[v] : g_bank == 4 ? VOICE_COLORS_E[v] : (g_bank == 2 ? VOICE_COLORS_C[v] : (g_bank ? VOICE_COLORS_B[v] : VOICE_COLORS[v]))) : COL_BORDER;

    fm1_lcd_fill_rect(x, y, W, H, border);
    if (is_on) {
        fm1_lcd_fill_rect(x + 2u, y + 2u, W - 4u, H - 4u, COL_GOLD_HI);
        fm1_lcd_fill_rect(x + 3u, y + 3u, W - 6u, H - 6u, card_bg);
    } else {
        fm1_lcd_fill_rect(x + 2u, y + 2u, W - 4u, H - 4u, card_bg);
    }

    char short_lbl[8];
    static const char *PP_WAVE_NAMES[4] = { "SINE", "SQUARE", "TRI", "SAW" };
    const char *src = g_bank == BANK_POCKET ? (v < PP_MODES ? PP_NAMES[v] : (pp_is_fm(g_pp_mode) ? (g_pp_wave[g_pp_mode] ? "OCT UP" : "OCT") : PP_WAVE_NAMES[g_pp_wave[g_pp_mode] & 3u])) :
                      g_bank == BANK_RHYTHM ? VOICE_NAMES_D[v] : g_bank == 4 ? VOICE_NAMES_E[v] : (g_bank == 2 ? VOICE_NAMES_C[v] : (g_bank ? VOICE_NAMES_B[v] : VOICE_NAMES[v]));
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

/* the LOWREY eye made bloodshot: red veins, sore red lids, a hazel iris */
static void eye_bloodshot(int is_on) {
    if (!is_on) return;
    for (int i = 0; i < IC_W * IC_H; i++) {
        if (g_blit_buf[i] == COL_WORM) g_blit_buf[i] = 0xEB0Cu;        /* sore red lids */
        else if (g_blit_buf[i] == COL_BUBBLE_SH) {                     /* a hazel iris: green, brown inside */
            int dx = i % IC_W - 21, dy = i / IC_W - 20, d2 = dx * dx + dy * dy;
            if (d2 <= 25) g_blit_buf[i] = (d2 > 12) ? 0x4C66u : 0x8A63u;  /* (not the tear's trail) */
        }
    }
    static const int8_t VX[22] = { 8, 9, 10, 11, 12, 13, 9, 10, 11, 34, 33, 32, 31, 30, 29, 33, 32, 31, 30, 13, 29, 12 };
    static const int8_t VY[22] = { 20, 20, 21, 21, 20, 21, 18, 19, 19, 20, 21, 21, 20, 20, 21, 18, 19, 18, 19, 23, 23, 17 };
    for (int k = 0; k < 22; k++) {                               /* red veins toward the iris */
        int idx = VY[k] * IC_W + VX[k];
        if (g_blit_buf[idx] == COL_TEXT) g_blit_buf[idx] = 0xD0E4u;   /* only on the white */
    }
    for (int x = 9; x <= 33; x++) {                              /* a red rim along the lower lid */
        int y = 20 + 9 - ((x - 21) * (x - 21)) / 18;
        if (y > 20 && y < IC_H && g_blit_buf[y * IC_W + x] == 0xEB0Cu) g_blit_buf[y * IC_W + x] = 0xC0A3u;
    }
}

static void draw_bank_b_icon(uint32_t v, int is_on, int step, uint16_t card_bg) {
    const uint16_t muted = 0x8410u;
    const uint16_t dark = is_on ? COL_INK : COL_BORDER;
    #define ON(c) (is_on ? (c) : muted)

    if (v == VB_LOWREY) {                       /* a bloodshot eye that blinks */
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
        eye_bloodshot(is_on);
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
        int m = is_on ? g_pk_morph : 0;                          /* 0 = sun ... 8 = moon (a sun when switched off) */
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
/* ---- bank C icons: the same little world, a bit more broken ---- */
static int g_shatter = 0;          /* GLASS: 0 = whole ... 6 = in pieces (shatters while notes play) */
static uint32_t g_shatter_ms = 0u;
static int g_crushm = 0;           /* CRUSH: 0 = a fresh can ... 6 = flat */
static uint32_t g_crush_ms = 0u;
static int g_ray = 0;              /* STEPS (white horse): 0 = standing ... 6 = full gallop */
static uint32_t g_ray_ms = 0u;
static int g_fzg = 0;              /* FUZZ: 0 = a happy face ... 6 = fully glitched out */
static uint32_t g_fzg_ms = 0u;
static int g_fence = 0;            /* bank C LOWREY (chain-link fence): 0 = a small cut ... 6 = split open */
static uint32_t g_fence_ms = 0u;
static int g_tape = 0;             /* FLICKER (lantern): 0 = dark ... 6 = blazing */
static uint32_t g_tape_ms = 0u;
static int g_moon = 0;             /* HUSH: 0 = behind the clouds ... 6 = clouds parted, glowing */
static uint32_t g_moon_ms = 0u;

/* Four of TastyWaves' factory wavetables (marksweiss/tastywaves, GPL-3.0; made by its
 * tools/gen_wavetables.py, no third-party data): SAWSQ and FOLD. Every other
 * frame of 32 (16 kept). Per frame: 6 band-limited levels, level L = 256 >> L samples
 * plus a guard sample, at WTE_LEVEL_OFF[L]; level L keeps harmonics 1 .. 64 >> L. */
static const int16_t WTE[2][16][510] = {
    { /* SAWSQ */
      {
        0,22050,29490,25237,22097,24244,25837,23966,22501,23615,24384,23099,22128,22863,23301,22283,21550,22087,22345,21480,20887,21302,21448,20682,
        20182,20514,20582,19886,19453,19725,19735,19092,18709,18934,18901,18298,17954,18143,18075,17505,17193,17352,17255,16712,16426,16561,16439,15919,
        15655,15769,15627,15127,14882,14977,14817,14334,14106,14185,14010,13542,13328,13393,13204,12749,12549,12601,12399,11957,11769,11809,11596,11164,
        10987,11017,10793,10372,10205,10225,9991,9580,9422,9433,9190,8787,8639,8641,8389,7995,7855,7849,7589,7203,7070,7057,6790,6411,
        6285,6264,5990,5618,5500,5472,5191,4826,4715,4680,4392,4034,3929,3888,3593,3242,3144,3096,2795,2450,2358,2304,1996,1657,
        1572,1511,1198,865,786,719,399,73,0,-73,-399,-719,-786,-865,-1198,-1511,-1572,-1657,-1996,-2304,-2358,-2450,-2795,-3096,
        -3144,-3242,-3593,-3888,-3929,-4034,-4392,-4680,-4715,-4826,-5191,-5472,-5500,-5618,-5990,-6264,-6285,-6411,-6790,-7057,-7070,-7203,-7589,-7849,
        -7855,-7995,-8389,-8641,-8639,-8787,-9190,-9433,-9422,-9580,-9991,-10225,-10205,-10372,-10793,-11017,-10987,-11164,-11596,-11809,-11769,-11957,-12399,-12601,
        -12549,-12749,-13204,-13393,-13328,-13542,-14010,-14185,-14106,-14334,-14817,-14977,-14882,-15127,-15627,-15769,-15655,-15919,-16439,-16561,-16426,-16712,-17255,-17352,
        -17193,-17505,-18075,-18143,-17954,-18298,-18901,-18934,-18709,-19092,-19735,-19725,-19453,-19886,-20582,-20514,-20182,-20682,-21448,-21302,-20887,-21480,-22345,-22087,
        -21550,-22283,-23301,-22863,-22128,-23099,-24384,-23615,-22501,-23966,-25837,-24244,-22097,-25237,-29490,-22050,0,0,21977,29091,24518,21311,23379,24639,
        22454,20929,21958,22388,20795,19770,20414,20507,19187,18406,18845,18752,17592,16958,17268,17056,16002,15467,15688,15391,14414,13953,14106,13745,
        12827,12423,12524,12111,11242,10884,10940,10485,9656,9338,9357,8865,8071,7787,7773,7249,6486,6233,6189,5636,4901,4677,4605,4024,
        3317,3119,3021,2414,1732,1560,1436,805,148,0,-148,-805,-1436,-1560,-1732,-2414,-3021,-3119,-3317,-4024,-4605,-4677,-4901,-5636,
        -6189,-6233,-6486,-7249,-7773,-7787,-8071,-8865,-9357,-9338,-9656,-10485,-10940,-10884,-11242,-12111,-12524,-12423,-12827,-13745,-14106,-13953,-14414,-15391,
        -15688,-15467,-16002,-17056,-17268,-16958,-17592,-18752,-18845,-18406,-19187,-20507,-20414,-19770,-20795,-22388,-21958,-20929,-22454,-24639,-23379,-21311,-24518,-29091,
        -21977,0,0,21829,28287,23081,19751,21647,22225,19434,17810,18641,18364,16190,15094,15512,14871,12998,12173,12359,11503,9819,9170,9197,
        8191,6645,6129,6032,4906,3473,3069,2865,1634,304,0,-304,-1634,-2865,-3069,-3473,-4906,-6032,-6129,-6645,-8191,-9197,-9170,-9819,
        -11503,-12359,-12173,-12998,-14871,-15512,-15094,-16190,-18364,-18641,-17810,-19434,-22225,-21647,-19751,-23081,-28287,-21829,0,0,21525,26653,20216,16682,
        18173,17319,13402,11681,11996,10173,6993,5923,5694,3368,639,0,-639,-3368,-5694,-5923,-6993,-10173,-11996,-11681,-13402,-17319,-18173,-16682,
        -20216,-26653,-21525,0,0,20886,23285,14523,10759,11180,7146,1405,0,-1405,-7146,-11180,-10759,-14523,-23285,-20886,0,0,19481,16138,
        3342,0,-3342,-16138,-19481,0,
      },
      {
        0,21341,28552,24446,21409,23490,25042,23242,21826,22907,23662,22428,21491,22205,22640,21664,20956,21479,21741,20912,20340,20745,20898,20166,
        19683,20008,20086,19421,19003,19270,19292,18678,18308,18530,18510,17935,17603,17790,17736,17193,16891,17050,16969,16452,16175,16310,16205,15710,
        15454,15569,15445,14968,14731,14829,14687,14227,14005,14088,13932,13486,13278,13347,13178,12744,12549,12606,12425,12003,11819,11865,11673,11262,
        11088,11124,10923,10521,10356,10383,10173,9779,9623,9642,9424,9038,8890,8901,8675,8297,8156,8160,7928,7556,7421,7419,7180,6815,
        6686,6678,6433,6073,5950,5937,5687,5332,5214,5196,4942,4591,4476,4455,4198,3850,3737,3715,3456,3108,2996,2974,2718,2366,
        2247,2236,1992,1619,1473,1510,1338,782,0,-782,-1338,-1510,-1473,-1619,-1992,-2236,-2247,-2366,-2718,-2974,-2996,-3108,-3456,-3715,
        -3737,-3850,-4198,-4455,-4476,-4591,-4942,-5196,-5214,-5332,-5687,-5937,-5950,-6073,-6433,-6678,-6686,-6815,-7180,-7419,-7421,-7556,-7928,-8160,
        -8156,-8297,-8675,-8901,-8890,-9038,-9424,-9642,-9623,-9779,-10173,-10383,-10356,-10521,-10923,-11124,-11088,-11262,-11673,-11865,-11819,-12003,-12425,-12606,
        -12549,-12744,-13178,-13347,-13278,-13486,-13932,-14088,-14005,-14227,-14687,-14829,-14731,-14968,-15445,-15569,-15454,-15710,-16205,-16310,-16175,-16452,-16969,-17050,
        -16891,-17193,-17736,-17790,-17603,-17935,-18510,-18530,-18308,-18678,-19292,-19270,-19003,-19421,-20086,-20008,-19683,-20166,-20898,-20745,-20340,-20912,-21741,-21479,
        -20956,-21664,-22640,-22205,-21491,-22428,-23662,-22907,-21826,-23242,-25042,-23490,-21409,-24446,-28552,-21341,0,0,21273,28179,23773,20674,22681,23922,
        21828,20354,21356,21796,20273,19284,19913,20027,18767,18013,18446,18381,17275,16662,16972,16792,15787,15269,15494,15233,14302,13854,14014,13693,
        12818,12423,12533,12164,11334,10983,11053,10644,9851,9536,9571,9129,8368,8083,8090,7620,6885,6626,6608,6115,5402,5164,5127,4616,
        3918,3693,3648,3131,2431,2197,2181,1717,852,0,-852,-1717,-2181,-2197,-2431,-3131,-3648,-3693,-3918,-4616,-5127,-5164,-5402,-6115,
        -6608,-6626,-6885,-7620,-8090,-8083,-8368,-9129,-9571,-9536,-9851,-10644,-11053,-10983,-11334,-12164,-12533,-12423,-12818,-13693,-14014,-13854,-14302,-15233,
        -15494,-15269,-15787,-16792,-16972,-16662,-17275,-18381,-18446,-18013,-18767,-20027,-19913,-19284,-20273,-21796,-21356,-20354,-21828,-23922,-22681,-20674,-23773,-28179,
        -21273,0,0,21135,27427,22429,19213,21061,21666,19001,17434,18254,18036,15965,14903,15328,14762,12977,12173,12379,11612,10002,9361,9423,
        8519,7032,6506,6464,5465,4060,3607,3517,2494,998,0,-998,-2494,-3517,-3607,-4060,-5465,-6464,-6506,-7032,-8519,-9423,-9361,-10002,
        -11612,-12379,-12173,-12977,-14762,-15328,-14903,-15965,-18036,-18254,-17434,-19001,-21666,-21061,-19213,-22429,-27427,-21135,0,0,20851,25901,19748,16335,
        17813,17089,13356,11681,12042,10404,7354,6271,6162,4119,1313,0,-1313,-4119,-6162,-6271,-7354,-10404,-12042,-11681,-13356,-17089,-17813,-16335,
        -19748,-25901,-20851,0,0,20258,22764,14415,10759,11288,7667,2034,0,-2034,-7667,-11288,-10759,-14415,-22764,-20258,0,0,18960,16138,
        3863,0,-3863,-16138,-18960,0,
      },
      {
        0,20632,27613,23655,20722,22736,24247,22517,21151,22198,22940,21757,20853,21546,21978,21045,20362,20871,21136,20345,19793,20188,20348,19649,
        19184,19502,19589,18956,18553,18815,18849,18264,17907,18126,18119,17573,17252,17438,17398,16882,16590,16748,16683,16191,15923,16059,15971,15501,
        15253,15370,15263,14810,14580,14680,14557,14120,13905,13990,13854,13430,13228,13301,13152,12740,12549,12611,12451,12049,11869,11921,11751,11359,
        11188,11231,11053,10669,10507,10541,10355,9979,9824,9851,9658,9289,9141,9162,8961,8599,8457,8472,8266,7909,7772,7782,7571,7219,
        7087,7092,6877,6529,6400,6402,6184,5838,5713,5712,5492,5148,5023,5023,4803,4458,4331,4334,4118,3767,3633,3645,3440,3074,
        2922,2960,2787,2373,2161,2301,2276,1491,0,-1491,-2276,-2301,-2161,-2373,-2787,-2960,-2922,-3074,-3440,-3645,-3633,-3767,-4118,-4334,
        -4331,-4458,-4803,-5023,-5023,-5148,-5492,-5712,-5713,-5838,-6184,-6402,-6400,-6529,-6877,-7092,-7087,-7219,-7571,-7782,-7772,-7909,-8266,-8472,
        -8457,-8599,-8961,-9162,-9141,-9289,-9658,-9851,-9824,-9979,-10355,-10541,-10507,-10669,-11053,-11231,-11188,-11359,-11751,-11921,-11869,-12049,-12451,-12611,
        -12549,-12740,-13152,-13301,-13228,-13430,-13854,-13990,-13905,-14120,-14557,-14680,-14580,-14810,-15263,-15370,-15253,-15501,-15971,-16059,-15923,-16191,-16683,-16748,
        -16590,-16882,-17398,-17438,-17252,-17573,-18119,-18126,-17907,-18264,-18849,-18815,-18553,-18956,-19589,-19502,-19184,-19649,-20348,-20188,-19793,-20345,-21136,-20871,
        -20362,-21045,-21978,-21546,-20853,-21757,-22940,-22198,-21151,-22517,-24247,-22736,-20722,-23655,-27613,-20632,0,0,20568,27266,23028,20036,21983,23205,
        21201,19780,20755,21203,19751,18797,19413,19547,18348,17621,18048,18010,16958,16366,16675,16528,15573,15072,15299,15075,14190,13755,13922,13640,
        12808,12423,12543,12217,11426,11082,11165,10802,10045,9733,9786,9394,8664,8379,8407,7991,7284,7019,7028,6595,5902,5651,5650,5209,
        4519,4268,4275,3848,3129,2834,2926,2630,1556,0,-1556,-2630,-2926,-2834,-3129,-3848,-4275,-4268,-4519,-5209,-5650,-5651,-5902,-6595,
        -7028,-7019,-7284,-7991,-8407,-8379,-8664,-9394,-9786,-9733,-10045,-10802,-11165,-11082,-11426,-12217,-12543,-12423,-12808,-13640,-13922,-13755,-14190,-15075,
        -15299,-15072,-15573,-16528,-16675,-16366,-16958,-18010,-18048,-17621,-18348,-19547,-19413,-18797,-19751,-21203,-20755,-19780,-21201,-23205,-21983,-20036,-23028,-27266,
        -20568,0,0,20440,26567,21777,18675,20474,21108,18569,17057,17867,17708,15739,14712,15145,14654,12956,12173,12400,11721,10186,9552,9648,
        8847,7419,6883,6897,6023,4646,4145,4169,3353,1692,0,-1692,-3353,-4169,-4145,-4646,-6023,-6897,-6883,-7419,-8847,-9648,-9552,-10186,
        -11721,-12400,-12173,-12956,-14654,-15145,-14712,-15739,-17708,-17867,-17057,-18569,-21108,-20474,-18675,-21777,-26567,-20440,0,0,20178,25150,19279,15988,
        17452,16858,13311,11681,12087,10634,7715,6618,6631,4870,1986,0,-1986,-4870,-6631,-6618,-7715,-10634,-12087,-11681,-13311,-16858,-17452,-15988,
        -19279,-25150,-20178,0,0,19630,22244,14307,10759,11396,8187,2662,0,-2662,-8187,-11396,-10759,-14307,-22244,-19630,0,0,18440,16138,
        4384,0,-4384,-16138,-18440,0,
      },
      {
        0,19923,26675,22864,20034,21982,23452,21793,20476,21490,22217,21087,20215,20888,21317,20426,19768,20263,20531,19777,19246,19631,19797,19133,
        18685,18996,19093,18491,18103,18360,18405,17850,17506,17722,17729,17210,16901,17085,17060,16570,16289,16447,16397,15931,15672,15808,15738,15291,
        15052,15170,15081,14652,14429,14531,14428,14013,13804,13893,13776,13374,13177,13254,13126,12735,12549,12616,12477,12096,11920,11977,11829,11457,
        11289,11338,11182,10818,10658,10699,10537,10179,10025,10061,9892,9540,9392,9422,9247,8901,8758,8783,8604,8262,8123,8144,7962,7623,
        7488,7506,7320,6984,6851,6867,6680,6344,6212,6229,6043,5705,5570,5590,5408,5065,4925,4953,4779,4425,4271,4316,4163,3782,
        3597,3684,3582,3128,2848,3092,3214,2200,0,-2200,-3214,-3092,-2848,-3128,-3582,-3684,-3597,-3782,-4163,-4316,-4271,-4425,-4779,-4953,
        -4925,-5065,-5408,-5590,-5570,-5705,-6043,-6229,-6212,-6344,-6680,-6867,-6851,-6984,-7320,-7506,-7488,-7623,-7962,-8144,-8123,-8262,-8604,-8783,
        -8758,-8901,-9247,-9422,-9392,-9540,-9892,-10061,-10025,-10179,-10537,-10699,-10658,-10818,-11182,-11338,-11289,-11457,-11829,-11977,-11920,-12096,-12477,-12616,
        -12549,-12735,-13126,-13254,-13177,-13374,-13776,-13893,-13804,-14013,-14428,-14531,-14429,-14652,-15081,-15170,-15052,-15291,-15738,-15808,-15672,-15931,-16397,-16447,
        -16289,-16570,-17060,-17085,-16901,-17210,-17729,-17722,-17506,-17850,-18405,-18360,-18103,-18491,-19093,-18996,-18685,-19133,-19797,-19631,-19246,-19777,-20531,-20263,
        -19768,-20426,-21317,-20888,-20215,-21087,-22217,-21490,-20476,-21793,-23452,-21982,-20034,-22864,-26675,-19923,0,0,19864,26354,22284,19399,21284,22488,
        20574,19205,20154,20611,19229,18310,18912,19068,17929,17228,17649,17639,16641,16070,16378,16263,15359,14874,15104,14916,14078,13656,13829,13587,
        12798,12423,12553,12269,11519,11181,11277,10960,10240,9931,10000,9658,8961,8675,8723,8362,7682,7411,7447,7075,6403,6137,6172,5801,
        5121,4842,4901,4565,3827,3471,3670,3542,2260,0,-2260,-3542,-3670,-3471,-3827,-4565,-4901,-4842,-5121,-5801,-6172,-6137,-6403,-7075,
        -7447,-7411,-7682,-8362,-8723,-8675,-8961,-9658,-10000,-9931,-10240,-10960,-11277,-11181,-11519,-12269,-12553,-12423,-12798,-13587,-13829,-13656,-14078,-14916,
        -15104,-14874,-15359,-16263,-16378,-16070,-16641,-17639,-17649,-17228,-17929,-19068,-18912,-18310,-19229,-20611,-20154,-19205,-20574,-22488,-21284,-19399,-22284,-26354,
        -19864,0,0,19746,25707,21125,18137,19888,20549,18137,16680,17480,17379,15514,14521,14961,14545,12936,12173,12421,11829,10370,9744,9874,
        9175,7806,7260,7329,6582,5232,4683,4821,4213,2387,0,-2387,-4213,-4821,-4683,-5232,-6582,-7329,-7260,-7806,-9175,-9874,-9744,-10370,
        -11829,-12421,-12173,-12936,-14545,-14961,-14521,-15514,-17379,-17480,-16680,-18137,-20549,-19888,-18137,-21125,-25707,-19746,0,0,19504,24399,18811,15641,
        17091,16628,13266,11681,12132,10865,8075,6965,7099,5621,2660,0,-2660,-5621,-7099,-6965,-8075,-10865,-12132,-11681,-13266,-16628,-17091,-15641,
        -18811,-24399,-19504,0,0,19001,21723,14199,10759,11504,8708,3291,0,-3291,-8708,-11504,-10759,-14199,-21723,-19001,0,0,17919,16138,
        4904,0,-4904,-16138,-17919,0,
      },
      {
        0,19214,25737,22073,19347,21228,22657,21069,19800,20782,21495,20416,19577,20229,20655,19807,19175,19655,19926,19210,18699,19074,19247,18617,
        18186,18490,18596,18026,17653,17905,17962,17437,17106,17318,17338,16848,16550,16732,16722,16259,15988,16145,16111,15671,15421,15558,15504,15082,
        14851,14970,14900,14494,14278,14383,14298,13906,13704,13795,13698,13318,13127,13208,13100,12730,12549,12620,12503,12142,11970,12033,11907,11554,
        11390,11445,11312,10966,10808,10858,10718,10378,10226,10270,10125,9790,9643,9682,9533,9203,9059,9095,8942,8615,8475,8507,8352,8027,
        7888,7920,7764,7439,7301,7332,7177,6850,6711,6745,6593,6262,6117,6158,6013,5673,5519,5572,5441,5084,4909,4987,4885,4491,
        4272,4409,4377,3882,3536,3883,4153,2909,0,-2909,-4153,-3883,-3536,-3882,-4377,-4409,-4272,-4491,-4885,-4987,-4909,-5084,-5441,-5572,
        -5519,-5673,-6013,-6158,-6117,-6262,-6593,-6745,-6711,-6850,-7177,-7332,-7301,-7439,-7764,-7920,-7888,-8027,-8352,-8507,-8475,-8615,-8942,-9095,
        -9059,-9203,-9533,-9682,-9643,-9790,-10125,-10270,-10226,-10378,-10718,-10858,-10808,-10966,-11312,-11445,-11390,-11554,-11907,-12033,-11970,-12142,-12503,-12620,
        -12549,-12730,-13100,-13208,-13127,-13318,-13698,-13795,-13704,-13906,-14298,-14383,-14278,-14494,-14900,-14970,-14851,-15082,-15504,-15558,-15421,-15671,-16111,-16145,
        -15988,-16259,-16722,-16732,-16550,-16848,-17338,-17318,-17106,-17437,-17962,-17905,-17653,-18026,-18596,-18490,-18186,-18617,-19247,-19074,-18699,-19210,-19926,-19655,
        -19175,-19807,-20655,-20229,-19577,-20416,-21495,-20782,-19800,-21069,-22657,-21228,-19347,-22073,-25737,-19214,0,0,19160,25441,21539,18762,20586,21771,
        19947,18631,19552,20018,18706,17823,18412,18588,17510,16835,17250,17268,16325,15774,16081,15999,15144,14676,14910,14758,13966,13557,13737,13534,
        12788,12423,12563,12322,11611,11280,11389,11118,10434,10129,10214,9922,9258,8971,9040,8733,8081,7804,7866,7554,6903,6624,6694,6394,
        5722,5417,5528,5282,4525,4108,4415,4454,2965,0,-2965,-4454,-4415,-4108,-4525,-5282,-5528,-5417,-5722,-6394,-6694,-6624,-6903,-7554,
        -7866,-7804,-8081,-8733,-9040,-8971,-9258,-9922,-10214,-10129,-10434,-11118,-11389,-11280,-11611,-12322,-12563,-12423,-12788,-13534,-13737,-13557,-13966,-14758,
        -14910,-14676,-15144,-15999,-16081,-15774,-16325,-17268,-17250,-16835,-17510,-18588,-18412,-17823,-18706,-20018,-19552,-18631,-19947,-21771,-20586,-18762,-21539,-25441,
        -19160,0,0,19051,24847,20472,17599,19302,19990,17704,16303,17093,17051,15288,14329,14777,14437,12915,12173,12441,11938,10553,9935,10099,
        9504,8193,7636,7761,7141,5818,5221,5473,5073,3081,0,-3081,-5073,-5473,-5221,-5818,-7141,-7761,-7636,-8193,-9504,-10099,-9935,-10553,
        -11938,-12441,-12173,-12915,-14437,-14777,-14329,-15288,-17051,-17093,-16303,-17704,-19990,-19302,-17599,-20472,-24847,-19051,0,0,18830,23648,18342,15294,
        16731,16397,13220,11681,12178,11095,8436,7312,7568,6372,3334,0,-3334,-6372,-7568,-7312,-8436,-11095,-12178,-11681,-13220,-16397,-16731,-15294,
        -18342,-23648,-18830,0,0,18373,21202,14091,10759,11611,9229,3919,0,-3919,-9229,-11611,-10759,-14091,-21202,-18373,0,0,17398,16138,
        5425,0,-5425,-16138,-17398,0,
      },
      {
        0,18505,24798,21282,18659,20473,21863,20344,19125,20073,20773,19745,18940,19571,19994,19188,18581,19047,19321,18642,18152,18517,18697,18101,
        17687,17984,18100,17561,17202,17450,17518,17023,16705,16914,16947,16485,16199,16379,16384,15947,15686,15843,15825,15410,15170,15307,15270,14873,
        14650,14771,14718,14336,14127,14234,14168,13799,13603,13698,13620,13262,13077,13162,13074,12725,12549,12625,12529,12188,12020,12089,11985,11652,
        11490,11552,11442,11115,10959,11016,10900,10578,10427,10479,10359,10041,9895,9943,9819,9504,9361,9406,9281,8968,8826,8870,8743,8431,
        8289,8333,8207,7894,7751,7797,7673,7357,7210,7261,7143,6819,6664,6725,6618,6281,6112,6190,6102,5742,5547,5658,5607,5199,
        4948,5133,5172,4636,4223,4674,5091,3618,0,-3618,-5091,-4674,-4223,-4636,-5172,-5133,-4948,-5199,-5607,-5658,-5547,-5742,-6102,-6190,
        -6112,-6281,-6618,-6725,-6664,-6819,-7143,-7261,-7210,-7357,-7673,-7797,-7751,-7894,-8207,-8333,-8289,-8431,-8743,-8870,-8826,-8968,-9281,-9406,
        -9361,-9504,-9819,-9943,-9895,-10041,-10359,-10479,-10427,-10578,-10900,-11016,-10959,-11115,-11442,-11552,-11490,-11652,-11985,-12089,-12020,-12188,-12529,-12625,
        -12549,-12725,-13074,-13162,-13077,-13262,-13620,-13698,-13603,-13799,-14168,-14234,-14127,-14336,-14718,-14771,-14650,-14873,-15270,-15307,-15170,-15410,-15825,-15843,
        -15686,-15947,-16384,-16379,-16199,-16485,-16947,-16914,-16705,-17023,-17518,-17450,-17202,-17561,-18100,-17984,-17687,-18101,-18697,-18517,-18152,-18642,-19321,-19047,
        -18581,-19188,-19994,-19571,-18940,-19745,-20773,-20073,-19125,-20344,-21863,-20473,-18659,-21282,-24798,-18505,0,0,18456,24529,20795,18125,19888,21054,
        19320,18056,18951,19426,18184,17336,17912,18108,17090,16443,16852,16897,16008,15478,15785,15735,14930,14479,14715,14600,13854,13458,13644,13482,
        12778,12423,12573,12375,11704,11379,11501,11277,10629,10327,10429,10186,9554,9266,9357,9104,8480,8197,8285,8034,7403,7111,7216,6986,
        6323,5991,6155,5999,5224,4745,5159,5367,3669,0,-3669,-5367,-5159,-4745,-5224,-5999,-6155,-5991,-6323,-6986,-7216,-7111,-7403,-8034,
        -8285,-8197,-8480,-9104,-9357,-9266,-9554,-10186,-10429,-10327,-10629,-11277,-11501,-11379,-11704,-12375,-12573,-12423,-12778,-13482,-13644,-13458,-13854,-14600,
        -14715,-14479,-14930,-15735,-15785,-15478,-16008,-16897,-16852,-16443,-17090,-18108,-17912,-17336,-18184,-19426,-18951,-18056,-19320,-21054,-19888,-18125,-20795,-24529,
        -18456,0,0,18357,23988,19820,17060,18716,19432,17272,15926,16706,16723,15062,14138,14594,14328,12895,12173,12462,12046,10737,10126,10325,
        9832,8580,8013,8194,7699,6405,5759,6126,5933,3775,0,-3775,-5933,-6126,-5759,-6405,-7699,-8194,-8013,-8580,-9832,-10325,-10126,-10737,
        -12046,-12462,-12173,-12895,-14328,-14594,-14138,-15062,-16723,-16706,-15926,-17272,-19432,-18716,-17060,-19820,-23988,-18357,0,0,18156,22897,17874,14947,
        16370,16167,13175,11681,12223,11326,8796,7659,8036,7123,4008,0,-4008,-7123,-8036,-7659,-8796,-11326,-12223,-11681,-13175,-16167,-16370,-14947,
        -17874,-22897,-18156,0,0,17744,20682,13983,10759,11719,9749,4548,0,-4548,-9749,-11719,-10759,-13983,-20682,-17744,0,0,16878,16138,
        5945,0,-5945,-16138,-16878,0,
      },
      {
        0,17796,23860,20491,17972,19719,21068,19620,18450,19365,20051,19074,18302,18912,19332,18569,17987,18439,18716,18075,17605,17960,18147,17585,
        17188,17478,17603,17096,16752,16995,17075,16609,16304,16510,16557,16122,15848,16026,16045,15636,15385,15541,15539,15150,14919,15056,15036,14664,
        14449,14571,14536,14178,13977,14086,14038,13692,13502,13601,13542,13206,13026,13115,13048,12720,12549,12630,12555,12235,12071,12144,12063,11749,
        11591,11659,11572,11263,11110,11174,11082,10778,10629,10688,10593,10292,10146,10203,10105,9806,9662,9718,9619,9320,9177,9232,9134,8835,
        8690,8747,8650,8349,8201,8262,8170,7863,7709,7777,7693,7376,7212,7293,7223,6889,6706,6809,6764,6401,6184,6329,6329,5907,
        5623,5857,5966,5390,4911,5465,6030,4327,0,-4327,-6030,-5465,-4911,-5390,-5966,-5857,-5623,-5907,-6329,-6329,-6184,-6401,-6764,-6809,
        -6706,-6889,-7223,-7293,-7212,-7376,-7693,-7777,-7709,-7863,-8170,-8262,-8201,-8349,-8650,-8747,-8690,-8835,-9134,-9232,-9177,-9320,-9619,-9718,
        -9662,-9806,-10105,-10203,-10146,-10292,-10593,-10688,-10629,-10778,-11082,-11174,-11110,-11263,-11572,-11659,-11591,-11749,-12063,-12144,-12071,-12235,-12555,-12630,
        -12549,-12720,-13048,-13115,-13026,-13206,-13542,-13601,-13502,-13692,-14038,-14086,-13977,-14178,-14536,-14571,-14449,-14664,-15036,-15056,-14919,-15150,-15539,-15541,
        -15385,-15636,-16045,-16026,-15848,-16122,-16557,-16510,-16304,-16609,-17075,-16995,-16752,-17096,-17603,-17478,-17188,-17585,-18147,-17960,-17605,-18075,-18716,-18439,
        -17987,-18569,-19332,-18912,-18302,-19074,-20051,-19365,-18450,-19620,-21068,-19719,-17972,-20491,-23860,-17796,0,0,17752,23616,20050,17488,19189,20337,
        18693,17482,18350,18834,17662,16849,17411,17628,16671,16050,16453,16526,15691,15183,15488,15471,14715,14281,14521,14442,13742,13359,13552,13429,
        12769,12423,12582,12428,11796,11478,11613,11435,10824,10524,10643,10451,9851,9562,9673,9475,8878,8589,8705,8514,7904,7598,7739,7578,
        6925,6566,6782,6716,5922,5382,5904,6279,4373,0,-4373,-6279,-5904,-5382,-5922,-6716,-6782,-6566,-6925,-7578,-7739,-7598,-7904,-8514,
        -8705,-8589,-8878,-9475,-9673,-9562,-9851,-10451,-10643,-10524,-10824,-11435,-11613,-11478,-11796,-12428,-12582,-12423,-12769,-13429,-13552,-13359,-13742,-14442,
        -14521,-14281,-14715,-15471,-15488,-15183,-15691,-16526,-16453,-16050,-16671,-17628,-17411,-16849,-17662,-18834,-18350,-17482,-18693,-20337,-19189,-17488,-20050,-23616,
        -17752,0,0,17663,23128,19168,16522,18129,18873,16840,15549,16319,16395,14837,13947,14410,14219,12874,12173,12482,12155,10921,10317,10551,
        10160,8966,8390,8626,8258,6991,6298,6778,6793,4470,0,-4470,-6793,-6778,-6298,-6991,-8258,-8626,-8390,-8966,-10160,-10551,-10317,-10921,
        -12155,-12482,-12173,-12874,-14219,-14410,-13947,-14837,-16395,-16319,-15549,-16840,-18873,-18129,-16522,-19168,-23128,-17663,0,0,17483,22146,17405,14600,
        16010,15936,13130,11681,12268,11556,9157,8006,8504,7875,4681,0,-4681,-7875,-8504,-8006,-9157,-11556,-12268,-11681,-13130,-15936,-16010,-14600,
        -17405,-22146,-17483,0,0,17116,20161,13876,10759,11827,10270,5176,0,-5176,-10270,-11827,-10759,-13876,-20161,-17116,0,0,16357,16138,
        6466,0,-6466,-16138,-16357,0,
      },
      {
        0,17087,22921,19701,17285,18965,20273,18896,17775,18657,19329,18403,17664,18254,18671,17950,17393,17831,18111,17507,17058,17403,17597,17068,
        16689,16972,17107,16631,16302,16540,16632,16195,15903,16106,16166,15760,15496,15673,15707,15324,15084,15239,15253,14889,14667,14805,14802,14455,
        14248,14371,14354,14020,13826,13937,13908,13585,13402,13503,13464,13150,12976,13069,13022,12716,12549,12635,12581,12281,12121,12200,12141,11846,
        11691,11766,11702,11412,11261,11332,11264,10977,10830,10898,10827,10543,10397,10463,10391,10108,9963,10029,9957,9673,9528,9595,9524,9239,
        9091,9161,9094,8804,8651,8727,8666,8369,8208,8293,8243,7933,7759,7860,7828,7497,7300,7428,7425,7059,6822,6999,7051,6616,
        6298,6582,6761,6144,5598,6255,6968,5035,0,-5035,-6968,-6255,-5598,-6144,-6761,-6582,-6298,-6616,-7051,-6999,-6822,-7059,-7425,-7428,
        -7300,-7497,-7828,-7860,-7759,-7933,-8243,-8293,-8208,-8369,-8666,-8727,-8651,-8804,-9094,-9161,-9091,-9239,-9524,-9595,-9528,-9673,-9957,-10029,
        -9963,-10108,-10391,-10463,-10397,-10543,-10827,-10898,-10830,-10977,-11264,-11332,-11261,-11412,-11702,-11766,-11691,-11846,-12141,-12200,-12121,-12281,-12581,-12635,
        -12549,-12716,-13022,-13069,-12976,-13150,-13464,-13503,-13402,-13585,-13908,-13937,-13826,-14020,-14354,-14371,-14248,-14455,-14802,-14805,-14667,-14889,-15253,-15239,
        -15084,-15324,-15707,-15673,-15496,-15760,-16166,-16106,-15903,-16195,-16632,-16540,-16302,-16631,-17107,-16972,-16689,-17068,-17597,-17403,-17058,-17507,-18111,-17831,
        -17393,-17950,-18671,-18254,-17664,-18403,-19329,-18657,-17775,-18896,-20273,-18965,-17285,-19701,-22921,-17087,0,0,17048,22704,19306,16851,18491,19621,
        18066,16907,17748,18241,17139,16362,16911,17149,16252,15657,16054,16155,15375,14887,15191,15206,14501,14083,14326,14283,13630,13260,13459,13376,
        12759,12423,12592,12480,11888,11577,11725,11593,11018,10722,10857,10715,10148,9858,9990,9846,9277,8982,9124,8994,8404,8085,8261,8171,
        7526,7140,7409,7433,6620,6019,6648,7192,5077,0,-5077,-7192,-6648,-6019,-6620,-7433,-7409,-7140,-7526,-8171,-8261,-8085,-8404,-8994,
        -9124,-8982,-9277,-9846,-9990,-9858,-10148,-10715,-10857,-10722,-11018,-11593,-11725,-11577,-11888,-12480,-12592,-12423,-12759,-13376,-13459,-13260,-13630,-14283,
        -14326,-14083,-14501,-15206,-15191,-14887,-15375,-16155,-16054,-15657,-16252,-17149,-16911,-16362,-17139,-18241,-17748,-16907,-18066,-19621,-18491,-16851,-19306,-22704,
        -17048,0,0,16968,22268,18516,15984,17543,18314,16407,15173,15932,16067,14611,13756,14226,14111,12853,12173,12503,12264,11104,10508,10776,
        10488,9353,8767,9058,8817,7577,6836,7430,7652,5164,0,-5164,-7652,-7430,-6836,-7577,-8817,-9058,-8767,-9353,-10488,-10776,-10508,-11104,
        -12264,-12503,-12173,-12853,-14111,-14226,-13756,-14611,-16067,-15932,-15173,-16407,-18314,-17543,-15984,-18516,-22268,-16968,0,0,16809,21395,16937,14253,
        15649,15706,13084,11681,12314,11787,9518,8353,8973,8626,5355,0,-5355,-8626,-8973,-8353,-9518,-11787,-12314,-11681,-13084,-15706,-15649,-14253,
        -16937,-21395,-16809,0,0,16487,19641,13768,10759,11935,10790,5804,0,-5804,-10790,-11935,-10759,-13768,-19641,-16487,0,0,15837,16138,
        6987,0,-6987,-16138,-15837,0,
      },
      {
        0,16378,21983,18910,16597,18211,19478,18171,17100,17949,18606,17732,17026,17595,18009,17331,16800,17224,17506,16940,16511,16846,17046,16552,
        16191,16466,16610,16166,15852,16084,16188,15782,15503,15702,15775,15397,15145,15320,15369,15013,14783,14937,14967,14629,14416,14555,14568,14245,
        14047,14172,14172,13862,13675,13789,13779,13478,13301,13406,13387,13094,12926,13023,12996,12711,12549,12639,12607,12327,12171,12256,12219,11944,
        11792,11873,11831,11560,11412,11490,11446,11177,11031,11107,11061,10793,10648,10724,10677,10410,10264,10341,10295,10026,9879,9958,9915,9643,
        9491,9575,9537,9259,9101,9192,9163,8875,8706,8810,8794,8490,8306,8428,8432,8105,7894,8047,8087,7718,7460,7670,7774,7324,
        6973,7306,7556,6898,6286,7046,7907,5744,0,-5744,-7907,-7046,-6286,-6898,-7556,-7306,-6973,-7324,-7774,-7670,-7460,-7718,-8087,-8047,
        -7894,-8105,-8432,-8428,-8306,-8490,-8794,-8810,-8706,-8875,-9163,-9192,-9101,-9259,-9537,-9575,-9491,-9643,-9915,-9958,-9879,-10026,-10295,-10341,
        -10264,-10410,-10677,-10724,-10648,-10793,-11061,-11107,-11031,-11177,-11446,-11490,-11412,-11560,-11831,-11873,-11792,-11944,-12219,-12256,-12171,-12327,-12607,-12639,
        -12549,-12711,-12996,-13023,-12926,-13094,-13387,-13406,-13301,-13478,-13779,-13789,-13675,-13862,-14172,-14172,-14047,-14245,-14568,-14555,-14416,-14629,-14967,-14937,
        -14783,-15013,-15369,-15320,-15145,-15397,-15775,-15702,-15503,-15782,-16188,-16084,-15852,-16166,-16610,-16466,-16191,-16552,-17046,-16846,-16511,-16940,-17506,-17224,
        -16800,-17331,-18009,-17595,-17026,-17732,-18606,-17949,-17100,-18171,-19478,-18211,-16597,-18910,-21983,-16378,0,0,16343,21791,18561,16214,17793,18904,
        17439,16333,17147,17649,16617,15875,16410,16669,15833,15265,15656,15784,15058,14591,14895,14942,14287,13885,14131,14125,13517,13161,13367,13324,
        12749,12423,12602,12533,11981,11676,11837,11751,11213,10920,11072,10979,10445,10154,10307,10218,9676,9375,9543,9473,8905,8572,8783,8763,
        8127,7715,8036,8149,7319,6657,7393,8104,5781,0,-5781,-8104,-7393,-6657,-7319,-8149,-8036,-7715,-8127,-8763,-8783,-8572,-8905,-9473,
        -9543,-9375,-9676,-10218,-10307,-10154,-10445,-10979,-11072,-10920,-11213,-11751,-11837,-11676,-11981,-12533,-12602,-12423,-12749,-13324,-13367,-13161,-13517,-14125,
        -14131,-13885,-14287,-14942,-14895,-14591,-15058,-15784,-15656,-15265,-15833,-16669,-16410,-15875,-16617,-17649,-17147,-16333,-17439,-18904,-17793,-16214,-18561,-21791,
        -16343,0,0,16274,21408,17864,15446,16957,17756,15975,14796,15545,15739,14386,13565,14043,14002,12833,12173,12524,12372,11288,10699,11002,
        10816,9740,9144,9490,9375,8163,7374,8082,8512,5859,0,-5859,-8512,-8082,-7374,-8163,-9375,-9490,-9144,-9740,-10816,-11002,-10699,-11288,
        -12372,-12524,-12173,-12833,-14002,-14043,-13565,-14386,-15739,-15545,-14796,-15975,-17756,-16957,-15446,-17864,-21408,-16274,0,0,16135,20644,16468,13906,
        15288,15475,13039,11681,12359,12017,9878,8700,9441,9377,6029,0,-6029,-9377,-9441,-8700,-9878,-12017,-12359,-11681,-13039,-15475,-15288,-13906,
        -16468,-20644,-16135,0,0,15859,19120,13660,10759,12043,11311,6433,0,-6433,-11311,-12043,-10759,-13660,-19120,-15859,0,0,15316,16138,
        7507,0,-7507,-16138,-15316,0,
      },
      {
        0,15669,21044,18119,15910,17457,18683,17447,16425,17240,17884,17062,16389,16937,17348,16712,16206,16616,16901,16372,15964,16289,16496,16036,
        15692,15960,16114,15701,15402,15629,15745,15368,15102,15298,15385,15034,14794,14967,15031,14701,14482,14636,14681,14369,14165,14304,14334,14036,
        13846,13972,13991,13704,13524,13640,13649,13371,13201,13308,13309,13039,12875,12976,12970,12706,12549,12644,12633,12374,12221,12312,12296,12041,
        11893,11980,11961,11709,11563,11648,11627,11377,11232,11316,11295,11044,10899,10984,10963,10712,10566,10652,10633,10379,10230,10320,10306,10047,
        9892,9989,9981,9714,9551,9657,9659,9381,9205,9326,9344,9047,8853,8995,9037,8713,8487,8666,8748,8376,8098,8341,8496,8032,
        7648,8030,8351,7653,6973,7837,8845,6453,0,-6453,-8845,-7837,-6973,-7653,-8351,-8030,-7648,-8032,-8496,-8341,-8098,-8376,-8748,-8666,
        -8487,-8713,-9037,-8995,-8853,-9047,-9344,-9326,-9205,-9381,-9659,-9657,-9551,-9714,-9981,-9989,-9892,-10047,-10306,-10320,-10230,-10379,-10633,-10652,
        -10566,-10712,-10963,-10984,-10899,-11044,-11295,-11316,-11232,-11377,-11627,-11648,-11563,-11709,-11961,-11980,-11893,-12041,-12296,-12312,-12221,-12374,-12633,-12644,
        -12549,-12706,-12970,-12976,-12875,-13039,-13309,-13308,-13201,-13371,-13649,-13640,-13524,-13704,-13991,-13972,-13846,-14036,-14334,-14304,-14165,-14369,-14681,-14636,
        -14482,-14701,-15031,-14967,-14794,-15034,-15385,-15298,-15102,-15368,-15745,-15629,-15402,-15701,-16114,-15960,-15692,-16036,-16496,-16289,-15964,-16372,-16901,-16616,
        -16206,-16712,-17348,-16937,-16389,-17062,-17884,-17240,-16425,-17447,-18683,-17457,-15910,-18119,-21044,-15669,0,0,15639,20879,17817,15576,17095,18187,
        16812,15758,16546,17057,16095,15388,15910,16189,15413,14872,15257,15413,14741,14295,14598,14678,14072,13688,13937,13967,13405,13062,13275,13271,
        12739,12423,12612,12586,12073,11775,11949,11910,11407,11117,11286,11243,10741,10450,10624,10589,10074,9767,9963,9953,9405,9059,9305,9356,
        8729,8289,8663,8866,8017,7294,8137,9017,6485,0,-6485,-9017,-8137,-7294,-8017,-8866,-8663,-8289,-8729,-9356,-9305,-9059,-9405,-9953,
        -9963,-9767,-10074,-10589,-10624,-10450,-10741,-11243,-11286,-11117,-11407,-11910,-11949,-11775,-12073,-12586,-12612,-12423,-12739,-13271,-13275,-13062,-13405,-13967,
        -13937,-13688,-14072,-14678,-14598,-14295,-14741,-15413,-15257,-14872,-15413,-16189,-15910,-15388,-16095,-17057,-16546,-15758,-16812,-18187,-17095,-15576,-17817,-20879,
        -15639,0,0,15580,20549,17212,14908,16371,17197,15543,14419,15158,15410,14160,13374,13859,13893,12812,12173,12544,12481,11472,10890,11227,
        11144,10127,9520,9923,9934,8750,7912,8734,9372,6553,0,-6553,-9372,-8734,-7912,-8750,-9934,-9923,-9520,-10127,-11144,-11227,-10890,-11472,
        -12481,-12544,-12173,-12812,-13893,-13859,-13374,-14160,-15410,-15158,-14419,-15543,-17197,-16371,-14908,-17212,-20549,-15580,0,0,15461,19892,16000,13559,
        14928,15245,12994,11681,12404,12248,10239,9047,9910,10128,6703,0,-6703,-10128,-9910,-9047,-10239,-12248,-12404,-11681,-12994,-15245,-14928,-13559,
        -16000,-19892,-15461,0,0,15231,18599,13552,10759,12151,11832,7061,0,-7061,-11832,-12151,-10759,-13552,-18599,-15231,0,0,14795,16138,
        8028,0,-8028,-16138,-14795,0,
      },
      {
        0,14960,20106,17328,15222,16703,17889,16722,15750,16532,17162,16391,15751,16278,16686,16093,15612,16008,16296,15805,15417,15732,15946,15520,
        15193,15454,15617,15237,14952,15174,15301,14954,14701,14894,14994,14672,14443,14614,14692,14390,14180,14334,14395,14108,13914,14053,14101,13827,
        13645,13772,13809,13545,13373,13492,13519,13264,13100,13211,13231,12983,12825,12930,12944,12701,12549,12649,12659,12420,12272,12368,12374,12139,
        11993,12087,12091,11858,11714,11806,11809,11576,11433,11525,11528,11295,11151,11244,11249,11014,10867,10964,10972,10732,10581,10683,10696,10451,
        10293,10402,10424,10169,10001,10122,10156,9887,9704,9842,9894,9604,9400,9563,9642,9321,9081,9285,9410,9035,8735,9012,9218,8740,
        8323,8755,9146,8407,7660,8628,9783,7162,0,-7162,-9783,-8628,-7660,-8407,-9146,-8755,-8323,-8740,-9218,-9012,-8735,-9035,-9410,-9285,
        -9081,-9321,-9642,-9563,-9400,-9604,-9894,-9842,-9704,-9887,-10156,-10122,-10001,-10169,-10424,-10402,-10293,-10451,-10696,-10683,-10581,-10732,-10972,-10964,
        -10867,-11014,-11249,-11244,-11151,-11295,-11528,-11525,-11433,-11576,-11809,-11806,-11714,-11858,-12091,-12087,-11993,-12139,-12374,-12368,-12272,-12420,-12659,-12649,
        -12549,-12701,-12944,-12930,-12825,-12983,-13231,-13211,-13100,-13264,-13519,-13492,-13373,-13545,-13809,-13772,-13645,-13827,-14101,-14053,-13914,-14108,-14395,-14334,
        -14180,-14390,-14692,-14614,-14443,-14672,-14994,-14894,-14701,-14954,-15301,-15174,-14952,-15237,-15617,-15454,-15193,-15520,-15946,-15732,-15417,-15805,-16296,-16008,
        -15612,-16093,-16686,-16278,-15751,-16391,-17162,-16532,-15750,-16722,-17889,-16703,-15222,-17328,-20106,-14960,0,0,14935,19966,17072,14939,16396,17470,
        16186,15184,15945,16464,15573,14902,15410,15710,14994,14479,14858,15042,14424,13999,14301,14414,13858,13490,13742,13809,13293,12963,13182,13218,
        12729,12423,12622,12638,12166,11874,12061,12068,11602,11315,11500,11507,11038,10745,10940,10960,10473,10160,10382,10433,9905,9546,9828,9948,
        9330,8864,9290,9583,8715,7931,8882,9929,7189,0,-7189,-9929,-8882,-7931,-8715,-9583,-9290,-8864,-9330,-9948,-9828,-9546,-9905,-10433,
        -10382,-10160,-10473,-10960,-10940,-10745,-11038,-11507,-11500,-11315,-11602,-12068,-12061,-11874,-12166,-12638,-12622,-12423,-12729,-13218,-13182,-12963,-13293,-13809,
        -13742,-13490,-13858,-14414,-14301,-13999,-14424,-15042,-14858,-14479,-14994,-15710,-15410,-14902,-15573,-16464,-15945,-15184,-16186,-17470,-16396,-14939,-17072,-19966,
        -14935,0,0,14885,19689,16560,14370,15784,16638,15111,14042,14771,15082,13934,13183,13676,13785,12792,12173,12565,12590,11655,11081,11453,
        11472,10514,9897,10355,10493,9336,8450,9386,10232,7247,0,-7247,-10232,-9386,-8450,-9336,-10493,-10355,-9897,-10514,-11472,-11453,-11081,-11655,
        -12590,-12565,-12173,-12792,-13785,-13676,-13183,-13934,-15082,-14771,-14042,-15111,-16638,-15784,-14370,-16560,-19689,-14885,0,0,14788,19141,15532,13212,
        14567,15014,12948,11681,12450,12478,10600,9394,10378,10879,7376,0,-7376,-10879,-10378,-9394,-10600,-12478,-12450,-11681,-12948,-15014,-14567,-13212,
        -15532,-19141,-14788,0,0,14602,18079,13444,10759,12258,12352,7690,0,-7690,-12352,-12258,-10759,-13444,-18079,-14602,0,0,14275,16138,
        8548,0,-8548,-16138,-14275,0,
      },
      {
        0,14252,19168,16537,14535,15948,17094,15998,15075,15824,16440,15720,15113,15620,16025,15474,15018,15400,15691,15237,14870,15175,15396,15004,
        14694,14948,15121,14772,14502,14719,14858,14540,14300,14490,14603,14309,14092,14261,14354,14079,13879,14032,14109,13848,13663,13802,13867,13618,
        13444,13573,13627,13387,13222,13343,13389,13157,12999,13113,13153,12927,12775,12884,12918,12697,12549,12654,12685,12466,12322,12424,12452,12236,
        12094,12194,12221,12006,11865,11964,11991,11776,11634,11735,11762,11546,11402,11505,11535,11315,11168,11275,11310,11085,10932,11046,11087,10855,
        10694,10816,10867,10624,10451,10587,10652,10393,10203,10358,10444,10161,9947,10130,10247,9929,9675,9904,10071,9693,9373,9683,9940,9449,
        8998,9479,9941,9161,8348,9419,10722,7871,0,-7871,-10722,-9419,-8348,-9161,-9941,-9479,-8998,-9449,-9940,-9683,-9373,-9693,-10071,-9904,
        -9675,-9929,-10247,-10130,-9947,-10161,-10444,-10358,-10203,-10393,-10652,-10587,-10451,-10624,-10867,-10816,-10694,-10855,-11087,-11046,-10932,-11085,-11310,-11275,
        -11168,-11315,-11535,-11505,-11402,-11546,-11762,-11735,-11634,-11776,-11991,-11964,-11865,-12006,-12221,-12194,-12094,-12236,-12452,-12424,-12322,-12466,-12685,-12654,
        -12549,-12697,-12918,-12884,-12775,-12927,-13153,-13113,-12999,-13157,-13389,-13343,-13222,-13387,-13627,-13573,-13444,-13618,-13867,-13802,-13663,-13848,-14109,-14032,
        -13879,-14079,-14354,-14261,-14092,-14309,-14603,-14490,-14300,-14540,-14858,-14719,-14502,-14772,-15121,-14948,-14694,-15004,-15396,-15175,-14870,-15237,-15691,-15400,
        -15018,-15474,-16025,-15620,-15113,-15720,-16440,-15824,-15075,-15998,-17094,-15948,-14535,-16537,-19168,-14252,0,0,14231,19054,16327,14302,15698,16753,
        15559,14609,15343,15872,15050,14415,14909,15230,14575,14087,14460,14670,14108,13704,14005,14150,13644,13292,13548,13650,13181,12864,13090,13165,
        12720,12423,12631,12691,12258,11973,12173,12226,11797,11513,11715,11772,11335,11041,11257,11331,10872,10553,10801,10912,10406,10033,10350,10540,
        9931,9438,9917,10300,9413,8568,9627,10842,7894,0,-7894,-10842,-9627,-8568,-9413,-10300,-9917,-9438,-9931,-10540,-10350,-10033,-10406,-10912,
        -10801,-10553,-10872,-11331,-11257,-11041,-11335,-11772,-11715,-11513,-11797,-12226,-12173,-11973,-12258,-12691,-12631,-12423,-12720,-13165,-13090,-12864,-13181,-13650,
        -13548,-13292,-13644,-14150,-14005,-13704,-14108,-14670,-14460,-14087,-14575,-15230,-14909,-14415,-15050,-15872,-15343,-14609,-15559,-16753,-15698,-14302,-16327,-19054,
        -14231,0,0,14191,18829,15908,13832,15198,16080,14678,13665,14384,14754,13709,12992,13492,13676,12771,12173,12585,12698,11839,11272,11679,
        11801,10901,10274,10787,11051,9922,8988,10038,11091,7942,0,-7942,-11091,-10038,-8988,-9922,-11051,-10787,-10274,-10901,-11801,-11679,-11272,-11839,
        -12698,-12585,-12173,-12771,-13676,-13492,-12992,-13709,-14754,-14384,-13665,-14678,-16080,-15198,-13832,-15908,-18829,-14191,0,0,14114,18390,15063,12865,
        14206,14784,12903,11681,12495,12709,10960,9741,10847,11630,8050,0,-8050,-11630,-10847,-9741,-10960,-12709,-12495,-11681,-12903,-14784,-14206,-12865,
        -15063,-18390,-14114,0,0,13974,17558,13337,10759,12366,12873,8318,0,-8318,-12873,-12366,-10759,-13337,-17558,-13974,0,0,13754,16138,
        9069,0,-9069,-16138,-13754,0,
      },
      {
        0,13543,18229,15746,13847,15194,16299,15274,14399,15115,15718,15049,14475,14961,15363,14855,14425,14792,15086,14670,14323,14618,14846,14488,
        14195,14442,14624,14307,14052,14264,14415,14126,13900,14087,14213,13947,13741,13908,14016,13767,13578,13730,13823,13588,13411,13552,13633,13408,
        13242,13373,13445,13229,13071,13194,13259,13050,12899,13016,13075,12871,12725,12837,12892,12692,12549,12658,12710,12513,12372,12480,12530,12334,
        12195,12301,12351,12155,12015,12122,12173,11976,11835,11944,11996,11796,11653,11765,11821,11617,11469,11587,11648,11438,11283,11408,11478,11259,
        11094,11230,11311,11079,10901,11052,11149,10899,10702,10874,10994,10718,10494,10698,10852,10537,10269,10523,10733,10352,10011,10353,10662,10157,
        9674,10203,10735,9915,9035,10210,11660,8580,0,-8580,-11660,-10210,-9035,-9915,-10735,-10203,-9674,-10157,-10662,-10353,-10011,-10352,-10733,-10523,
        -10269,-10537,-10852,-10698,-10494,-10718,-10994,-10874,-10702,-10899,-11149,-11052,-10901,-11079,-11311,-11230,-11094,-11259,-11478,-11408,-11283,-11438,-11648,-11587,
        -11469,-11617,-11821,-11765,-11653,-11796,-11996,-11944,-11835,-11976,-12173,-12122,-12015,-12155,-12351,-12301,-12195,-12334,-12530,-12480,-12372,-12513,-12710,-12658,
        -12549,-12692,-12892,-12837,-12725,-12871,-13075,-13016,-12899,-13050,-13259,-13194,-13071,-13229,-13445,-13373,-13242,-13408,-13633,-13552,-13411,-13588,-13823,-13730,
        -13578,-13767,-14016,-13908,-13741,-13947,-14213,-14087,-13900,-14126,-14415,-14264,-14052,-14307,-14624,-14442,-14195,-14488,-14846,-14618,-14323,-14670,-15086,-14792,
        -14425,-14855,-15363,-14961,-14475,-15049,-15718,-15115,-14399,-15274,-16299,-15194,-13847,-15746,-18229,-13543,0,0,13527,18141,15583,13665,15000,16036,
        14932,14035,14742,15279,14528,13928,14409,14750,14155,13694,14061,14299,13791,13408,13708,13885,13429,13095,13353,13492,13069,12765,12997,13113,
        12710,12423,12641,12744,12350,12072,12285,12384,11991,11711,11929,12036,11631,11337,11574,11702,11270,10945,11220,11392,10906,10519,10872,11133,
        10533,10013,10543,11017,10112,9205,10371,11754,8598,0,-8598,-11754,-10371,-9205,-10112,-11017,-10543,-10013,-10533,-11133,-10872,-10519,-10906,-11392,
        -11220,-10945,-11270,-11702,-11574,-11337,-11631,-12036,-11929,-11711,-11991,-12384,-12285,-12072,-12350,-12744,-12641,-12423,-12710,-13113,-12997,-12765,-13069,-13492,
        -13353,-13095,-13429,-13885,-13708,-13408,-13791,-14299,-14061,-13694,-14155,-14750,-14409,-13928,-14528,-15279,-14742,-14035,-14932,-16036,-15000,-13665,-15583,-18141,
        -13527,0,0,13497,17969,15255,13293,14612,15521,14246,13289,13997,14426,13483,12801,13308,13567,12750,12173,12606,12807,12023,11463,11904,
        12129,11288,10651,11220,11610,10508,9526,10690,11951,8636,0,-8636,-11951,-10690,-9526,-10508,-11610,-11220,-10651,-11288,-12129,-11904,-11463,-12023,
        -12807,-12606,-12173,-12750,-13567,-13308,-12801,-13483,-14426,-13997,-13289,-14246,-15521,-14612,-13293,-15255,-17969,-13497,0,0,13440,17639,14595,12518,
        13846,14553,12858,11681,12540,12939,11321,10088,11315,12381,8724,0,-8724,-12381,-11315,-10088,-11321,-12939,-12540,-11681,-12858,-14553,-13846,-12518,
        -14595,-17639,-13440,0,0,13345,17038,13229,10759,12474,13393,8946,0,-8946,-13393,-12474,-10759,-13229,-17038,-13345,0,0,13234,16138,
        9590,0,-9590,-16138,-13234,0,
      },
      {
        0,12834,17291,14955,13160,14440,15504,14549,13724,14407,14996,14378,13838,14303,14702,14236,13831,14184,14482,14102,13776,14061,14295,13971,
        13696,13935,14128,13842,13602,13809,13971,13713,13499,13683,13822,13584,13390,13555,13678,13456,13277,13428,13537,13327,13160,13301,13399,13199,
        13041,13173,13263,13071,12921,13046,13130,12943,12798,12918,12997,12815,12674,12791,12866,12687,12549,12663,12736,12559,12423,12536,12608,12431,
        12295,12408,12481,12303,12166,12281,12355,12175,12036,12153,12230,12047,11904,12026,12107,11919,11770,11898,11986,11791,11634,11771,11868,11663,
        11495,11644,11754,11534,11351,11517,11645,11405,11201,11390,11544,11276,11041,11265,11457,11144,10862,11142,11394,11010,10649,11024,11385,10865,
        10349,10928,11530,10669,9723,11001,12599,9289,0,-9289,-12599,-11001,-9723,-10669,-11530,-10928,-10349,-10865,-11385,-11024,-10649,-11010,-11394,-11142,
        -10862,-11144,-11457,-11265,-11041,-11276,-11544,-11390,-11201,-11405,-11645,-11517,-11351,-11534,-11754,-11644,-11495,-11663,-11868,-11771,-11634,-11791,-11986,-11898,
        -11770,-11919,-12107,-12026,-11904,-12047,-12230,-12153,-12036,-12175,-12355,-12281,-12166,-12303,-12481,-12408,-12295,-12431,-12608,-12536,-12423,-12559,-12736,-12663,
        -12549,-12687,-12866,-12791,-12674,-12815,-12997,-12918,-12798,-12943,-13130,-13046,-12921,-13071,-13263,-13173,-13041,-13199,-13399,-13301,-13160,-13327,-13537,-13428,
        -13277,-13456,-13678,-13555,-13390,-13584,-13822,-13683,-13499,-13713,-13971,-13809,-13602,-13842,-14128,-13935,-13696,-13971,-14295,-14061,-13776,-14102,-14482,-14184,
        -13831,-14236,-14702,-14303,-13838,-14378,-14996,-14407,-13724,-14549,-15504,-14440,-13160,-14955,-17291,-12834,0,0,12823,17229,14838,13028,14301,15319,
        14305,13460,14141,14687,14006,13441,13909,14270,13736,13301,13662,13928,13474,13112,13411,13621,13215,12897,13159,13334,12957,12666,12905,13060,
        12700,12423,12651,12796,12443,12171,12397,12543,12186,11908,12143,12300,11928,11633,11891,12073,11669,11338,11640,11872,11407,11006,11395,11725,
        11134,10587,11170,11734,10810,9842,11116,12667,9302,0,-9302,-12667,-11116,-9842,-10810,-11734,-11170,-10587,-11134,-11725,-11395,-11006,-11407,-11872,
        -11640,-11338,-11669,-12073,-11891,-11633,-11928,-12300,-12143,-11908,-12186,-12543,-12397,-12171,-12443,-12796,-12651,-12423,-12700,-13060,-12905,-12666,-12957,-13334,
        -13159,-12897,-13215,-13621,-13411,-13112,-13474,-13928,-13662,-13301,-13736,-14270,-13909,-13441,-14006,-14687,-14141,-13460,-14305,-15319,-14301,-13028,-14838,-17229,
        -12823,0,0,12802,17110,14603,12755,14026,14962,13814,12912,13610,14098,13258,12610,13125,13459,12730,12173,12627,12916,12206,11654,12130,
        12457,11675,11028,11652,12169,11094,10065,11343,12811,9330,0,-9330,-12811,-11343,-10065,-11094,-12169,-11652,-11028,-11675,-12457,-12130,-11654,-12206,
        -12916,-12627,-12173,-12730,-13459,-13125,-12610,-13258,-14098,-13610,-12912,-13814,-14962,-14026,-12755,-14603,-17110,-12802,0,0,12766,16888,14126,12171,
        13485,14322,12812,11681,12586,13170,11682,10435,11784,13132,9398,0,-9398,-13132,-11784,-10435,-11682,-13170,-12586,-11681,-12812,-14322,-13485,-12171,
        -14126,-16888,-12766,0,0,12717,16517,13121,10759,12582,13914,9575,0,-9575,-13914,-12582,-10759,-13121,-16517,-12717,0,0,12713,16138,
        10110,0,-10110,-16138,-12713,0,
      },
      {
        0,12125,16352,14164,12472,13686,14709,13825,13049,13699,14273,13708,13200,13644,14040,13618,13237,13576,13877,13535,13229,13504,13745,13455,
        13197,13429,13631,13377,13152,13354,13528,13299,13098,13279,13431,13221,13039,13203,13339,13144,12975,13126,13251,13067,12909,13050,13165,12990,
        12840,12974,13082,12913,12770,12897,13000,12836,12698,12821,12919,12759,12624,12744,12840,12682,12549,12668,12762,12605,12473,12592,12686,12529,
        12396,12515,12610,12452,12317,12439,12536,12375,12237,12362,12464,12298,12155,12286,12393,12221,12072,12210,12325,12144,11985,12133,12259,12067,
        11896,12057,12198,11989,11801,11982,12142,11911,11700,11907,12095,11833,11588,11833,12062,11752,11456,11761,12056,11669,11286,11695,12107,11574,
        11024,11652,12325,11423,10410,11792,13537,9998,0,-9998,-13537,-11792,-10410,-11423,-12325,-11652,-11024,-11574,-12107,-11695,-11286,-11669,-12056,-11761,
        -11456,-11752,-12062,-11833,-11588,-11833,-12095,-11907,-11700,-11911,-12142,-11982,-11801,-11989,-12198,-12057,-11896,-12067,-12259,-12133,-11985,-12144,-12325,-12210,
        -12072,-12221,-12393,-12286,-12155,-12298,-12464,-12362,-12237,-12375,-12536,-12439,-12317,-12452,-12610,-12515,-12396,-12529,-12686,-12592,-12473,-12605,-12762,-12668,
        -12549,-12682,-12840,-12744,-12624,-12759,-12919,-12821,-12698,-12836,-13000,-12897,-12770,-12913,-13082,-12974,-12840,-12990,-13165,-13050,-12909,-13067,-13251,-13126,
        -12975,-13144,-13339,-13203,-13039,-13221,-13431,-13279,-13098,-13299,-13528,-13354,-13152,-13377,-13631,-13429,-13197,-13455,-13745,-13504,-13229,-13535,-13877,-13576,
        -13237,-13618,-14040,-13644,-13200,-13708,-14273,-13699,-13049,-13825,-14709,-13686,-12472,-14164,-16352,-12125,0,0,12119,16317,14094,12391,13603,14602,
        13678,12886,13539,14095,13484,12954,13408,13791,13317,12909,13264,13557,13157,12816,13115,13357,13001,12699,12964,13176,12845,12567,12813,13007,
        12690,12423,12661,12849,12535,12270,12509,12701,12380,12106,12358,12564,12225,11929,12207,12444,12068,11731,12059,12352,11907,11493,11917,12317,
        11735,11162,11797,12451,11508,10479,11860,13579,10006,0,-10006,-13579,-11860,-10479,-11508,-12451,-11797,-11162,-11735,-12317,-11917,-11493,-11907,-12352,
        -12059,-11731,-12068,-12444,-12207,-11929,-12225,-12564,-12358,-12106,-12380,-12701,-12509,-12270,-12535,-12849,-12661,-12423,-12690,-13007,-12813,-12567,-12845,-13176,
        -12964,-12699,-13001,-13357,-13115,-12816,-13157,-13557,-13264,-12909,-13317,-13791,-13408,-12954,-13484,-14095,-13539,-12886,-13678,-14602,-13603,-12391,-14094,-16317,
        -12119,0,0,12108,16250,13951,12217,13439,14404,13381,12535,13223,13770,13032,12419,12941,13350,12709,12173,12647,13024,12390,11845,12355,
        12785,12062,11405,12084,12727,11681,10603,11995,13671,10025,0,-10025,-13671,-11995,-10603,-11681,-12727,-12084,-11405,-12062,-12785,-12355,-11845,-12390,
        -13024,-12647,-12173,-12709,-13350,-12941,-12419,-13032,-13770,-13223,-12535,-13381,-14404,-13439,-12217,-13951,-16250,-12108,0,0,12093,16137,13658,11824,
        13124,14092,12767,11681,12631,13400,12042,10782,12252,13884,10071,0,-10071,-13884,-12252,-10782,-12042,-13400,-12631,-11681,-12767,-14092,-13124,-11824,
        -13658,-16137,-12093,0,0,12089,15996,13013,10759,12690,14435,10203,0,-10203,-14435,-12690,-10759,-13013,-15996,-12089,0,0,12193,16138,
        10631,0,-10631,-16138,-12193,0,
      },
      {
        0,11416,15414,13373,11785,12932,13915,13101,12374,12990,13551,13037,12562,12986,13379,12999,12644,12968,13272,12968,12682,12947,13195,12939,
        12698,12923,13135,12912,12702,12899,13084,12885,12697,12875,13041,12859,12688,12850,13001,12833,12674,12825,12965,12807,12658,12799,12931,12781,
        12639,12774,12900,12755,12619,12749,12870,12729,12597,12724,12841,12703,12574,12698,12814,12678,12549,12673,12788,12652,12523,12647,12764,12626,
        12496,12622,12740,12600,12468,12597,12718,12574,12438,12571,12698,12549,12407,12546,12679,12523,12373,12521,12663,12497,12337,12496,12650,12471,
        12297,12471,12641,12444,12252,12447,12638,12417,12199,12423,12645,12390,12135,12400,12667,12360,12050,12380,12717,12327,11924,12366,12829,12282,
        11699,12376,13120,12178,11098,12583,14476,10707,0,-10707,-14476,-12583,-11098,-12178,-13120,-12376,-11699,-12282,-12829,-12366,-11924,-12327,-12717,-12380,
        -12050,-12360,-12667,-12400,-12135,-12390,-12645,-12423,-12199,-12417,-12638,-12447,-12252,-12444,-12641,-12471,-12297,-12471,-12650,-12496,-12337,-12497,-12663,-12521,
        -12373,-12523,-12679,-12546,-12407,-12549,-12698,-12571,-12438,-12574,-12718,-12597,-12468,-12600,-12740,-12622,-12496,-12626,-12764,-12647,-12523,-12652,-12788,-12673,
        -12549,-12678,-12814,-12698,-12574,-12703,-12841,-12724,-12597,-12729,-12870,-12749,-12619,-12755,-12900,-12774,-12639,-12781,-12931,-12799,-12658,-12807,-12965,-12825,
        -12674,-12833,-13001,-12850,-12688,-12859,-13041,-12875,-12697,-12885,-13084,-12899,-12702,-12912,-13135,-12923,-12698,-12939,-13195,-12947,-12682,-12968,-13272,-12968,
        -12644,-12999,-13379,-12986,-12562,-13037,-13551,-12990,-12374,-13101,-13915,-12932,-11785,-13373,-15414,-11416,0,0,11414,15404,13349,11754,12905,13885,
        13051,12311,12938,13502,12961,12467,12908,13311,12898,12516,12865,13186,12841,12520,12818,13093,12786,12501,12769,13017,12733,12468,12720,12955,
        12680,12423,12671,12902,12628,12369,12621,12859,12575,12304,12572,12829,12521,12224,12524,12815,12466,12123,12478,12831,12407,11980,12439,12910,
        12337,11737,12424,13168,12207,11117,12605,14492,10710,0,-10710,-14492,-12605,-11117,-12207,-13168,-12424,-11737,-12337,-12910,-12439,-11980,-12407,-12831,
        -12478,-12123,-12466,-12815,-12524,-12224,-12521,-12829,-12572,-12304,-12575,-12859,-12621,-12369,-12628,-12902,-12671,-12423,-12680,-12955,-12720,-12468,-12733,-13017,
        -12769,-12501,-12786,-13093,-12818,-12520,-12841,-13186,-12865,-12516,-12898,-13311,-12908,-12467,-12961,-13502,-12938,-12311,-13051,-13885,-12905,-11754,-13349,-15404,
        -11414,0,0,11413,15390,13299,11679,12853,13845,12949,12158,12836,13441,12807,12228,12757,13242,12688,12173,12668,13133,12574,12036,12581,
        13113,12449,11781,12517,13286,12267,11141,12647,14530,10719,0,-10719,-14530,-12647,-11141,-12267,-13286,-12517,-11781,-12449,-13113,-12581,-12036,-12574,
        -13133,-12668,-12173,-12688,-13242,-12757,-12228,-12807,-13441,-12836,-12158,-12949,-13845,-12853,-11679,-13299,-15390,-11413,0,0,11419,15386,13189,11476,
        12764,13861,12722,11681,12676,13631,12403,11129,12721,14635,10745,0,-10745,-14635,-12721,-11129,-12403,-13631,-12676,-11681,-12722,-13861,-12764,-11476,
        -13189,-15386,-11419,0,0,11460,15476,12905,10759,12797,14955,10832,0,-10832,-14955,-12797,-10759,-12905,-15476,-11460,0,0,11672,16138,
        11151,0,-11151,-16138,-11672,0,
      },
    },
    { /* FOLD */
      {
        0,493,985,1476,1965,2452,2936,3416,3893,4365,4831,5293,5748,6196,6638,7072,7498,7917,8326,8727,9119,9502,9875,10238,
        10591,10934,11267,11590,11902,12203,12494,12775,13045,13305,13554,13793,14022,14241,14450,14649,14838,15018,15188,15350,15502,15645,15780,15906,
        16023,16133,16235,16329,16415,16493,16565,16629,16686,16736,16779,16815,16845,16868,16884,16894,16897,16894,16884,16868,16845,16815,16779,16736,
        16686,16629,16565,16493,16415,16329,16235,16133,16023,15906,15780,15645,15502,15350,15188,15018,14838,14649,14450,14241,14022,13793,13554,13305,
        13045,12775,12494,12203,11902,11590,11267,10934,10591,10238,9875,9502,9119,8727,8326,7917,7498,7072,6638,6196,5748,5293,4831,4365,
        3893,3416,2936,2452,1965,1476,985,493,0,-493,-985,-1476,-1965,-2452,-2936,-3416,-3893,-4365,-4831,-5293,-5748,-6196,-6638,-7072,
        -7498,-7917,-8326,-8727,-9119,-9502,-9875,-10238,-10591,-10934,-11267,-11590,-11902,-12203,-12494,-12775,-13045,-13305,-13554,-13793,-14022,-14241,-14450,-14649,
        -14838,-15018,-15188,-15350,-15502,-15645,-15780,-15906,-16023,-16133,-16235,-16329,-16415,-16493,-16565,-16629,-16686,-16736,-16779,-16815,-16845,-16868,-16884,-16894,
        -16897,-16894,-16884,-16868,-16845,-16815,-16779,-16736,-16686,-16629,-16565,-16493,-16415,-16329,-16235,-16133,-16023,-15906,-15780,-15645,-15502,-15350,-15188,-15018,
        -14838,-14649,-14450,-14241,-14022,-13793,-13554,-13305,-13045,-12775,-12494,-12203,-11902,-11590,-11267,-10934,-10591,-10238,-9875,-9502,-9119,-8727,-8326,-7917,
        -7498,-7072,-6638,-6196,-5748,-5293,-4831,-4365,-3893,-3416,-2936,-2452,-1965,-1476,-985,-493,0,0,985,1965,2936,3893,4831,5748,
        6638,7498,8326,9119,9875,10591,11267,11902,12494,13045,13554,14022,14450,14838,15188,15502,15780,16023,16235,16415,16565,16686,16779,16845,
        16884,16897,16884,16845,16779,16686,16565,16415,16235,16023,15780,15502,15188,14838,14450,14022,13554,13045,12494,11902,11267,10591,9875,9119,
        8326,7498,6638,5748,4831,3893,2936,1965,985,0,-985,-1965,-2936,-3893,-4831,-5748,-6638,-7498,-8326,-9119,-9875,-10591,-11267,-11902,
        -12494,-13045,-13554,-14022,-14450,-14838,-15188,-15502,-15780,-16023,-16235,-16415,-16565,-16686,-16779,-16845,-16884,-16897,-16884,-16845,-16779,-16686,-16565,-16415,
        -16235,-16023,-15780,-15502,-15188,-14838,-14450,-14022,-13554,-13045,-12494,-11902,-11267,-10591,-9875,-9119,-8326,-7498,-6638,-5748,-4831,-3893,-2936,-1965,
        -985,0,0,1965,3893,5748,7498,9119,10591,11902,13045,14022,14838,15502,16023,16415,16686,16845,16897,16845,16686,16415,16023,15502,
        14838,14022,13045,11902,10591,9119,7498,5748,3893,1965,0,-1965,-3893,-5748,-7498,-9119,-10591,-11902,-13045,-14022,-14838,-15502,-16023,-16415,
        -16686,-16845,-16897,-16845,-16686,-16415,-16023,-15502,-14838,-14022,-13045,-11902,-10591,-9119,-7498,-5748,-3893,-1965,0,0,3893,7498,10591,13045,
        14838,16023,16686,16897,16686,16023,14838,13045,10591,7498,3893,0,-3893,-7498,-10591,-13045,-14838,-16023,-16686,-16897,-16686,-16023,-14838,-13045,
        -10591,-7498,-3893,0,0,7489,13052,16027,16888,16027,13052,7489,0,-7489,-13052,-16027,-16888,-16027,-13052,-7489,0,0,12497,17673,
        12497,0,-12497,-17673,-12497,0,
      },
      {
        111,715,1318,1919,2517,3111,3700,4284,4861,5431,5993,6547,7091,7625,8148,8660,9160,9647,10123,10585,11033,11468,11889,12296,
        12688,13066,13430,13779,14114,14434,14740,15032,15310,15575,15826,16063,16288,16501,16701,16889,17066,17232,17386,17531,17666,17791,17907,18014,
        18112,18203,18286,18362,18430,18492,18548,18597,18641,18678,18711,18738,18760,18777,18789,18796,18798,18796,18789,18777,18760,18738,18711,18678,
        18641,18597,18548,18492,18430,18362,18286,18203,18112,18014,17907,17791,17666,17531,17386,17232,17066,16889,16701,16501,16288,16063,15826,15575,
        15310,15032,14740,14434,14114,13779,13430,13066,12688,12296,11889,11468,11033,10585,10123,9647,9160,8660,8148,7625,7091,6547,5993,5431,
        4861,4284,3700,3111,2517,1919,1318,715,111,-494,-1098,-1700,-2300,-2897,-3489,-4077,-4658,-5233,-5800,-6359,-6910,-7450,-7980,-8500,
        -9008,-9504,-9988,-10459,-10917,-11361,-11792,-12209,-12611,-13000,-13374,-13733,-14078,-14409,-14726,-15029,-15317,-15592,-15854,-16102,-16337,-16559,-16769,-16967,
        -17153,-17328,-17491,-17645,-17787,-17921,-18044,-18158,-18264,-18361,-18450,-18532,-18606,-18673,-18733,-18787,-18834,-18875,-18910,-18940,-18964,-18982,-18995,-19003,
        -19006,-19003,-18995,-18982,-18964,-18940,-18910,-18875,-18834,-18787,-18733,-18673,-18606,-18532,-18450,-18361,-18264,-18158,-18044,-17921,-17787,-17645,-17491,-17328,
        -17153,-16967,-16769,-16559,-16337,-16102,-15854,-15592,-15317,-15029,-14726,-14409,-14078,-13733,-13374,-13000,-12611,-12209,-11792,-11361,-10917,-10459,-9988,-9504,
        -9008,-8500,-7980,-7450,-6910,-6359,-5800,-5233,-4658,-4077,-3489,-2897,-2300,-1700,-1098,-494,111,111,1318,2517,3700,4861,5993,7091,
        8148,9160,10123,11033,11889,12688,13430,14114,14740,15310,15826,16288,16701,17066,17386,17666,17907,18112,18286,18430,18548,18641,18711,18760,
        18789,18798,18789,18760,18711,18641,18548,18430,18286,18112,17907,17666,17386,17066,16701,16288,15826,15310,14740,14114,13430,12688,11889,11033,
        10123,9160,8148,7091,5993,4861,3700,2517,1318,111,-1098,-2300,-3489,-4658,-5800,-6910,-7980,-9008,-9988,-10917,-11792,-12611,-13374,-14078,
        -14726,-15317,-15854,-16337,-16769,-17153,-17491,-17787,-18044,-18264,-18450,-18606,-18733,-18834,-18910,-18964,-18995,-19006,-18995,-18964,-18910,-18834,-18733,-18606,
        -18450,-18264,-18044,-17787,-17491,-17153,-16769,-16337,-15854,-15317,-14726,-14078,-13374,-12611,-11792,-10917,-9988,-9008,-7980,-6910,-5800,-4658,-3489,-2300,
        -1098,111,111,2517,4861,7091,9160,11033,12688,14114,15310,16288,17066,17666,18112,18430,18641,18760,18798,18760,18641,18430,18112,17666,
        17066,16288,15310,14114,12688,11033,9160,7091,4861,2517,111,-2300,-4658,-6910,-9008,-10917,-12611,-14078,-15317,-16337,-17153,-17787,-18264,-18606,
        -18834,-18964,-19006,-18964,-18834,-18606,-18264,-17787,-17153,-16337,-15317,-14078,-12611,-10917,-9008,-6910,-4658,-2300,111,111,4861,9160,12688,15310,
        17066,18112,18641,18798,18641,18112,17066,15310,12688,9160,4861,111,-4658,-9008,-12611,-15317,-17153,-18264,-18834,-19006,-18834,-18264,-17153,-15317,
        -12611,-9008,-4658,111,111,9134,15330,18123,18771,18123,15330,9134,111,-8983,-15337,-18274,-18979,-18274,-15337,-8983,111,107,14340,20172,
        14340,107,-14340,-20387,-14340,107,
      },
      {
        337,1104,1869,2629,3384,4131,4870,5599,6316,7021,7711,8387,9046,9687,10310,10914,11499,12062,12605,13125,13624,14101,14555,14987,
        15396,15783,16147,16490,16811,17110,17389,17648,17888,18108,18310,18494,18662,18814,18951,19073,19182,19278,19363,19437,19501,19556,19602,19641,
        19673,19699,19720,19736,19748,19757,19763,19767,19770,19771,19771,19770,19769,19768,19767,19767,19767,19767,19767,19768,19769,19770,19771,19771,
        19770,19767,19763,19757,19748,19736,19720,19699,19673,19641,19602,19556,19501,19437,19363,19278,19182,19073,18951,18814,18662,18494,18310,18108,
        17888,17648,17389,17110,16811,16490,16147,15783,15396,14987,14555,14101,13624,13125,12605,12062,11499,10914,10310,9687,9046,8387,7711,7021,
        6316,5599,4870,4131,3384,2629,1869,1104,337,-430,-1198,-1963,-2724,-3480,-4229,-4970,-5701,-6421,-7128,-7822,-8501,-9164,-9810,-10438,
        -11047,-11637,-12207,-12756,-13283,-13790,-14274,-14737,-15177,-15595,-15992,-16366,-16719,-17051,-17362,-17652,-17923,-18174,-18407,-18621,-18819,-19000,-19165,-19315,
        -19451,-19573,-19683,-19781,-19869,-19946,-20014,-20073,-20124,-20169,-20207,-20239,-20266,-20289,-20307,-20323,-20335,-20345,-20353,-20359,-20364,-20367,-20369,-20371,
        -20371,-20371,-20369,-20367,-20364,-20359,-20353,-20345,-20335,-20323,-20307,-20289,-20266,-20239,-20207,-20169,-20124,-20073,-20014,-19946,-19869,-19781,-19683,-19573,
        -19451,-19315,-19165,-19000,-18819,-18621,-18407,-18174,-17923,-17652,-17362,-17051,-16719,-16366,-15992,-15595,-15177,-14737,-14274,-13790,-13283,-12756,-12207,-11637,
        -11047,-10438,-9810,-9164,-8501,-7822,-7128,-6421,-5701,-4970,-4229,-3480,-2724,-1963,-1198,-430,337,337,1869,3384,4870,6316,7711,9046,
        10310,11499,12605,13624,14555,15396,16147,16811,17389,17888,18310,18662,18951,19182,19363,19501,19602,19673,19720,19748,19763,19770,19771,19769,
        19767,19767,19767,19769,19771,19770,19763,19748,19720,19673,19602,19501,19363,19182,18951,18662,18310,17888,17389,16811,16147,15396,14555,13624,
        12605,11499,10310,9046,7711,6316,4870,3384,1869,337,-1198,-2724,-4229,-5701,-7128,-8501,-9810,-11047,-12207,-13283,-14274,-15177,-15992,-16719,
        -17362,-17923,-18407,-18819,-19165,-19451,-19683,-19869,-20014,-20124,-20207,-20266,-20307,-20335,-20353,-20364,-20369,-20371,-20369,-20364,-20353,-20335,-20307,-20266,
        -20207,-20124,-20014,-19869,-19683,-19451,-19165,-18819,-18407,-17923,-17362,-16719,-15992,-15177,-14274,-13283,-12207,-11047,-9810,-8501,-7128,-5701,-4229,-2724,
        -1198,337,337,3384,6316,9046,11499,13624,15396,16811,17888,18662,19182,19501,19673,19748,19770,19769,19767,19769,19770,19748,19673,19501,
        19182,18662,17888,16811,15396,13624,11499,9046,6316,3384,337,-2724,-5701,-8501,-11047,-13283,-15177,-16719,-17923,-18819,-19451,-19869,-20124,-20266,
        -20335,-20364,-20371,-20364,-20335,-20266,-20124,-19869,-19451,-18819,-17923,-16719,-15177,-13283,-11047,-8501,-5701,-2724,337,337,6316,11499,15396,17888,
        19182,19673,19770,19767,19770,19673,19182,17888,15396,11499,6316,337,-5701,-11047,-15177,-17923,-19451,-20124,-20335,-20371,-20335,-20124,-19451,-17923,
        -15177,-11047,-5701,337,337,11418,17950,19705,19682,19705,17950,11418,337,-10966,-17985,-20156,-20285,-20156,-17985,-10966,337,319,16049,22377,
        16049,319,-16049,-23016,-16049,319,
      },
      {
        724,1680,2631,3575,4509,5429,6334,7221,8088,8932,9752,10545,11310,12046,12750,13422,14061,14665,15235,15769,16268,16732,17159,17552,
        17910,18233,18524,18781,19007,19202,19369,19507,19618,19705,19768,19809,19830,19832,19817,19786,19741,19685,19617,19541,19457,19367,19273,19175,
        19076,18976,18877,18779,18685,18594,18508,18428,18354,18287,18228,18177,18134,18101,18077,18063,18058,18063,18077,18101,18134,18177,18228,18287,
        18354,18428,18508,18594,18685,18779,18877,18976,19076,19175,19273,19367,19457,19541,19617,19685,19741,19786,19817,19832,19830,19809,19768,19705,
        19618,19507,19369,19202,19007,18781,18524,18233,17910,17552,17159,16732,16268,15769,15235,14665,14061,13422,12750,12046,11310,10545,9752,8932,
        8088,7221,6334,5429,4509,3575,2631,1680,724,-235,-1193,-2148,-3097,-4037,-4966,-5880,-6779,-7659,-8518,-9354,-10165,-10949,-11706,-12432,
        -13128,-13792,-14422,-15020,-15583,-16112,-16606,-17066,-17492,-17884,-18242,-18568,-18862,-19124,-19357,-19562,-19738,-19888,-20014,-20116,-20196,-20256,-20297,-20321,
        -20329,-20322,-20304,-20274,-20234,-20186,-20132,-20072,-20009,-19942,-19874,-19806,-19738,-19672,-19609,-19549,-19493,-19442,-19397,-19358,-19325,-19299,-19280,-19269,
        -19265,-19269,-19280,-19299,-19325,-19358,-19397,-19442,-19493,-19549,-19609,-19672,-19738,-19806,-19874,-19942,-20009,-20072,-20132,-20186,-20234,-20274,-20304,-20322,
        -20329,-20321,-20297,-20256,-20196,-20116,-20014,-19888,-19738,-19562,-19357,-19124,-18862,-18568,-18242,-17884,-17492,-17066,-16606,-16112,-15583,-15020,-14422,-13792,
        -13128,-12432,-11706,-10949,-10165,-9354,-8518,-7659,-6779,-5880,-4966,-4037,-3097,-2148,-1193,-235,724,724,2631,4509,6334,8088,9752,11310,
        12750,14061,15235,16268,17159,17910,18524,19007,19369,19618,19768,19830,19817,19741,19617,19457,19273,19076,18877,18685,18508,18354,18228,18134,
        18077,18058,18077,18134,18228,18354,18508,18685,18877,19076,19273,19457,19617,19741,19817,19830,19768,19618,19369,19007,18524,17910,17159,16268,
        15235,14061,12750,11310,9752,8088,6334,4509,2631,724,-1193,-3097,-4966,-6779,-8518,-10165,-11706,-13128,-14422,-15583,-16606,-17492,-18242,-18862,
        -19357,-19738,-20014,-20196,-20297,-20329,-20304,-20234,-20132,-20009,-19874,-19738,-19609,-19493,-19397,-19325,-19280,-19265,-19280,-19325,-19397,-19493,-19609,-19738,
        -19874,-20009,-20132,-20234,-20304,-20329,-20297,-20196,-20014,-19738,-19357,-18862,-18242,-17492,-16606,-15583,-14422,-13128,-11706,-10165,-8518,-6779,-4966,-3097,
        -1193,724,724,4509,8088,11310,14061,16268,17910,19007,19618,19830,19741,19457,19076,18685,18354,18134,18058,18134,18354,18685,19076,19457,
        19741,19830,19618,19007,17910,16268,14061,11310,8088,4509,724,-3097,-6779,-10165,-13128,-15583,-17492,-18862,-19738,-20196,-20329,-20234,-20009,-19738,
        -19493,-19325,-19265,-19325,-19493,-19738,-20009,-20234,-20329,-20196,-19738,-18862,-17492,-15583,-13128,-10165,-6779,-3097,724,724,8088,14061,17910,19618,
        19741,19076,18354,18058,18354,19076,19741,19618,17910,14061,8088,724,-6779,-13128,-17492,-19738,-20329,-20009,-19493,-19265,-19493,-20009,-20329,-19738,
        -17492,-13128,-6779,724,721,13830,19798,19164,17817,19164,19798,13830,721,-12895,-19918,-20100,-19021,-20100,-19918,-12895,721,662,16441,22590,
        16441,662,-16441,-23913,-16441,662,
      },
      {
        1275,2440,3596,4738,5863,6965,8040,9084,10095,11067,11998,12885,13726,14518,15259,15948,16583,17164,17690,18162,18578,18941,19249,19506,
        19712,19868,19977,20040,20061,20040,19982,19888,19760,19603,19419,19210,18979,18730,18464,18186,17897,17599,17297,16991,16684,16379,16077,15781,
        15492,15213,14944,14687,14444,14216,14004,13809,13633,13475,13336,13218,13121,13045,12990,12958,12947,12958,12990,13045,13121,13218,13336,13475,
        13633,13809,14004,14216,14444,14687,14944,15213,15492,15781,16077,16379,16684,16991,17297,17599,17897,18186,18464,18730,18979,19210,19419,19603,
        19760,19888,19982,20040,20061,20040,19977,19868,19712,19506,19249,18941,18578,18162,17690,17164,16583,15948,15259,14518,13726,12885,11998,11067,
        10095,9084,8040,6965,5863,4738,3596,2440,1275,105,-1064,-2229,-3383,-4524,-5646,-6746,-7819,-8861,-9868,-10839,-11768,-12655,-13496,-14288,
        -15032,-15725,-16366,-16954,-17489,-17972,-18402,-18781,-19108,-19386,-19615,-19798,-19935,-20030,-20085,-20101,-20081,-20029,-19945,-19834,-19698,-19539,-19361,-19165,
        -18955,-18733,-18501,-18263,-18019,-17774,-17528,-17284,-17044,-16810,-16583,-16364,-16157,-15960,-15777,-15608,-15454,-15316,-15195,-15091,-15006,-14939,-14891,-14862,
        -14852,-14862,-14891,-14939,-15006,-15091,-15195,-15316,-15454,-15608,-15777,-15960,-16157,-16364,-16583,-16810,-17044,-17284,-17528,-17774,-18019,-18263,-18501,-18733,
        -18955,-19165,-19361,-19539,-19698,-19834,-19945,-20029,-20081,-20101,-20085,-20030,-19935,-19798,-19615,-19386,-19108,-18781,-18402,-17972,-17489,-16954,-16366,-15725,
        -15032,-14288,-13496,-12655,-11768,-10839,-9868,-8861,-7819,-6746,-5646,-4524,-3383,-2229,-1064,105,1275,1275,3596,5863,8040,10095,11998,13726,
        15259,16583,17690,18578,19249,19712,19977,20061,19982,19760,19419,18979,18464,17897,17297,16684,16077,15492,14944,14444,14004,13633,13336,13121,
        12990,12947,12990,13121,13336,13633,14004,14444,14944,15492,16077,16684,17297,17897,18464,18979,19419,19760,19982,20061,19977,19712,19249,18578,
        17690,16583,15259,13726,11998,10095,8040,5863,3596,1275,-1064,-3383,-5646,-7819,-9868,-11768,-13496,-15032,-16366,-17489,-18402,-19108,-19615,-19935,
        -20085,-20081,-19945,-19698,-19361,-18955,-18501,-18019,-17528,-17044,-16583,-16157,-15777,-15454,-15195,-15006,-14891,-14852,-14891,-15006,-15195,-15454,-15777,-16157,
        -16583,-17044,-17528,-18019,-18501,-18955,-19361,-19698,-19945,-20081,-20085,-19935,-19615,-19108,-18402,-17489,-16366,-15032,-13496,-11768,-9868,-7819,-5646,-3383,
        -1064,1275,1275,5863,10095,13726,16583,18578,19712,20061,19760,18979,17897,16684,15492,14444,13633,13121,12947,13121,13633,14444,15492,16684,
        17897,18979,19760,20061,19712,18578,16583,13726,10095,5863,1275,-3383,-7819,-11768,-15032,-17489,-19108,-19935,-20081,-19698,-18955,-18019,-17044,-16157,
        -15454,-15006,-14852,-15006,-15454,-16157,-17044,-18019,-18955,-19698,-20081,-19935,-19108,-17489,-15032,-11768,-7819,-3383,1275,1275,10094,16583,19712,19760,
        17896,15493,13633,12946,13633,15493,17896,19760,19712,16583,10094,1275,-7818,-15032,-19108,-20081,-18954,-17045,-15454,-14852,-15454,-17045,-18954,-20081,
        -19108,-15032,-7818,1275,1266,16007,20215,15704,12355,15704,20215,16007,1266,-14444,-20536,-17268,-14245,-17268,-20536,-14444,1266,1105,14890,19952,
        14890,1105,-14890,-22163,-14890,1105,
      },
      {
        1942,3331,4704,6055,7376,8659,9899,11090,12224,13298,14306,15244,16109,16898,17608,18238,18787,19255,19641,19946,20172,20321,20395,20397,
        20330,20197,20003,19751,19447,19093,18695,18258,17785,17282,16753,16203,15636,15057,14468,13875,13281,12689,12103,11525,10959,10408,9872,9356,
        8861,8388,7939,7516,7120,6752,6414,6105,5827,5580,5366,5183,5033,4917,4833,4783,4766,4783,4833,4917,5033,5183,5366,5580,
        5827,6105,6414,6752,7120,7516,7939,8388,8861,9356,9872,10408,10959,11525,12103,12689,13281,13875,14468,15057,15636,16203,16753,17282,
        17785,18258,18695,19093,19447,19751,20003,20197,20330,20397,20395,20321,20172,19946,19641,19255,18787,18238,17608,16898,16109,15244,14306,13298,
        12224,11090,9899,8659,7376,6055,4704,3331,1942,545,-852,-2242,-3617,-4969,-6292,-7579,-8824,-10020,-11162,-12244,-13263,-14214,-15094,-15901,
        -16631,-17285,-17860,-18358,-18777,-19119,-19386,-19579,-19701,-19754,-19741,-19667,-19535,-19348,-19111,-18828,-18503,-18141,-17746,-17323,-16876,-16409,-15926,-15431,
        -14928,-14421,-13914,-13408,-12909,-12417,-11937,-11471,-11021,-10588,-10176,-9786,-9418,-9076,-8759,-8470,-8209,-7976,-7774,-7601,-7459,-7348,-7269,-7221,
        -7205,-7221,-7269,-7348,-7459,-7601,-7774,-7976,-8209,-8470,-8759,-9076,-9418,-9786,-10176,-10588,-11021,-11471,-11937,-12417,-12909,-13408,-13914,-14421,
        -14928,-15431,-15926,-16409,-16876,-17323,-17746,-18141,-18503,-18828,-19111,-19348,-19535,-19667,-19741,-19754,-19701,-19579,-19386,-19119,-18777,-18358,-17860,-17285,
        -16631,-15901,-15094,-14214,-13263,-12244,-11162,-10020,-8824,-7579,-6292,-4969,-3617,-2242,-852,545,1942,1942,4704,7376,9899,12224,14306,16109,
        17608,18787,19641,20172,20395,20330,20003,19447,18695,17785,16753,15636,14468,13281,12103,10959,9872,8861,7939,7120,6414,5827,5366,5033,
        4833,4766,4833,5033,5366,5827,6414,7120,7939,8861,9872,10959,12103,13281,14468,15636,16753,17785,18695,19447,20003,20330,20395,20172,
        19641,18787,17608,16109,14306,12224,9899,7376,4704,1942,-852,-3617,-6292,-8824,-11162,-13263,-15094,-16631,-17860,-18777,-19386,-19701,-19741,-19535,
        -19111,-18503,-17746,-16876,-15926,-14928,-13914,-12909,-11937,-11021,-10176,-9418,-8759,-8209,-7774,-7459,-7269,-7205,-7269,-7459,-7774,-8209,-8759,-9418,
        -10176,-11021,-11937,-12909,-13914,-14928,-15926,-16876,-17746,-18503,-19111,-19535,-19741,-19701,-19386,-18777,-17860,-16631,-15094,-13263,-11162,-8824,-6292,-3617,
        -852,1942,1942,7376,12224,16109,18787,20172,20330,19447,17785,15636,13281,10959,8861,7120,5827,5033,4766,5033,5827,7120,8861,10959,
        13281,15636,17785,19447,20330,20172,18787,16109,12224,7376,1942,-3617,-8824,-13263,-16631,-18777,-19701,-19535,-18503,-16876,-14928,-12909,-11021,-9418,
        -8209,-7459,-7205,-7459,-8209,-9418,-11021,-12909,-14928,-16876,-18503,-19535,-19701,-18777,-16631,-13263,-8824,-3617,1942,1942,12222,18788,20332,17784,
        13280,8862,5827,4764,5827,8862,13280,17784,20332,18788,12222,1942,-8822,-16632,-19702,-18501,-14927,-11023,-8209,-7203,-8209,-11023,-14927,-18501,
        -19702,-16632,-8822,1942,1913,17525,18795,9299,3501,9299,18795,17525,1913,-15328,-19515,-11495,-5888,-11495,-19515,-15328,1913,1553,11237,14339,
        11237,1553,-11237,-17445,-11237,1553,
      },
      {
        2621,4245,5845,7409,8926,10384,11774,13087,14314,15448,16482,17411,18230,18936,19527,20002,20361,20605,20736,20756,20670,20482,20197,19821,
        19360,18822,18212,17539,16809,16030,15209,14354,13471,12567,11649,10723,9795,8869,7953,7049,6162,5296,4454,3639,2855,2102,1383,700,
        54,-555,-1125,-1656,-2147,-2599,-3011,-3383,-3715,-4008,-4261,-4474,-4649,-4785,-4881,-4939,-4959,-4939,-4881,-4785,-4649,-4474,-4261,-4008,
        -3715,-3383,-3011,-2599,-2147,-1656,-1125,-555,54,700,1383,2102,2855,3639,4454,5296,6162,7049,7953,8869,9795,10723,11649,12567,
        13471,14354,15209,16030,16809,17539,18212,18822,19360,19821,20197,20482,20670,20756,20736,20605,20361,20002,19527,18936,18230,17411,16482,15448,
        14314,13087,11774,10384,8926,7409,5845,4245,2621,983,-656,-2284,-3889,-5461,-6987,-8458,-9863,-11194,-12443,-13602,-14664,-15626,-16482,-17229,
        -17866,-18392,-18806,-19109,-19304,-19394,-19381,-19271,-19068,-18778,-18408,-17962,-17448,-16873,-16244,-15567,-14849,-14097,-13319,-12519,-11705,-10883,-10056,-9232,
        -8414,-7608,-6816,-6044,-5293,-4568,-3870,-3202,-2565,-1963,-1395,-863,-369,88,506,885,1225,1525,1785,2006,2186,2327,2427,2487,
        2507,2487,2427,2327,2186,2006,1785,1525,1225,885,506,88,-369,-863,-1395,-1963,-2565,-3202,-3870,-4568,-5293,-6044,-6816,-7608,
        -8414,-9232,-10056,-10883,-11705,-12519,-13319,-14097,-14849,-15567,-16244,-16873,-17448,-17962,-18408,-18778,-19068,-19271,-19381,-19394,-19304,-19109,-18806,-18392,
        -17866,-17229,-16482,-15626,-14664,-13602,-12443,-11194,-9863,-8458,-6987,-5461,-3889,-2284,-656,983,2621,2621,5845,8926,11774,14314,16482,18230,
        19527,20361,20736,20670,20197,19360,18212,16809,15209,13471,11649,9795,7953,6162,4454,2855,1383,54,-1125,-2147,-3011,-3715,-4261,-4649,
        -4881,-4959,-4881,-4649,-4261,-3715,-3011,-2147,-1125,54,1383,2855,4454,6162,7953,9795,11649,13471,15209,16809,18212,19360,20197,20670,
        20736,20361,19527,18230,16482,14314,11774,8926,5845,2621,-656,-3889,-6987,-9863,-12443,-14664,-16482,-17866,-18806,-19304,-19381,-19068,-18408,-17448,
        -16244,-14849,-13319,-11705,-10056,-8414,-6816,-5293,-3870,-2565,-1395,-369,506,1225,1785,2186,2427,2507,2427,2186,1785,1225,506,-369,
        -1395,-2565,-3870,-5293,-6816,-8414,-10056,-11705,-13319,-14849,-16244,-17448,-18408,-19068,-19381,-19304,-18806,-17866,-16482,-14664,-12443,-9863,-6987,-3889,
        -656,2621,2621,8926,14314,18230,20361,20670,19360,16809,13471,9795,6162,2855,54,-2147,-3715,-4649,-4959,-4649,-3715,-2147,54,2855,
        6162,9795,13471,16809,19360,20670,20361,18230,14314,8926,2621,-3889,-9863,-14664,-17866,-19304,-19068,-17448,-14849,-11705,-8414,-5293,-2565,-369,
        1225,2186,2507,2186,1225,-369,-2565,-5293,-8414,-11705,-14849,-17448,-19068,-19304,-17866,-14664,-9863,-3889,2621,2620,14306,20365,19367,13465,
        6157,61,-3714,-4967,-3714,61,6157,13465,19367,20365,14306,2620,-9855,-17870,-19075,-14843,-8410,-2573,1223,2515,1223,-2573,-8410,-14843,
        -19075,-17870,-9855,2620,2540,17909,15467,839,-7336,839,15467,17909,2540,-15297,-16853,-3451,5030,-3451,-16853,-15297,2540,1847,5894,6489,
        5894,1847,-5894,-10182,-5894,1847,
      },
      {
        3173,5044,6877,8654,10360,11979,13495,14897,16172,17311,18305,19147,19833,20361,20729,20938,20990,20890,20643,20257,19738,19097,18342,17486,
        16538,15510,14413,13260,12060,10826,9567,8294,7016,5743,4481,3239,2024,840,-307,-1413,-2475,-3489,-4453,-5367,-6228,-7037,-7794,-8498,
        -9151,-9754,-10308,-10814,-11274,-11689,-12062,-12393,-12685,-12938,-13154,-13335,-13482,-13595,-13675,-13723,-13739,-13723,-13675,-13595,-13482,-13335,-13154,-12938,
        -12685,-12393,-12062,-11689,-11274,-10814,-10308,-9754,-9151,-8498,-7794,-7037,-6228,-5367,-4453,-3489,-2475,-1413,-307,840,2024,3239,4481,5743,
        7016,8294,9567,10826,12060,13260,14413,15510,16538,17486,18342,19097,19738,20257,20643,20890,20990,20938,20729,20361,19833,19147,18305,17311,
        16172,14897,13495,11979,10360,8654,6877,5044,3173,1283,-610,-2487,-4331,-6124,-7850,-9494,-11040,-12478,-13794,-14979,-16026,-16927,-17678,-18276,
        -18721,-19013,-19154,-19149,-19002,-18720,-18310,-17781,-17143,-16404,-15576,-14669,-13694,-12661,-11582,-10465,-9322,-8162,-6993,-5824,-4662,-3515,-2389,-1289,
        -221,813,1807,2759,3667,4529,5343,6110,6827,7496,8116,8689,9213,9691,10123,10510,10853,11153,11411,11627,11803,11940,12037,12095,
        12114,12095,12037,11940,11803,11627,11411,11153,10853,10510,10123,9691,9213,8689,8116,7496,6827,6110,5343,4529,3667,2759,1807,813,
        -221,-1289,-2389,-3515,-4662,-5824,-6993,-8162,-9322,-10465,-11582,-12661,-13694,-14669,-15576,-16404,-17143,-17781,-18310,-18720,-19002,-19149,-19154,-19013,
        -18721,-18276,-17678,-16927,-16026,-14979,-13794,-12478,-11040,-9494,-7850,-6124,-4331,-2487,-610,1283,3173,3173,6877,10360,13495,16172,18305,19833,
        20729,20990,20643,19738,18342,16538,14413,12060,9567,7016,4481,2024,-307,-2475,-4453,-6228,-7794,-9151,-10308,-11274,-12062,-12685,-13154,-13482,
        -13675,-13739,-13675,-13482,-13154,-12685,-12062,-11274,-10308,-9151,-7794,-6228,-4453,-2475,-307,2024,4481,7016,9567,12060,14413,16538,18342,19738,
        20643,20990,20729,19833,18305,16172,13495,10360,6877,3173,-610,-4331,-7850,-11040,-13794,-16026,-17678,-18721,-19154,-19002,-18310,-17143,-15576,-13694,
        -11582,-9322,-6993,-4662,-2389,-221,1807,3667,5343,6827,8116,9213,10123,10853,11411,11803,12037,12114,12037,11803,11411,10853,10123,9213,
        8116,6827,5343,3667,1807,-221,-2389,-4662,-6993,-9322,-11582,-13694,-15576,-17143,-18310,-19002,-19154,-18721,-17678,-16026,-13794,-11040,-7850,-4331,
        -610,3173,3173,10360,16172,19833,20990,19738,16538,12060,7016,2024,-2475,-6228,-9151,-11274,-12685,-13482,-13739,-13482,-12685,-11274,-9151,-6228,
        -2475,2024,7016,12060,16538,19738,20990,19833,16172,10360,3173,-4331,-11040,-16026,-18721,-19002,-17143,-13694,-9322,-4662,-221,3667,6827,9213,
        10853,11803,12114,11803,10853,9213,6827,3667,-221,-4662,-9322,-13694,-17143,-19002,-18721,-16026,-11040,-4331,3173,3173,16145,21002,16560,6996,
        -2488,-9127,-12680,-13764,-12680,-9127,-2488,6996,16560,21002,16145,3173,-11013,-18732,-17165,-9302,-206,6802,10848,12141,10848,6802,-206,-9302,
        -17165,-18732,-11013,3173,2975,16729,10561,-7933,-17697,-7933,10561,16729,2975,-14172,-12895,5375,16414,5375,-12895,-14172,2975,1808,-166,-2043,
        -166,1808,166,-1574,166,1808,
      },
      {
        3476,5600,7669,9657,11540,13297,14908,16354,17621,18696,19569,20234,20688,20929,20960,20785,20413,19852,19115,18216,17168,15988,14693,13300,
        11827,10291,8709,7098,5473,3849,2239,657,-888,-2385,-3827,-5205,-6516,-7753,-8916,-10000,-11007,-11935,-12787,-13563,-14267,-14901,-15469,-15974,
        -16421,-16814,-17157,-17454,-17709,-17928,-18113,-18268,-18398,-18504,-18590,-18658,-18711,-18751,-18778,-18794,-18799,-18794,-18778,-18751,-18711,-18658,-18590,-18504,
        -18398,-18268,-18113,-17928,-17709,-17454,-17157,-16814,-16421,-15974,-15469,-14901,-14267,-13563,-12787,-11935,-11007,-10000,-8916,-7753,-6516,-5205,-3827,-2385,
        -888,657,2239,3849,5473,7098,8709,10291,11827,13300,14693,15988,17168,18216,19115,19852,20413,20785,20960,20929,20688,20234,19569,18696,
        17621,16354,14908,13297,11540,9657,7669,5600,3476,1321,-837,-2972,-5059,-7073,-8990,-10789,-12449,-13953,-15285,-16434,-17389,-18144,-18695,-19042,
        -19185,-19129,-18881,-18450,-17847,-17084,-16175,-15135,-13980,-12725,-11388,-9983,-8528,-7037,-5525,-4006,-2492,-996,472,1904,3290,4625,5902,7117,
        8268,9351,10365,11311,12187,12996,13739,14417,15035,15593,16095,16545,16946,17300,17612,17883,18117,18317,18486,18624,18734,18819,18878,18913,
        18925,18913,18878,18819,18734,18624,18486,18317,18117,17883,17612,17300,16946,16545,16095,15593,15035,14417,13739,12996,12187,11311,10365,9351,
        8268,7117,5902,4625,3290,1904,472,-996,-2492,-4006,-5525,-7037,-8528,-9983,-11388,-12725,-13980,-15135,-16175,-17084,-17847,-18450,-18881,-19129,
        -19185,-19042,-18695,-18144,-17389,-16434,-15285,-13953,-12449,-10789,-8990,-7073,-5059,-2972,-837,1321,3476,3476,7669,11540,14908,17621,19569,20688,
        20960,20413,19115,17168,14693,11827,8709,5473,2239,-888,-3827,-6516,-8916,-11007,-12787,-14267,-15469,-16421,-17157,-17709,-18113,-18398,-18590,-18711,
        -18778,-18799,-18778,-18711,-18590,-18398,-18113,-17709,-17157,-16421,-15469,-14267,-12787,-11007,-8916,-6516,-3827,-888,2239,5473,8709,11827,14693,17168,
        19115,20413,20960,20688,19569,17621,14908,11540,7669,3476,-837,-5059,-8990,-12449,-15285,-17389,-18695,-19185,-18881,-17847,-16175,-13980,-11388,-8528,
        -5525,-2492,472,3290,5902,8268,10365,12187,13739,15035,16095,16946,17612,18117,18486,18734,18878,18925,18878,18734,18486,18117,17612,16946,
        16095,15035,13739,12187,10365,8268,5902,3290,472,-2492,-5525,-8528,-11388,-13980,-16175,-17847,-18881,-19185,-18695,-17389,-15285,-12449,-8990,-5059,
        -837,3476,3476,11540,17621,20688,20413,17168,11827,5473,-888,-6516,-11007,-14267,-16421,-17709,-18398,-18711,-18799,-18711,-18398,-17709,-16421,-14267,
        -11007,-6516,-888,5473,11827,17168,20413,20688,17621,11540,3476,-5059,-12449,-17389,-19185,-17847,-13980,-8528,-2492,3290,8268,12187,15035,16946,
        18117,18734,18925,18734,18117,16946,15035,12187,8268,3290,-2492,-8528,-13980,-17847,-19185,-17389,-12449,-5059,3476,3473,17542,20448,11889,-946,
        -11045,-16351,-18385,-18872,-18385,-16351,-11045,-946,11889,20448,17542,3473,-12368,-19217,-14047,-2434,8310,14962,18103,19002,18103,14962,8310,-2434,
        -14047,-19217,-12368,3473,3045,13752,4808,-14802,-24649,-14802,4808,13752,3045,-11892,-8267,12941,25478,12941,-8267,-11892,3045,1315,-5592,-9224,
        -5592,1315,5592,6594,5592,1315,
      },
      {
        3471,5856,8161,10353,12397,14264,15927,17363,18554,19485,20147,20534,20646,20489,20069,19399,18496,17377,16065,14583,12955,11207,9366,7457,
        5505,3536,1570,-369,-2263,-4095,-5849,-7514,-9079,-10536,-11879,-13104,-14211,-15199,-16070,-16827,-17474,-18018,-18465,-18820,-19093,-19291,-19422,-19494,
        -19514,-19492,-19433,-19347,-19238,-19115,-18982,-18846,-18711,-18581,-18461,-18353,-18261,-18188,-18134,-18101,-18090,-18101,-18134,-18188,-18261,-18353,-18461,-18581,
        -18711,-18846,-18982,-19115,-19238,-19347,-19433,-19492,-19514,-19494,-19422,-19291,-19093,-18820,-18465,-18018,-17474,-16827,-16070,-15199,-14211,-13104,-11879,-10536,
        -9079,-7514,-5849,-4095,-2263,-369,1570,3536,5505,7457,9366,11207,12955,14583,16065,17377,18496,19399,20069,20489,20646,20534,20147,19485,
        18554,17363,15927,14264,12397,10353,8161,5856,3471,1044,-1389,-3790,-6123,-8354,-10448,-12377,-14112,-15633,-16918,-17954,-18731,-19243,-19490,-19473,
        -19201,-18685,-17939,-16980,-15827,-14504,-13032,-11435,-9737,-7963,-6136,-4279,-2413,-559,1267,3047,4767,6414,7980,9455,10834,12112,13287,14359,
        15328,16196,16967,17644,18233,18739,19168,19526,19821,20058,20245,20388,20492,20565,20612,20637,20647,20645,20635,20621,20605,20591,20579,20571,
        20569,20571,20579,20591,20605,20621,20635,20645,20647,20637,20612,20565,20492,20388,20245,20058,19821,19526,19168,18739,18233,17644,16967,16196,
        15328,14359,13287,12112,10834,9455,7980,6414,4767,3047,1267,-559,-2413,-4279,-6136,-7963,-9737,-11435,-13032,-14504,-15827,-16980,-17939,-18685,
        -19201,-19473,-19490,-19243,-18731,-17954,-16918,-15633,-14112,-12377,-10448,-8354,-6123,-3790,-1389,1044,3471,3471,8161,12397,15927,18554,20147,20646,
        20069,18496,16065,12955,9366,5505,1570,-2263,-5849,-9079,-11879,-14211,-16070,-17474,-18465,-19093,-19422,-19514,-19433,-19238,-18982,-18711,-18461,-18261,
        -18134,-18090,-18134,-18261,-18461,-18711,-18982,-19238,-19433,-19514,-19422,-19093,-18465,-17474,-16070,-14211,-11879,-9079,-5849,-2263,1570,5505,9366,12955,
        16065,18496,20069,20646,20147,18554,15927,12397,8161,3471,-1389,-6123,-10448,-14112,-16918,-18731,-19490,-19201,-17939,-15827,-13032,-9737,-6136,-2413,
        1267,4767,7980,10834,13287,15328,16967,18233,19168,19821,20245,20492,20612,20647,20635,20605,20579,20569,20579,20605,20635,20647,20612,20492,
        20245,19821,19168,18233,16967,15328,13287,10834,7980,4767,1267,-2413,-6136,-9737,-13032,-15827,-17939,-19201,-19490,-18731,-16918,-14112,-10448,-6123,
        -1389,3471,3471,12397,18554,20646,18496,12955,5505,-2263,-9079,-14211,-17474,-19093,-19514,-19238,-18711,-18261,-18090,-18261,-18711,-19238,-19514,-19093,
        -17474,-14211,-9079,-2263,5505,12955,18496,20646,18554,12397,3471,-6123,-14113,-18731,-19201,-15827,-9737,-2413,4767,10834,15328,18233,19821,20492,
        20647,20605,20569,20605,20647,20492,19821,18233,15328,10834,4767,-2413,-9737,-15827,-19201,-18731,-14113,-6123,3471,3463,18349,18590,5663,-9230,
        -17567,-19338,-18680,-18272,-18680,-19338,-17567,-9230,5663,18590,18349,3463,-13901,-19285,-9910,4918,15435,19634,20612,20766,20612,19634,15435,4918,
        -9910,-19285,-13901,3463,2634,9110,-773,-17740,-25738,-17740,-773,9110,2634,-8574,-3738,17204,29490,17204,-3738,-8574,2634,379,-9022,-13138,
        -9022,379,9022,12380,9022,379,
      },
      {
        3219,5869,8409,10793,12975,14916,16582,17944,18981,19679,20030,20034,19697,19032,18059,16801,15288,13552,11627,9551,7361,5095,2790,480,
        -1800,-4020,-6154,-8178,-10070,-11816,-13402,-14820,-16064,-17132,-18026,-18748,-19305,-19706,-19959,-20075,-20068,-19949,-19732,-19430,-19055,-18622,-18141,-17626,
        -17086,-16533,-15976,-15423,-14884,-14364,-13870,-13408,-12983,-12599,-12259,-11966,-11724,-11533,-11396,-11314,-11286,-11314,-11396,-11533,-11724,-11966,-12259,-12599,
        -12983,-13408,-13870,-14364,-14884,-15423,-15976,-16533,-17086,-17626,-18141,-18622,-19055,-19430,-19732,-19949,-20068,-20075,-19959,-19706,-19305,-18748,-18026,-17132,
        -16064,-14820,-13402,-11816,-10070,-8178,-6154,-4020,-1800,480,2790,5095,7361,9551,11627,13552,15288,16801,18059,19032,19697,20034,20030,19679,
        18981,17944,16582,14916,12975,10793,8409,5869,3219,510,-2207,-4880,-7460,-9898,-12150,-14176,-15942,-17418,-18583,-19420,-19922,-20087,-19919,-19430,
        -18636,-17560,-16228,-14669,-12915,-11001,-8961,-6831,-4645,-2436,-235,1930,4031,6047,7957,9747,11402,12913,14275,15484,16540,17443,18200,18815,
        19296,19652,19894,20031,20075,20037,19928,19759,19541,19285,19000,18695,18380,18062,17748,17446,17160,16896,16659,16451,16277,16140,16040,15979,
        15959,15979,16040,16140,16277,16451,16659,16896,17160,17446,17748,18062,18380,18695,19000,19285,19541,19759,19928,20037,20075,20031,19894,19652,
        19296,18815,18200,17443,16540,15484,14275,12913,11402,9747,7957,6047,4031,1930,-235,-2436,-4645,-6831,-8961,-11001,-12915,-14669,-16228,-17560,
        -18636,-19430,-19919,-20087,-19922,-19420,-18583,-17418,-15942,-14176,-12150,-9898,-7460,-4880,-2207,510,3219,3219,8409,12975,16582,18981,20030,19697,
        18059,15288,11627,7361,2790,-1800,-6154,-10070,-13402,-16064,-18026,-19305,-19959,-20068,-19732,-19055,-18141,-17086,-15976,-14884,-13870,-12983,-12259,-11724,
        -11396,-11286,-11396,-11724,-12259,-12983,-13870,-14884,-15976,-17086,-18141,-19055,-19732,-20068,-19959,-19305,-18026,-16064,-13402,-10070,-6154,-1800,2790,7361,
        11627,15288,18059,19697,20030,18981,16582,12975,8409,3219,-2207,-7460,-12150,-15942,-18583,-19922,-19919,-18636,-16228,-12915,-8961,-4645,-235,4031,
        7957,11402,14275,16540,18200,19296,19894,20075,19928,19541,19000,18380,17748,17160,16659,16277,16040,15959,16040,16277,16659,17160,17748,18380,
        19000,19541,19928,20075,19894,19296,18200,16540,14275,11402,7957,4031,-235,-4645,-8961,-12915,-16228,-18636,-19919,-19922,-18583,-15942,-12150,-7460,
        -2207,3219,3219,12975,18981,19697,15288,7361,-1800,-10070,-16064,-19305,-20068,-19055,-17086,-14884,-12983,-11724,-11286,-11724,-12983,-14884,-17086,-19055,
        -20068,-19305,-16064,-10070,-1800,7361,15288,19697,18981,12975,3219,-7460,-15942,-19922,-18636,-12915,-4645,4031,11402,16540,19296,20075,19541,18380,
        17160,16277,15959,16277,17160,18380,19541,20075,19296,16540,11402,4031,-4645,-12915,-18636,-19922,-15942,-7460,3219,3195,18504,15518,-1445,-16415,
        -20270,-16688,-12919,-11693,-12919,-16688,-20270,-16415,-1445,15518,18504,3195,-15445,-18834,-5043,11756,19538,19111,17081,16409,17081,19111,19538,11756,
        -5043,-18834,-15445,3195,1748,3413,-5087,-15665,-20009,-15665,-5087,3413,1748,-4552,-19,16804,26725,16804,-19,-4552,1748,-805,-9528,-12670,
        -9528,-805,9528,14280,9528,-805,
      },
      {
        2896,5814,8586,11147,13439,15411,17022,18238,19036,19405,19343,18859,17971,16707,15102,13197,11040,8680,6172,3567,919,-1723,-4310,-6799,
        -9151,-11334,-13320,-15088,-16622,-17913,-18956,-19751,-20305,-20627,-20729,-20627,-20338,-19882,-19278,-18547,-17709,-16785,-15794,-14754,-13682,-12594,-11505,-10428,
        -9373,-8351,-7370,-6437,-5559,-4740,-3984,-3295,-2674,-2124,-1646,-1240,-907,-648,-463,-352,-315,-352,-463,-648,-907,-1240,-1646,-2124,
        -2674,-3295,-3984,-4740,-5559,-6437,-7370,-8351,-9373,-10428,-11505,-12594,-13682,-14754,-15794,-16785,-17709,-18547,-19278,-19882,-20338,-20627,-20729,-20627,
        -20305,-19751,-18956,-17913,-16622,-15088,-13320,-11334,-9151,-6799,-4310,-1723,919,3567,6172,8680,11040,13197,15102,16707,17971,18859,19343,19405,
        19036,18238,17022,15411,13439,11147,8586,5814,2896,-103,-3112,-6062,-8886,-11519,-13903,-15987,-17728,-19091,-20051,-20596,-20721,-20432,-19745,-18685,
        -17282,-15574,-13606,-11423,-9073,-6608,-4075,-1523,1004,3465,5822,8044,10103,11977,13652,15114,16358,17383,18191,18789,19185,19392,19425,19298,
        19029,18635,18135,17544,16881,16161,15401,14615,13817,13017,12229,11460,10721,10019,9360,8749,8192,7693,7255,6880,6571,6329,6155,6050,
        6015,6050,6155,6329,6571,6880,7255,7693,8192,8749,9360,10019,10721,11460,12229,13017,13817,14615,15401,16161,16881,17544,18135,18635,
        19029,19298,19425,19392,19185,18789,18191,17383,16358,15114,13652,11977,10103,8044,5822,3465,1004,-1523,-4075,-6608,-9073,-11423,-13606,-15574,
        -17282,-18685,-19745,-20432,-20721,-20596,-20051,-19091,-17728,-15987,-13903,-11519,-8886,-6062,-3112,-103,2896,2896,8586,13439,17022,19036,19343,17971,
        15102,11040,6172,919,-4310,-9151,-13320,-16622,-18956,-20305,-20729,-20338,-19278,-17709,-15794,-13682,-11505,-9373,-7370,-5559,-3984,-2674,-1646,-907,
        -463,-315,-463,-907,-1646,-2674,-3984,-5559,-7370,-9373,-11505,-13682,-15794,-17709,-19278,-20338,-20729,-20305,-18956,-16622,-13320,-9151,-4310,919,
        6172,11040,15102,17971,19343,19036,17022,13439,8586,2896,-3112,-8886,-13903,-17728,-20051,-20721,-19745,-17282,-13606,-9073,-4075,1004,5822,10103,
        13652,16358,18191,19185,19425,19029,18135,16881,15401,13817,12229,10721,9360,8192,7255,6571,6155,6015,6155,6571,7255,8192,9360,10721,
        12229,13817,15401,16881,18135,19029,19425,19185,18191,16358,13652,10103,5822,1004,-4075,-9073,-13606,-17282,-19745,-20721,-20051,-17728,-13903,-8886,
        -3112,2896,2896,13439,19036,17971,11040,919,-9151,-16622,-20305,-20338,-17709,-13682,-9373,-5559,-2674,-907,-315,-907,-2674,-5559,-9373,-13682,
        -17709,-20338,-20305,-16622,-9151,919,11040,17971,19036,13439,2896,-8886,-17728,-20721,-17282,-9073,1004,10103,16358,19185,19029,16881,13817,10721,
        8192,6571,6015,6571,8192,10721,13817,16881,19029,19185,16358,10103,1004,-9073,-17282,-20721,-17728,-8886,2896,2832,18032,11546,-8433,-21046,
        -18099,-8564,-2555,-1133,-2555,-8564,-18099,-21046,-8433,11546,18032,2832,-16672,-17706,172,17108,19520,12925,8035,6941,8035,12925,19520,17108,
        172,-17706,-16672,2832,541,-2281,-7257,-9013,-8699,-9013,-7257,-2281,541,-374,2420,11669,17292,11669,2420,-374,541,-1878,-7014,-8041,
        -7014,-1878,7014,11797,7014,-1878,
      },
      {
        2749,5939,8935,11654,14023,15977,17468,18458,18927,18870,18295,17228,15705,13774,11495,8932,6155,3238,253,-2727,-5637,-8413,-11001,-13355,
        -15437,-17217,-18675,-19801,-20591,-21052,-21194,-21036,-20599,-19910,-18998,-17893,-16626,-15229,-13731,-12161,-10547,-8912,-7280,-5670,-4098,-2579,-1124,257,
        1558,2774,3902,4940,5888,6747,7518,8204,8807,9331,9778,10151,10452,10683,10847,10945,10978,10945,10847,10683,10452,10151,9778,9331,
        8807,8204,7518,6747,5888,4940,3902,2774,1558,257,-1124,-2579,-4098,-5670,-7280,-8912,-10547,-12161,-13731,-15229,-16626,-17893,-18998,-19910,
        -20599,-21036,-21194,-21052,-20591,-19801,-18675,-17217,-15437,-13355,-11001,-8413,-5637,-2727,253,3238,6155,8932,11495,13774,15705,17228,18295,18870,
        18927,18458,17468,15977,14023,11654,8935,5939,2749,-546,-3855,-7086,-10148,-12960,-15446,-17541,-19193,-20364,-21031,-21184,-20829,-19987,-18689,-16979,
        -14911,-12543,-9942,-7175,-4311,-1416,1445,4214,6837,9269,11472,13416,15079,16448,17517,18286,18762,18960,18895,18589,18065,17349,16467,15447,
        14314,13094,11811,10489,9147,7804,6477,5180,3926,2724,1583,509,-492,-1416,-2262,-3026,-3710,-4311,-4830,-5268,-5626,-5903,-6100,-6218,
        -6258,-6218,-6100,-5903,-5626,-5268,-4830,-4311,-3710,-3026,-2262,-1416,-492,509,1583,2724,3926,5180,6477,7804,9147,10489,11811,13094,
        14314,15447,16467,17349,18065,18589,18895,18960,18762,18286,17517,16448,15079,13416,11472,9269,6837,4214,1445,-1416,-4311,-7175,-9942,-12543,
        -14911,-16979,-18689,-19987,-20829,-21184,-21031,-20364,-19193,-17541,-15446,-12960,-10148,-7086,-3855,-546,2749,2749,8935,14023,17468,18927,18295,15705,
        11495,6155,253,-5637,-11001,-15437,-18675,-20591,-21194,-20599,-18998,-16626,-13731,-10547,-7280,-4098,-1124,1558,3902,5888,7518,8807,9778,10452,
        10847,10978,10847,10452,9778,8807,7518,5888,3902,1558,-1124,-4098,-7280,-10547,-13731,-16626,-18998,-20599,-21194,-20591,-18675,-15437,-11001,-5637,
        253,6155,11495,15705,18295,18927,17468,14023,8935,2749,-3855,-10148,-15446,-19193,-21031,-20829,-18689,-14911,-9942,-4311,1445,6837,11472,15079,
        17517,18762,18895,18065,16467,14314,11811,9147,6477,3926,1583,-492,-2262,-3710,-4830,-5626,-6100,-6258,-6100,-5626,-4830,-3710,-2262,-492,
        1583,3926,6477,9147,11811,14314,16467,18065,18895,18762,17517,15079,11472,6837,1445,-4311,-9942,-14911,-18689,-20829,-21031,-19193,-15446,-10148,
        -3855,2749,2749,14023,18927,15705,6155,-5637,-15437,-20591,-20599,-16626,-10547,-4098,1558,5888,8808,10452,10978,10452,8808,5888,1558,-4098,
        -10547,-16626,-20599,-20591,-15437,-5637,6155,15705,18927,14023,2749,-10148,-19193,-20829,-14911,-4311,6837,15079,18762,18065,14314,9147,3926,-492,
        -3710,-5626,-6258,-5626,-3710,-492,3926,9147,14314,18065,18762,15079,6837,-4311,-14911,-20829,-19193,-10148,2749,2598,16999,7181,-14123,-22023,
        -11219,3049,9005,9495,9005,3049,-11219,-22023,-14123,7181,16999,2598,-17139,-15747,5254,20214,15216,2244,-3993,-4530,-3993,2244,15216,20214,
        5254,-15747,-17139,2598,-699,-6684,-6909,163,4810,163,-6909,-6684,-699,3260,3464,3261,3477,3261,3464,3260,-699,-2421,-2358,-913,
        -2358,-2421,2358,5755,2358,-2421,
      },
      {
        3000,6462,9674,12530,14936,16817,18116,18797,18843,18264,17086,15358,13141,10516,7569,4396,1095,-2236,-5502,-8617,-11501,-14088,-16324,-18166,
        -19588,-20576,-21127,-21253,-20973,-20317,-19321,-18025,-16474,-14714,-12791,-10751,-8636,-6487,-4340,-2227,-174,1795,3662,5413,7039,8534,9895,11124,
        12222,13194,14048,14791,15432,15978,16441,16827,17147,17409,17619,17785,17913,18007,18072,18109,18122,18109,18072,18007,17913,17785,17619,17409,
        17147,16827,16441,15978,15432,14791,14048,13194,12222,11124,9895,8534,7039,5413,3662,1795,-174,-2227,-4340,-6487,-8636,-10751,-12791,-14714,
        -16474,-18025,-19321,-20317,-20973,-21253,-21127,-20576,-19588,-18166,-16324,-14088,-11501,-8617,-5502,-2236,1095,4396,7569,10516,13141,15358,17086,18264,
        18843,18797,18116,16817,14936,12530,9674,6462,3000,-599,-4214,-7727,-11021,-13991,-16541,-18593,-20087,-20982,-21260,-20922,-19992,-18509,-16530,-14127,
        -11381,-8381,-5217,-1982,1234,4347,7282,9974,12366,14417,16097,17385,18277,18776,18895,18656,18087,17222,16096,14750,13222,11553,9781,7941,
        6068,4189,2333,522,-1226,-2896,-4475,-5955,-7331,-8599,-9759,-10811,-11758,-12604,-13352,-14008,-14578,-15065,-15476,-15815,-16086,-16293,-16438,-16525,
        -16553,-16525,-16438,-16293,-16086,-15815,-15476,-15065,-14578,-14008,-13352,-12604,-11758,-10811,-9759,-8599,-7331,-5955,-4475,-2896,-1226,522,2333,4189,
        6068,7941,9781,11553,13222,14750,16096,17222,18087,18656,18895,18776,18277,17385,16097,14417,12366,9974,7282,4347,1234,-1982,-5217,-8381,
        -11381,-14127,-16530,-18509,-19992,-20922,-21260,-20982,-20087,-18593,-16541,-13991,-11021,-7727,-4214,-599,3000,3000,9674,14936,18116,18843,17086,13141,
        7569,1095,-5502,-11501,-16324,-19588,-21127,-20973,-19321,-16474,-12791,-8636,-4340,-174,3662,7039,9895,12222,14048,15432,16441,17147,17619,17913,
        18072,18122,18072,17913,17619,17147,16441,15432,14048,12222,9895,7039,3662,-174,-4340,-8636,-12791,-16474,-19321,-20973,-21127,-19588,-16324,-11501,
        -5502,1095,7569,13141,17086,18843,18116,14936,9674,3000,-4214,-11021,-16541,-20087,-21260,-19992,-16530,-11381,-5217,1234,7282,12366,16097,18277,
        18895,18087,16096,13222,9781,6068,2333,-1226,-4475,-7331,-9759,-11758,-13352,-14578,-15476,-16086,-16438,-16553,-16438,-16086,-15476,-14578,-13352,-11758,
        -9759,-7331,-4475,-1226,2333,6068,9781,13222,16096,18087,18895,18277,16097,12366,7282,1234,-5217,-11381,-16530,-19992,-21260,-20087,-16541,-11021,
        -4214,3000,3000,14936,18844,13142,1095,-11501,-19588,-20973,-16474,-8637,-174,7039,12221,15432,17148,17913,18122,17913,17148,15432,12221,7039,
        -174,-8637,-16474,-20973,-19588,-11501,1095,13142,18844,14936,3000,-11021,-20087,-19992,-11381,1234,12366,18277,18088,13222,6067,-1226,-7331,-11758,
        -14578,-16086,-16553,-16086,-14578,-11758,-7331,-1226,6067,13222,18088,18277,12366,1234,-11381,-19992,-20087,-11021,3000,2674,15446,3018,-17416,-18980,
        -1199,14715,17433,15689,17433,14715,-1199,-18980,-17416,3018,15446,2674,-16412,-12904,9616,20666,7567,-10225,-15036,-13615,-15036,-10225,7567,20666,
        9616,-12904,-16412,2674,-1626,-8667,-4354,8736,15956,8736,-4354,-8667,-1626,5651,3342,-5720,-10680,-5720,3342,5651,-1626,-2132,2785,6070,
        2785,-2132,-2785,-1806,-2785,-2132,
      },
      {
        3733,7468,10885,13850,16251,17998,19029,19312,18845,17656,15800,13357,10427,7125,3576,-89,-3742,-7262,-10536,-13466,-15973,-17994,-19489,-20435,
        -20830,-20687,-20039,-18927,-17404,-15533,-13377,-11004,-8481,-5872,-3236,-628,1906,4324,6594,8692,10600,12306,13806,15100,16194,17097,17821,18381,
        18793,19073,19241,19312,19305,19236,19121,18973,18805,18631,18459,18298,18157,18041,17954,17901,17883,17901,17954,18041,18157,18298,18459,18631,
        18805,18973,19121,19236,19305,19312,19241,19073,18793,18381,17821,17097,16194,15100,13806,12306,10600,8692,6594,4324,1906,-628,-3236,-5872,
        -8481,-11004,-13377,-15533,-17404,-18927,-20039,-20687,-20830,-20435,-19489,-17994,-15973,-13466,-10536,-7262,-3742,-89,3576,7125,10427,13357,15800,17656,
        18845,19312,19029,17998,16251,13850,10885,7468,3733,-175,-4104,-7899,-11415,-14517,-17088,-19037,-20295,-20826,-20620,-19698,-18107,-15919,-13223,-10128,
        -6748,-3207,375,3880,7201,10242,12922,15179,16966,18255,19037,19317,19115,18464,17406,15990,14272,12309,10159,7879,5522,3138,772,-1538,
        -3760,-5867,-7839,-9661,-11325,-12826,-14163,-15341,-16366,-17246,-17992,-18616,-19130,-19547,-19879,-20138,-20337,-20485,-20594,-20670,-20723,-20757,-20778,-20789,
        -20793,-20789,-20778,-20757,-20723,-20670,-20594,-20485,-20337,-20138,-19879,-19547,-19130,-18616,-17992,-17246,-16366,-15341,-14163,-12826,-11325,-9661,-7839,-5867,
        -3760,-1538,772,3138,5522,7879,10159,12309,14272,15990,17406,18464,19115,19317,19037,18255,16966,15179,12922,10242,7201,3880,375,-3207,
        -6748,-10128,-13223,-15919,-18107,-19698,-20620,-20826,-20295,-19037,-17088,-14517,-11415,-7899,-4104,-175,3733,3733,10885,16251,19029,18845,15800,10427,
        3576,-3742,-10536,-15973,-19489,-20830,-20039,-17404,-13377,-8481,-3236,1906,6594,10600,13806,16194,17821,18793,19241,19305,19121,18805,18459,18157,
        17954,17883,17954,18157,18459,18805,19121,19305,19241,18793,17821,16194,13806,10600,6594,1906,-3236,-8481,-13377,-17404,-20039,-20830,-19489,-15973,
        -10536,-3742,3576,10427,15800,18845,19029,16251,10885,3733,-4104,-11415,-17088,-20295,-20620,-18107,-13223,-6748,375,7201,12922,16966,19037,19115,
        17406,14272,10159,5522,772,-3760,-7839,-11325,-14163,-16366,-17992,-19130,-19879,-20337,-20594,-20723,-20778,-20793,-20778,-20723,-20594,-20337,-19879,-19130,
        -17992,-16366,-14163,-11325,-7839,-3760,772,5522,10159,14272,17406,19115,19037,16966,12922,7201,375,-6748,-13223,-18107,-20620,-20295,-17088,-11415,
        -4104,3733,3733,16250,18846,10428,-3743,-15974,-20829,-17404,-8482,1905,10600,16194,18792,19305,18806,18157,17883,18157,18806,19305,18792,16194,
        10600,1905,-8482,-17404,-20829,-15974,-3743,10428,18846,16250,3733,-11414,-20295,-18108,-6748,7202,16965,19114,14273,5523,-3761,-11325,-16365,-19130,
        -20337,-20723,-20792,-20723,-20337,-19130,-16365,-11325,-3761,5523,14273,19114,16965,7202,-6748,-18108,-20295,-11414,3733,3086,13337,-401,-17592,-12525,
        9238,22576,19154,14275,19154,22576,9238,-12525,-17592,-401,13337,3086,-14223,-9327,12596,18489,-1511,-20916,-20999,-16241,-20999,-20916,-1511,18489,
        12596,-9327,-14223,3086,-1961,-7701,-566,13594,20658,13594,-566,-7701,-1961,6292,2497,-12186,-20597,-12186,2497,6292,-1961,-996,6527,10227,
        6527,-996,-6527,-8235,-6527,-996,
      },
      {
        4827,8834,12441,15486,17833,19380,20064,19863,18797,16923,14337,11160,7539,3636,-383,-4351,-8112,-11524,-14467,-16846,-18593,-19669,-20062,-19789,
        -18887,-17414,-15446,-13066,-10368,-7446,-4394,-1300,1754,4696,7464,10009,12293,14290,15986,17375,18461,19256,19777,20045,20088,19931,19604,19136,
        18555,17889,17162,16398,15619,14843,14087,13366,12693,12077,11528,11052,10656,10343,10118,9982,9936,9982,10118,10343,10656,11052,11528,12077,
        12693,13366,14087,14843,15619,16398,17162,17889,18555,19136,19604,19931,20088,20045,19777,19256,18461,17375,15986,14290,12293,10009,7464,4696,
        1754,-1300,-4394,-7446,-10368,-13066,-15446,-17414,-18887,-19789,-20062,-19669,-18593,-16846,-14467,-11524,-8112,-4351,-383,3636,7539,11160,14337,16923,
        18797,19863,20064,19380,17833,15486,12441,8834,4827,604,-3644,-7722,-11446,-14651,-17196,-18974,-19915,-19990,-19209,-17619,-15305,-12377,-8970,-5236,
        -1331,2587,6366,9870,12978,15595,17648,19091,19902,20084,19664,18684,17204,15295,13034,10503,7785,4959,2099,-726,-3458,-6049,-8457,-10654,
        -12619,-14340,-15814,-17044,-18039,-18812,-19380,-19764,-19984,-20064,-20024,-19888,-19676,-19409,-19104,-18780,-18450,-18129,-17828,-17557,-17325,-17137,-17000,-16916,
        -16887,-16916,-17000,-17137,-17325,-17557,-17828,-18129,-18450,-18780,-19104,-19409,-19676,-19888,-20024,-20064,-19984,-19764,-19380,-18812,-18039,-17044,-15814,-14340,
        -12619,-10654,-8457,-6049,-3458,-726,2099,4959,7785,10503,13034,15295,17204,18684,19664,20084,19902,19091,17648,15595,12978,9870,6366,2587,
        -1331,-5236,-8970,-12377,-15305,-17619,-19209,-19990,-19915,-18974,-17196,-14651,-11446,-7722,-3644,604,4827,4827,12441,17833,20064,18797,14337,7539,
        -383,-8112,-14467,-18593,-20062,-18887,-15446,-10368,-4394,1754,7464,12293,15986,18461,19777,20088,19604,18555,17162,15619,14087,12693,11528,10656,
        10118,9936,10118,10656,11528,12693,14087,15619,17162,18555,19604,20088,19777,18461,15986,12293,7464,1754,-4394,-10368,-15446,-18887,-20062,-18593,
        -14467,-8112,-383,7539,14337,18797,20064,17833,12441,4827,-3644,-11446,-17196,-19915,-19209,-15305,-8970,-1331,6366,12978,17648,19902,19664,17204,
        13034,7785,2099,-3458,-8457,-12619,-15814,-18039,-19380,-19984,-20024,-19676,-19104,-18450,-17828,-17325,-17000,-16887,-17000,-17325,-17828,-18450,-19104,-19676,
        -20024,-19984,-19380,-18039,-15814,-12619,-8457,-3458,2099,7785,13034,17204,19664,19902,17648,12978,6366,-1331,-8970,-15305,-19209,-19915,-17196,-11446,
        -3644,4827,4827,17831,18798,7542,-8113,-18595,-18885,-10367,1752,12292,18464,20088,18553,15618,12695,10656,9934,10656,12695,15618,18553,20088,
        18464,12292,1752,-10367,-18885,-18595,-8113,7542,18798,17831,4827,-11444,-19916,-15307,-1330,12981,19900,17202,7787,-3457,-12621,-18040,-19982,-19675,
        -18453,-17325,-16885,-17325,-18453,-19675,-19982,-18040,-12621,-3457,7787,17202,19900,12981,-1330,-15307,-19916,-11444,4827,3643,10569,-2710,-14583,-4225,
        16947,23741,13027,5127,13027,23741,16947,-4225,-14583,-2710,10569,3643,-10627,-5406,13566,14140,-9602,-26507,-19296,-10481,-19296,-26507,-9602,14140,
        13566,-5406,-10627,3643,-1603,-4175,3076,12906,17079,12906,3076,-4175,-1603,5072,1398,-13803,-22822,-13803,1398,5072,-1603,634,7473,9934,
        7473,634,-7473,-11203,-7473,634,
      },
    }
};

static const uint16_t WTE_LEVEL_OFF[6] = { 0u, 257u, 386u, 451u, 484u, 501u };
static const uint8_t VE_TABLE[NUM_VOICES] = { 0, 0, 0, 0, 1, 1, 1, 0 };

/* the shape of a harmonic voice, for its icon (t: 0..1 of a cycle) */
static int ve_icon_wave(uint32_t v, float t) {
    float y = 0.0f, w = 6.2831853f * t;
    if (v == VE_SINE) y = sinf(w);
    else if (v == VE_LUSH) { float u = t - (float)(int)t; y = 1.0f - 2.0f * u; }
    else                   { y = 0.4f * sinf(w) + 0.7f * sinf(w * 1.5f) + 0.6f * sinf(w * 2.0f); y *= 0.6f; }
    y = y > 1.0f ? 1.0f : (y < -1.0f ? -1.0f : y);
    return (int)(29490.0f * y);
}

/* Bank E icons: the voice's own wavetable, drawn as a stack of its frames
 * in perspective (a waterfall), with the frame it's playing lit up and
 * moving as the sound moves through the table */
static uint32_t g_icon_t = 0u;                  /* WAVES and POCKET icons: a frame every 150 ms while you play */
static void draw_bank_e_icon(uint32_t v, int is_on, int step, uint16_t card_bg) {
    (void)step; (void)card_bg;
    const int sh = is_on ? (int)((g_icon_t * 23u) & 255u) : 0;      /* the waves roll along */
    const float shf = (float)sh / 256.0f;
    const uint16_t hi = is_on ? VOICE_COLORS_E[v] : 0x9CF3u;
    const uint16_t lo = is_on ? (uint16_t)((VOICE_COLORS_E[v] >> 1) & 0x7BEFu) : 0x4A49u;
    ic_box(3, 2, 36, 36, is_on ? 0x2124u : 0x2945u);
    int tb = VE_TABLE[v];
    float pos = (v == VE_SQUARE || v == VE_MAXFOLD) ? 1.0f : ((v == VE_GROWL || v == VE_FOLDS) && is_on ? g_e_pos[v] : 0.0f);
    int cur = (int)(pos * 15.0f + 0.5f);
    for (int f = 15; f >= 0; f -= 3) {                         /* back to front: frames 15, 12 .. 0 */
        int k = f / 3;                                         /* 5 .. 0 */
        int x0 = 6 + k, y0 = 27 - k * 3, w = 25;
        const int16_t *fr = WTE[tb][f];
        int py = -100;
        for (int x = 0; x < w; x++) {
            int s = (v == VE_SINE || v == VE_LUSH || v >= VE_FIRST_ADD) ? ve_icon_wave(v, (float)x / (float)w + 0.07f * (float)k + shf) : fr[((x * 256) / w + sh) & 255];
            int yy = y0 - (s * 9) / 32768;
            if (py > -100) { int a = py < yy ? py : yy, b = py < yy ? yy : py; ic_box(x0 + x, a, 1, b - a + 1, lo); }
            py = yy;
        }
    }
    {                                                          /* the frame playing, in front and bright */
        int f = cur;
        const int16_t *fr = WTE[tb][f];
        int x0 = 6, y0 = 26, w = 30, py = -100;
        for (int x = 0; x < w; x++) {
            int s = (v == VE_SINE || v == VE_LUSH || v >= VE_FIRST_ADD) ? ve_icon_wave(v, (float)x / (float)w + shf) : fr[((x * 256) / w + sh) & 255];
            int yy = y0 - (s * 11) / 32768;
            if (py > -100) { int a = py < yy ? py : yy, b = py < yy ? yy : py; ic_box(x0 + x, a, 1, b - a + 1, hi); }
            py = yy;
        }
    }
    int bx = 6 + (int)(pos * 29.0f);                          /* where in the table: a marker on a rail */
    ic_box(6, 34, 30, 1, lo);
    ic_box(bx, 33, 2, 3, hi);
}

/* RHYTHM: each beat drawn as its pattern - one row of lamps per drum it
 * uses, like the lamps on an old rhythm unit - with the step that is
 * playing lit up, and the beats of the bar along the bottom */
static void draw_bank_d_icon(uint32_t v, int is_on, uint16_t card_bg) {
    const uint16_t *LAMP = DRUM_LAMP;
    const Beat *b = &BEATS[v];
    int slot = (g_beat[0] == (int)v) ? 0 : ((g_beat[1] == (int)v) ? 1 : -1);
    int playing = is_on && slot >= 0;
    int cur = playing ? g_beat_step[slot] : -1;
    int grp = (b->spb == 2) ? 4 : b->spb;               /* lamps in groups (a beat; two for a beat in eighths) */
    int nb = b->len / grp;
    int lw = (b->len == 16) ? 2 : 3;                     /* each step's width */
    int lamp = 2;                                        /* a lit lamp's width */
    int gw = (b->len * lw + (nb - 1) * 2 <= 40) ? 2 : 1; /* a gap between the beats */
    int total = b->len * lw + (nb - 1) * gw;
    int x0 = (IC_W - total) / 2;
    int rows[NUM_DRUMS], nr = 0;
    for (int d = 0; d < NUM_DRUMS && nr < 5; d++) if (b->hits[d]) rows[nr++] = d;
    int rh = 4, rg = 2, y0 = 4 + (5 - nr) * (rh + rg) / 2;
    for (int s = 0; s < b->len; s++) {
        int x = x0 + s * lw + (s / grp) * gw;
        if (s == cur) ic_box(x - 1, y0 - 2, lamp + 2, nr * (rh + rg) + 2, 0x4A69u);   /* the playhead */
        for (int r = 0; r < nr; r++) {
            int d = rows[r], y = y0 + r * (rh + rg);
            int hit = (b->hits[d] >> s) & 1;
            if (hit) {
                uint16_t c = !is_on ? 0x9492u : ((s == cur) ? 0xFFFFu : LAMP[d]);
                int gh = (d == DR_SD && ((b->soft >> s) & 1u));          /* a ghost note: a short lamp */
                ic_box(x, y + (gh ? 2 : 0), lamp, gh ? rh - 2 : rh, c);
            } else {
                ic_box(x, y + 1, lamp, 2, !is_on ? 0x39E7u : 0x2945u);   /* an unlit lamp */
            }
        }
    }
    /* the beats of the bar: the one playing lights up */
    int by = 35, bw = 4, bx0 = (IC_W - (nb * bw + (nb - 1) * 3)) / 2;
    for (int k = 0; k < nb; k++) {
        int lit = playing && (cur / grp) == k;
        uint16_t c = !is_on ? 0x528Au : (lit ? (k == 0 ? 0xF8C3u : 0xFFE0u) : 0x4208u);
        ic_box(bx0 + k * (bw + 3), by, bw, 3, c);
    }
    (void)card_bg;
}

/* POCKET: a little picture of what each mode does */
static void draw_bank_p_icon(uint32_t v, int is_on, uint16_t card_bg) {
    uint16_t c = is_on ? PP_COLORS[v] : 0x8410u;
    uint16_t d = is_on ? 0x7BEFu : 0x4A69u;
    (void)card_bg;
    const int t = is_on ? (int)(g_icon_t & 0xFFFFu) : 0;       /* moves while you play */
    const uint16_t W = is_on ? 0xFFFFu : c;                   /* the bright one */
    if (v == PP_VIBRATO) {                    /* a wobbling wave */
        float py = 0.0f;
        for (int x = 3; x < 39; x++) {
            float y = 20.0f + 9.0f * sinf((float)(x + t * 3) * 0.55f) * (0.4f + 0.6f * sinf((float)(x + t) * 0.09f));
            if (x > 3) ic_line((float)(x - 1), py, (float)x, y, 2.0f, c);
            py = y;
        }
    } else if (v == PP_SWEEPER) {             /* stepping down through the octaves */
        for (int k = 0; k < 4; k++) ic_box(4 + k * 9, 6 + k * 8, 8, 3, (is_on && (t & 3) == k) ? W : c), ic_box(4 + k * 9, 9 + k * 8, 1, 24 - k * 8, d);
    } else if (v == PP_ARP || v == PP_FMARP) {  /* notes climbing, then again an octave up */
        for (int k = 0; k < 4; k++) {
            ic_box(4 + k * 4, 30 - k * 4, 3, 3, (is_on && (t & 7) == k) ? W : c);
            ic_box(22 + k * 4, 18 - k * 4, 3, 3, (is_on && (t & 7) == k + 4) ? W : c);
        }
        ic_line(4.0f, 35.0f, 38.0f, 35.0f, 1.0f, d);
        if (v == PP_FMARP) { ic_disc(32, 30, 4, 4, d); ic_disc(32, 30, 2, 2, c); }
    } else if (v == PP_CASCADE) {             /* notes tumbling through the octaves */
        for (int k = 0; k < 4; k++)
            for (int r = 0; r < 4; r++)
                if (((k + r + t) & 3) == 0 || ((k + r * 2 + t) & 3) == 1) ic_box(5 + k * 9, 6 + r * 8, 5, 4, (r & 1) ? c : d);
    } else if (v == PP_PLAIN) {               /* a chord, held straight */
        for (int k = 0; k < 3; k++) { ic_box(6, 9 + k * 9, 30, 5, c); ic_box(6, 14 + k * 9, 30, 1, d); }
        if (is_on && t) ic_box(6 + (t * 3) % 29, 8, 2, 25, W);                   /* a glint passing over the held chord */
    } else if (v == PP_GLIDER) {              /* a slide from one note to the next */
        float py = 32.0f;
        for (int x = 4; x < 38; x++) {
            float t = (float)(x - 4) / 33.0f;
            float y = 32.0f - 22.0f * (t * t * (3.0f - 2.0f * t));
            if (x > 4) ic_line((float)(x - 1), py, (float)x, y, 2.0f, c);
            py = y;
        }
        ic_box(4, 31, 4, 3, d); ic_box(34, 9, 4, 3, d);
        if (is_on && t) {                                                         /* a note riding the slide */
            int x = 4 + (t * 3) % 34;
            float tt = (float)(x - 4) / 33.0f;
            int y = (int)(32.0f - 22.0f * (tt * tt * (3.0f - 2.0f * tt)));
            ic_box(x - 1, y - 2, 4, 4, W);
        }
    } else {                                  /* the BUTTON: the waveform, or the octave jump */
        int m = g_pp_mode;
        if (pp_is_fm(m)) {
            uint16_t a = g_pp_wave[m] ? c : 0x8410u;
            float b = (is_on && (t & 1)) ? -3.0f : 0.0f;                         /* jumping */
            ic_line(21.0f, 32.0f + b, 21.0f, 9.0f + b, 2.0f, a);
            ic_line(21.0f, 8.0f + b, 13.0f, 17.0f + b, 2.0f, a);
            ic_line(21.0f, 8.0f + b, 29.0f, 17.0f + b, 2.0f, a);
        } else {
            int w = g_pp_wave[m];
            uint16_t a = PP_COLORS[m];
            float py = 20.0f;
            for (int x = 4; x < 38; x++) {
                float ph = (float)((x - 4 + t * 2) % 17) / 17.0f;
                float y = 20.0f - 10.0f * (w == 1 ? (ph < 0.5f ? 1.0f : -1.0f) : w == 2 ? (ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph) : w == 3 ? (2.0f * ph - 1.0f) : sinf(6.2832f * ph));
                if (x > 4) ic_line((float)(x - 1), py, (float)x, y, 2.0f, a);
                py = y;
            }
        }
    }
}

static void draw_bank_c_icon(uint32_t v, int is_on, int step, uint16_t card_bg) {
    const uint16_t muted = 0x8410u;
    const uint16_t dark = is_on ? COL_INK : COL_BORDER;
    #define ON(c) (is_on ? (c) : muted)
    if (v == VC_LOWREY) {                       /* a chain-link fence, cut open and splitting while you play */
        int g = is_on ? g_fence : 0;                             /* 0 = a small cut ... 6 = split wide open */
        const uint16_t wire = ON(0xBDF7u), wire_sh = ON(0x7BEFu), cut = ON(0xFFFFu);
        ic_box(3, 2, 36, 2, ON(0x6B4Du));                        /* the top rail */
        ic_box(3, 2, 36, 1, ON(0x9CF3u));
        ic_box(3, 4, 2, 34, ON(0x6B4Du)); ic_box(37, 4, 2, 34, ON(0x6B4Du));   /* posts */
        for (int y = 5; y < 38; y++) {
            float t = (float)(y - 22) / 17.0f; if (t < 0.0f) t = -t;
            float open = (1.6f + 1.9f * (float)g) * (1.0f - t);  /* how far the halves are pushed apart here */
            if (g == 0 && t > 0.3f) open = 0.0f;                 /* at rest: just a small cut */
            for (int x = 5; x < 37; x++) {
                float d = (float)(x - 21);
                float ad = d < 0.0f ? -d : d;
                if (open > 0.0f && ad < open) continue;          /* the hole */
                int xs = (int)(d < 0.0f ? (float)x + open : (float)x - open + 0.5f);   /* the mesh, pushed aside */
                int m1 = ((xs + y) % 8) == 0, m2 = ((xs - y + 80) % 8) == 0;   /* big diamonds */
                if (!(m1 || m2)) continue;
                int edge = open > 0.0f && ad < open + 1.5f;      /* a cut wire end, bright */
                ic_px(x, y, edge ? cut : (m1 && m2 ? wire_sh : wire));
            }
        }
        return;
    }
    if (v == VC_FUZZ) {                         /* a smiley face that glitches out completely while you play */
        int g = is_on ? g_fzg : 0;
        /* draw the face into the icon, then tear it apart by how far gone it is */
        ic_disc(21, 20, 14, 14, ON(COL_GOLD_HI));
        ic_disc(21, 20, 12, 12, ON(0xFFE0u));
        ic_box(15, 13, 3, 5, dark); ic_box(25, 13, 3, 5, dark);           /* eyes */
        for (int x = 13; x <= 29; x++) {                                  /* the smile */
            int d = x - 21, y = 24 + ((d * d) < 64 ? 2 - (d * d) / 32 : 0);
            ic_px(x, y, dark); ic_px(x, y + 1, dark);
        }
        if (g > 0 && is_on) {
            uint32_t h = 2246822519u * (uint32_t)(step + 3) + (uint32_t)g * 40503u;
            /* slices torn sideways, more and further as it goes */
            for (int y = 4; y < 37; y += 2) {
                h ^= h << 13; h ^= h >> 17; h ^= h << 5;
                if ((int)(h % 7u) >= g) continue;
                int off = (int)((h >> 8) % (uint32_t)(2 * g + 1)) - g;
                uint16_t row[IC_W];
                for (int x = 0; x < IC_W; x++) row[x] = g_blit_buf[y * IC_W + x];
                for (int x = 0; x < IC_W; x++) {
                    int sx = x - off;
                    g_blit_buf[y * IC_W + x] = (sx >= 0 && sx < IC_W) ? row[sx] : card_bg;
                    if (y + 1 < IC_H) g_blit_buf[(y + 1) * IC_W + x] = g_blit_buf[y * IC_W + x];
                }
            }
            /* colour split: cyan and magenta ghosts at the edges */
            if (g >= 2) {
                for (int y = 6; y < 35; y++) {
                    ic_px(7 - (g / 2), y, ((y + step) % 3 == 0) ? 0x07FFu : card_bg);
                    ic_px(35 + (g / 2), y, ((y + step) % 3 == 1) ? 0xF81Fu : card_bg);
                }
            }
            /* blocks of corrupted pixels */
            for (int k = 0; k < g * 2; k++) {
                h ^= h << 13; h ^= h >> 17; h ^= h << 5;
                static const uint16_t BAD[4] = { 0x07FFu, 0xF81Fu, 0x07E0u, 0xFFFFu };
                ic_box((int)(h % 36u) + 2, (int)((h >> 6) % 34u) + 2, 2 + (int)((h >> 12) % 5u), 1 + (int)((h >> 16) % 3u), BAD[(h >> 20) & 3u]);
            }
            if (g >= 6) {                                                 /* gone: just noise and a frown in the static */
                for (int y = 2; y < 38; y += 3) {
                    h ^= h << 13; h ^= h >> 17; h ^= h << 5;
                    ic_box((int)(h % 20u), y, 6 + (int)((h >> 9) % 16u), 1, (h >> 4) & 1u ? 0x6B6Du : 0xBDF7u);
                }
            }
        }
    } else if (v == VC_GLITCH) {                /* an old TV, its picture rolling and tearing */
        int play = is_on && g_any_note_held;
        ic_line(21.0f, 10.0f, 13.0f, 2.0f, 1.0f, ON(COL_DIM));    /* rabbit ears */
        ic_line(21.0f, 10.0f, 30.0f, 3.0f, 1.0f, ON(COL_DIM));
        ic_box(4, 10, 34, 27, ON(COL_TRUCK_SH));                 /* wooden box */
        ic_box(7, 13, 23, 20, ON(COL_INK));
        ic_box(8, 14, 21, 18, ON(COL_BUBBLE_SH));                /* the screen */
        ic_box(32, 15, 3, 3, ON(COL_GOLD)); ic_box(32, 21, 3, 3, ON(COL_GOLD));   /* dials */
        ic_box(31, 28, 5, 1, ON(COL_TRUCK_HI)); ic_box(31, 30, 5, 1, ON(COL_TRUCK_HI));
        ic_box(7, 37, 3, 2, ON(COL_INK)); ic_box(32, 37, 3, 2, ON(COL_INK));
        if (is_on) {
            static const uint16_t BAND[3] = { COL_LEAF_HI, 0xF81Fu, 0x07FFu };   /* green, magenta, cyan */
            uint32_t h = 2654435761u * (uint32_t)(step + 1) + (play ? 0u : 77u);
            for (int y = 14; y < 32; y++) {
                h ^= h << 13; h ^= h >> 17; h ^= h << 5;
                if (!play) { if ((y & 3) == 0) ic_box(9, y, 19, 1, 0x6B6Du); continue; }
                int off = (int)(h % 5u) - 2;
                if ((h >> 8) % 4u == 0u) ic_box(8 + (off > 0 ? off : 0), y, 21 - (off > 0 ? off : -off), 1, BAND[(h >> 12) % 3u]);
                else if ((h >> 8) % 4u == 1u) ic_box(8, y, 21, 1, 0xBDF7u);
            }
            if (play) ic_box(8, 14 + (step * 5) % 18, 21, 2, COL_TEXT);   /* the rolling bar */
        }
    } else if (v == VC_FLICKER) {               /* a hand holding up a lantern that lights up while you play */
        int gl = is_on ? g_tape : 0;                             /* 0 = dark ... 6 = blazing */
        int lit = gl > 0;
        int play = is_on && g_any_note_held;
        if (is_on && gl >= 3) {                                  /* rays of light, growing */
            int len = gl;
            static const int8_t DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
            for (int k = 0; k < 8; k++) {
                if (((k + step) & 1) && gl < 5) continue;
                int r0 = 14;
                for (int j = 0; j < len; j++)
                    ic_px(20 + DX[k] * (r0 + j), 22 + DY[k] * (r0 + j) * 9 / 10, (j & 1) ? COL_GOLD : COL_GOLD_HI);
            }
        }
        const uint16_t frame = ON(0x3186u), frame_hi = ON(0x6B4Du);
        ic_line(20.0f, 5.0f, 20.0f, 8.0f, 1.0f, ON(COL_DIM));    /* the bail */
        ic_box(14, 8, 13, 2, frame);                             /* the cap */
        ic_box(12, 10, 17, 2, frame_hi);
        uint16_t glass = !is_on ? muted : (!lit ? 0x4208u : (gl <= 1 ? 0x7A43u : (gl == 2 ? 0xBC66u : (gl <= 4 ? 0xFFB0u : 0xFFF8u))));   /* dark, then warming up */
        ic_box(11, 12, 19, 19, glass);                           /* the glass */
        ic_box(11, 12, 2, 19, frame); ic_box(28, 12, 2, 19, frame);   /* its frame */
        ic_box(11, 18, 19, 1, frame_hi);                         /* a thin crossbar */
        if (lit) {                                               /* the flame, a flickering teardrop */
            static const int8_t FJ[4] = { 1, 2, 0, 1 };
            int fh = 3 + gl + (play ? FJ[step & 3] : 0);         /* grows slowly from a small flame */
            if (fh > 13) fh = 13;
            for (int y = 0; y < fh; y++) {
                int q = (y * 8) / fh;                            /* 0 = bottom ... 7 = tip */
                int w = q < 1 ? 2 : (q < 4 ? 3 : (q < 6 ? 2 : 1));
                int cx = 20 + ((play && q >= 5) ? ((step & 1) ? 1 : -1) : 0);
                ic_box(cx - w + 1, 29 - y, 2 * w - 1, 1, q < 1 ? 0xD8A0u : 0xFC80u);   /* orange */
                if (w > 1 && q < 6) ic_px(cx, 29 - y, q < 2 ? 0xFDA0u : 0xFFE0u);       /* yellow core */
            }
            ic_box(19, 29, 3, 2, 0x4208u);                       /* the wick's holder */
        } else {
            ic_box(19, 27, 3, 2, ON(0x6B4Du));                   /* an unlit wick */
        }
        ic_box(9, 31, 23, 3, frame);                             /* the base */
        ic_box(9, 31, 23, 1, frame_hi);
        const uint16_t skin = ON(COL_WORM), skin_sh = ON(COL_TRUCK_HI);
        ic_box(16, 2, 9, 4, skin);                               /* the hand gripping the bail */
        ic_box(17, 1, 7, 1, skin);
        ic_px(18, 4, skin_sh); ic_px(20, 4, skin_sh); ic_px(22, 4, skin_sh);
        ic_box(24, 2, 9, 4, skin);                               /* into the wrist */
        ic_box(33, 1, 9, 6, ON(COL_BUBBLE_SH));                  /* a sleeve */
        ic_box(33, 1, 1, 6, ON(COL_BUBBLE_HI));
    } else if (v == VC_BURST) {                 /* a wine glass that shatters while you play */
        int sh = is_on ? g_shatter : 0;
        const uint16_t glass = ON(COL_BUBBLE_HI), wine = ON(COL_APPLE_MD);
        ic_line(21.0f, 22.0f, 21.0f, 33.0f, 2.0f, glass);        /* the stem */
        ic_disc(21, 35, 7, 1, glass);                            /* the foot */
        if (sh < 3) {
            ic_disc(21, 13, 9, 10, glass);                       /* the bowl */
            ic_box(11, 2, 21, 5, card_bg);                       /* (open at the top) */
            ic_disc(21, 13, 7, 8, ON(0xDF1Fu));
            ic_box(13, 4, 17, 8, card_bg);
            ic_disc(21, 15, 7, 6, wine);                         /* the wine */
            ic_box(13, 8, 17, 4, card_bg);
            ic_box(14, 12, 15, 1, ON(COL_APPLE_HI));             /* its surface */
            ic_box(12, 7, 19, 1, glass);                         /* the rim */
            ic_line(12.0f, 7.0f, 13.0f, 13.0f, 1.0f, glass);     /* its sides */
            ic_line(30.0f, 7.0f, 29.0f, 13.0f, 1.0f, glass);
            ic_px(14, 14, COL_TEXT); ic_px(14, 15, ON(COL_TEXT));  /* a glint */
            if (sh >= 1) {                                       /* cracks */
                ic_line(18.0f, 8.0f, 21.0f, 14.0f, 1.0f, dark);
                ic_line(21.0f, 14.0f, 19.0f, 19.0f, 1.0f, dark);
            }
            if (sh >= 2) {
                ic_line(26.0f, 9.0f, 23.0f, 15.0f, 1.0f, dark);
                ic_line(21.0f, 14.0f, 27.0f, 17.0f, 1.0f, dark);
                ic_px(24, 22, wine); ic_px(24, 24, wine);         /* a drip */
            }
        } else {                                                 /* in pieces */
            int t = sh - 2;                                      /* 1..4 */
            static const int8_t SH[7][4] = {                     /* x, y, dx, dy of each shard */
                { 14, 9, -2, -1 }, { 19, 7, -1, -2 }, { 25, 7, 1, -2 }, { 29, 10, 2, -1 },
                { 15, 16, -2, 1 }, { 27, 16, 2, 1 }, { 21, 18, 0, 2 } };
            for (int k = 0; k < 7; k++) {
                int x = SH[k][0] + SH[k][2] * t, y = SH[k][1] + SH[k][3] * t + (t * t) / 3;
                if (y > 36) y = 36;
                ic_px(x, y, glass); ic_px(x + 1, y, glass); ic_px(x, y + 1, ON(0xDF1Fu));
                if (k & 1) ic_px(x + 1, y + 1, glass);
            }
            for (int k = 0; k < 5; k++) {                        /* the wine, splashing down */
                int x = 15 + k * 3, y = 14 + t * 4 + (k & 1) * 2;
                if (y > 37) y = 37;
                ic_box(x, y, 2, 2, wine);
            }
            if (t >= 3) ic_box(12, 37, 18, 1, wine);             /* a puddle */
        }
    } else if (v == VC_CRUSH) {                 /* a beer can getting crushed */
        int c = is_on ? g_crushm : 0;
        int top = 6 + c * 3;                                     /* it squashes down */
        int bulge = c / 2;
        const uint16_t can = ON(COL_GOLD), rim = ON(COL_DIM);
        ic_box(14 - bulge, top, 15 + 2 * bulge, 36 - top, can);  /* the body */
        ic_box(15 - bulge, top - 1, 13 + 2 * bulge, 1, rim);     /* lid */
        ic_box(14 - bulge, 36, 15 + 2 * bulge, 1, rim);          /* base */
        int band = top + (36 - top) * 2 / 5, bh = (36 - top) / 4 + 1;
        ic_box(14 - bulge, band, 15 + 2 * bulge, bh, ON(COL_CREAM));   /* a white band */
        ic_box(14 - bulge, band + bh - 1, 15 + 2 * bulge, 1, ON(COL_APPLE_MD));   /* a red stripe */
        if (bh >= 7) {                                           /* "LWY" in red (it crumples away) */
            static const uint8_t L_[5] = { 4, 4, 4, 4, 7 };            /* 3 wide */
            static const uint8_t W_[5] = { 17, 17, 21, 21, 10 };       /* 5 wide */
            static const uint8_t Y_[5] = { 5, 5, 2, 2, 2 };            /* 3 wide */
            int ty = band + 1;
            for (int row = 0; row < 5; row++) {
                for (int col = 0; col < 3; col++) if ((L_[row] >> (2 - col)) & 1u) ic_px(15 + col, ty + row, ON(COL_APPLE_MD));
                for (int col = 0; col < 5; col++) if ((W_[row] >> (4 - col)) & 1u) ic_px(19 + col, ty + row, ON(COL_APPLE_MD));
                for (int col = 0; col < 3; col++) if ((Y_[row] >> (2 - col)) & 1u) ic_px(25 + col, ty + row, ON(COL_APPLE_MD));
            }
        }
        ic_box(16 - bulge, top + 1, 1, band - top - 1, ON(COL_GOLD_HI));        /* a shine, */
        ic_box(16 - bulge, band + bh, 1, 35 - band - bh, ON(COL_GOLD_HI));      /* (not on the band) */
        if (c == 0) { ic_box(20, top - 2, 3, 1, rim); }          /* the tab */
        for (int k = 0; k < c; k++) {                            /* crumples */
            int y = top + 2 + k * (34 - top) / (c + 1);
            ic_line((float)(14 - bulge), (float)y, 21.0f, (float)(y + 2), 1.0f, ON(COL_GOLD_SH));
            ic_line(21.0f, (float)(y + 2), (float)(28 + bulge), (float)y, 1.0f, ON(COL_GOLD_SH));
            ic_px(13 - bulge, y + 1, ON(COL_GOLD_SH)); ic_px(29 + bulge, y + 1, ON(COL_GOLD_SH));
        }
        if (bh >= 7) {                                           /* a little red star above the name */
            int sx = 21, sy = band - 3;
            ic_px(sx, sy - 1, ON(COL_APPLE_MD));
            ic_box(sx - 2, sy, 5, 1, ON(COL_APPLE_MD));
            ic_box(sx - 1, sy + 1, 3, 1, ON(COL_APPLE_MD));
            ic_px(sx - 1, sy + 2, ON(COL_APPLE_MD)); ic_px(sx + 1, sy + 2, ON(COL_APPLE_MD));
        }
        if (is_on && c >= 1) {                                   /* foam bursting out the top */
            static const int8_t FO[9][3] = { { 0, 0, 3 }, { -4, 1, 2 }, { 4, 1, 2 }, { -2, -3, 2 }, { 3, -3, 2 },
                                             { -6, -1, 2 }, { 6, -2, 2 }, { 0, -6, 2 }, { -3, -7, 1 } };
            int n = 2 + c + (c > 3 ? 2 : 0);
            if (n > 9) n = 9;
            int fy = top - 2 - c / 2;
            for (int k = 0; k < n; k++) {
                int wob = ((k + step) & 1) ? 1 : 0;
                ic_disc(21 + FO[k][0], fy + FO[k][1] - wob, FO[k][2], FO[k][2], COL_TEXT);
            }
            for (int k = 0; k < n; k += 2) ic_px(21 + FO[k][0] - 1, fy + FO[k][1] - 1, COL_CREAM);   /* bubbles */
            if (c >= 2) {                                        /* flying flecks */
                ic_px(12 - c, fy - 2 - step, COL_TEXT); ic_px(30 + c, fy - 1 - (step & 1), COL_TEXT);
                ic_px(9 - c / 2, fy + 4, COL_BUBBLE_HI); ic_px(33 + c / 2, fy + 3, COL_BUBBLE_HI);
            }
            if (c >= 3) {                                        /* drips running down */
                ic_px(13 - bulge, top + 2, COL_TEXT); ic_px(13 - bulge, top + 4, COL_CREAM);
                ic_px(29 + bulge, top + 3, COL_TEXT);
            }
        }
    } else if (v == VC_WASH) {                  /* the moon: the clouds part, it glows and wakes up */
        int m = is_on ? g_moon : 0;
        static const int8_t ST[6][2] = { { 5, 5 }, { 36, 4 }, { 34, 31 }, { 7, 33 }, { 13, 3 }, { 38, 20 } };
        for (int k = 0; k < 6; k++) {                            /* stars, more as the sky clears */
            if (k >= 2 + m) continue;
            int tw = is_on && ((k + step) & 1);
            ic_px(ST[k][0], ST[k][1], tw ? COL_GOLD_HI : ON(COL_DIM));
            if (tw && m >= 4) { ic_px(ST[k][0] - 1, ST[k][1], COL_GOLD); ic_px(ST[k][0] + 1, ST[k][1], COL_GOLD); }
        }
        ic_disc(22, 17, 10, 10, ON(COL_CREAM));                  /* the moon */
        ic_disc(27, 14, 9, 10, card_bg);                         /* a crescent */
        if (is_on && m >= 3) {                                   /* a ring of glow around it, turning */
            for (int k = 0; k < 16; k++) {
                if (((k + step) & 1) && m < 5) continue;
                float a = (float)k * 0.3927f + (float)step * 0.2f;
                int gx = 22 + (int)(13.0f * cosf(a) + (cosf(a) < 0.0f ? -0.5f : 0.5f));
                int gy = 17 + (int)(13.0f * sinf(a) + (sinf(a) < 0.0f ? -0.5f : 0.5f));
                ic_px(gx, gy, (k & 1) ? COL_GOLD : 0xFFF0u);
            }
        }
        if (m >= 5) { ic_px(15, 15, dark); ic_px(16, 14, dark); ic_px(17, 15, dark); }   /* awake, a happy eye */
        else { ic_px(15, 16, dark); ic_px(16, 16, dark); ic_px(17, 16, dark); }           /* asleep */
        if (m >= 5) { ic_px(14, 22, dark); ic_px(15, 23, dark); ic_px(16, 23, dark); ic_px(17, 22, dark);   /* a smile */
                      ic_px(13, 19, COL_APPLE_HI); }
        if (is_on && m >= 6 && step == 2) {                      /* a shooting star */
            ic_line(30.0f, 3.0f, 38.0f, 7.0f, 1.0f, COL_TEXT);
            ic_px(29, 3, COL_GOLD_HI);
        }
        int dx = (is_on ? step : 0) + m * 3;                     /* the clouds drift apart */
        const uint16_t cl = ON(0xE71Cu), cs = ON(0xB596u);
        ic_disc(10 - dx, 26, 6, 3, cs); ic_disc(15 - dx, 24, 6, 4, cl); ic_disc(21 - dx / 2, 27, 5, 3, cl);
        ic_disc(31 + dx, 12, 5, 2, cs); ic_disc(27 + dx, 11, 4, 3, cl);
    } else {                                    /* VC_STEPS: a white medieval horse, saddled, galloping */
        int g = is_on ? g_ray : 0;                               /* 0 = standing ... 6 = full gallop */
        int play = is_on && g_any_note_held;
        int hop = (play && g >= 2) ? ((step & 1) ? 2 : 0) : 0;
        const uint16_t coat = ON(0xF79Eu), coat_sh = ON(0xBDD7u);      /* white, grey shading */
        const uint16_t mane = ON(0xDEFBu), mane_sh = ON(0x9CF3u);
        const uint16_t cloth = ON(0xB0E4u), trim = ON(COL_GOLD), leather = ON(0x7A43u);
        int by = 21 - hop;
        static const int8_t LEG[4][4] = { { 0, 0, 0, 0 }, { 3, -2, -3, 2 }, { 0, 0, 0, 0 }, { -3, 2, 3, -2 } };
        int ls = play ? (step & 3) : 0;
        static const int8_t LX[4] = { 10, 14, 25, 29 };
        for (int k = 0; k < 4; k++) {                            /* legs, at a gallop */
            int lx = LX[k] + LEG[ls][k];
            ic_line((float)LX[k], (float)(by + 4), (float)lx, (float)(by + 12), 2.0f, k & 1 ? coat_sh : coat);
            ic_box(lx - 1, by + 12, 3, 1, ON(0x4208u));          /* hooves */
        }
        ic_disc(20, by + 1, 12, 5, coat);                        /* the body */
        ic_box(9, by + 4, 22, 1, coat_sh);                       /* belly shading */
        ic_line(29.0f, (float)(by - 2), 33.0f, (float)(by - 9), 5.0f, coat);   /* the neck */
        ic_disc(35, by - 10, 4, 3, coat);                        /* the head */
        ic_line(36.0f, (float)(by - 10), 40.0f, (float)(by - 7), 3.0f, coat);  /* the nose */
        ic_px(40, by - 7, coat_sh);
        ic_px(34, by - 10, dark);                                /* an eye */
        ic_px(33, by - 14, coat_sh); ic_px(34, by - 14, coat);   /* ears */
        ic_line(33.0f, (float)(by - 11), 39.0f, (float)(by - 8), 1.0f, leather);   /* a bridle */
        ic_line(35.0f, (float)(by - 9), 27.0f, (float)(by - 4), 1.0f, leather);    /* the reins */
        for (int k = 0; k < 3; k++)                              /* a flowing white mane */
            ic_disc(31 - k, by - 11 + k * 2, 1, 1, (k & 1) ? mane_sh : mane);
        /* a small saddle cloth: red with a gold edge, under a leather saddle */
        ic_box(15, by - 3, 10, 6, cloth);
        ic_box(15, by + 3, 10, 1, trim);
        ic_box(24, by - 3, 1, 7, trim);
        ic_box(17, by - 5, 6, 2, leather);                       /* the saddle */
        ic_box(21, by - 6, 2, 1, leather);
        ic_line(20.0f, (float)(by + 4), 20.0f, (float)(by + 7), 1.0f, ON(0x8C71u));   /* a stirrup */
        int tw = play ? (step & 1) : 0;                          /* the tail, flowing */
        ic_line(8.0f, (float)(by - 1), 4.0f, (float)(by + 6 - tw * 3), 3.0f, mane);
        ic_line(7.0f, (float)by, 3.0f, (float)(by + 8 - tw * 2), 1.0f, mane_sh);
    }
    #undef ON
}

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

    if (g_bank == BANK_POCKET) {
        draw_bank_p_icon(v, is_on, card_bg);
    } else if (g_bank == BANK_RHYTHM) {
        draw_bank_d_icon(v, is_on, card_bg);
    } else if (g_bank == 4) {
        draw_bank_e_icon(v, is_on, step, card_bg);
    } else if (g_bank == 2) {
        draw_bank_c_icon(v, is_on, step, card_bg);
    } else if (g_bank) {
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

        int burn = is_on ? g_cig : 0;                          /* burns down the longer you play */
        int cig_y = 26;
        int filter_x = 6;
        int filter_w = 7;
        int paper_len = 18 - burn * 2;
        if (paper_len < 4) paper_len = 4;
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

        if (is_on) {
            /* a plume of smoke curling up from the tip: a thin wisp at rest,
             * a full, drifting plume while you play */
            static const int8_t SWAY[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };
            int puffs = play_anim ? 6 : 3;
            int sx0 = ash_x + 3;
            for (int k = 0; k < puffs; k++) {
                int yy = cig_y - 3 - k * 3 - (play_anim ? (step & 1) : 0);
                int w = play_anim ? 2 + k / 2 : 1 + k / 2;     /* it widens as it rises */
                if (w > 4) w = 4;
                int h = 2;
                int xx = sx0 + SWAY[(k + step * 2) & 7] - k / 2 - w / 2 + 1;
                if (yy < 1) break;
                uint16_t c = (k >= 3 && (k & 1)) ? COL_TEXT : smoke_col;
                FILL_BOX(xx, yy, w, h, c);
                if (play_anim && k == 3) PUT_PX(xx + w + 1, yy - 1, smoke_col);   /* a stray wisp */
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
static float g_prev_bvol = -1.0f, g_prev_btone = -1.0f;
static int g_prev_bstep[2] = { -9, -9 }, g_prev_beat[2] = { -9, -9 }, g_prev_pulse = -1;
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

/* ---- the sequencer screen ---- */
/* up to 16 steps: two rows of big cells; 17..32: four rows of short ones */
#define SEQ_BIG   (g_seq_len <= 16)
#define SEQ_CX(i) (14u + (uint32_t)((i) & 7) * 27u)
#define SEQ_CY(i) (SEQ_BIG ? 78u + (uint32_t)((i) >> 3) * 44u : 76u + (uint32_t)((i) >> 3) * 29u)
#define SEQ_CW 24u
#define SEQ_CH (SEQ_BIG ? 38u : 26u)
#define SEQ_SHOWN (SEQ_BIG ? 16 : 32)
static void seq_draw_cell(int i, int playing, int cursor) {
    uint32_t x = SEQ_CX(i), y = SEQ_CY(i);
    int used = (i < g_seq_len);
    uint16_t edge = playing ? COL_GOLD_HI : (cursor ? COL_APPLE_MD : (used ? COL_BORDER : COL_STAGE_BG));
    uint16_t fill = used ? COL_BG : COL_STAGE_BG;
    fm1_lcd_fill_rect(x, y, SEQ_CW, SEQ_CH, edge);
    fm1_lcd_fill_rect(x + 2u, y + 2u, SEQ_CW - 4u, SEQ_CH - 4u, playing ? COL_INK : fill);
    if (!used) return;
    if ((i & 3) == 0) fm1_lcd_fill_rect(x + 2u, y + SEQ_CH - 4u, SEQ_CW - 4u, 2u, COL_DIM);   /* the beats */
    /* its notes, a tiny piano roll: higher notes higher up, a chord's notes
     * side by side */
    for (int k = 0; k < 4; k++) {
        int nt = g_seq[i][k];
        if (!nt) continue;
        int p = nt - 41;                                   /* F2 .. about F6 */
        if (p < 0) p = 0;
        if (p > 48) p = 48;
        uint32_t ny = y + SEQ_CH - 9u - (uint32_t)(p * (int)(SEQ_CH - 13u) / 48);
        fm1_lcd_fill_rect(x + 3u + (uint32_t)k * 5u, ny, 4u, 4u, playing ? COL_GOLD_HI : COL_BUBBLE_MD);
    }
}
/* ---- the drum sequencer screen: a row of lamps for each drum ---- */
#define DSEQ_Y0 72u
#define DSEQ_RH 9u
static void dseq_draw_col(int i, int playing, int cursor) {
    (void)cursor;
    if (i >= 16) return;
    uint32_t cw = 12u;
    uint32_t x = 34u + (uint32_t)i * cw;
    uint16_t bg = playing ? 0x4A69u : COL_STAGE_BG;
    fm1_lcd_fill_rect(x, DSEQ_Y0 - 2u, cw, (uint32_t)NUM_DRUMS * DSEQ_RH + 2u, bg);
    if (i >= g_dseq_len) return;
    for (int d = 0; d < NUM_DRUMS; d++) {
        uint32_t y = DSEQ_Y0 + (uint32_t)d * DSEQ_RH;
        if ((g_dseq[i] >> d) & 1u)
            fm1_lcd_fill_rect(x + 1u, y, cw - 2u, DSEQ_RH - 2u, playing ? 0xFFFFu : DRUM_LAMP[d]);
        else                                               /* an unlit lamp; brighter on the beats */
            fm1_lcd_fill_rect(x + cw / 2u - 1u, y + 2u, 2u, 2u,
                              d == g_dseq_cur ? DRUM_LAMP[d] : ((i & 3) == 0 ? COL_DIM : 0x4208u));
    }
}

static void seq_num(char *buf, int v) {                   /* up to 3 digits */
    if (v >= 100) { buf[0] = (char)('0' + v / 100); buf[1] = (char)('0' + (v / 10) % 10); buf[2] = (char)('0' + v % 10); buf[3] = 0; }
    else if (v >= 10) { buf[0] = (char)('0' + v / 10); buf[1] = (char)('0' + v % 10); buf[2] = 0; }
    else { buf[0] = (char)('0' + v); buf[1] = 0; }
}

static void render_screen(void) {
    /* MASTER for the screen: its reading wobbles by a few steps, so the top bar
     * only follows a real move (10 steps, 1 %), and snaps to the ends */
    static uint32_t m_shown = 99999u;
    uint32_t m_now = fm1_master();
    uint32_t m_diff = (m_now > m_shown) ? m_now - m_shown : m_shown - m_now;
    if (m_shown > FM1_MASTER_MAX || m_diff >= 10u ||
        (m_now == 0u && m_shown != 0u) || (m_now == FM1_MASTER_MAX && m_shown != FM1_MASTER_MAX))
        m_shown = m_now;
    uint32_t m_raw = m_shown;
    /* pictures move only while keys are held (or HOLD keeps notes on), not
     * while a note's tail rings out, so they settle soon after you let go */
    static uint32_t last_held_ms = 0u;
    if (g_any_note_held) last_held_ms = g_tick_ms;
    int sounding = (g_any_note_held != 0u) ||
                   (last_held_ms != 0u && g_tick_ms - last_held_ms < 500u);   /* a short moment after you let go */
    int ringing = (g_any_note_sounding != 0u);

    if (g_need_full_clear || g_prev_view != g_view) {
        g_need_full_clear = 0;
        g_prev_view = g_view;
        g_seq_screen_gen++;
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
        g_prev_bvol = -1.0f; g_prev_btone = -1.0f; g_prev_bstep[0] = g_prev_bstep[1] = -9; g_prev_beat[0] = g_prev_beat[1] = -9; g_prev_pulse = -1;
        g_prev_filter = -1.0f;
        g_prev_mel = -1.0f;
        g_prev_mello_key = -99;
        g_prev_master = 99999u;

        /* Splash & truck pages paint the whole scene themselves */
        if (g_view != VIEW_SPLASH && g_view != VIEW_TRUCK) {
            fm1_lcd_fill_rect(0u, 0u, FM1_LCD_WIDTH, FM1_LCD_HEIGHT, g_view == VIEW_FACE ? 0x0000u : COL_BG);
        }
        if (g_view == VIEW_SCOPE) {
            draw_scope_frame();
        }
    }

    if (g_view == VIEW_FACE) {
        /* one picture, five times its size, and nothing else: the FUZZ smiley
         * (glitches out), the STEPS horse
         * (gallops), the LOWREY eye (blinks and cries) or the GLITCH TV (rolls
         * and tears) - each moving while you play */
        static int face_g = 0, prev_g = -1, prev_step = -1, prev_sel = -1;
        static uint32_t face_ms = 0u;
        int tgt = sounding ? 6 : 0;
        if (face_g != tgt && g_tick_ms - face_ms >= 150u) {
            face_g += (tgt > face_g) ? 1 : -1;
            face_ms = g_tick_ms;
        }
        int step = sounding ? (int)((g_tick_ms / (g_face_sel == 2 ? 420u : 120u)) % 4u) : 0;   /* the eye: slower */
        if (g_prev_anim_phase == -1 || prev_sel != g_face_sel) prev_g = -1;   /* just arrived: draw it */
        if (face_g != prev_g || ((face_g > 0 || sounding) && step != prev_step)) {
            prev_g = face_g; prev_step = step; prev_sel = g_face_sel;
            g_prev_anim_phase = 0;
            int keep_f = g_fzg, keep_r = g_ray;
            g_fzg = face_g; g_ray = face_g;
            for (uint32_t i = 0u; i < IC_W * IC_H; i++) g_blit_buf[i] = 0x0000u;   /* on black */
            switch (g_face_sel) {
            case 1:  draw_bank_c_icon(VC_STEPS, 1, step, 0x0000u); break;
            case 2:  draw_bank_b_icon(VB_LOWREY, 1, step, 0x0000u); break;   /* the bloodshot eye */
            case 3:  draw_bank_c_icon(VC_GLITCH, 1, step, 0x0000u); break;
            default: draw_bank_c_icon(VC_FUZZ, 1, step, 0x0000u); break;
            }
            g_fzg = keep_f; g_ray = keep_r;
            static uint16_t big[210u * 5u];
            for (uint32_t y = 0u; y < 40u; y++) {
                for (uint32_t x = 0u; x < 42u; x++) {
                    uint16_t c = g_blit_buf[y * IC_W + x];
                    for (uint32_t k = 0u; k < 5u; k++) big[x * 5u + k] = c;
                }
                for (uint32_t r = 1u; r < 5u; r++)
                    for (uint32_t x = 0u; x < 210u; x++) big[r * 210u + x] = big[x];
                fm1_lcd_blit(15u, 20u + y * 5u, 210u, 5u, big, 210u * 5u);
            }
        }
        return;
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
            float speed = held ? 1.0f : (ringing ? 0.35f : 0.0f);
            if (g_prev_anim_phase >= 0) {
                g_truck_pos += speed;
                if (g_truck_pos >= (float)total_steps) g_truck_pos -= (float)total_steps;
                g_truck_step = (uint32_t)g_truck_pos;
            }
            g_prev_anim_phase = (int)step;

            float target = held ? 7.0f : (ringing ? 4.0f : 0.0f);
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
#ifdef FM1_BATTERY_FULL
        {
            static uint32_t batt_t = 0u;
            if (g_batt == -2 || g_tick_ms - batt_t >= 1000u) { g_batt = fm1_battery(); batt_t = g_tick_ms; }
        }
        int batt_changed = (g_batt != g_prev_batt);
#else
        int batt_changed = 0;
#endif
        int pulse = 0;
        for (int k = 0; k < 2; k++)
            if (g_beat[k] >= 0 && g_beat_step[k] >= 0 && (g_beat_step[k] % BEATS[g_beat[k]].spb) == 0) pulse = 1;
        static int p_hdr_bpm = -1;
        int hdr_bpm = (g_bank == BANK_RHYTHM) ? g_seq_bpm : -1;
        if (hdr_bpm != p_hdr_bpm) { p_hdr_bpm = hdr_bpm; g_prev_hold = -1; }
        if (g_prev_master != m_raw || g_prev_oct != g_octave_shift ||
            g_prev_hold != g_hold_on || g_prev_anim_phase != anim_phase || batt_changed || pulse != g_prev_pulse) {
            g_prev_pulse = pulse;
#ifdef FM1_BATTERY_FULL
            g_prev_batt = g_batt;
#endif
            g_prev_master = m_raw;
            g_prev_oct = g_octave_shift;
            g_prev_hold = g_hold_on;

            fm1_lcd_fill_rect(0u, 0u, 240u, 18u, COL_STAGE_BG);
            draw_retro_star(4u, 6u, pulse ? 0xFA8Au : (sounding ? COL_GOLD_HI : COL_GOLD), COL_STAGE_BG);   /* red on each beat of a running rhythm */
            draw_text_blit(13u, 5u, g_bank == BANK_POCKET ? "POCKET" : g_bank == BANK_RHYTHM ? "RHYTHM" : g_bank == 4 ? "WAVES" : (g_bank == 2 ? "BANK C" : (g_bank ? "BANK B" : "LOWREY")),
                           g_bank == BANK_POCKET ? 0xFD20u : g_bank == BANK_RHYTHM ? 0xFA8Au : g_bank == 4 ? 0xFD20u : (g_bank == 2 ? COL_APPLE_HI : (g_bank ? COL_BUBBLE_HI : COL_GOLD_HI)), COL_STAGE_BG, 1u);

            char obuf[7];
            format_oct(obuf, g_octave_shift);
            draw_text_blit(53u, 5u, obuf, COL_LEAF_HI, COL_STAGE_BG, 1u);

            if (g_bank == BANK_RHYTHM) {                 /* the RHYTHM bank: the tempo instead of HOLD */
                char tb[9] = "BPM:";
                seq_num(tb + 4, g_seq_bpm);
                draw_text_blit(96u, 5u, tb, COL_APPLE_HI, COL_STAGE_BG, 1u);
            } else
            draw_text_blit(96u, 5u, g_hold_on ? "HOLD:ON" : "HOLD:--",
                           g_hold_on ? COL_APPLE_HI : COL_DIM, COL_STAGE_BG, 1u);

            int vol_pct = (int)(((float)m_raw / (float)FM1_MASTER_MAX) * 100.0f + 0.5f);
            char vbuf[9] = "VOL:000%";
            vbuf[4] = (char)('0' + (vol_pct / 100) % 10);
            vbuf[5] = (char)('0' + (vol_pct / 10) % 10);
            vbuf[6] = (char)('0' + (vol_pct % 10));
            draw_text_blit(152u, 5u, vbuf, COL_TEXT, COL_STAGE_BG, 1u);
#ifdef FM1_BATTERY_FULL
            draw_battery_icon(214u, 5u, g_batt);
#else
            draw_retro_star(230u, 6u, sounding ? COL_GOLD_HI : COL_GOLD, COL_STAGE_BG);
#endif
        }

        const uint32_t col_x[4] = { 4u, 62u, 120u, 178u };
        const uint32_t row_y[2] = { 20u, 82u };

        for (uint32_t v = 0u; v < NUM_VOICES; v++) {
            int on = voice_lit(v);
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

        /* STRINGS' cigarette burns down while you play; after 1.2 s of quiet
         * a fresh one is lit */
        if (g_bank == 0 && g_voice_on[VOICE_STRINGS]) {
            if (sounding) {
                g_cig_quiet_ms = g_tick_ms;
                if (g_cig < 7 && g_tick_ms - g_cig_ms >= 900u) {
                    g_cig++;
                    g_cig_ms = g_tick_ms;
                    draw_voice_card_icon(VOICE_STRINGS, col_x[VOICE_STRINGS % 4u], row_y[VOICE_STRINGS / 4u], 1, anim_phase);
                }
            } else if (g_cig > 0 && g_tick_ms - g_cig_quiet_ms >= 1200u) {
                g_cig = 0;
                draw_voice_card_icon(VOICE_STRINGS, col_x[VOICE_STRINGS % 4u], row_y[VOICE_STRINGS / 4u], 1, anim_phase);
            }
        }

        /* POCKET's sun sets into a moon while you play, and rises back after */
        if (g_bank == 1 && g_voice_on[VB_POCKET]) {
            int tgt = sounding ? 8 : 0;
            if (g_pk_morph != tgt && g_tick_ms - g_pk_ms >= 260u) {   /* ~2 s each way */
                g_pk_morph += (tgt > g_pk_morph) ? 1 : -1;
                g_pk_ms = g_tick_ms;
                draw_voice_card_icon(VB_POCKET, col_x[VB_POCKET % 4u], row_y[VB_POCKET / 4u], 1, anim_phase);
            }
        } else {
            g_pk_morph = 0;
        }
        /* bank C: the glass shatters, the can gets crushed, the light pours
         * through the cloud while you play; all slowly undo after */
        if (g_bank == 2) {
            int tgt = sounding ? 6 : 0;
            #define CMORPH(var, ms, vc, rate) do { \
                if (g_voice_on[vc]) { \
                    if ((var) != tgt && g_tick_ms - (ms) >= ((tgt > (var)) ? (rate) : (rate) / 2u)) { \
                        (var) += (tgt > (var)) ? 1 : -1; (ms) = g_tick_ms; \
                        draw_voice_card_icon(vc, col_x[(vc) % 4u], row_y[(vc) / 4u], 1, anim_phase); } \
                } else (var) = 0; } while (0)
            CMORPH(g_shatter, g_shatter_ms, VC_BURST, 140u);
            CMORPH(g_crushm, g_crush_ms, VC_CRUSH, 220u);
            CMORPH(g_ray, g_ray_ms, VC_STEPS, 400u);
            CMORPH(g_fzg, g_fzg_ms, VC_FUZZ, 150u);
            CMORPH(g_tape, g_tape_ms, VC_FLICKER, 480u);
            CMORPH(g_fence, g_fence_ms, VC_LOWREY, 260u);
            CMORPH(g_moon, g_moon_ms, VC_WASH, 300u);
            #undef CMORPH
        }

        /* RHYTHM: the beat's lamps follow the playhead */
        for (int k = 0; k < 2 && g_bank == BANK_RHYTHM; k++) {
            int bt = g_beat[k], bs = g_beat_step[k];
            if (bs == g_prev_bstep[k] && bt == g_prev_beat[k]) continue;
            int ob = g_prev_beat[k];
            g_prev_bstep[k] = bs; g_prev_beat[k] = bt;
            if (ob >= 0 && ob != bt && ob < (int)NUM_VOICES)
                draw_voice_card_icon((uint32_t)ob, col_x[ob % 4], row_y[ob / 4], voice_lit((uint32_t)ob), anim_phase);
            if (bt >= 0)
                draw_voice_card_icon((uint32_t)bt, col_x[bt % 4], row_y[bt / 4], voice_lit((uint32_t)bt), anim_phase);
        }

        /* WAVES: the icons follow the sound, redrawn every 150 ms while you play */
        if ((g_bank == 4 || g_bank == BANK_POCKET) && sounding && g_tick_ms - g_dt_ms >= 150u) {
            g_dt_ms = g_tick_ms;
            g_icon_t++;
            for (uint32_t v = 0u; v < NUM_VOICES; v++)
                if (voice_lit(v)) draw_voice_card_icon(v, col_x[v % 4u], row_y[v / 4u], 1, anim_phase);
        }

        /* WARM's police car catches fire slowly while you play, and burns out after */
        if (g_bank == 1 && g_voice_on[VB_WARM]) {
            int tgt = sounding ? 8 : 0;
            if (g_fire != tgt && g_tick_ms - g_fire_ms >= 280u) {
                g_fire += (tgt > g_fire) ? 1 : -1;
                g_fire_ms = g_tick_ms;
                draw_voice_card_icon(VB_WARM, col_x[VB_WARM % 4u], row_y[VB_WARM / 4u], 1, anim_phase);
            }
        } else {
            g_fire = 0;
        }

        if (g_bank == BANK_POCKET) {
            static float pv1 = -1.0f, pv2 = -1.0f, pvr = -1.0f, pvv = -1.0f;
            static int pm = -1, pgen = -1;
            if (pgen != g_seq_screen_gen) { pgen = g_seq_screen_gen; pm = -1; }   /* (the screen was cleared) */
            int m = g_pp_mode;
            if (pm != m || pv1 != g_pp_p1[m] || pv2 != g_pp_p2[m] || pvr != g_rev_mix || pvv != g_pp_vol) {
                pm = m; pv1 = g_pp_p1[m]; pv2 = g_pp_p2[m]; pvr = g_rev_mix; pvv = g_pp_vol;
                g_prev_decay = -1.0f; g_prev_rev_mix = -1.0f;
                fm1_lcd_fill_rect(4u, 144u, 232u, 92u, COL_STAGE_BG);
                char pbuf[6];
                /* laid out as the other banks: REV top left, its first knob where DLY is,
                 * VOL where DEC is, its second knob where FLT is */
                draw_text_blit(12u, 151u, "REV", COL_BUBBLE_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(48u, 149u, 44u, 11u, g_rev_mix, COL_BUBBLE_MD);
                format_pct(pbuf, g_rev_mix);
                draw_text_blit(96u, 151u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(136u, 151u, "VOL", COL_APPLE_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(172u, 149u, 28u, 11u, g_pp_vol, COL_APPLE_MD);
                format_pct(pbuf, g_pp_vol);
                draw_text_blit(204u, 151u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(12u, 173u, PP_P1[m], COL_GOLD_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(48u, 171u, 44u, 11u, g_pp_p1[m], COL_GOLD);
                format_pct(pbuf, g_pp_p1[m]);
                draw_text_blit(96u, 173u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(136u, 173u, PP_P2[m], COL_LEAF_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(172u, 171u, 28u, 11u, g_pp_p2[m], COL_LEAF_MD);
                format_pct(pbuf, g_pp_p2[m]);
                draw_text_blit(204u, 173u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(12u, 204u, "K1:REV K2 K4:MODE K3:VOL OCT:TUNE", COL_GOLD, COL_STAGE_BG, 1u);
                draw_text_blit(12u, 218u, "REC:BUTTON SCL:HOLD/HOLD=BACK", COL_DIM, COL_STAGE_BG, 1u);
                g_prev_mello_key = -99;
            }
        } else if (g_bank == BANK_RHYTHM) {
            if (g_prev_rev_mix != g_rev_mix || g_prev_dly_mix != g_dly_mix ||
                g_prev_bvol != g_beat_vol || g_prev_btone != g_beat_tone) {
                g_prev_rev_mix = g_rev_mix; g_prev_dly_mix = g_dly_mix;
                g_prev_bvol = g_beat_vol; g_prev_btone = g_beat_tone;
                g_prev_decay = -1.0f;             /* the other banks' panel draws itself again after */
                fm1_lcd_fill_rect(4u, 144u, 232u, 92u, COL_STAGE_BG);
                char pbuf[6];
                draw_text_blit(12u, 151u, "REV", COL_BUBBLE_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(38u, 149u, 54u, 11u, g_rev_mix, COL_BUBBLE_MD);
                format_pct(pbuf, g_rev_mix);
                draw_text_blit(96u, 151u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(136u, 151u, "VOL", COL_GOLD_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(162u, 149u, 36u, 11u, g_beat_vol, COL_GOLD);
                format_pct(pbuf, g_beat_vol);
                draw_text_blit(202u, 151u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(12u, 173u, "DLY", COL_APPLE_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(38u, 171u, 54u, 11u, g_dly_mix, COL_APPLE_MD);
                format_pct(pbuf, g_dly_mix);
                draw_text_blit(96u, 173u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(136u, 173u, "TON", COL_LEAF_HI, COL_STAGE_BG, 1u);
                draw_bar_meter(162u, 171u, 36u, 11u, g_beat_tone, COL_LEAF_MD);
                format_pct(pbuf, g_beat_tone);
                draw_text_blit(202u, 173u, pbuf, COL_TEXT, COL_STAGE_BG, 1u);
                draw_text_blit(12u, 204u, "TAP:BEAT ALGO:BPM K3:VOL K4:TONE", COL_GOLD, COL_STAGE_BG, 1u);
                draw_text_blit(12u, 218u, "HOLD SEQ:DRUM SEQ HOLD PLAY:BACK", COL_DIM, COL_STAGE_BG, 1u);
                g_prev_mello_key = -99;
            }
        } else if (g_prev_rev_mix != g_rev_mix || g_prev_dly_mix != g_dly_mix ||
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
            int mel_on = (g_mel_level > 0.005f) && g_bank != BANK_RHYTHM && g_bank != BANK_POCKET;   /* (no flute on the rhythm or POCKET banks) */
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
    } else if (g_view == VIEW_GLOBAL) {
        static int p_gen = -1, p_sig = -1;
        static const int ROW_BANK[5] = { 0, 1, 2, 4, BANK_RHYTHM };
        static const char *ROW_NAME[5] = { "BANK A", "BANK B", "BANK C", "WAVES", "RHYTHM" };
        static const char *OUT_NAME[4] = { "KEYS", "SEQUENCER", "DRUMS", "CLOCK" };
        int outv[4] = { g_mo_keys, g_mo_seq, g_mo_drums, g_mo_clock };
        int sig = g_midi_multi * 7 + g_glob_row * 131;
        for (int i = 0; i < 5; i++) sig = sig * 17 + g_bank_ch[ROW_BANK[i]];
        for (int i = 0; i < 4; i++) sig = sig * 3 + outv[i];
        sig = sig * 3 + g_usb_split;
        if (p_gen != g_seq_screen_gen) {
            p_gen = g_seq_screen_gen; p_sig = -1;
            fm1_lcd_fill_rect(8u, 8u, 224u, 224u, COL_STAGE_BG);
            draw_text_blit(16u, 12u, "GLOBAL", COL_GOLD_HI, COL_STAGE_BG, 2u);
            draw_text_blit(14u, 218u, "SEL:CHOOSE K1:CHANGE HOLD GLO:BACK", COL_LEAF_HI, COL_STAGE_BG, 1u);
        }
        if (sig != p_sig) {
            p_sig = sig;
            fm1_lcd_fill_rect(10u, 30u, 220u, 184u, COL_STAGE_BG);
            int row = 0;
            uint32_t y = 32u;
            draw_text_blit(14u, y, "MIDI IN", COL_DIM, COL_STAGE_BG, 1u);
            y += 12u;
            /* one line: its name and value, the one picked shown highlighted */
            #define GLOB_LINE(name, val, valcol) do { \
                int rs_ = (g_glob_row == row); uint16_t bg_ = rs_ ? COL_APPLE_MD : COL_STAGE_BG; \
                if (rs_) fm1_lcd_fill_rect(12u, y - 2u, 216u, 12u, bg_); \
                draw_text_blit(20u, y, name, rs_ ? COL_INK : COL_TEXT, bg_, 1u); \
                draw_text_blit(120u, y, val, rs_ ? COL_INK : (valcol), bg_, 1u); \
                row++; y += 12u; } while (0)
            GLOB_LINE("MODE", g_midi_multi ? "MULTI" : "SINGLE", COL_GOLD_HI);
            if (!g_midi_multi) {
                draw_text_blit(20u, y, "BANK ON SCREEN: ANY CHANNEL", COL_DIM, COL_STAGE_BG, 1u);
                y += 12u;
            } else {
                for (int i = 0; i < 5; i++) {
                    char cb[8] = "CH ";
                    int c = g_bank_ch[ROW_BANK[i]];
                    if (c == 0) { cb[0] = 'O'; cb[1] = 'F'; cb[2] = 'F'; cb[3] = 0; }
                    else seq_num(cb + 3, c);
                    GLOB_LINE(ROW_NAME[i], cb, c ? COL_GOLD_HI : COL_DIM);
                }
            }
            y += 4u;
            draw_text_blit(14u, y, "MIDI OUT", COL_DIM, COL_STAGE_BG, 1u);
            y += 12u;
            for (int i = 0; i < 4; i++) GLOB_LINE(OUT_NAME[i], outv[i] ? "ON" : "OFF", outv[i] ? COL_LEAF_HI : COL_DIM);
            y += 4u;
            draw_text_blit(14u, y, "USB AUDIO", COL_DIM, COL_STAGE_BG, 1u);
            y += 13u;
            GLOB_LINE("SPLIT", g_usb_split ? "L:ORGAN R:DRUMS" : "OFF", g_usb_split ? COL_LEAF_HI : COL_DIM);
            #undef GLOB_LINE
        }
    } else if (g_view == VIEW_DSEQ) {
        static int p_play = -1, p_rec = -1, p_bpm = -1, p_len = -1, p_vol = -1, p_tone = -1;
        static int p_step = -1, p_cur = -1, p_big = -1;
        static uint32_t p_ver = 0xFFFFFFFFu;
        static int p_view_gen = -1;
        if (p_view_gen != g_seq_screen_gen) {               /* the screen was just opened */
            p_view_gen = g_seq_screen_gen;
            p_play = p_rec = p_bpm = p_len = p_vol = p_tone = p_step = p_cur = p_big = -1;
            p_ver = 0xFFFFFFFFu;
            fm1_lcd_fill_rect(8u, 8u, 224u, 224u, COL_STAGE_BG);
            draw_text_blit(16u, 16u, "DRUM SEQ", 0xFA8Au, COL_STAGE_BG, 2u);
            draw_text_blit(14u, 190u, "SEL:DRUM  WHITE KEYS:STEPS", COL_GOLD, COL_STAGE_BG, 1u);
            draw_text_blit(14u, 202u, "PLAY:START REC:LIVE HOLD REC:CLR", COL_GOLD, COL_STAGE_BG, 1u);
            draw_text_blit(14u, 214u, "ALGO:BPM K2:LEN HOLD SEQ:BACK", COL_LEAF_HI, COL_STAGE_BG, 1u);
        }
        int play = g_dseq_play, rec = g_dseq_rec;
        if (play != p_play || rec != p_rec) {
            p_play = play;
            p_rec = rec;
            fm1_lcd_fill_rect(132u, 14u, 96u, 20u, COL_STAGE_BG);
            draw_text_blit(134u, 20u, play ? "PLAY" : "STOP", play ? COL_LEAF_HI : COL_DIM, COL_STAGE_BG, 1u);
            if (rec) {
                fm1_lcd_fill_rect(172u, 17u, 10u, 10u, COL_APPLE_MD);
                draw_text_blit(186u, 20u, "REC", COL_APPLE_HI, COL_STAGE_BG, 1u);
            }
            p_cur = -2;
        }
        int vol = (int)(g_beat_vol * 100.0f + 0.5f), tone = (int)(g_beat_tone * 100.0f + 0.5f);
        if (g_seq_bpm != p_bpm || g_dseq_len != p_len || vol != p_vol || tone != p_tone) {
            int len_changed = (g_dseq_len != p_len);
            p_bpm = g_seq_bpm; p_len = g_dseq_len; p_vol = vol; p_tone = tone;
            char b[6];
            fm1_lcd_fill_rect(12u, 42u, 216u, 26u, COL_STAGE_BG);
            draw_text_blit(14u, 44u, "BPM", COL_DIM, COL_STAGE_BG, 1u);
            seq_num(b, g_seq_bpm); draw_text_blit(14u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            draw_text_blit(70u, 44u, "LEN", COL_DIM, COL_STAGE_BG, 1u);
            seq_num(b, g_dseq_len); draw_text_blit(70u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            draw_text_blit(124u, 44u, "VOL", COL_DIM, COL_STAGE_BG, 1u);
            format_pct(b, g_beat_vol); draw_text_blit(124u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            draw_text_blit(180u, 44u, "TONE", COL_DIM, COL_STAGE_BG, 1u);
            format_pct(b, g_beat_tone); draw_text_blit(180u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            if (len_changed) {
                int big = (g_dseq_len <= 16);
                if (p_big != big) {                          /* 16 wide cells or 32 narrow ones */
                    p_big = big;
                    fm1_lcd_fill_rect(34u, DSEQ_Y0 - 2u, 194u, (uint32_t)NUM_DRUMS * DSEQ_RH + 2u, COL_STAGE_BG);
                }
                p_ver = 0xFFFFFFFFu;
            }
        }
        int step = play ? g_dseq_step : -1;
        int cur = -1;
        uint32_t ver = g_dseq_ver;
        int shown = 16;
        static int p_sel = -1;
        if (p_ver == 0xFFFFFFFFu) p_sel = -1;
        if (p_sel != g_dseq_cur) {                       /* the drum SELECT picked: a lit label */
            p_sel = g_dseq_cur;
            for (int d = 0; d < NUM_DRUMS; d++) {
                uint32_t y = DSEQ_Y0 + (uint32_t)d * DSEQ_RH;
                int on = (d == p_sel);
                fm1_lcd_fill_rect(10u, y - 1u, 23u, DSEQ_RH, on ? DRUM_LAMP[d] : COL_STAGE_BG);
                draw_text_blit(14u, y, DRUM_SHORT[d], on ? 0x0000u : DRUM_LAMP[d], on ? DRUM_LAMP[d] : COL_STAGE_BG, 1u);
            }
            p_ver = 0xFFFFFFFEu;                         /* the lamps of that row brighten */
        }
        if (ver != p_ver) {
            p_ver = ver;
            for (int i = 0; i < shown; i++) dseq_draw_col(i, i == step, i == cur);
            p_step = step;
            p_cur = cur;
        } else {
            if (step != p_step) {
                if (p_step >= 0) dseq_draw_col(p_step, 0, p_step == cur);
                if (step >= 0) dseq_draw_col(step, 1, 0);
                p_step = step;
            }
            if (cur != p_cur) {
                if (p_cur >= 0) dseq_draw_col(p_cur, p_cur == step, 0);
                if (cur >= 0) dseq_draw_col(cur, cur == step, 1);
                p_cur = cur;
            }
        }
    } else if (g_view == VIEW_SEQ) {
        static int p_play = -1, p_rec = -1, p_bpm = -1, p_len = -1, p_gate = -1, p_sw = -1;
        static int p_step = -1, p_cur = -1;
        static uint32_t p_ver = 0xFFFFFFFFu;
        static int p_view_gen = -1;
        if (p_view_gen != g_seq_screen_gen) {               /* the screen was just opened */
            p_view_gen = g_seq_screen_gen;
            p_play = p_rec = p_bpm = p_len = p_gate = p_sw = p_step = p_cur = -1;
            p_ver = 0xFFFFFFFFu;
            fm1_lcd_fill_rect(8u, 8u, 224u, 224u, COL_STAGE_BG);
            draw_text_blit(16u, 16u, "SEQUENCER", COL_GOLD_HI, COL_STAGE_BG, 2u);
            draw_text_blit(14u, 196u, "PLAY:START/STOP  REC:RECORD", COL_GOLD, COL_STAGE_BG, 1u);
            draw_text_blit(14u, 208u, "HOLD REC:CLEAR  SEL:STEP", COL_GOLD, COL_STAGE_BG, 1u);
            draw_text_blit(14u, 220u, "HOLD SEQ, TAP HOME -> STAGE", COL_LEAF_HI, COL_STAGE_BG, 1u);
        }
        int play = g_seq_play, rec = g_seq_rec;
        if (play != p_play || rec != p_rec) {
            p_play = play;
            p_rec = rec;
            fm1_lcd_fill_rect(132u, 14u, 96u, 20u, COL_STAGE_BG);
            draw_text_blit(134u, 20u, play ? "PLAY" : "STOP", play ? COL_LEAF_HI : COL_DIM, COL_STAGE_BG, 1u);
            if (rec) {
                fm1_lcd_fill_rect(172u, 17u, 10u, 10u, COL_APPLE_MD);
                draw_text_blit(186u, 20u, "REC", COL_APPLE_HI, COL_STAGE_BG, 1u);
            }
            p_cur = -2;                                      /* the cursor shows only when recording */
        }
        int gate = (int)(g_seq_gate * 100.0f + 0.5f), sw = (int)(g_seq_swing * 100.0f + 0.5f);
        if (g_seq_bpm != p_bpm || g_seq_len != p_len || gate != p_gate || sw != p_sw) {
            int len_changed = (g_seq_len != p_len);
            p_bpm = g_seq_bpm; p_gate = gate; p_sw = sw; p_len = g_seq_len;
            char b[6];
            fm1_lcd_fill_rect(12u, 42u, 216u, 28u, COL_STAGE_BG);
            draw_text_blit(14u, 44u, "BPM", COL_DIM, COL_STAGE_BG, 1u);
            seq_num(b, g_seq_bpm); draw_text_blit(14u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            draw_text_blit(70u, 44u, "LEN", COL_DIM, COL_STAGE_BG, 1u);
            seq_num(b, g_seq_len); draw_text_blit(70u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            draw_text_blit(124u, 44u, "GATE", COL_DIM, COL_STAGE_BG, 1u);
            format_pct(b, g_seq_gate); draw_text_blit(124u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            draw_text_blit(180u, 44u, "SWING", COL_DIM, COL_STAGE_BG, 1u);
            format_pct(b, g_seq_swing); draw_text_blit(180u, 56u, b, COL_TEXT, COL_STAGE_BG, 1u);
            if (len_changed) {                               /* the cells in and out of the pattern */
                static int p_big = -1;
                if (p_big != (int)SEQ_BIG) {                 /* the grid's layout changed: clear it */
                    p_big = (int)SEQ_BIG;
                    fm1_lcd_fill_rect(10u, 74u, 220u, 118u, COL_STAGE_BG);
                }
                p_ver = 0xFFFFFFFFu;
            }
        }
        int step = play ? g_seq_step : -1;
        int cur = (rec && !play) ? g_seq_cur : -1;
        uint32_t ver = g_seq_ver;
        if (ver != p_ver) {                                  /* the pattern changed: every cell */
            p_ver = ver;
            for (int i = 0; i < SEQ_SHOWN; i++) seq_draw_cell(i, i == step, i == cur);
            p_step = step;
            p_cur = cur;
        } else {
            if (step != p_step) {
                if (p_step >= 0) seq_draw_cell(p_step, 0, p_step == cur);
                if (step >= 0) seq_draw_cell(step, 1, 0);
                p_step = step;
            }
            if (cur != p_cur) {
                if (p_cur >= 0) seq_draw_cell(p_cur, p_cur == step, 0);
                if (cur >= 0) seq_draw_cell(cur, cur == step, 1);
                p_cur = cur;
            }
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
    "J5mpqxJyk4rqGRElmam7InKDmssrEjeoqbwhcYOayRsSRZiprCBRBJrJCxJkkJm7OFAFmbmMA3OhicsgIBaZqZwCcpKKyikR"
    "JpmprBJRhIrKGgJkmJi7IEAHmZmbE3OQmMsoIBeZmZwDYaKJyhkRJ5monBFRk4raGgJkoJm6EVAFisgLglShmKwoMAeJuIwR"
    "YZKZyhkhJpmonQFRg5rKGxJGqJisIEAGirkbAlSQqcsgQAaZuQsSZJCZyigwF5mpjAJSkorbCCEmqKmsEmGDmtoaEUWgqawR"
    "UYSZ2AoRc5CZqyBAFZq5DQFzkYrKKCAWmbibAWSCqtkpICWZqawRcYOayhoiNai5rQFxg5rYGhFEqIm8EWGEiskaEUSgqbsQ"
    "cYWJyQkRY5iZqxBhBJrICwJkoJi6GEEWiqmcEnKRmbopMSepuJwhcpKayRkhJpm5nCFxkpnKGRImqKmcIGKTmsobMieoma0h"
    "UZOa2hoSJ6iZrBJhgprJGxM2uJmtIVGDqtkbIjapma0hUYOb2QsjNqmZrSFRg6rKGyI3qJqtMVCDm9kKIkWomawhUISauRwS"
    "JpiarSFRlJrIGhJFqaicIVGUmskZEjW5qJ4hUZKa6SkRU6monSFRk5vZKhJEuKitMUGVitkZElKoia0hMYaa2BkCQ7CZvTFB"
    "BZvJHARBoJq7QTGHmckrE2GgmbtAEReauA0TUKGKyzACJrqwjxMws4v7OAEkuaifMiCUi9opA2OZma0yMIWb2ikEQaiZrTEx"
    "hprZKhNRoJm9MRIWm8kbFEGxic0wESSq2QwjQKKa3CASFKnZDCJBoprMKBM0udmNIkGSi9woA0OpuZ8iMZOb/CgRMri5nyIx"
    "g6v8KRIysLqvMiGFmtoaI1Ogqq4xIQaayQsUQpCavTAyFarpCyNhkZrbOCElqrmOIkGji9wYAyWpuJ4iMZSa+RkSQqipniEi"
    "BJvqGhNioJmtISIUq+kKE2KxmMwwEhSqyQ0iQbKJzSgDJaq4jiJAkor7KQJDqaieITGEmuoqElOoma4xIQSL6hoiUqiYriEh"
    "BZrYCyNSoJnMMCEVmskNElGhidsoEiSauZ8iMZSa6ykSNKm5nyExhJrqGhNTqJmuITEFmtkKIlKhmswgIhWayYwiQqOa7BgS"
    "JKnInCJBlJnqGRJTqZieISGFmtkqEjSpqZ8hMIWK2hkhU7ignhExhIrqCRJTqKitEjKFiuoZEVOoqK0hQYSK2QoSY6CorDAx"
    "BprJGxJUoJitEDIGiskLEmOhmK0oIRaZyQsSYqKJzBgSNaq4nSFRk4nMKhFFqKisEUEEmcoLIWShidsoESWpuI4RUZKZ6hgR"
    "M6i6rhJRhIrKChJFoJm8IEEVmskLEnORiss5EReoqI0RQZOK6xkCRKionRExhonKChJEkKm8OEEVmcqbI3KSitspITWauZ4S"
    "QYOL6xkRRKiorSExBorKChJzkJnLICEXmrgLAnOiibwoMSaayYsScpKZ2xghJZm5nRJRg4vrGRE1qKmeETEFmtoJAkSgqK0R"
    "MQeKuRsCZJCZyyAwB4m5CwJzkZnLKCEXmbmLEnOjmcwQIBeZuIsScZKJvCgRNZm5ngJQlInJGhFUqJisEVCEicoaAVSgmKwg"
    "MAeJuQwBY5GJvCggJ5m4jRFBkwq9KQEnqLCcEWCTidoJETWouJ4CQISJ2hkQY5ioqyBwg5nZGgFUmKicAVCEicoJAVSYmKwR"
    "QASJ2goRY5CorCAwB4nJCgFUkJi8IDAHibkLAVWQmKwQSAWJyQoBc5CYuxBAB4m4DAFjkJjLEDAHibkLAmSQmLsoQAeJuQsC"
    "c5GYvBAwF5m4jAJjkJjLECAXmaiMAlKRmcsYMReZyAsCcpGZuxBBB4m5CxFkkImsEDAHibmLEmSQmMsQQAWKyAsCY6GYvCBA"
    "BYnJCwFkkJi7KEEVmboNAWORmdsQIBeZuIsCc5GJyzkQJ5mpjBFhkpnKKhImmamdIWCSidoZAjWaqJ4hQJOK6hkRRJmZnRFB"
    "g5rqGgI1qJmeEUCEirobEmWYiawgMQaayAsDVKCJvCAxF5rICyJyoIjLIBEWmriMEmOhidsoESWpuY0hUpKa6ikRJamorTFh"
    "koraKRE0uKmuMUGUitoqAlSomcsxUZOa6ioCVJmJvDJAlIrpGYNiqJi7QTCWidkZAmKomKsxQYWL2SqDY6iYvEEghYrJGgNz"
    "qJi7MkGFmukZg2KomLtCIIaKyRoEUKCJrCEhB4vICgRBoInMIREGiqkNE1Cgido4AhSpyI0jQKKa+ziCJKrAjTI4pYrpKAIy"
    "ubifIjCWitkpAlO5iJ4xIJSa6CoDQ6mZrkEQhIq6HARiqJisQRCFmrgbFGGgibxAERSbyQwjYaCJ2zABJauwDhJQoYnqOAEk"
    "qriOIlChCesoAkO6qA8hMLOK+ymDNLmpnzIwowv8KIFTqZidIjCUC/kZAlKoiawxIYaK2SoCYqCJvEARhYrIGwNioYrbOBIV"
    "msgNEkCjmts5EiWayJwiQZSK+ikCQqmonSIwhprYKgJTqJm8IjEHitkaElKgib0hIQeKyAoSYaGJvDARB4mpjBJhkYnbKBE0"
    "qriPEUGSiuopEUOomZ8RIYSK2RsDcpCJyyAhFYrJjBJRo5nrGBFEqaisIlCEitkKEmOgmLwhMAeZuA0CUqGYvCgSJ5qonRJB"
    "k4rrKhI0qKmvIUCEitoZEWOYmawhMQaKyhsSc5CZyyAxBorYGxFioZjLKCEWmcibE2KSiusYESWZqZ0SQYOL+xkRNKmwriEx"
    "BYraChJToZm9IDEXmrgNEVKRmesoESSZyY0SQZKZzBghNam5jhFBlArbKQE1qaieEUGDC/oZAUOguK0hMQaK6QkRUqCYvDAh"
    "B4nJChFioZjbICAViskLAnKiibwgEReZuIwSYaKJ2yggNKnIjRFQkwrrKRBTqJitITAGiskKAnOgiKwgIBaKuQ0CYZGJyygg"
    "JZnIjBFggoraGRE0mLifATAGisgKgmOQmMsQMBaZyIsCYqOJvCkgJ6iwjgExhYrKGQFFqKCdATGGickKAnOgkKwROAaJuQyC"
    "c6GIrBAgFoq4DQFRkonrKBglmaidAlCTidsZEUSooJ4RMIWJ2hmBRKCYvBFABorICgJSkYm9IBAXiakNAUGjiesYECWZsJ4C"
    "QIQK2hkQY5iYnRE4BQrZGoFjkJisEDAWisgLAWOiCb0QIBaJyIsCcYGJ2hgQNJm4nhFQk4nbKQBFqKicEVGTifoZADSYqJ4B"
    "MYWJ2RqBRJCYrRAwForJCgFykZjLECAWibmMAXORicsoICaauIwRcaKIyxggJpmojgExlInqGQFEmaCdETCFidkaAUSgmJ4Q"
    "IQWKyRsCc5CJvCEwBpnICwJUoJjbICAGibkMAmKRmcsoIReZuA0RQZKZzCggJZm5jhFBk4rrKRAlmLmdElGjiesZESWYqY4B"
    "QZOK6wgRJpm4nBJxkZjKGBEmirmcImGSicwYEjSa2YsxcZGY2ygRNJq5niJQkpnbKQImmbidElGSicwYAjWayKsyYZOKzBkD"
    "NZnYmzFAhZnKGhNjmKmsQCAGiroLFGOYqJ0gEQeZuQoTcpCorDARB4m6ixVRoJi8MBEXmbmLI3KRmbw4EiapyZszYaOa6ykD"
    "NKnJrEIwlZnqKQJSqKidMSCGiskaBEGgqawxIQeZyhoEUaCZrEARBYrKCyRQoJi8QBAVisqLFUChicwwARWZyQwiUKKKvDiD"
    "J5q4jSJAo5rrKINEmridMkCkidspA1OZyJwxIYaK2hkDU5morTEwh4rIChNiqJC8QBAGirkKI2GxiL0gEiWayYwiUaKJzCkS"
    "VKmonDFBlIrbGRJUmaidMCEFi8obI3OgmK0wEQaKyQoicaCQrCAhFZrJDBJSoQnNIBAVqbiMImGiiesoASWpuI0SQaMazRgB"
    "NamonhFBkwr6GQFimJi7IUEFC9kKAmKhmMsQIRaZyIsSYqIJzBgBNbignxEwhQraGQFjmJisETAHCskaAWKhCa0gEBWZyAsB"
    "cqKIvCgBNpqwjgFBkwr7GAA0qaCeAjCFidoZATSYqJ8BMAWKyRuCVJCYvCAgF4nJG4JyoYi7KDAnitiKAlGSicwoACaZsI2C"
    "UJMK+RiAQ5iYrQIwBQrZGoFjkJi8ISgmmsCLAnCSCcspEDWosJ8BQAMK+xmBU6CIrRE4FYroGoFSoQitECglisgMAVGSCcwY"
    "ADWZsJ4CMIUJ2hmARKCQrRE4BYnYCoFikYi8IBgXibiLgnGCCcwYEDSJuI8AQAOJ6wkBY4ignQAwBQnZCgFSkZC9ECgmiciM"
    "AVGCCcwYACWYsI4AMQSJ6gkQUpCQnQA4FYnYioFEgJitADAWidgKAGKRiK0AMBUJ6QoAYpGYrAAxFgrZCgBjoZCtEBEGCckK"
    "AWKRmLwQISaK2AsBYpGIvBghJpnQCwBSk4nMKRAmmcAMgVGSCcwQEDSZyI0BQZMJzRgRJKjQiwFikgnMGBA1mciMAVKSCdwY"
    "ECWZqJ0CQZMJ7BgANKionhFAgwr7GABDsKCeETAECvsIEFKgoJ0BMYWJ2hkBU6CgnhAwBYroGQBDoKCuESEFiugZAEKhmJ4Q"
    "MAQK6hkAU6CgrSEwBYrpCQFToKCeETCECuoZEFOwoJ0RQIQK6ikAQ5iojwEwhArbKQBEqKCdAUGECuoYgDSooI8BMIQK+hgA"
    "M6ionxEwhArqKQBTqJCeAUCDCuspAGOooZ0RMIUK2ikAU7CQnhEgBYrZGQFSoJidEDAGCtkZAFOgmK0RMAYKyiqBVJiYnRE4"
    "honJGQFToKidIDCHickZgUSgmK0RQISJ2hkBY5ignRFIhInaKQBTmJieESAFitkZAUOwkJ8BIAWJyhoCcqCQrBEgBonJCgJy"
    "kJi7ETAnmtgKEVKhiK4RKBWZ2AoRcZCIrCAQForICxJhoom8ICAXCtgKAVGiiLwoETWKyY0CUJOK2ykRRKjAnAJBhArrGABj"
    "mJisAkAEitoZAVOgmK0RMAaJ2RoBYpCYrBAhForIDBFRkZi8KCEXickLAmKimLwQESeKyAsRcZEJvCAQFom5DQFioYisKBEW"
    "mcgLEXKhiKwQERaZuA0BQqOJzRgSJJnYixFikonMGBElmciLEXKSibwpEiaZyIwCUaOJ6xggNKnIjRExlonKGQJUqJidIDCG"
    "ickKEnKgkKwREQYKuYsicpGorCgSF4q5izJxopitKCEWiskLEnKhiLwgEReKyIoScZCIrCABJZrYihJhkZi8EBImmsiLImGS"
    "mbwpE0WawJwiQJSJ2xkDY5mwnSEwhonKGgNioKi7QDAHirkLI3GhmK0gEQeJuQsTcqGorDARB4nJChJhkZnLKBIWicmMElGS"
    "mbw5EiaZ2JsTYZKJzBgCJYnJjCFQk5nMKRI0qdCcITGGmdkaEmKYmKwgIQaJygoScpCYrCAQF4m5jBJhkYi8KAFFmbicIWCj"
    "CeoZAVOYqK0hMIYJ2RoBYaGIyyghFYrYCxFhkgm9GBJEiriOETCVCdoaAmOYoKwRIAeJyAsSYZGJ2xAQJZnAjRExlArrGAE0"
    "qcGdAkCECtoZAVOoka4RIAUK2AoBUqEIvRAgJpqwjRFAlAnbKQBEqLGOATgFCtkJAWKggK0RGBYKyAqBYqEIvBAQF4mwjYJB"
    "oxncGABEmaCdAjCFGuoIAFOYkJ4BIIUa2AmBYqCArBE5BgrQGoBikAitAigEidAbgWKhCLwgOBWK4BuBYpEJvCAYJorADIFR"
    "kgnrKBgkmbCOgUGUCdopAFOYoJ4BMIUJ6RkAUpiAngE4BAroCYFCoYCuASgWCsgbgVKiCL0QGCaZwYwBQZMa3BgANKixnwFA"
    "gwr6GAhDmJCPACAECukZAEOYkJ8BIAQK2QmBU5CQngAgFQrYCoFSkZCtGCAWicgLAWKBCb0QECWY2IuCUpMJ3BgoNJm4jwFA"
    "ggn7CAEkmLiOATGECcwZEESgqJ4BMYUJ6hkAU6CgnRAxBAr6CQFDoJCuECEVitkKEVOhmJ8QIBSK2QoRU6GorRAxFQr5ChFR"
    "oZCtECEUiekKAWKhiLwQMRWZ4AsBUpKJ3CggJJrIDQFCkonNEBEUidiLAmKSicwoECWZuI4BQpKJ3BAQJJnAjRExlIn7GCBC"
    "qbGeAjGECvsZETOosK8RMYWJ6hkBQ6CwngFABIrZGgFjoKCsETAHCtgaAVGhiLwgIBaZ0AsCYZEJvCggJajQjAJBkwrsKABD"
    "qLCOgVGCCuopAEOYsJ4BMYYK2RkBUqCQngEgBQrZGQBDoKCeEDAFiekJAVKQmJ0AMQUK6RoBUpCYrREwhgnZGgFioJCsESAG"
    "idkZAFOgkJ4BIAUK2QmBY6CAvBEgBgnZCQBikIisECAWCtgKAGKRiLwQECYK2AoAYaIIvCgANZnAnIJhgonqGABTmKiNEEiE"
    "icoZgUWYqJwQQIUJ2hmBY6CYuxFRhQnLCYJUkJmsIDAHCskKAmKhmKwQMBeKyAsCcZGYyiggForICwJxkQnLKBAWibmMEmCj"
    "idsYESaawIsScJKJyxgRJoq4jRFQk4rbKQEmmbCdEkCUidopADWpsJ0hUJSZ2RkCNJnInCFQlInaGRE0qLieIECFiskaEjSo"
    "uJ8RMIaJyhoSRKiovDFQhInLChNFqLisMWCTiesZAmOYqasxYJSJyhoSVJiprDFAhJnLGgM2mMmcIUGEmdsZAkSYyKsiUISJ"
    "ywoDRYi5vDFgg6jLGxJFkcmrKFIFicuKE3OQsKwoIgeIyooSYpGoyykiJpnJmyJigqmtGRInicmLElGTqbwZIieIypsScpKY"
    "rAkiNZjamxJxgpnLGRImiMqbEnGCmawZEReIuZshcYKYrSkBFgjZigFRkpC8KQEXiLicEWCCiLwZETWIyZ0RQJSAvBkQNQjY"
    "qxBggwDMCgFECLCtAEAEGOsKAVKAoLwAMBcIygsAc4CQrAAoFgjJi4FygZDLGBgWGLmNAFCCgLwJACcIwJsAUIMQ7BkIUoCg"
    "rABIBBDbCwBjAKCsCTAWENqLAHGBgMsIGCUQyoyAUQKQvIkgJijYm4hhg4HMiBgkIMCdiEADAeuKAFMQsb0IMAcAyQsIYgCB"
    "vQgoFhi5jYBSgZLMCRAlGNiLCGCCAbyKADYIwZwIMIYQyoqIVAChrYAQFhjJmoBigYHMCBglCMCbiGGDENwJGEMYwayAQAQQ"
    "64mAU4CirYAoBhDJigligYG8iBgXKMiLmGKBEcyICDQY0oyYQYMg+wkJQxiyrYhIBCDaigljAKKdiTgGKNiKiFKBgq2JKBUg"
    "2IuJcYEBy4kYJRDRq4hRAxHciQlEALKtiEgUKOqKgFMAka2IKBYYyIuIYoGCvIkpFxDImolhAhC8mhg2AMGcCUATINyKCDQR"
    "sa+IIAYQyZqIYwGRvIk4FiDZmohhggGtiRglKNCLqGIBAbyJGSYowZyYUQIgzIkJRAChrYhAEyD6iohTAJKuiDgkEOmakFMB"
    "gcyJKCUQ2JuAQQSBvIsoRQHAnIlBFADbighEAaCtiUAUAdqbGFMCob2JOCcIyJsIYgKAzAkYJRDInIhCA4HMmhA1EdiriVIT"
    "Ac2ZGDUQwKyIUBMBzIoZRAGxrogxJADrighTAqC9mDEWAdqaCFMCob2JMCUC6pqIQxOhvapANBHaq4ljE5G9mjhFAbmsClIz"
    "kMybKEUCubyJQiWAy6soRQKorYoxJ4i5uyhUEri8ikE1gMq7KUQUqLypQSQCy7saRCSwy6pINAO7rolDQ5C8qjk2EsmsiVEy"
    "gcyZGkQSwKuqURQC25oJRBKwvKlYJALLm4pUIqC8qjA2EuqaiUIUkbyqOEUCyauKUySAzJkYUwPIq5pCNYDbmilTE8i7mkEm"
    "gcqrGGMTqbypQUSByqoJRBOorZpANAHLnAlDJKi8qkAlAtqqCTQzwa2pQCMjzJuKRCKin5koJCLbqopEIrKtqkgkItuqimMi"
    "oq6oKCUCyqoKYjKxrLtANBPbugtjM7GtuzEmEtmqC1MzkK6qIDUCybsLUyWQrLowRAPJuwtDNqC7yzhUg6q7DTI1oKu9MFME"
    "qsoLI1Wgmrs4RASqug0iRJCr2ihDBKq6nCRDkbvrKDMVqrqOIlOhquooIhWqqY4iQqGazDgiFprJCyJjoZrbOCIHmrgME2Kg"
    "ibxAIAWayAsEYaCYyjARFZvIHBJhoInLISEVm9gbA2KxidswEQaauAwTYqCJvEAgFJvYGwNyoQq8MCEGmsAMA1GhCusgERSq"
    "uA4SQaIL+ygRM7rIjiJAkov6KAFDuaieIzCkivoYAjO5uK8jQJOb+SkRQ6mZriIhhZrYGhJTsJnMMSEUq9kLFFKQmswwMRS6"
    "2AwiQrKKzSgiFbm4jiJBkpvqKSIkubmeIkKDnOkqEiSoqZ8hMQOc6RoiQ7CarjBBA7rZDCI0oZu+ICMWuamNMVKRmusYIiS4"
    "qp4hQoOb+hoSU6CavDBSBKrYCyJzkIrLIDEWqriNElORmusoISSpuY8RMpSa6xghNLmprhJChKrJKyJFqJmtIEGFmbkMEnOQ"
    "mbsgQQaZyAsScpGKyxAxFanIjBJSopnrKCAlqamdElKSiuoZESWomZ4RQZOK6hkBNaiprRJQhJnJGgFFmJnLEVADitoLElSQ"
    "mdsQMRWayYsDZJCJyygwB5ipjAJikZnKGCEXmaicAmKSitoYICWZqY4BUpGJyykRF5iZnAFikpnZGRE0qLm8InGDmsobEkao"
    "qLshcYOK2goSNaCpvSFQhZmpDAJzkJm6OEAGmbiMAnOhicooIBaZqff/MCOD3wozM8HPKCMD+asxQ4PdijNSwa0oEzP6nBIh"
    "BMwKE0HSuzGBNeqKEhgHuxmCcLGcMZhTyYoSKYe7EKJwsYshmjfKGoJKhawgoFGxDAAJNdoZgTqHqyCwcKELEJlFuhmROhed"
    "EKBgsQuCmTbaGJE6h6sSuHCxGoGaJ7opkisHjBC4YbEKAYo12hiROwerEbhwsgsCqzfaEJErhpshuHCxCxOsJsogoTuHmyHI"
    "YLAqgpsXuiGgTJSMEslRwAgCmyW7MLFblIwiylHAKQGMFbsisGqyGyLrUrgpkosHuiKxS5SNIslRwBkCmxXKIbFLlIwiyVG4"
    "KgONJKwwsUyTjSLJULAqApw0y0ihK5aLMtlAsBoEnCTKMLFLlIwi2THAKgONFLsxwkuki0LZQLAqE44yyzDCS5ONMspQwBiC"
    "mxarMbA6lY0TyUHAGQONBKowwjuVjDLJSLErBLtEyiCzHIWbQshJsRoUy0PKIKMdA6wz2EmyGxSsQ8ogkw4DnDLQSbEaBKtD"
    "yjCiHgONMchJwioSrEPKMKIdg4wy2UjBKQKcM8tAoRyEjCPZSLA5Aq0kyjGxLJSLFMlAsDqDnTPbIqEeA40iyDmzLBOtQskw"
    "oQwUnCLAOqMsA8xRuDCRjSOcI+g6lAwT2kCwOJKeJLoiwSyEDCLKSLE5gq1DySGhDQSLE+g5szsDvVK4MLKPFKoi0DqkKwPM"
    "QsAoko0zrCPYOpQbEusxwSiSnUO6I8grhwsS2UihKYGdM8khsBwFixP5OKIqgr1juDCgDRSbIug5kxwCvFPAOJCNFJoSyEqT"
    "HALbUbAokY0kmxLYOZQbEtxBwSiRjRSpErlKlBsC20GxKKGOJKoSyEsDDALrQbEYoo4kqiHQKoWKEttBsSmijiSqIchLhAsS"
    "60ChKZGOM7kg0CoFiwPqQKEZko4zySDROwWLEvowohqCnkOpEMErBosh+TCiGwKtU7gowhsGmhLZSKIaActTuBChHhSbEdA5"
    "lAsS61GgKYCNJKoRwDsGixL6QKEago0zySDRGhWbIekwohsDnzKwKaIPFKoh6DiTDCHrQbEZko0kuSDROgSMIdpAohuCjTPJ"
    "INEqFKsy+UmSCwOeM8gosh4juzL4OZQLEq1SsCmSDxO5IeA5gw0SrEKwKZKPJLoh0DmDjCLMQrEqgo8jujHQOgSMEtoxwhmC"
    "jiPIIMErBZsi+DiSCxOfMtAosgwVqiHgKJOLI71SsCmjDxO5MfA5ggwSy1GgGaMOI7pA0CmEizLMMbIrhI8jySDSKhOcMvog"
    "ogoEnSPIILEtBJtB2SCzGwSdQsgooh0Tq0LpIKIbBKxDyCiiHhO7Qtg4ohwirULAKaMNI7tC4DmjDCO8QsApkw4Tq0HQOaMM"
    "I8wy0SmCjRS6MuApkwwjvELAKZOOFJsx0CmUDCLaQbgYhI0juzLhOpQMIstRuCiTDhOcMdA5sxwyzELIKIKNBJsy0DmyGyS8"
    "UsgokgwTnEG4ScIaI8wz6SCSDQSMMdhIsBkjrTPLMLMehJtC2EigGhOsFLoxwyyUDDHoMMAZE50zrDDDLJSLQslAuCkDnQWb"
    "IcJKsgtTykG5GASNBJsx0UmxCkLKQroohA2EqjHSObIMQspCqimEjBScMNI5sotjyTG5GQWMBJsw0zqzjEPpMbgaBYwEmzDC"
    "OqSMQrpCyBkEDQOrMNNKoowy6DG4GgWMI6wgw0qijELJQbgJBIwUujizPaSbUslBuAkUnBS6ILQ7lpsy2UGwChSNI7s4tDyE"
    "nEDQMLEKMq0kyRiUHBOsQMA4s4wzvFPIGYMNFKsg0zqUjDHoQKAKE50z2SiSHYSaMOAgooszzEPIGIINI7tI4jmTjEHKQbAJ"
    "BI0juii0LIWbQchIoQsjrVO5GJMeE6wx0DijjDLbQsAZAo0UqjjSSoKMIdhAsRoSnUPJGKMtA5ww4TiiDCKsQ8gZgw0UqzDR"
    "OZSMMcpCsBsjnxS5ILI8BJ0h0DDCCiKsNMkpoy0EnCDBOLOMMupCuAkTjhS6ILM9lJtB2DHBCiKsJcoQkx2EmyHhMKEMQbpD"
    "ySmDDhO7IdM5pA0xykLICAMNE6wwwkqiDCLZMtgpAowUnDDBObMNMtoy2BkTjSOsMMI6pA0iyUHIGBKNE6wxwjqzDTPqMcgp"
    "A40TrCKyPLMNM/kwwCkSnBO7MqQOA40i4SihK0LbMdAoAp0jnDHSKqMMJOowwCiDjQObI9E7pBwz3DHIIIOPEqoj0TuTHTL7"
    "IbAoA58iqiLBK5UbMvo4sTkSvkPJIbIdA4wT8DmiKxO9QsghoQ0jnCLhKpQbE8wywCiSjSOrMvEqlBsi20CgKYOfI6oi4SqU"
    "GxPrQMEoko0jqyPgKoUcEcpQwCCAjCOrI+E7lRsS6kCwOACdM8oSwi2UGxLpMKErEutBwBCBjRSqErEdhQsS2UCwKBGeQroh"
    "oh6ECyHpQLAYEaxTuiGyL5McEdgwwSkRnRSbIuFKsioh+kGpEJINBAwRwDnDKiG8U7ohkh+TCyHhSbEZIq0kuxKTD5QKEdFJ"
    "wRgRyjO7EYQPhAsR0UnBGCC6Q7shlB+TCzDRScEZMdszyxGDH6MKIOM5wRlBykKrEIQNlIow4jjACEGrFKsQhR2Ti0DBWbAJ"
    "MtoyuxAFDZQLINNJsAlBySO7KIUdlIsw0lmwCTLLJJwAhB2UCzjBWLgIMrwlnACkS8MaONBBuQgTjQWbELNrwgkg0DLKgCON"
    "hYsQs3uxGki5NKwAAx2UDBCxeLiAMKoWjICCW8IJKMBSupETHZULGNNYwIAgqRSbkIRbwgko0VG5gCGMhoqAkmqxCTjoQqqA"
    "Eg2FCwiiebCIMLomq5EDHpUKGbJ4uIE4yiWMkAIrphoZ0lCpkTCrB4qIk1yxCDngMqqhQQ2VGpijerAAKcgkm7ISLqQriMN5"
    "qIE5qxcMkAIsszkL4lK6kziNhhuhgVrBEAnAQ6uCOYyHK6CSa8EQGsFCq5Q5m4croAFMwjiasnGqgzqqBwuhAUzCKImxcKkC"
    "O6oXDKEBK6U6qbN5wRAqyEOMkhgMhiuYonqwIAqwY5ujKA2WKpiiesEQGrBDjJIYHZUqqLN5sCAbwEOboykdhhuQoXqxECvI"
    "U4uyOA2GGpmzerEBGshDmqEQHYYrqKJ6sRAayFOpkTmMBgqYolukGRvRYaiQKJoHCpiSTKMqiuJgsAApuheKkIEshgqJsmih"
    "CRnIY5mgIA0GipCRWqIJGsByqJAoixeamIJbpAoZ0VGwiCiaF5qgAj2VigjBYKCYIKomqpABPIYMCLFwoIkgqjW6iBEsh5oI"
    "ommyiji5VbmIIAwHm5GTa6ILANFiuAkgqRa6gQE8l4sBsXCwGhC4JsoAASuGjACyaMEZKKk02hgBCoabAZF6sSsQwFLZEBAL"
    "BZwBolrCGiDIQtkQEIsGjAGROsQrEdBB2BAAigWNAqE6wywBuGLZIACaFJwSkDykHRG4aLgoEZwknRKQLJQdAbBouDgAqyWs"
    "E5AclRwCuGmwKAHLU7oSkQ2FDBKoS8NKgMhRuSGAnBWMg6A7pjsB2VDAIAibJJwDoC2EHAHIWLA4GLwlugSIDIYbAcBJskoI"
    "ylO6E4iMBQsCuEykO4HZYLghGZxDqwOgLYUcAchZoSmA2VK5EoiMFYuCsFyzOwH6UbgBAIwFi4OwTKQqCOBBuBAAjQWKgaE8"
    "pSoY6EG4ARCdFYuBskyjGyD4QKgAEY4ECwCyTLMaIPhBuQGCjgULEMFaoRkQ6UKqAQGNBQsAwlqhCTD5MakQEY8ECwDCSrIZ"
    "MPoyuoIDjwQLEOJJoAghy0OrgpQelAoQ0UiwACGtJJsQpC2jGzDxSLgAIp4UmwGjLaQKMPAwuAAinhSLALM9tBpA6ECpACGN"
    "E4wQw1uyGljJQakAAo0EixDDSsIZQNoyqgATj4SKELJrsRlI2EG5ACKOA4sQtEyyiVDYMbmAFI0EixikS7IKaNgxqYgUjQSL"
    "KMRKoRow6TKqiBQOlIko0kmxiDHaM7sAhh2TijjiSLCIQbsWmoiFLKOLSOJIsIgxqyWrCIUclIoo0liwiDG7JpyAhCyji0jS"
    "WKiIMawGioiELKOLWMBBqZgynAaaCIMtpIo40WComDGrFpuAgz2ji0niQKiJIqsXm4CDPaQLOdFRuIgwqxeLmJVLo4so4lG4"
    "kDC6F5qIAi2VihmzebCASblEm5ARG4cLCaN7sgk60GKpkCCaBwuYk2qxCBrCcamRKKkHiqACS7QJCrR4sJE5qhcLqCE8tRkb"
    "02CokTqpF4ugEj3DGArSUamSKqgXi6ACPLUpirJwsIE7yBYLoCAtlRqpk3mwARuwNZvCKBuHGqmTe7IYC8FzmqIpigcbqIJr"
    "shiLwnKZkSqZFwyhADq0OYyycaiROqonDKCBSqQqisFxsIApqRYLsRA9lRqponCwgSq5J4uxIC2VGpmiebEACrA2q6IoHYYa"
    "qZJ5oQkZwGKokCmaJ4uoAjyWGorCYaiAKqg1q7IoHAcKqaN6sggKsXO4kTqKJ4yYgmuzGgrScagIGYklm6ABTJUbmaJ4sQga"
    "oEW6oSAcBwuZo3mhCQmgVLmQEAsXi5iSerMbCcFyqIgYiQeakAFasxsKwnK4gBmYJ5yREDuXG5iicLCIKLgmq5EgLJcKCZF4"
    "sAgZoES7gTgMBwyIglrBCAixY6qAKIoHi5ACTMMqibJyyAAYqEScgBAalxuIknqwGBmxYroBOakWjJEQS7QribNw2AEouRaM"
    "gQBLwxoIw3i4EBiqFpuSECunG4GyebggGbk1rIIQDIcMgZFKwigJyFO6giCNBQyBkUvDKYjRYKkBGKoGC6OBHpUakLJ5wBEJ"
    "uCSbgwkNhwqRoVrCOInIYakCCaoXC5GALKU5idBQsBEauyeLkpAslTqY2HCoEQqpNKuECR2FK5CoebEgCsljqYIJiwcakKhq"
    "ojmZ2GGoEQqbJguimVulKZjIcJgBCqoXipKJS5Q6meBQoAEKqiaao5gthiqYyGCgEAqqJpqTmSyHKqjBaJAYiaomipGILIUq"
    "mNhooQAJqkSZgZkdBhqgqHmhEIm6VKiBiR0GCpCweqIYicljmYEJDAYKgLh5oRCJukWpkogthRqQyGihGAm6JpmBmSyHGpC4"
    "cKABCosXipGgS5UZmMhxmIAImxcKkKBblBoI2WGYgAibJoqAqFuVGojIYZiAGI0VioCgW5MaCehhqIEImxeKgJBLlAmIyGKo"
    "AIiMB4mQoWqiCRjZUqiAGA0FigDAWaIJKOpSmYAADQWKgLJbowkp+UKpgRCNBQqIokyzGSn4QaiAII4ECojDWrIISdkyqZER"
    "D5UJCMJZoIA4yyWLkIMvoxop8UCogCCchgmIs1uyCEnoQamRIY0Eigi0W7IIWtgimZAiD4MLGMNqoIgw2iSLoAQdlAop0lio"
    "kECqFIugFB6TChnEWZiISLkkjIgTH6OJKNJoqIhAqhSLiIQtowo641CpkDGrBwuYhEvCCDrRQpuQUIuFG5mFW7EJKdJCm7FR"
    "i5YamYRasJA5wRSMoUAbpSqbhVm4kUqwFQywMSumKpuEabiSPJAUHcAwGrQYnIRAqqMtkQQc0TgZtDmshEipkh2SBBzAOCnD"
    "OK0DWJq0LJGFO+ggKcIRnZNBi8Q6iIQ78BAYsiCdglCKox6RAzzgKBixMa2CaKijHaITLPE4CqNAnQFImaMekRMs4SgJo0Cd"
    "kliokx2hEyvyGAmUOOsCSagCDqIhKvIpCZQ5u4NZsIQNoTIr8imYhTrpAjqxAw+hQQrCOpmGKdggKrIxn5JYmKIckSMs8RAK"
    "lBCckligkh2hQgrRKRqGGbqDa7IRjpFRmbI6mgcZyAA5pACdkmCgkRuoJonAKSuHGaqSerIADKBEmcEpiQcKqIFqogiLwXOZ"
    "oSmKBwmpgVqjGYyxcbGRK7g3i7AYS5cJmbJ4sggbsEWpkBkrF4uwknukCQqxcqiQKYoXirAQS5YKibJwoIAboEWqsSAcBwuY"
    "kWmiGgvCc6mAKYoHiqABOpcKiaJgoAkqqDa7oSAth4qYo3mxCAmgRLqRKAsnnICBa7MaCtJiuAgomxebkYJLlByIsXHAACmp"
    "BpqBADuXG4jCcLgBGakWm5IQLJUcgLF4sCgJuDWsgwgchhyRkFnBKAmoQ8oDGpsXDJKQW7JKoLBxuBEJqiabg4ktlSuyoHqy"
    "SZm4RKoDC4sXG7KoerI4qchyqAILixcLo5papDm4yHGwEouaF4qjijqXKaipcaARi5o1irOLPAcrsLh4oiCcqESZkosrFxqw"
    "qXmjKIypVJmSixoXGsCYebIQmrhkmZIKGwcasJhooiiruHSoggsbBwmgmWmjKaq4c6CCjAkXirKKaaMpurBzsYINiRaKsola"
    "lCm6oHGxEQ2JJQuxiVqVGbmgcbERjJgmC6GKSoYaqaBxoAGLmTaKwQk6hwmooGChEJuZRZmhChoXGrioeaMYm8hzoJEKmjeK"
    "sIhMhSqqwXCQgAq4JomgiCsHCbiQeqMZC8hzmJEKihcKqJFrkgiKyHKgkRqaJ4qgkFuUGZrBeJGIGrk2mqGALYYJmcJYsgAb"
    "yVSZoQgcBgqYsXmhABrJRKmRCC2FGpnCaKEIGrlFmqGBLYUamcJooQgqujWpoogthwqIsXmhABrJNJqhgS6EGonRUKCAOawl"
    "iqCSPpQaCeFQoIAYqyaakYAthRqJ4kihCBjKNKmhAQ8FCojBaaEIKdkzuZKBHwQLgMFpsQApylOpgQAOhRqIwlmhGSnKNKqi"
    "Eo+FGojCWaEZKehCqZEBDYUaiLF6oQg56UKagQANhRqIsnuhGSjKQ6qSEQ+ECgiye7EoKthCmpERDgQLCMNaoRkp8EGpgRCM"
    "BQuQpEuzKjrxULgAOJwVi4CzTLMaOfFQqQAgjYUKCLJqsRg52iSqgREfgwsI02mxCDnJQ7qSIQ+FioCyeaAIOclDm5ECH4QL"
    "CMNpsAA5uiabkIMulAoZ01iwGCnZJJqQIR6ECwjCabEIGLlFqpARHYYLgLJ5oAgpuTW6kSAehRuJw2igCCnJNLqiIB2GC5Ci"
    "eqEZGsBjqZAgDIYKmLN6kQkawWGokCiqFwuggkukGorTYbCQOakmjKGBS5UKmaN5sQgquDarsiAeBguYommyCRnQQ6mQKQsn"
    "m7CDXKQaCsFxqIApiiWrsgE9lxqJsWGgCBqpJ6qRADyWK5mheKEZCrBFuZEpCxeLoJJ7oyuKwXKwCBmZJpuhGDyXGpmicLAA"
    "Gqg1m7EoLYcKmKF4sRgKsFSpkRoaF4ugkXqiGYqwZLiAGQoXi6CBerIZCrBkuIAZCheLoJF6oiqKwXOpACqKBwqwgVqjGprC"
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
    "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"
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
static inline float flush_denormal(float x);

/* ---------------- RHYTHM: the drums ----------------
 * In the manner of the late-60s organ rhythm boxes (the Rhythm Ace kind):
 * the drums with a pitch are a pure tuned ping - a resonator struck once - and
 * the snare, hats, cymbal and shakers are soft bursts of filtered hiss, all
 * at one level, as those boxes had no accents. (The square-wave "metal" and
 * the high-pass hiss are kept for other kits: modes 1, 3 and 4.) */
typedef struct {
    float f1, t1amp, t1dec;     /* first ping: Hz, level, length (ms) */
    float f2, t2amp, t2dec;     /* second ping (the snare's body, the rim shot) */
    float namp, ndec;           /* hiss / metal: level, length (ms) */
    float nf, nq;               /* its filter: Hz, damping */
    uint8_t mode;               /* 0 band-pass hiss, 1 high-pass hiss, 2 guiro, 3 metal + hiss, 4 cowbell */
    float metal;                /* mode 3: how much square-wave cluster against hiss */
    float att_ms, pan;
} DrumDef;
static const DrumDef DRUMS[NUM_DRUMS] = {
    /*   f1    amp    dec     f2    amp   dec    namp  ndec    nf     nq  mode metal att    pan */
    {   74.f, 0.72f, 240.f,   0.f, 0.0f,  0.f,  0.00f,   0.f,    0.f, 0.0f, 0, 0.0f, 0.f,  0.00f },  /* BASS DRUM: a round, ringing boom */
    {  190.f, 0.14f,  40.f,   0.f, 0.0f,  0.f,  0.95f, 150.f, 2600.f, 0.8f, 0, 0.0f, 1.f,  0.05f },  /* SNARE: mostly a "chh" of hiss */
    { 1500.f, 0.42f,  22.f, 420.f, 0.22f, 18.f,  0.00f,   0.f,    0.f, 0.0f, 0, 0.0f, 0.f,  0.15f },  /* RIM SHOT */
    {    0.f, 0.0f,    0.f,   0.f, 0.0f,  0.f,  0.50f,  55.f, 6200.f, 1.0f, 0, 0.0f, 1.f, -0.25f },  /* HI-HAT: a short "ts" */
    {    0.f, 0.0f,    0.f,   0.f, 0.0f,  0.f,  0.40f, 800.f, 5800.f, 0.0f, 1, 0.0f, 0.f,  0.30f },  /* CYMBAL: the first kit's bright, long sizzle, kept low */
    { 2300.f, 0.42f,  38.f,   0.f, 0.0f,  0.f,  0.00f,   0.f,    0.f, 0.0f, 0, 0.0f, 0.f, -0.30f },  /* CLAVES */
    {  845.f, 0.32f, 110.f, 560.f, 0.28f, 90.f,  0.00f,   0.f,    0.f, 0.0f, 0, 0.0f, 0.f,  0.25f },  /* COWBELL: two pings */
    {  600.f, 0.50f,  70.f,   0.f, 0.0f,  0.f,  0.00f,   0.f,    0.f, 0.0f, 0, 0.0f, 0.f, -0.35f },  /* BONGO high */
    {  400.f, 0.52f,  95.f,   0.f, 0.0f,  0.f,  0.00f,   0.f,    0.f, 0.0f, 0, 0.0f, 0.f, -0.15f },  /* BONGO low */
    {  290.f, 0.52f, 170.f,   0.f, 0.0f,  0.f,  0.00f,   0.f,    0.f, 0.0f, 0, 0.0f, 0.f,  0.20f },  /* CONGA */
    {    0.f, 0.0f,    0.f,   0.f, 0.0f,  0.f,  1.20f,  45.f, 5200.f, 1.1f, 0, 0.0f, 5.f,  0.35f },  /* MARACAS */
    {    0.f, 0.0f,    0.f,   0.f, 0.0f,  0.f,  0.55f, 190.f, 3000.f, 0.5f, 2, 0.0f, 25.f,-0.20f },  /* GUIRO */
};
#define NUM_DVOICES 8
typedef struct {
    int on, d;
    float ph, inc, ta, tk;               /* first ping */
    float ph2, inc2, t2a, t2k;           /* second ping */
    float na, nk, rise, rise_k;          /* the hiss / metal */
    float lp, bp, sf, sq;
    float gph, ginc;                     /* guiro ratchet */
    float gl, gr;
} DrumVoice;
static DrumVoice g_dv[NUM_DVOICES];
static uint32_t g_dnoise = 0x2545F491u;
static float g_drum_lp_l = 0.0f, g_drum_lp_r = 0.0f;
static float g_beat_acc[2] = { 0.0f, 0.0f };
/* AUTO BASS: where in this block the beat's (or drum sequencer's) kicks fell */
#define AB_MAX_KICKS 4
static int g_kick_off[AB_MAX_KICKS];
static int g_kick_n = 0;
/* the square-wave cluster (the cowbell is its two top ones) */
static const float METAL_HZ[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
static float g_mph[6];

static inline void metal_step(float *m6, float *mcb) {
    float sq[6];
    for (int j = 0; j < 6; j++) {
        g_mph[j] += METAL_HZ[j] * (1.0f / (float)FM1_AUDIO_RATE);
        if (g_mph[j] >= 1.0f) g_mph[j] -= 1.0f;
        sq[j] = g_mph[j] < 0.5f ? 1.0f : -1.0f;
    }
    *m6 = (sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5]) * (1.0f / 6.0f);
    *mcb = 0.5f * (sq[4] + sq[5]);
}

static float drum_env_k(float ms) {
    return ms > 0.0f ? expf(-6.9f / (ms * 0.001f * (float)FM1_AUDIO_RATE)) : 0.0f;
}

/* MIDI OUT of the drums: General MIDI drum notes, each held 50 ms */
static const uint8_t GM_DRUM[NUM_DRUMS] = { 36, 38, 37, 42, 49, 75, 56, 60, 61, 64, 70, 73 };
static int g_mo_dleft[NUM_DRUMS];
static int mo_drum_ch(void) { return (g_midi_multi && g_bank_ch[BANK_RHYTHM]) ? g_bank_ch[BANK_RHYTHM] : 10; }
static void mo_drum(int d, float vel) {
    int ch = mo_drum_ch();
    if (g_mo_dleft[d] > 0) mo_off(ch, GM_DRUM[d]);
    int v = (int)(vel * 100.0f + 0.5f);
    mo_on(ch, GM_DRUM[d], v < 1 ? 1 : (v > 127 ? 127 : v));
    g_mo_dleft[d] = (int)(0.05f * (float)FM1_AUDIO_RATE);
}

static int mo_running_now(void) {
    return g_seq_play || g_dseq_play || g_beat_sel[0] >= 0 || g_beat_sel[1] >= 0;
}

static void drum_hit(int d, float vel) {
    if (d < 0 || d >= NUM_DRUMS) return;
    const DrumDef *df = &DRUMS[d];
    /* the same drum again takes over its own voice (a hi-hat chokes itself);
     * otherwise a free voice, or the quietest */
    int k = -1;
    for (int i = 0; i < NUM_DVOICES; i++) if (g_dv[i].on && g_dv[i].d == d) { k = i; break; }
    if (k < 0) for (int i = 0; i < NUM_DVOICES; i++) if (!g_dv[i].on) { k = i; break; }
    if (k < 0) {
        float lo = 9.0f;
        for (int i = 0; i < NUM_DVOICES; i++) {
            float a = g_dv[i].ta + g_dv[i].t2a + g_dv[i].na;
            if (a < lo) { lo = a; k = i; }
        }
    }
    DrumVoice *v = &g_dv[k];
    const float sr = (float)FM1_AUDIO_RATE;
    float tsc = 0.85f + 0.3f * g_beat_tone;               /* TONE also tunes the pings a touch */
    v->on = 1; v->d = d;
    v->ph = 0.0f; v->inc = df->f1 * tsc / sr; v->ta = df->t1amp * vel; v->tk = drum_env_k(df->t1dec);
    v->ph2 = 0.0f; v->inc2 = df->f2 * tsc / sr; v->t2a = df->t2amp * vel; v->t2k = drum_env_k(df->t2dec);
    v->na = df->namp * vel; v->nk = drum_env_k(df->ndec);
    v->rise = df->att_ms > 0.0f ? 0.0f : 1.0f;
    v->rise_k = df->att_ms > 0.0f ? 1.0f / (df->att_ms * 0.001f * sr) : 1.0f;
    float fc = df->nf * (0.75f + 0.5f * g_beat_tone);
    int onepole = (df->mode == 1 || df->mode == 3);
    if (fc > 7000.0f && !onepole) fc = 7000.0f;
    v->sf = onepole ? 1.0f - expf(-6.2831853f * fc / sr) : 2.0f * sinf(3.14159265f * fc / sr);
    v->sq = df->nq;
    v->lp = v->bp = 0.0f;
    v->gph = 0.0f; v->ginc = 42.0f / sr;
    v->gl = 0.5f * (1.0f - df->pan); v->gr = 0.5f * (1.0f + df->pan);
}

/* a key, MIDI note or sequencer note on the rhythm bank: the twelve drums
 * repeat every octave, from the bass drum on F */
static float dseq_step_len(void) { return 60.0f * (float)FM1_AUDIO_RATE / ((float)g_seq_bpm * 4.0f); }

#define DSEQ_MAX 16
static void drum_note(int note_idx, int is_midi) {
    /* the drum sequencer without REC: step mode, like a Volca - the white keys
     * are the 16 steps of the drum picked with SELECT; a black key plays it */
    if (g_view == VIEW_DSEQ && !g_dseq_rec && !is_midi) {
        int st = white_step(note_idx);
        int dd = (g_dseq_cur >= 0 && g_dseq_cur < NUM_DRUMS) ? g_dseq_cur : 0;
        if (st < 0) { drum_hit(dd, 1.0f); return; }
        if (st < g_dseq_len) { g_dseq[st] ^= (uint16_t)(1u << dd); g_dseq_skip[st] = 0u; g_dseq_ver++; }
        return;
    }
    int m = is_midi ? note_idx - 53 : note_idx;
    int d = ((m % 12) + 12) % 12;
    drum_hit(d, 1.0f);
    if (!g_dseq_rec || g_view != VIEW_DSEQ) return;
    uint16_t bit = (uint16_t)(1u << d);
    if (g_dseq_play && g_dseq_step >= 0) {           /* playing: onto the nearest step */
        int st = g_dseq_step;
        if (g_dseq_acc > 0.5f * dseq_step_len()) {    /* (early for the next one: don't play it twice) */
            st = (st + 1) % g_dseq_len;
            g_dseq_skip[st] |= bit;
        }
        g_dseq[st] |= bit;
    }                                                 /* (stopped: the keys just play) */
    g_dseq_ver++;
}

/* where the clocks are, in beats (-1: not running) */
static float seq_pos_beats(void) {
    return g_seq_play ? ((float)g_seq_step + (float)g_seq_tick / 24.0f) / 4.0f : -1.0f;
}
static float dseq_pos_beats(void) {
    return (g_dseq_play && g_dseq_step >= 0) ? ((float)g_dseq_step + g_dseq_acc / dseq_step_len()) / 4.0f : -1.0f;
}
static float beat_pos_beats(int k) {
    if (g_beat[k] < 0 || g_beat_step[k] < 0) return -1.0f;
    const Beat *b = &BEATS[g_beat[k]];
    float sl = 60.0f * (float)FM1_AUDIO_RATE / ((float)g_seq_bpm * (float)b->spb);
    return ((float)g_beat_step[k] + g_beat_acc[k] / sl) / (float)b->spb;
}
/* join a running clock mid-step (that step's hits are already gone by) */
static void dseq_set_pos(float pos) {
    float x = pos * 4.0f;
    int st = (int)x;
    g_dseq_acc = (x - (float)st) * dseq_step_len();
    g_dseq_step = st % g_dseq_len;
}
static void beat_set_pos(int k, float pos) {
    const Beat *b = &BEATS[g_beat[k]];
    float sl = 60.0f * (float)FM1_AUDIO_RATE / ((float)g_seq_bpm * (float)b->spb);
    float x = pos * (float)b->spb;
    int st = (int)x;
    g_beat_acc[k] = (x - (float)st) * sl;
    g_beat_step[k] = st % b->len;
}

/* beat control, once per block (from audio()) */
static void beat_update(void) {
    /* the drum sequencer */
    if (g_dseq_req_clear) {
        g_dseq_req_clear = 0;
        for (int i = 0; i < DSEQ_STEPS; i++) { g_dseq[i] = 0u; g_dseq_skip[i] = 0u; }
        g_dseq_cur = 0;
        g_dseq_ver++;
    }
    if (g_dseq_len < 1) g_dseq_len = 1;
    if (g_dseq_len > DSEQ_MAX) g_dseq_len = DSEQ_MAX;
    if (g_dseq_cur < 0 || g_dseq_cur >= NUM_DRUMS) g_dseq_cur = 0;
    if (g_dseq_req_play >= 0) {
        int on = g_dseq_req_play;
        g_dseq_req_play = -1;
        if (on && !g_dseq_play) {
            for (int i = 0; i < DSEQ_STEPS; i++) g_dseq_skip[i] = 0u;
            float p = seq_pos_beats();                     /* in step with what is running */
            if (p < 0.0f) p = beat_pos_beats(0);
            if (p < 0.0f) p = beat_pos_beats(1);
            g_dseq_play = 1;
            if (p >= 0.0f) dseq_set_pos(p);
            else { g_dseq_step = -1; g_dseq_acc = 1.0e9f; }
        } else if (!on && g_dseq_play) {
            g_dseq_play = 0;
            g_dseq_step = -1;
        }
    }
    if (g_dseq_sync) {                                   /* the sequencer started: from the top */
        g_dseq_sync = 0;
        if (g_dseq_play) { g_dseq_step = -1; g_dseq_acc = 1.0e9f; }
    }
    if (g_dseq_step >= g_dseq_len) g_dseq_step = g_dseq_len - 1;
    /* the built-in beats (two can layer) */
    int sync = g_beat_sync;
    g_beat_sync = 0;
    for (int k = 0; k < 2; k++) {
        int want = g_beat_sel[k];
        if (want < 0) { g_beat[k] = -1; continue; }
        if (g_beat[k] < 0) {
            float p = seq_pos_beats();                     /* join what is running, in step... */
            if (p < 0.0f) p = beat_pos_beats(k ^ 1);
            if (p < 0.0f) p = dseq_pos_beats();
            g_beat[k] = want;
            if (p >= 0.0f) beat_set_pos(k, p);
            else { g_beat_step[k] = -1; g_beat_acc[k] = 1.0e9f; }   /* ...or start now, from the top */
        }
        if (sync) { g_beat_step[k] = -1; g_beat_acc[k] = 1.0e9f; }
    }
}

/* the beat and the drums for one block, added into bl/br */
static void render_drums(float *bl, float *br, int n) {
    g_kick_n = 0;
    int any = (g_beat[0] >= 0) || (g_beat[1] >= 0) || g_dseq_play;
    for (int i = 0; i < NUM_DVOICES; i++) any |= g_dv[i].on;
    if (!any) return;
    const float sr = (float)FM1_AUDIO_RATE;
    float lpa = 1.0f - expf(-6.2831853f * (3000.0f * powf(7.0f, g_beat_tone)) / sr);   /* 3k .. 21k */
    float vol = g_beat_vol * g_beat_vol * 1.9f;
    float dsl = dseq_step_len();
    for (int i = 0; i < n; i++) {
        if (g_dseq_play) {                                 /* your drum pattern, in sixteenths */
            g_dseq_acc += 1.0f;
            if (g_dseq_acc >= dsl) {
                g_dseq_acc = (g_dseq_acc > 1.0e8f) ? 0.0f : g_dseq_acc - dsl;
                int st = g_dseq_step + 1;
                if (st >= g_dseq_len) st = 0;
                g_dseq_step = st;
                uint16_t h = (uint16_t)(g_dseq[st] & (uint16_t)~g_dseq_skip[st]);
                g_dseq_skip[st] = 0u;
                for (int d = 0; d < NUM_DRUMS; d++)
                    if ((h >> d) & 1u) { drum_hit(d, 1.0f); if (g_mo_drums) mo_drum(d, 1.0f); }
                if (((h >> DR_BD) & 1u) && g_kick_n < AB_MAX_KICKS) g_kick_off[g_kick_n++] = i;
            }
        }
        for (int k = 0; k < 2; k++) {
            if (g_beat[k] < 0) continue;
            const Beat *b = &BEATS[g_beat[k]];
            float step_len = 60.0f * sr / ((float)g_seq_bpm * (float)b->spb);
            g_beat_acc[k] += 1.0f;
            if (g_beat_acc[k] >= step_len) {
                g_beat_acc[k] = (g_beat_acc[k] > 1.0e8f) ? 0.0f : g_beat_acc[k] - step_len;
                int st = g_beat_step[k] + 1;
                if (st >= b->len) {
                    st = 0;
                    if (g_beat_sel[k] >= 0 && g_beat_sel[k] != g_beat[k]) {   /* a new beat starts on the bar */
                        g_beat[k] = g_beat_sel[k]; b = &BEATS[g_beat[k]];
                    }
                }
                g_beat_step[k] = st;
                for (int d = 0; d < NUM_DRUMS; d++) {
                    if ((b->hits[d] >> st) & 1u) {
                        float vel = (d == DR_SD && ((b->soft >> st) & 1u)) ? 0.5f : 1.0f;   /* ghost snares soft */
                        drum_hit(d, vel);
                        if (g_mo_drums) mo_drum(d, vel);
                        if (d == DR_BD && g_kick_n < AB_MAX_KICKS) g_kick_off[g_kick_n++] = i;
                    }
                }
            }
        }
        /* the square-wave cluster: worked out only when a hat, cymbal or cowbell rings */
        float m6 = 0.0f, mcb = 0.0f;
        int mdone = 0;
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < NUM_DVOICES; k++) {
            DrumVoice *v = &g_dv[k];
            if (!v->on) continue;
            float y = 0.0f;
            if (v->ta > 0.0005f) {                       /* the pings */
                y += v->ta * fast_sin(v->ph);
                v->ph += v->inc; if (v->ph >= 1.0f) v->ph -= 1.0f;
                v->ta *= v->tk;
            } else v->ta = 0.0f;
            if (v->t2a > 0.0005f) {
                y += v->t2a * fast_sin(v->ph2);
                v->ph2 += v->inc2; if (v->ph2 >= 1.0f) v->ph2 -= 1.0f;
                v->t2a *= v->t2k;
            } else v->t2a = 0.0f;
            if (v->na > 0.0005f) {
                const DrumDef *df = &DRUMS[v->d];
                float x;
                if (df->mode >= 3 && !mdone) { mdone = 1; metal_step(&m6, &mcb); }
                if (df->mode == 4) x = mcb;
                else {
                    g_dnoise ^= g_dnoise << 13; g_dnoise ^= g_dnoise >> 17; g_dnoise ^= g_dnoise << 5;
                    x = (float)(int32_t)g_dnoise * (1.0f / 2147483648.0f);
                    if (df->mode == 3) x = (1.0f - df->metal) * x + df->metal * m6;
                }
                float h;
                if (df->mode == 1 || df->mode == 3) {   /* high-pass */
                    v->lp += v->sf * (x - v->lp);
                    h = x - v->lp;
                } else {                                /* band-pass */
                    v->lp += v->sf * v->bp;
                    float hp = x - v->lp - v->sq * v->bp;
                    v->bp += v->sf * hp;
                    h = v->bp;
                    if (df->mode == 2) {                /* the guiro's ridges */
                        v->gph += v->ginc; if (v->gph >= 1.0f) v->gph -= 1.0f;
                        h *= (v->gph < 0.45f) ? 1.6f : 0.15f;
                    }
                }
                if (v->rise < 1.0f) { v->rise += v->rise_k; if (v->rise > 1.0f) v->rise = 1.0f; }
                y += v->na * v->rise * h;
                v->na *= v->nk;
            } else v->na = 0.0f;
            if (v->ta == 0.0f && v->t2a == 0.0f && v->na == 0.0f) { v->on = 0; continue; }
            l += y * v->gl; r += y * v->gr;
        }
        g_drum_lp_l += lpa * (l - g_drum_lp_l);
        g_drum_lp_r += lpa * (r - g_drum_lp_r);
        bl[i] += g_drum_lp_l * vol;
        br[i] += g_drum_lp_r * vol;
    }
    g_drum_lp_l = flush_denormal(g_drum_lp_l);
    g_drum_lp_r = flush_denormal(g_drum_lp_r);
}

/* ================= POCKET: the Pocket Piano engine ================= */
typedef struct {
    float ph, inc, tinc;      /* phase, frequency (cycles a sample), glide target */
    float mph;                /* FM: the modulator's phase */
    float env;                /* 0..1 */
    int gate;                 /* key (or arp step) down */
    int gate_left;            /* arp/cascade: samples until the note lets go (-1: held) */
    int note;                 /* the note (MIDI) */
    int dk;                   /* PLAIN: past its attack, fading while held */
} PPVoice;
#define PP_VOICES 8
static PPVoice g_ppv[PP_VOICES];
static int g_pp_held[PP_VOICES];         /* the notes held, in the order pressed (0: free) */
static int g_pp_stack[16], g_pp_nstack;   /* mono modes: the newest held note last */
static int g_pp_keymidi[32];             /* the note each FM-1 key started */
static float g_pp_acc = 0.0f;            /* the pattern clock (samples into the step) */
static int g_pp_arp_i = PP_VOICES - 1, g_pp_arp_oct = -1;      /* the arp's place: restarts on the first note */
static int g_pp_cs_l = 0, g_pp_cs_c2 = 0;          /* the cascade's place */
static int g_pp_sweep = 0;                         /* the sweeper's octave */
static float g_pp_lfo = 0.0f;
static int g_pp_sh = 0; static float g_pp_hold_l = 0.0f;   /* its lo-fi: every other sample held */
static uint32_t g_pp_rng = 0x1234567u;

static float pp_freq_inc(int note) {
    return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f) / (float)FM1_AUDIO_RATE;
}
static int pp_count(void) { int n = 0; for (int i = 0; i < PP_VOICES; i++) n += g_pp_held[i] != 0; return n; }

static void pp_note_on(int note) {
    if (note < 1 || note > 127) return;
    int was = pp_count();
    int j;
    for (j = 0; j < PP_VOICES; j++) if (g_pp_held[j] == note) break;
    if (j == PP_VOICES) for (j = 0; j < PP_VOICES; j++) if (!g_pp_held[j]) { g_pp_held[j] = note; break; }   /* up to 8 */
    if (g_pp_nstack < 16) g_pp_stack[g_pp_nstack++] = note;
    if (was == 0) {                         /* a new chord: the pattern starts from its first note, now */
        g_pp_arp_i = PP_VOICES - 1; g_pp_arp_oct = -1;
        g_pp_cs_l = 3; g_pp_cs_c2 = 2;
        g_pp_sweep = -1;
        g_pp_acc = 1.0e9f;
    }
}
static void pp_note_off(int note) {
    if (g_hold_on) return;                  /* HOLD: the notes stay */
    for (int j = 0; j < PP_VOICES; j++) if (g_pp_held[j] == note) g_pp_held[j] = 0;
    int k = 0;
    for (int i = 0; i < g_pp_nstack; i++) if (g_pp_stack[i] != note) g_pp_stack[k++] = g_pp_stack[i];
    g_pp_nstack = k;
}
static void pp_all_off(void) {
    for (int j = 0; j < PP_VOICES; j++) g_pp_held[j] = 0;
    g_pp_nstack = 0;
}
static void pp_trigger(int v, int note, int gate_len) {
    PPVoice *p = &g_ppv[v];
    p->note = note;
    p->tinc = pp_freq_inc(note);
    if (g_pp_mode != PP_GLIDER || p->env < 0.01f) p->inc = p->tinc;
    p->gate = 1;
    p->gate_left = gate_len;
    /* each step strikes afresh - from wherever the last one's level is, so a
     * note still ringing isn't cut to silence mid-wave (that was the pop) */
    p->dk = 0;
}

/* the BUTTON: the next waveform, or the octave jump */
static void pp_button(void) {
    if (pp_is_fm(g_pp_mode)) g_pp_wave[g_pp_mode] ^= 1u;
    else g_pp_wave[g_pp_mode] = (uint8_t)((g_pp_wave[g_pp_mode] + 1u) & 3u);
}

static inline float pp_wave(int w, float ph) {
    switch (w) {
    case 1: return ph < 0.5f ? 0.8f : -0.8f;                                     /* square */
    case 2: return ph < 0.5f ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);          /* triangle */
    case 3: return 2.0f * ph - 1.0f;                                             /* saw */
    default: return fast_sin(ph);                                                /* sine */
    }
}

/* each mode's level, matched to bank A's everyday setting (measured on a three-note chord) */
static const float PP_LEVEL[PP_MODES] = { 0.351f, 0.410f, 0.767f, 0.489f, 0.358f, 0.758f, 0.596f };   /* (by loudness, to bank A's FLUTE+HARPSI+STRINGS+BASS on a chord) */

/* and each waveform as loud as the others (measured: square loudest; sine,
 * saw and triangle brought up to it) */
static const float PP_WAVE_GAIN[4] = { 1.202f, 1.000f, 1.471f, 1.334f };

static void pp_render(float *bl, float *br, int n) {
    const float sr = (float)FM1_AUDIO_RATE;
    /* a new mode or waveform while notes sound: fade out over 4 ms on the old
     * one, switch in the silence, fade back in - no jump in the wave or level */
    static int rm = -1, rw = 0;
    static float duck = 1.0f;
    int want_m = g_pp_mode, want_w = g_pp_wave[want_m];
    int fading = 0;
    if (rm < 0 || ((want_m != rm || want_w != rw) && duck <= 0.0f)) { rm = want_m; rw = want_w; }
    else if (want_m != rm || want_w != rw) fading = 1;
    const float dstep = 1.0f / (0.004f * sr);
    int m = rm;
    float p1 = g_pp_p1[m], p2 = g_pp_p2[m];
    int w = rw;
    int jump = pp_is_fm(m) ? 12 * rw : 0;
    int arpish = (m == PP_ARP || m == PP_CASCADE || m == PP_FMARP || m == PP_SWEEPER);
    float steps_s = 2.0f * powf(2.0f, p1 * 4.0f);                 /* 2 .. 32 steps a second */
    float step_len = sr / steps_s;
    float gate_frac = (m == PP_FMARP) ? 0.5f : (0.08f + 0.92f * p2);
    int nheld = pp_count();

    /* the plain modes: a voice for each held note (or the newest, for the mono ones) */
    if (!arpish) {
        if (m == PP_GLIDER) {
            int note = g_pp_nstack ? g_pp_stack[g_pp_nstack - 1] + jump : 0;
            PPVoice *p = &g_ppv[0];
            if (note) {
                if (!p->gate || p->note != note) {
                    p->note = note; p->tinc = pp_freq_inc(note);
                    if (m != PP_GLIDER || !p->gate) p->inc = p->tinc;
                    if (!p->gate) p->env = 0.0f;
                }
                p->gate = 1; p->gate_left = -1;
            } else p->gate = 0;
            for (int v = 1; v < PP_VOICES; v++) g_ppv[v].gate = 0;
        } else {
            for (int v = 0; v < PP_VOICES; v++) {
                PPVoice *p = &g_ppv[v];
                if (g_pp_held[v]) {
                    if (!p->gate || p->note != g_pp_held[v]) { p->note = g_pp_held[v]; p->inc = p->tinc = pp_freq_inc(p->note); p->dk = 0; }
                    p->gate = 1; p->gate_left = -1;
                } else p->gate = 0;
            }
        }
    }

    float vib_rate = 0.5f * powf(2.0f, p1 * 5.0f);                 /* 0.5 .. 16 Hz */
    float vib_depth = (m == PP_VIBRATO) ? p2 * p2 * 12.0f : (m == PP_GLIDER ? p2 * p2 * 2.0f : 0.0f);
    if (m == PP_GLIDER) vib_rate = 5.5f;
    float glide_k = 1.0f - expf(-1.0f / ((0.002f + p1 * p1 * 0.6f) * sr));
    float fm_ratio = 2.0f;
    float fm_index = (m == PP_FMARP) ? p2 * 8.0f : 0.0f;
    /* PLAIN: K1 decay while held (0: holds like an organ), K2 tone (dark .. bright) */
    float pl_dec = (m == PP_PLAIN && p1 > 0.02f) ? expf(-1.0f / ((4.0f * (1.0f - p1) * (1.0f - p1) + 0.06f) * sr)) : 1.0f;
    float pl_lp = (m == PP_PLAIN) ? 1.0f - expf(-6.2831853f * (250.0f * powf(70.0f, p2)) / sr) : 1.0f;
    float atk = 1.0f / (0.003f * sr), rel = expf(-1.0f / (0.012f * sr));
    int span = 1 + (int)(p2 * 3.0f + 0.5f);                          /* sweeper: 1 .. 4 octaves */

    for (int i = 0; i < n; i++) {
        /* the pattern clock */
        if (arpish && nheld) {
            g_pp_acc += 1.0f;
            if (g_pp_acc >= step_len) {
                g_pp_acc = (g_pp_acc > 1.0e8f) ? 0.0f : g_pp_acc - step_len;
                int gl = (int)(gate_frac * step_len);
                if (m == PP_ARP || m == PP_FMARP) {
                    /* two-octave arp: the held notes in the order pressed, then again an octave up */
                    int k = 0;
                    do { g_pp_arp_i = (g_pp_arp_i + 1) % PP_VOICES; if (!g_pp_arp_i) g_pp_arp_oct++; k++; }
                    while (!g_pp_held[g_pp_arp_i] && k < PP_VOICES);
                    if (g_pp_held[g_pp_arp_i]) pp_trigger(0, g_pp_held[g_pp_arp_i] + 12 * (g_pp_arp_oct & 1) + jump, gl);
                } else if (m == PP_CASCADE) {
                    /* octave cascade (Chris's shifter): every held note again, each in its own octave */
                    int sh[PP_VOICES];
                    g_pp_cs_l = (g_pp_cs_l + 1) & 3;
                    if (++g_pp_cs_c2 > 2) g_pp_cs_c2 = 0;
                    for (int v = 0; v < PP_VOICES; v++)
                        sh[v] = g_pp_cs_c2 == 0 ? ((g_pp_cs_l + v) & 3) : (g_pp_cs_c2 == 1 ? ((g_pp_cs_l + ((v & 1) * 2)) & 3) : g_pp_cs_l);
                    for (int v = 0; v < PP_VOICES; v++)
                        if (g_pp_held[v]) pp_trigger(v, g_pp_held[v] + 12 - 12 * sh[v], gl);
                    g_pp_rng = g_pp_rng * 1664525u + 1013904223u;          /* a few ms of looseness */
                    g_pp_acc -= (float)((g_pp_rng >> 24) % 5u) * 0.001f * sr;
                } else {
                    /* harmonic sweeper: the held notes step down through the octaves */
                    g_pp_sweep = (g_pp_sweep + 1) % span;
                    for (int v = 0; v < PP_VOICES; v++) {
                        PPVoice *p = &g_ppv[v];
                        if (g_pp_held[v]) {
                            p->note = g_pp_held[v];
                            p->inc = p->tinc = pp_freq_inc(p->note) / (float)(1 << g_pp_sweep);
                            if (!p->gate) p->env = 0.0f;
                            p->gate = 1; p->gate_left = -1;
                        } else p->gate = 0;
                    }
                }
            }
        } else if (arpish) {
            for (int v = 0; v < PP_VOICES; v++) if (g_ppv[v].gate_left < 0) g_ppv[v].gate = 0;
        }
        g_pp_lfo += vib_rate / sr; if (g_pp_lfo >= 1.0f) g_pp_lfo -= 1.0f;
        float vib = (vib_depth > 0.0f) ? powf(2.0f, vib_depth * fast_sin(g_pp_lfo) / 12.0f) : 1.0f;

        float y = 0.0f;
        for (int v = 0; v < PP_VOICES; v++) {
            PPVoice *p = &g_ppv[v];
            if (p->gate_left > 0 && --p->gate_left == 0) p->gate = 0;
            if (p->gate) {
                if (!p->dk) { p->env += atk; if (p->env >= 1.0f) { p->env = 1.0f; if (pl_dec < 1.0f) p->dk = 1; } }
                else p->env *= pl_dec;                     /* PLAIN: fading while held */
            }
            else { p->env *= rel; if (p->env < 0.0005f) { p->env = 0.0f; continue; } }
            if (m == PP_GLIDER) p->inc += (p->tinc - p->inc) * glide_k;
            float inc = p->inc * vib;
            float o;
            if (fm_index > 0.0f) {                         /* FM: a sine bent by a sine */
                p->mph += inc * fm_ratio; if (p->mph >= 1.0f) p->mph -= (float)(int)p->mph;
                float ph = p->ph + fm_index * 0.159f * fast_sin(p->mph);
                ph -= floorf(ph);
                o = fast_sin(ph);
            } else {
                o = pp_wave(w, p->ph);
            }
            p->ph += inc; if (p->ph >= 1.0f) p->ph -= (float)(int)p->ph;
            y += o * p->env;
        }
        if (m == PP_PLAIN) { static float lp = 0.0f; lp += pl_lp * (y - lp); y = flush_denormal(lp); }   /* TONE */
        /* its lo-fi edge: every other sample held */
        if ((g_pp_sh ^= 1) != 0) g_pp_hold_l = y;
        if (fading) { duck -= dstep; if (duck < 0.0f) duck = 0.0f; }
        else if (duck < 1.0f) { duck += dstep; if (duck > 1.0f) duck = 1.0f; }
        float o = duck * g_pp_hold_l * 0.22f * g_pp_vol * PP_LEVEL[m] * (pp_is_fm(m) ? 1.0f : PP_WAVE_GAIN[w]);
        bl[i] += o; br[i] += o;
    }
}

static const uint16_t NOTE_COST_A[NUM_VOICES] = { 49, 76, 54, 59, 81, 61, 58, 40 };
static const uint16_t NOTE_COST_B[NUM_VOICES] = { 117, 60, 55, 77, 95, 69, 60, 39 };   /* (PIANO: estimated) */
static const uint16_t NOTE_COST_C[NUM_VOICES] = { 140, 86, 76, 123, 92, 48, 93, 109 };   /* measured, on bank B's scale */
static const uint16_t NOTE_COST_E[NUM_VOICES] = { 70, 100, 70, 70, 70, 70, 70, 120 };   /* measured, on bank B's scale */
#ifndef CPU_BUDGET
#define CPU_BUDGET   2800
#endif
#define CPU_BASE     525
#define DRUM_COST2   90               /* a second beat on top (measured) */
#define DRUM_COST    225              /* measured: a busy beat, about two bank A notes */
static volatile int g_max_notes = (int)MAX_NOTES;

static int bank_note_cost(int bank, const int *von) {
    const uint16_t *cost = bank == 4 ? NOTE_COST_E : (bank == 2 ? NOTE_COST_C : (bank ? NOTE_COST_B : NOTE_COST_A));
    int sum = 0, top = 0, nv = 0, nh = 0;
    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        if (von[v]) {
            sum += cost[v];
            if (cost[v] > top) top = cost[v];
            nv++;
            if (v == VB_LOWREY || v == VB_MBOX || v == VB_WARM) nh++;
        }
    }
    int per_note;
    if (bank == 4) {
        /* bank E: each voice a wavetable read; stacked ones share the note loop */
        per_note = 45 + sum - (nv > 1 ? 25 * (nv - 1) : 0);
    } else if (bank == 2) {
        /* bank C: each voice is its own; stacked ones (not LOWREY, bank B's code)
         * share the loop (fitted to measured stacks) */
        int nc = nv - (von[VC_LOWREY] ? 1 : 0);
        per_note = 49 + sum - (nc > 1 ? 23 * (nc - 1) : 0);
    } else if (bank) {
        /* bank B: voices share a little per-note work; LOWREY, M.BOX and WARM
         * also share one harmonic series (fitted to measured stacks) */
        per_note = 68 + sum - (nh > 1 ? 27 * (nh - 1) : 0) - (nv > 1 ? 11 * (nv - 1) : 0);
    } else {
        /* bank A: stacked voices share the same oscillators heavily */
        per_note = 41 + top + ((sum - top) * 42) / 100;
    }
    return per_note;
}

static void update_note_cap(void) {
    int per_note = bank_note_cost(g_bank, g_voice_on);
    if (g_midi_multi)                               /* MULTI: the costliest bank that can be playing */
        for (int k = 0; k < 5; k++)
            if (k != BANK_RHYTHM && k != g_bank && g_bank_ch[k]) {
                int c = bank_note_cost(k, g_bank_voices[k]);
                if (c > per_note) per_note = c;
            }
    if (g_mel_level > 0.005f) per_note += 78;
    int budget = CPU_BUDGET - CPU_BASE;
    if (g_beat_sel[0] >= 0 || g_beat_sel[1] >= 0 || g_rhythm_ui || g_dseq_play ||
        (g_midi_multi && g_bank_ch[BANK_RHYTHM])) budget -= DRUM_COST;
    if (g_beat_sel[0] >= 0 && g_beat_sel[1] >= 0) budget -= DRUM_COST2;   /* two beats layered */   /* the drums take their share */
    int cap = budget / (per_note > 1 ? per_note : 1);
    if (cap < 2) cap = 2;
    if (cap > (int)MAX_NOTES) cap = (int)MAX_NOTES;
    g_max_notes = cap;
}

static NoteVoice *g_last_nv = 0;               /* the voice note_on just started */
static uint32_t g_note_serial = 0u;

/* Out of notes while the auto bass is playing: which one to give up. The bass
 * goes last. First the oldest note already fading out (let go), then an old
 * bass tail, then the oldest held note, and a sounding "bum" only if nothing
 * else is left. -1: no auto-bass notes about, use the usual rule. */
static int steal_pick(void) {
    int any_ab = 0;
    for (uint32_t s = 0u; s < MAX_NOTES; s++) if (g_pool[s].ab && (g_pool[s].active || g_pool[s].env > 0.03f)) any_ab = 1;
    if (!any_ab) return -1;
    int best = -1, best_rank = 9;
    uint32_t best_t = 0u;
    for (uint32_t s = 0u; s < MAX_NOTES; s++) {
        const NoteVoice *q = &g_pool[s];
        if (!(q->active || q->sustained || q->env > 0.03f)) continue;
        int held = q->active || q->sustained;
        int rank = q->ab ? (held && !q->ab_fade ? 3 : 1) : (held ? 2 : 0);
        if (rank < best_rank || (rank == best_rank && q->t0 < best_t)) { best = (int)s; best_rank = rank; best_t = q->t0; }
    }
    return best;
}
/* AUTO BASS's zone: the FM-1's lowest 12 keys, or MIDI notes below C3 */
#define AB_ZONE(q) (((q)->is_midi == 0 && (q)->note_idx < 12) || ((q)->is_midi == 1 && (q)->note_idx < 48))

static int note_busy(const NoteVoice *nv) {
    return nv->active || nv->sustained || nv->env > 0.03f;
}

/* is_midi = 0: note_idx is an FM-1 key (follows the octave buttons)
 * is_midi = 1: note_idx is a MIDI note number (plays at its true pitch) */
static void note_on(int note_idx, int is_midi) {
    if (g_rhythm_ui && is_midi != 2 && g_note_bank < 0) { drum_note(note_idx, is_midi); return; }   /* keys and MIDI: drums; the sequencer: the bank underneath */
    if (g_bank == BANK_POCKET && g_note_bank < 0) {        /* POCKET: its own engine */
        int nt = is_midi ? note_idx : 53 + note_idx + 12 * g_octave_shift;
        if (!is_midi && note_idx >= 0 && note_idx < 32) g_pp_keymidi[note_idx] = nt;
        pp_note_on(nt);
        return;
    }
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
    int pick = (busy >= g_max_notes) ? steal_pick() : -1;
    if (pick >= 0) {
        chosen = (uint32_t)pick;                    /* (auto bass about: the bass goes last) */
    } else if (busy >= g_max_notes) {
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
    g_last_nv = nv;
    nv->ab = 0; nv->ab_left = 0; nv->ab_off = 0; nv->ab_soft = 0; nv->ab_fade = 0;
    nv->t0 = ++g_note_serial;
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
    nv->cp_ph = 0.0f; nv->cp_amp = 1.0f; nv->cp_s1 = nv->cp_s2 = 0.0f;
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
    for (int k = 0; k < CF_N; k++) nv->cf[k] = 0.0f;
    for (int k = 0; k < 4; k++) nv->ci[k] = 0;
    nv->cf[CF_FZ_K] = nv->cf[CF_FZ_KT] = 2.0f;
    nv->cf[CF_FZ_GATE] = 1.0f;
    nv->bank = (g_note_bank >= 0) ? g_note_bank : g_bank;
    nv->ch = (g_note_bank >= 0) ? g_note_ch : 0;

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

static void note_off_ch(int note_idx, int is_midi, int ch) {
    for (uint32_t s = 0u; s < MAX_NOTES; s++) {
        if (g_pool[s].active && g_pool[s].is_midi == is_midi &&
            g_pool[s].note_idx == note_idx && (ch < 0 || g_pool[s].ch == ch)) {
            g_pool[s].active = 0;
            g_pool[s].sustained = g_hold_on ? 1 : 0;
            if (!g_hold_on) {
                g_pool[s].stage = 2;
            }
        }
    }
}

static void note_off(int note_idx, int is_midi) {
    if (g_bank == BANK_POCKET) {
        int nt = is_midi ? note_idx : ((note_idx >= 0 && note_idx < 32 && g_pp_keymidi[note_idx]) ? g_pp_keymidi[note_idx] : 53 + note_idx + 12 * g_octave_shift);
        pp_note_off(nt);
    }
    note_off_ch(note_idx, is_midi, -1);
}

static void apply_hold_change(int hold_enabled) {
    g_hold_on = hold_enabled;
    if (!g_hold_on && g_bank == BANK_POCKET) {          /* POCKET: keep only the keys still down */
        pp_all_off();
        for (int n = 0; n < (int)FM1_NOTE_KEYS && n < 32; n++)
            if (fm1_in.notes & (1u << n)) pp_note_on(g_pp_keymidi[n] ? g_pp_keymidi[n] : 53 + n + 12 * g_octave_shift);
    }
    if (!g_hold_on) {
        for (uint32_t s = 0u; s < MAX_NOTES; s++) {
            g_pool[s].sustained = 0;
            if (!g_pool[s].active) {
                g_pool[s].stage = 2;
            }
        }
    }
}

/* the sequencer's own notes are note_on(note, 2): true pitch, kept apart
 * from the keys (0) and from MIDI (1) */
static uint8_t g_seq_sounding[4];
static uint8_t g_seq_out_ch[4];          /* MIDI OUT: the channel each went out on (0: not sent) */
static int g_seq_chord_open = 0;

static void seq_release(void) {
    for (int k = 0; k < 4; k++) {
        if (g_seq_sounding[k]) {
            note_off(g_seq_sounding[k], 2);
            if (g_seq_out_ch[k]) { mo_off(g_seq_out_ch[k], g_seq_sounding[k]); g_seq_out_ch[k] = 0; }
            g_seq_sounding[k] = 0;
        }
    }
}
static void seq_trigger(void) {
    seq_release();
    for (int k = 0; k < 4; k++) {
        uint8_t nt = g_seq[g_seq_step][k];
        if (nt) {
            g_seq_sounding[k] = nt;
            note_on(nt, 2);
            if (g_mo_seq) { int ch = mo_ch(); mo_on(ch, nt, 100); g_seq_out_ch[k] = (uint8_t)ch; }
        }
    }
    g_seq_gate_left = (uint32_t)(24.0f * g_seq_gate + 0.5f);
    if (!g_seq_gate_left) g_seq_gate_left = 1u;
}
static void seq_step_add(int st, int note) {
    for (int k = 0; k < 4; k++) if (g_seq[st][k] == note) return;
    for (int k = 0; k < 4; k++) if (!g_seq[st][k]) { g_seq[st][k] = (uint8_t)note; return; }
}
/* a note you played (MIDI number), from the keys or MIDI: recorded while REC is on */
static void seq_rec_note(int note) {
    if (!g_seq_rec || note < 1 || note > 127) return;
    if (g_rhythm_ui) return;                        /* drum keys go to the drum sequencer */
    if (g_seq_play) {                               /* into the step it falls on */
        int st = (g_seq_tick < 12u) ? g_seq_step : (g_seq_step + 1) % g_seq_len;
        seq_step_add(st, note);
    } else {                                        /* step recording: a new chord clears the step */
        if (!g_seq_chord_open) {
            for (int k = 0; k < 4; k++) g_seq[g_seq_cur][k] = 0;
            g_seq_chord_open = 1;
        }
        seq_step_add(g_seq_cur, note);
    }
    g_seq_ver++;
}
/* once per audio block */
static void seq_run(int frames) {
    if (g_seq_req_clear) {
        g_seq_req_clear = 0;
        for (int i = 0; i < SEQ_STEPS; i++) for (int k = 0; k < 4; k++) g_seq[i][k] = 0;
        g_seq_cur = 0;
        g_seq_chord_open = 0;
        g_seq_ver++;
    }
    if (g_seq_req_play >= 0) {
        int on = g_seq_req_play;
        g_seq_req_play = -1;
        if (on && !g_seq_play) {
            g_seq_play = 1;
            g_seq_step = 0;
            g_seq_tick = 0u;
            g_seq_clk = 0u;
            g_seq_chord_open = 0;
            g_beat_sync = 1;                    /* a running beat starts its bar with it */
            g_dseq_sync = 1;                    /* and so does the drum sequencer */
            mo_transport();                     /* MIDI start before its first note */
            seq_trigger();
        } else if (!on && g_seq_play) {
            g_seq_play = 0;
            seq_release();
        }
    }
    if (g_seq_cur >= g_seq_len) g_seq_cur = 0;
    /* step recording: every key let go -> on to the next step */
    if (g_seq_chord_open) {
        int held = (fm1_in.notes != 0u);
        for (uint32_t s = 0u; s < MAX_NOTES && !held; s++)
            if (g_pool[s].active && g_pool[s].is_midi == 1) held = 1;
        if (!held || !g_seq_rec || g_seq_play) {
            g_seq_chord_open = 0;
            if (!g_seq_play) g_seq_cur = (g_seq_cur + 1) % g_seq_len;
            g_seq_ver++;
        }
    }
    if (!g_seq_play) return;
    /* 96 ticks a beat: one tick is 60 * 44100 / (bpm * 96) samples */
    g_seq_clk += (uint32_t)frames * (uint32_t)g_seq_bpm * 96u;
    while (g_seq_clk >= 60u * (uint32_t)FM1_AUDIO_RATE) {
        g_seq_clk -= 60u * (uint32_t)FM1_AUDIO_RATE;
        uint32_t sw = (uint32_t)(g_seq_swing * 8.0f + 0.5f);
        uint32_t len = (g_seq_step & 1) ? 24u - sw : 24u + sw;
        if (g_seq_gate_left && !--g_seq_gate_left) seq_release();
        if (++g_seq_tick >= len) {
            g_seq_tick = 0u;
            g_seq_step = (g_seq_step + 1) % g_seq_len;
            seq_trigger();
        }
    }
}

static void handle_midi_packet(uint32_t pkt) {
    uint8_t status = FM1_MIDI_STATUS(pkt);
    uint8_t msg_type = status & 0xF0u;
    uint8_t d1 = FM1_MIDI_DATA1(pkt);
    uint8_t d2 = FM1_MIDI_DATA2(pkt);

    /* MULTI: the channel picks the bank(s), whatever is on screen */
    if (g_midi_multi && (msg_type == 0x90u || msg_type == 0x80u)) {
        int ch = (int)(status & 0x0Fu) + 1, note = (int)(d1 & 0x7Fu);
        int on = (msg_type == 0x90u && d2 > 0u);
        for (int k = 0; k < 5; k++) {
            if (g_bank_ch[k] != ch) continue;
            if (k == BANK_RHYTHM) { if (on) drum_note(note, 1); continue; }
            if (on) {
                g_note_bank = k; g_note_ch = ch;
                note_on(note, 1);
                g_note_bank = -1;
                if (k == g_bank) seq_rec_note(note);   /* the bank you are on records into the sequencer */
            } else {
                note_off_ch(note, 1, ch);
            }
        }
        return;
    }
    /* MIDI notes play at their real pitch: no octave folding, and the
     * FM-1's octave buttons don't move them */
    if (msg_type == 0x90u) {
        if (d2 > 0u) {
            note_on((int)(d1 & 0x7Fu), 1);
            seq_rec_note((int)(d1 & 0x7Fu));
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

/* a voice button tapped: on the RHYTHM bank it picks the beat (the lit one
 * again stops it); elsewhere it turns its voice on or off */
static void pp_all_off(void);
static void switch_bank(int nb) {
    int snd = (g_bank == BANK_RHYTHM) ? g_snd_bank : g_bank;
    if (snd == BANK_POCKET || nb == BANK_POCKET) pp_all_off();   /* keys let go elsewhere never reach POCKET: start clean */
    if (nb == BANK_RHYTHM) {
        g_snd_bank = snd;                       /* the sound stays; only the keys change */
    } else if (nb != snd) {
        for (uint32_t v = 0u; v < NUM_VOICES; v++) g_bank_voices[snd][v] = g_voice_on[v];
        for (uint32_t v = 0u; v < NUM_VOICES; v++) g_voice_on[v] = g_bank_voices[nb][v];
    }
    g_bank = nb;
    g_need_full_clear = 1;
}

static void voice_tap(uint32_t v) {
    if (g_bank == BANK_POCKET) {                 /* POCKET: a mode, or the BUTTON */
        if (v < PP_MODES) { if ((int)v != g_pp_mode) { g_pp_mode = (int)v; g_need_full_clear = 1; } }
        else { pp_button(); g_need_full_clear = 1; }
        return;
    }
    if (g_bank == BANK_RHYTHM) {
        /* tap a beat to start it; a second one layers on top; a lit one stops;
         * with two going, a third takes the place of the older at its next bar */
        if (g_beat_sel[0] == (int)v) g_beat_sel[0] = -1;
        else if (g_beat_sel[1] == (int)v) g_beat_sel[1] = -1;
        else if (g_beat_sel[0] < 0 || g_beat_sel[1] < 0) {
            int k = (g_beat_sel[0] < 0) ? 0 : 1;
            g_beat_sel[k] = (int)v;
            g_beat_old = (g_beat_sel[k ^ 1] >= 0) ? (k ^ 1) : k;
        } else {
            g_beat_sel[g_beat_old] = (int)v;
            g_beat_old ^= 1;
        }
        return;
    }
    else g_voice_on[v] = !g_voice_on[v];
}

static void process_controls(void) {
    uint32_t released = 0u;
    uint32_t pressed = fm1_input_edges(&released);

    /* SAVE: a short press opens/closes its screen (on release); holding it
     * for 0.6 s switches the Mellotron flute between its last level (90% at start-up) and 0% */
    static int save_down = 0, save_long = 0;
    static uint32_t save_t0 = 0u;
    if ((pressed & (1u << FM1_BTN_SAVE)) != 0u) {
        save_down = 1;
        save_long = 0;
        save_t0 = g_tick_ms;
    }
    if (save_down && !save_long && (g_tick_ms - save_t0) >= 600u) {
        save_long = 1;
        if (g_mel_level > 0.005f) {                     /* off, remembering where it was */
            g_mel_last = g_mel_level;
            g_mel_level = 0.0f;
        } else {                                        /* back on at that level */
            g_mel_level = g_mel_last;
        }
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

    /* HOME: a tap goes home (as before); holding it 0.6 s goes to bank A,
     * the Lowrey bank. Each bank keeps its own voice buttons. */
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
        switch_bank(0);
        g_view = VIEW_HOME;
        g_need_full_clear = 1;
    }
    if ((released & (1u << FM1_BTN_HOME)) != 0u) {
        home_down = 0;
    }

    /* SCL: a tap toggles HOLD (on release); held 0.6 s it goes to POCKET, and again back to A */
    {
        static int scl_down = 0, scl_long = 0;
        static uint32_t scl_t0 = 0u;
        if ((pressed & (1u << FM1_BTN_SCL)) != 0u) { scl_down = 1; scl_long = 0; scl_t0 = g_tick_ms; }
        if (scl_down && !scl_long && (g_tick_ms - scl_t0) >= 600u) {
            scl_long = 1;
            switch_bank(g_bank == BANK_POCKET ? 0 : BANK_POCKET);
            if (!is_home_page(g_view)) g_view = VIEW_HOME;
        }
        if ((released & (1u << FM1_BTN_SCL)) != 0u) {
            if (scl_down && !scl_long) apply_hold_change(!g_hold_on);
            scl_down = 0;
        }
    }

    if (save_short) {
        g_view = (g_view == VIEW_ENV_AD) ? VIEW_HOME : VIEW_ENV_AD;
    }

    /* FX: a tap steps through the reverb and delay screens (on release);
     * holding it 0.6 s shows just the smiley face, or leaves it */
    static int fx_down = 0, fx_long = 0;
    static uint32_t fx_t0 = 0u;
    if ((pressed & (1u << FM1_BTN_FX)) != 0u) { fx_down = 1; fx_long = 0; fx_t0 = g_tick_ms; }
    if (fx_down && !fx_long && (g_tick_ms - fx_t0) >= 600u) {
        /* each hold shows the next picture: smiley, horse, eye, TV, the intro scene */
        fx_long = 1;
        if (g_view == VIEW_FACE || g_view == VIEW_TRUCK) g_face_sel = (g_face_sel + 1) % 5;
        g_view = (g_face_sel == 4) ? VIEW_TRUCK : VIEW_FACE;
        g_need_full_clear = 1;
    }
    if ((released & (1u << FM1_BTN_FX)) != 0u) {
        if (fx_down && !fx_long) {
            if (g_view == VIEW_FX_REVERB) {
                g_view = VIEW_FX_DELAY;
            } else if (g_view == VIEW_FX_DELAY) {
                g_view = VIEW_HOME;
            } else {
                g_view = VIEW_FX_REVERB;
            }
        }
        fx_down = 0;
    }

    /* GLO: a tap toggles its voice (on release); holding it 0.6 s opens the
     * GLOBAL screen (MIDI channels), or leaves it */
    static int glo_down = 0, glo_long = 0;
    static uint32_t glo_t0 = 0u;
    int glo_tap = 0;
    if ((pressed & (1u << FM1_BTN_GLO)) != 0u) { glo_down = 1; glo_long = 0; glo_t0 = g_tick_ms; }
    if (glo_down && !glo_long && (g_tick_ms - glo_t0) >= 600u) {
        glo_long = 1;
        g_view = (g_view == VIEW_GLOBAL) ? VIEW_HOME : VIEW_GLOBAL;
    }
    if ((released & (1u << FM1_BTN_GLO)) != 0u) { glo_tap = glo_down && !glo_long; glo_down = 0; }

    /* SEQ: a tap toggles its voice (on release); holding it 0.6 s opens
     * the sequencer screen, or leaves it */
    static int seq_down = 0, seq_long = 0;
    static uint32_t seq_t0 = 0u;
    int seq_tap = 0;
    if ((pressed & (1u << FM1_BTN_SEQ)) != 0u) {
        seq_down = 1;
        seq_long = 0;
        seq_t0 = g_tick_ms;
    }
    if (seq_down && !seq_long && (g_tick_ms - seq_t0) >= 600u) {
        seq_long = 1;
        if (g_bank == BANK_RHYTHM || g_view == VIEW_DSEQ)   /* on the RHYTHM bank: the drum sequencer */
            g_view = (g_view == VIEW_DSEQ) ? VIEW_HOME : VIEW_DSEQ;
        else
            g_view = (g_view == VIEW_SEQ) ? VIEW_HOME : VIEW_SEQ;
    }
    if ((released & (1u << FM1_BTN_SEQ)) != 0u) {
        seq_tap = seq_down && !seq_long;
        seq_down = 0;
    }

    /* ENV, LFO and ARP: a tap toggles its voice (on release); holding one
     * 0.6 s goes to its bank - ENV: bank B (PS-20 ...), LFO: bank C (after
     * Broadcast), ARP: WAVES - and holding it again goes back to bank A */
    static const uint32_t BANK_BTN[3] = { FM1_BTN_ENV, FM1_BTN_LFO, FM1_BTN_ARP };
    static const int BANK_OF[3] = { 1, 2, 4 };
    static int bk_down[3], bk_long[3];
    static uint32_t bk_t0[3];
    int bk_tap[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) {
        uint32_t bit = 1u << BANK_BTN[k];
        if ((pressed & bit) != 0u) { bk_down[k] = 1; bk_long[k] = 0; bk_t0[k] = g_tick_ms; }
        if (bk_down[k] && !bk_long[k] && (g_tick_ms - bk_t0[k]) >= 600u) {
            bk_long[k] = 1;
            switch_bank((g_bank == BANK_OF[k]) ? 0 : BANK_OF[k]);
            if (!is_home_page(g_view)) g_view = VIEW_HOME;
            g_need_full_clear = 1;
        }
        if ((released & bit) != 0u) { bk_tap[k] = bk_down[k] && !bk_long[k]; bk_down[k] = 0; }
    }

    /* on the sequencer screen PLAY and REC run the sequencer: PLAY starts or
     * stops it, REC (on release) turns recording on or off, REC held 1 s
     * clears the pattern */
    static int rec_down = 0, rec_long = 0, rec_drums = 0;
    static uint32_t rec_t0 = 0u;
    int in_seq = (g_view == VIEW_SEQ);
    int in_dseq = (g_view == VIEW_DSEQ);              /* the drum sequencer: the same, for the drums */
    if (in_seq || in_dseq) {
        if ((pressed & (1u << FM1_BTN_PLAY)) != 0u) {
            if (in_dseq) g_dseq_req_play = !g_dseq_play;
            else g_seq_req_play = !g_seq_play;
        }
        if ((pressed & (1u << FM1_BTN_REC)) != 0u) {
            rec_down = 1;
            rec_long = 0;
            rec_drums = in_dseq;
            rec_t0 = g_tick_ms;
        }
    }
    if (rec_down && !rec_long && (g_tick_ms - rec_t0) >= 1000u) {
        rec_long = 1;
        if (rec_drums) g_dseq_req_clear = 1;
        else g_seq_req_clear = 1;
    }
    if ((released & (1u << FM1_BTN_REC)) != 0u) {
        if (rec_down && !rec_long) {
            if (rec_drums) g_dseq_rec = !g_dseq_rec;
            else g_seq_rec = !g_seq_rec;
        }
        rec_down = 0;
    }

    /* PLAY (off the sequencer screen): a tap toggles its voice (on release);
     * holding it 0.6 s goes to the RHYTHM bank, and holding it again back to
     * the bank it came from */
    static int play_down = 0, play_long = 0;
    static uint32_t play_t0 = 0u;
    int play_tap = 0;
    if (!in_seq && !in_dseq && (pressed & (1u << FM1_BTN_PLAY)) != 0u) { play_down = 1; play_long = 0; play_t0 = g_tick_ms; }
    if (play_down && !play_long && (g_tick_ms - play_t0) >= 600u) {
        play_long = 1;
        switch_bank((g_bank == BANK_RHYTHM) ? g_snd_bank : BANK_RHYTHM);   /* back to the bank it came from */
        if (!is_home_page(g_view)) g_view = VIEW_HOME;
        g_need_full_clear = 1;
    }
    if ((released & (1u << FM1_BTN_PLAY)) != 0u) { play_tap = play_down && !play_long; play_down = 0; }

    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        uint32_t btn = VOICE_BTN_MAP[v];
        if (btn == FM1_BTN_SEQ) {
            if (seq_tap) voice_tap(v);
            continue;
        }
        if (btn == FM1_BTN_ENV) { if (bk_tap[0]) voice_tap(v); continue; }
        if (btn == FM1_BTN_LFO) { if (bk_tap[1]) voice_tap(v); continue; }
        if (btn == FM1_BTN_ARP) { if (bk_tap[2]) voice_tap(v); continue; }
        if (btn == FM1_BTN_PLAY) { if (play_tap) voice_tap(v); continue; }
        if (btn == FM1_BTN_GLO) { if (glo_tap) voice_tap(v); continue; }
        if ((in_seq || in_dseq) && btn == FM1_BTN_REC) continue;
        if ((pressed & (1u << btn)) != 0u) {
            voice_tap(v);
        }
    }

    int32_t d_sel = fm1_enc_take(FM1_ENC_SELECT);
    int32_t d_k1  = fm1_enc_take(FM1_ENC_KNOB1);
    int32_t d_k2  = fm1_enc_take(FM1_ENC_KNOB2);
    int32_t d_k3  = fm1_enc_take(FM1_ENC_KNOB3);
    int32_t d_k4  = fm1_enc_take(FM1_ENC_KNOB4);
    int32_t d_alg = fm1_enc_take(FM1_ENC_ALGORITHM);
    int32_t d_pre = fm1_enc_take(FM1_ENC_PRESETS);
    /* SELECT: steps through the four banks - A (Lowrey), B, C (Broadcast),
     * WAVES; on the sequencer screen it moves the step cursor */
    if (d_sel != 0 && g_view == VIEW_GLOBAL) {        /* GLOBAL: pick a line */
        int r = g_glob_row + ((d_sel > 0) ? 1 : -1);
        int rows = (g_midi_multi ? 6 : 1) + 4 + 1;
        g_glob_row = r < 0 ? rows - 1 : (r >= rows ? 0 : r);
    } else if (d_sel != 0 && g_view == VIEW_DSEQ) {          /* the drum sequencer's step cursor */
        int c = g_dseq_cur + ((d_sel > 0) ? 1 : -1);    /* (now: which drum the white keys edit) */
        if (c < 0) c = NUM_DRUMS - 1;
        if (c >= NUM_DRUMS) c = 0;
        g_dseq_cur = c;
    } else if (d_sel != 0 && g_view == VIEW_SEQ) {
        int c = g_seq_cur + ((d_sel > 0) ? 1 : -1);
        if (c < 0) c = g_seq_len - 1;
        if (c >= g_seq_len) c = 0;
        g_seq_cur = c;
    } else if (d_sel != 0 && g_view != VIEW_SPLASH) {
        if (!is_home_page(g_view)) {
            g_view = VIEW_HOME;
        } else {
            static const int STOP_BANK[5] = { 0, 1, 2, 4, BANK_POCKET };   /* A, B, C, WAVES, POCKET */
            int b = (g_bank == BANK_RHYTHM) ? g_snd_bank : g_bank;
            int cur = (b == 4) ? 3 : (b == BANK_POCKET ? 4 : b);
            cur = (cur + ((d_sel > 0) ? 1 : 4)) % 5;
            {
                if (STOP_BANK[cur] != g_bank) switch_bank(STOP_BANK[cur]);
                g_view = VIEW_HOME;
                g_need_full_clear = 1;
            }
        }
    }

    /* PRESET: button colors, ALGORITHM: main screen colors */
    if (d_pre != 0) {
        g_btn_theme = (g_btn_theme + ((d_pre > 0) ? 1u : (uint32_t)NUM_THEMES - 1u)) % (uint32_t)NUM_THEMES;
        apply_themes();
        g_need_full_clear = 1;
    }
    if (d_alg != 0 && g_bank == BANK_RHYTHM && (g_view == VIEW_HOME || g_view == VIEW_DSEQ)) {
        /* on the RHYTHM bank's screens ALGORITHM is the tempo (shared with the sequencers) */
        int b = g_seq_bpm + (int)d_alg;
        g_seq_bpm = b < 40 ? 40 : (b > 240 ? 240 : b);
    } else if (d_alg != 0) {
        g_main_theme = (g_main_theme + ((d_alg > 0) ? 1u : (uint32_t)NUM_THEMES - 1u)) % (uint32_t)NUM_THEMES;
        apply_themes();
        g_need_full_clear = 1;
    }

    /* GLOBAL: K1 changes the line picked */
    if (g_view == VIEW_GLOBAL) {
        static const int ROW_BANK[5] = { 0, 1, 2, 4, BANK_RHYTHM };   /* A, B, C, WAVES, RHYTHM */
        if (d_k1 != 0) {
            int nin = g_midi_multi ? 6 : 1;               /* the MIDI IN lines */
            if (g_glob_row == 0) {
                g_midi_multi = !g_midi_multi;
            } else if (g_glob_row < nin) {
                int k = ROW_BANK[g_glob_row - 1];
                int c = g_bank_ch[k] + ((d_k1 > 0) ? 1 : -1);
                g_bank_ch[k] = (int8_t)(c < 0 ? 16 : (c > 16 ? 0 : c));   /* OFF, 1 .. 16 */
            } else if (g_glob_row == nin + 4) {          /* USB AUDIO: SPLIT */
                g_usb_split = !g_usb_split;
            } else {                                      /* the MIDI OUT switches */
                int *sw[4] = { &g_mo_keys, &g_mo_seq, &g_mo_drums, &g_mo_clock };
                int i = g_glob_row - nin;
                *sw[i] = !*sw[i];
                if (i == 3 && !g_mo_clock) mo_send(0xFCu, 0u, 0u);   /* clock off: tell it to stop */
            }
        }
        return;
    }
    /* the drum sequencer: K1 tempo (shared), K2 length, K3 beat level, K4 tone */
    if (g_view == VIEW_DSEQ) {
        int b = g_seq_bpm + 2 * (int)d_k1;
        g_seq_bpm = b < 40 ? 40 : (b > 240 ? 240 : b);
        int l = g_dseq_len + (int)d_k2;
        g_dseq_len = l < 1 ? 1 : (l > DSEQ_MAX ? DSEQ_MAX : l);
        g_beat_vol = clamp01(g_beat_vol + (float)d_k3 * 0.04f);
        g_beat_tone = clamp01(g_beat_tone + (float)d_k4 * 0.04f);
        return;
    }
    /* the sequencer screen: K1 tempo, K2 length, K3 gate, K4 swing */
    if (g_view == VIEW_SEQ) {
        int b = g_seq_bpm + 2 * (int)d_k1;
        g_seq_bpm = b < 40 ? 40 : (b > 240 ? 240 : b);
        int l = g_seq_len + (int)d_k2;
        g_seq_len = l < 1 ? 1 : (l > SEQ_STEPS ? SEQ_STEPS : l);
        float g = g_seq_gate + (float)d_k3 * 0.05f;
        g_seq_gate = g < 0.1f ? 0.1f : (g > 1.0f ? 1.0f : g);
        g_seq_swing = clamp01(g_seq_swing + (float)d_k4 * 0.05f);
        return;
    }

    /* K3: delay feedback on the delay screen (as its label says), decay elsewhere */
    if (g_bank == BANK_POCKET && g_view == VIEW_HOME) {   /* POCKET: K1 reverb, K2 and K4 the mode's knobs, K3 volume */
        g_rev_mix = clamp01(g_rev_mix + (float)d_k1 * 0.04f);                       /* where REV is on the others */
        g_pp_p1[g_pp_mode] = clamp01(g_pp_p1[g_pp_mode] + (float)d_k2 * 0.03f);   /* where DLY is */
        g_pp_vol = clamp01(g_pp_vol + (float)d_k3 * 0.04f);                         /* where DEC is */
        g_pp_p2[g_pp_mode] = clamp01(g_pp_p2[g_pp_mode] + (float)d_k4 * 0.03f);   /* where FLT is */
        return;
    }
    if (g_bank == BANK_RHYTHM && g_view == VIEW_HOME) {   /* the rhythm bank: K3 beat level, K4 tone */
        g_beat_vol = clamp01(g_beat_vol + (float)d_k3 * 0.04f);
        g_beat_tone = clamp01(g_beat_tone + (float)d_k4 * 0.04f);
    } else {
        if (g_view == VIEW_FX_DELAY) {
            g_dly_feedback = clamp01(g_dly_feedback + (float)d_k3 * 0.04f);
        } else {
            g_decay = clamp01(g_decay + (float)d_k3 * 0.04f);
        }
        g_filter_cutoff = clamp01(g_filter_cutoff + (float)d_k4 * 0.04f);
    }

    float s1 = (float)d_k1 * 0.05f;
    float s2 = (float)d_k2 * 0.05f;

    if (g_view == VIEW_ENV_AD) {
        /* SAVE screen: K1 Mellotron flute level, K2 attack (K3 decay, K4 filter) */
        g_mel_level = clamp01(g_mel_level + s1);
        if (g_mel_level > 0.005f) g_mel_last = g_mel_level;
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
    static int key_sent[FM1_NOTE_KEYS];         /* MIDI OUT: channel * 256 + note sent for each key (0: none) */
    uint32_t cur = fm1_in.notes;
    for (int n = 0; n < (int)FM1_NOTE_KEYS; n++) {
        uint32_t bit = (1u << (uint32_t)n);
        int was_down = (g_prev_notes_mask & bit) != 0u;
        int is_down = (cur & bit) != 0u;
        if (is_down && !was_down) {
            note_on(n, 0);
            seq_rec_note(53 + n + 12 * g_octave_shift);
            if (g_mo_keys) {
                if (g_rhythm_ui) mo_drum(n % 12, 1.0f);           /* the RHYTHM bank: drums */
                else {
                    int ch = mo_ch(), nt = 53 + n + 12 * g_octave_shift;
                    mo_on(ch, nt, 100);
                    key_sent[n] = ch * 256 + nt;
                }
            }
        } else if (!is_down && was_down) {
            note_off(n, 0);
            if (key_sent[n]) { mo_off(key_sent[n] >> 8, key_sent[n] & 0xFF); key_sent[n] = 0; }
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
 * Eight more voices (bank B, HOLD ENV). They share the note pool,
 * envelope, Mellotron layer and effects with bank A. Everything per block
 * (pitch tricks, oscillator set-up) is done once in render_note_block. */

#ifndef CP_LEVEL
#define CP_LEVEL 0.53f     /* PIANO's loudness */
#endif
#ifndef MB_LEVEL
#define MB_LEVEL 0.63f     /* M.BOX's loudness, matched to the old music box */
#endif
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
    float cp_inc, cp_dt, cp_g, cp_k, cp_d, cp_a, cp_da;   /* PIANO: pitch step, its BLEP width, filter, level ramp */
    float mb_a, mb_da, mb_b, mb_db, mb_h3;   /* M.BOX (PS-20 vibraphone): its two triangles' levels, ramped */
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
    (void)h2s;

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
          + bb->m_scr * 0.85f * nv->nlp * t;         /* ... then ring it around the tone: the screech (a quiet noise layer) */
        if (bb->m_crush > 1) {                       /* crunch: hold every N samples (aliased buzz) */
            if (++nv->ccount >= bb->m_crush) { nv->ccount = 0; nv->cheld_l = x; }
            x = nv->cheld_l;
        }
        l += x * g_pan_l[1]; r += x * g_pan_r[1];
    }
    if (layers & LAYER_BIT(VB_POCKET)) {
        /* PIANO, after Yamaha's analog CP-30 electronic piano:
         * a square and saw mix (smoothed at their edges, so no aliasing)
         * through a low-pass that closes as the note fades - a bright strike
         * that settles soft and round */
        float ph = nv->cp_ph, dt = bb->cp_dt;
        float saw = 2.0f * ph - 1.0f - poly_blep(ph, dt);
        float ph2 = ph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
        float sq = (ph < 0.5f ? 1.0f : -1.0f) + poly_blep(ph, dt) - poly_blep(ph2, dt);
        ph += bb->cp_inc; if (ph >= 1.0f) ph -= 1.0f;
        nv->cp_ph = ph;
        float in = 0.6f * sq + 0.4f * saw * 0.64f;
        /* two-pole low-pass (state variable, trapezoidal) */
        float hp = (in - (bb->cp_k + bb->cp_g) * nv->cp_s1 - nv->cp_s2) * bb->cp_d;
        float bp = bb->cp_g * hp + nv->cp_s1; nv->cp_s1 = bp + bb->cp_g * hp;
        float lp = bb->cp_g * bp + nv->cp_s2; nv->cp_s2 = lp + bb->cp_g * bp;
        x = bb->cp_a * lp;
        bb->cp_a += bb->cp_da;
        l += x * g_pan_l[2]; r += x * g_pan_r[2];
    }
    if (layers & LAYER_BIT(VB_PS20)) {
        rot_step(&bb->ov);
        float v1 = bb->ov.s, vt = 2.0f * bb->ov.c;
        float v2 = vt * v1, v3 = vt * v2 - v1, v4 = vt * v3 - v2;
        float v5 = vt * v4 - v3, v6 = vt * v5 - v4, v7 = vt * v6 - v5, v8 = vt * v7 - v6;
        x = 0.85f * (0.30f * v1 + 0.17f * v2 + 0.08f * v3 + 0.10f * v4 + 0.025f * v5 + 0.04f * v6 + 0.01f * v7 + 0.03f * v8
          + 0.16f * bb->ob.s);                                   /* the wobbly twin */
        l += x * g_pan_l[3]; r += x * g_pan_r[3];
    }
    if (layers & LAYER_BIT(VB_MBOX)) {
        /* music box: the 2nd harmonic of the shared series is its fundamental
         * (an octave up, in the weights); here the wooden-box twin, the tine's
         * own overtone (6.27x) and the pin's soft pluck */
        /* (now the PS-20 Vibraphone, after the Vital patch: a triangle on the
         * note and one two octaves up, each built from sines: sin - sin3/9 + sin5/25) */
        rot_step(&bb->oi[0]);
        rot_step(&bb->oi[1]);
        float s0 = bb->oi[0].s, q0 = s0 * s0;
        float t0 = s0 - s0 * (3.0f - 4.0f * q0) * (1.0f / 9.0f) + s0 * (5.0f - 20.0f * q0 + 16.0f * q0 * q0) * (1.0f / 25.0f);
        float s1 = bb->oi[1].s;
        float t1 = s1 - bb->mb_h3 * s1 * (3.0f - 4.0f * s1 * s1) * (1.0f / 9.0f);
        nv->strike2 += 0.02f * (1.0f - nv->strike2);         /* a ~1 ms onset, no click */
        x = nv->strike2 * (bb->mb_a * t0 + bb->mb_b * t1);
        bb->mb_a += bb->mb_da; bb->mb_b += bb->mb_db;
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
        x = 1.19f * (nv->aol_lp - nv->aol_hp);                   /* (turned up 1.5 dB) */
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
    if (layers & (LAYER_BIT(VB_SID) | LAYER_BIT(VB_MBOX) | LAYER_BIT(VB_HIGH) | LAYER_BIT(VB_PHONE) | LAYER_BIT(VB_POCKET))) nv->age += blk_s;

    if (layers & LAYER_BIT(VB_PHONE)) {
        /* a dial-up connection that only uses the notes being held (in any
         * octave), so it is always in tune:
         *   0-0.4 s    dial tone: the note, in two octaves
         *   0.4-1.4 s  dialing: 8 beeps, each one held note (now and then its 5th) in two octaves, 75 ms on / 50 ms off
         *   1.4-1.9 s  connected: a soft chime climbing the note (root, 5th, octave, 5th)
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
            /* the "connected" chime: four soft bell notes climbing up the
             * note: root, 5th, octave, then the 5th above, each ringing out */
            static const float CHIME[4] = { 2.0f, 2.0f * FIFTH, 4.0f, 4.0f * FIFTH };
            float ct = (age - 1.4f) / 0.125f;
            int ci = (int)ct;
            if (ci > 3) ci = 3;
            float ph = ct - (float)ci;
            ia_abs = CHIME[ci] * own;
            aa = 0.14f * (1.0f - 0.7f * ph);                /* each one fades as it rings */
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
            pn = 0.07f * nv->aol_chlv;                      /* very quiet (was 0.61: about 19 dB down) */
            ia_abs = 4.0f * HELD(nv->note_idx); aa = 0.035f;  /* faint carrier, in tune */
            uint32_t g = (dice >> 11) % 100u;
            if (g < 3u) {                                   /* rare instant dropout */
                pn = 0.0f; aa = 0.0f;
            } else if (g < 9u) {                            /* a little crackle */
                bb->crackle = 0.18f;
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
    /* M.BOX (the PS-20 vibraphone): the note and two octaves up; WARM: its sub-octave */
    {
        static const float RATIO[3] = { 1.0f, 4.0f, 0.5f };
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
    /* PIANO (CP-30 style): it fades by itself while held (longer
     * low), its filter starts bright and closes in ~0.2 s, and a light 5 Hz tremolo */
    if (layers & LAYER_BIT(VB_POCKET)) {
        float ci = base_inc;                           /* (at the key's own pitch, like the other voices) */
        if (ci > 0.45f) ci = 0.45f;
        float f = ci * (float)FM1_AUDIO_RATE;
        bb->cp_inc = ci; bb->cp_dt = ci;
        float bs = (float)n / (float)FM1_AUDIO_RATE;
        float tau = 2.2f * powf(261.6f / f, 0.35f);
        float a0 = nv->cp_amp, a1 = a0 * expf(-bs / tau);
        nv->cp_amp = a1;
        float fe = expf(-nv->age / 0.18f);
        float fc = f * (1.3f + 3.6f * fe);
        if (fc > 9000.0f) fc = 9000.0f;
        bb->cp_g = tanf(3.14159265f * fc / (float)FM1_AUDIO_RATE);
        bb->cp_k = 1.41421356f;                        /* Q 0.707 */
        bb->cp_d = 1.0f / (1.0f + bb->cp_g * (bb->cp_k + bb->cp_g));
        float t0 = nv->age * 5.0f, t1 = (nv->age + bs) * 5.0f;
        float w0 = 1.0f - 0.05f * (1.0f + fast_sin(t0 - floorf(t0)));
        float w1 = 1.0f - 0.05f * (1.0f + fast_sin(t1 - floorf(t1)));
        bb->cp_a = CP_LEVEL * a0 * w0;
        bb->cp_da = (CP_LEVEL * a1 * w1 - bb->cp_a) / (float)(n > 0 ? n : 1);
    }
    /* M.BOX, after the Vital patch "PS-20 Vibraphone": both triangles fade
     * to nothing in about 1.4 s (nearly straight down), the upper one through
     * a ~2 kHz low-pass that follows the keys a little, and a 3.7 Hz motor
     * tremolo (a triangle LFO from each strike) lifts both levels */
    if (layers & LAYER_BIT(VB_MBOX)) {
        float f = base_inc * (float)FM1_AUDIO_RATE;
        float bs = (float)n / (float)FM1_AUDIO_RATE;
        float e0 = nv->strike, e1 = e0 - bs / 1.36f;
        if (e1 < 0.0f) e1 = 0.0f;
        nv->strike = e1;
        float pa = nv->age * 3.73f, pb = (nv->age + bs) * 3.73f;
        pa -= floorf(pa); pb -= floorf(pb);
        float la = fabsf(2.0f * pa - 1.0f), lb = fabsf(2.0f * pb - 1.0f);   /* 1 .. 0 .. 1 */
        float fc = 2150.0f * powf(f / 261.6f, 0.179f);
        float r4 = 4.0f * f / fc, r12 = 12.0f * f / fc;
        float g4 = 1.0f / sqrtf(1.0f + r4 * r4 * r4 * r4);
        float g12 = (12.0f * f < 20000.0f) ? 1.0f / sqrtf(1.0f + r12 * r12 * r12 * r12) : 0.0f;
        bb->mb_h3 = g4 > 0.0f ? g12 / g4 : 0.0f;
        const float K = MB_LEVEL;
        float a0 = K * e0 * (0.422f + 0.284f * la), a1 = K * e1 * (0.422f + 0.284f * lb);
        float b0 = K * e0 * (0.414f + 0.094f * la) * g4, b1 = K * e1 * (0.414f + 0.094f * lb) * g4;
        float inv = 1.0f / (float)(n > 0 ? n : 1);
        bb->mb_a = a0; bb->mb_da = (a1 - a0) * inv;
        bb->mb_b = b0; bb->mb_db = (b1 - b0) * inv;
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
                x = (0.38f * saw + 0.34f * s2 + 0.19f * s3) * (0.40f + 0.60f * nv->pluck_state);   /* (about 5.5 dB up: still the gentle one) */
                l += x * g_pan_l[VOICE_HARPSI]; r += x * g_pan_r[VOICE_HARPSI];
            }
            if (layers & LAYER_BIT(VOICE_REED)) {
                float sq = (p < 0.5f) ? 0.48f : -0.48f;
                x = 0.19f * sq + 0.10f * s3;               /* (about 6 dB down, to sit with the rest) */
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
            x = 0.133f * (saw_a + saw_b) + 0.0666f * s2;   /* (about 3.5 dB down, to sit with the rest) */
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
            x = (0.267f * s1 + 0.178f * s2 + 0.089f * s4) * (0.92f + 0.08f * lfo_val);   /* 1 dB down */
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

/* ======================= BANK C voices =======================
 * Eight more voices (bank C, HOLD LFO). LOWREY is bank B's own code. */
typedef struct {
    float inc;
    float fz_k, fz_amp;                            /* FUZZ: fold depth, level (with its sputter) */
    float gl_inc, gl_amp; int gl_hold;             /* GLITCH */
    RotOsc fl; float fl_amp, fl_q, fl_qi; int fl_nh, fl_hf;   /* FLICKER: tone, level, bits, harmonics, rate */
    float bu_f1, bu_f2, bu_amp;                    /* BURST */
    float cr_hinc;                                 /* CRUSH: its sample clock's step */
    int ws_pos, ws_len, ws_k; float ws_inv;        /* WASH: where in the blip, which blip */
    float ws_inc[2], ws_g[2];                      /*       this blip and the next */
    float st_amp, st_inc[2], st_e[2], st_de[2];    /* STEPS: hiss level, glints */
} BankCBlock;

#ifndef FUZZ_LEVEL
#define FUZZ_LEVEL 0.24f
#endif
#ifndef GLITCH_LEVEL
#define GLITCH_LEVEL 0.145f
#endif
#ifndef FLICKER_LEVEL
#define FLICKER_LEVEL 0.155f
#endif
#ifndef BURST_LEVEL
#define BURST_LEVEL 0.85f
#endif
#ifndef CRUSH_LEVEL
#define CRUSH_LEVEL 0.34f
#endif
#ifndef WASH_LEVEL
#define WASH_LEVEL 0.75f
#endif
#ifndef STEPS_LEVEL
#define STEPS_LEVEL 0.40f
#endif

static float svf_coef(float hz) {                     /* Chamberlin f, kept stable */
    if (hz > 6000.0f) hz = 6000.0f;
    if (hz < 20.0f) hz = 20.0f;
    return 2.0f * sinf(3.14159265f * hz / (float)FM1_AUDIO_RATE);
}

static void bank_c_prepare(NoteVoice *nv, uint32_t layers, float inc, float lfo_mid, int n, BankCBlock *bc) {
    float blk_s = (float)n / (float)FM1_AUDIO_RATE;
    float age = nv->age;
    float *cf = nv->cf;
    bc->inc = inc;
    (void)lfo_mid;
    nv->lfsr ^= nv->lfsr << 13; nv->lfsr ^= nv->lfsr >> 17; nv->lfsr ^= nv->lfsr << 5;
    uint32_t dice = nv->lfsr;

    if (layers & LAYER_BIT(VC_FUZZ)) {
        /* a triangle folded back on itself: a metallic, buzzy fuzz. How
         * hard it folds jumps about, and it sputters (quick dips) */
        if (((dice >> 3) & 7u) == 0u) cf[CF_FZ_KT] = 1.2f + (float)((dice >> 8) % 100u) * 0.05f;   /* 1.2..6.2 */
        cf[CF_FZ_K] += 0.35f * (cf[CF_FZ_KT] - cf[CF_FZ_K]);
        bc->fz_k = cf[CF_FZ_K];
        uint32_t sp = (dice >> 16) % 100u;
        float gt = (age > 0.08f && sp < 14u) ? 0.12f : 1.0f;      /* sputter */
        cf[CF_FZ_GATE] += 0.6f * (gt - cf[CF_FZ_GATE]);
        bc->fz_amp = FUZZ_LEVEL * cf[CF_FZ_GATE];
    }
    if (layers & LAYER_BIT(VC_GLITCH)) {
        /* events of 3..12 blocks (17..70 ms): play, an octave up, an octave
         * down, a stutter gap, a crunchy hold */
        if (--nv->ci[CI_GL_SEG] <= 0) {
            nv->ci[CI_GL_SEG] = 3 + (int)((dice >> 4) % 10u);
            uint32_t r = (dice >> 12) % 100u;
            nv->ci[CI_GL_MODE] = age < 0.12f ? 0 : (r < 34u ? 0 : (r < 52u ? 1 : (r < 64u ? 2 : (r < 82u ? 3 : 4))));
        }
        float mul = 1.0f, amp = 1.0f; int hold = 1;
        switch (nv->ci[CI_GL_MODE]) {
            case 1: mul = 2.0f; break;
            case 2: mul = 0.5f; break;
            case 3: amp = 0.0f; break;                     /* stutter gap */
            case 4: hold = 6; break;                       /* crunchy */
            default: break;
        }
        bc->gl_inc = inc * mul;
        bc->gl_amp = GLITCH_LEVEL * amp;
        bc->gl_hold = hold;
    }
    float f0 = inc * (float)FM1_AUDIO_RATE;
    if (layers & LAYER_BIT(VC_FLICKER)) {
        /* "Corporeal": the synth's tone, played back like a worn-out sample
         * of a SID chip that degrades the longer it is held - its sample
         * rate and bits drop, it starts to stutter, and by ~2 s it
         * pulses fast (~35 times a second, quicker than the beat) */
        float *G = &cf[CF_FL_G0];                 /* [0] level, [1] target, [2] timer, [3] in a dip */
        float dg = (age - 0.1f) * (1.0f / 2.0f); dg = dg < 0.0f ? 0.0f : (dg > 1.0f ? 1.0f : dg);   /* degrading */
        float w = (age - 0.15f) * (1.0f / 0.85f); w = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);       /* stuttering */
        float w2 = (age - 1.2f) * (1.0f / 0.8f); w2 = w2 < 0.0f ? 0.0f : (w2 > 1.0f ? 1.0f : w2);  /* pulsing */
        bc->fl_hf = 1 + (int)(dg * 7.0f);                              /* 44 kHz down to 5.5 kHz */
        bc->fl_q = 128.0f * exp2f(-4.0f * dg);                          /* 8 bits down to 4 */
        bc->fl_qi = 1.0f / bc->fl_q;
        G[2] -= blk_s;
        if (G[2] <= 0.0f) {
            uint32_t r = (dice >> 4) % 100u;
            float rr = (float)((dice >> 15) % 1000u) * 0.001f;   /* 0..1 */
            float dd = (float)((dice >> 20) % 1000u) * 0.001f;
            if (G[3] > 0.5f || age < 0.15f) {                     /* steady for a moment */
                G[3] = 0.0f;
                G[1] = 0.85f + (float)((dice >> 11) % 16u) * 0.01f;
                float lo = 0.16f + (0.026f - 0.16f) * w + (0.011f - 0.026f) * w2;
                float span = 0.22f + (0.05f - 0.22f) * w + (0.005f - 0.05f) * w2;
                G[2] = age < 0.15f ? 0.15f - age + 0.01f : lo + span * rr;
            } else {                                             /* a dip */
                G[3] = 1.0f;
                uint32_t p_out = (uint32_t)(4.0f + 14.0f * w + 66.0f * w2);    /* full drop-outs */
                float pl = 0.011f + 0.004f * rr;                                /* a fast pulse's gap */
                if (r < p_out) { G[1] = 0.02f + 0.03f * dd; G[2] = (0.02f + 0.026f * rr) * (1.0f - w2) + pl * w2; }
                else if (r < p_out + 12u) { G[1] = 0.6f; G[2] = (0.013f + 0.013f * rr) * (1.0f - w2) + pl * w2; }   /* a wobble */
                else {                                                                                  /* a stutter */
                    float deep = 0.10f + 0.25f * dd;                    /* -9..-20 dB */
                    G[1] = 0.45f + (deep - 0.45f) * w - 0.06f * w2;     /* shallow at first */
                    if (G[1] < 0.03f) G[1] = 0.03f;
                    G[2] = (0.01f + 0.02f * rr) * (1.0f - w2) + pl * w2;
                }
            }
        }
        if (((dice >> 3) % 120u) == 0u) cf[CF_FL_DT] = (float)((int)((dice >> 10) % 25u) - 12);   /* drift */
        cf[CF_FL_DR] += 0.01f * (cf[CF_FL_DT] - cf[CF_FL_DR]);
        float fi = inc * (1.0f + 0.000578f * cf[CF_FL_DR]);
        rot_init(&bc->fl, cf[CF_FL_PH], fi);
        cf[CF_FL_PH] += fi * (float)n; cf[CF_FL_PH] -= (float)(int)cf[CF_FL_PH];
        int nh = (int)(0.42f / fi); bc->fl_nh = nh > 12 ? 12 : (nh < 1 ? 1 : nh);   /* stay under ~18.5 kHz */
        float on = age / 0.04f; if (on > 1.0f) on = 1.0f;
        bc->fl_amp = FLICKER_LEVEL * on;
    }
    if (layers & LAYER_BIT(VC_BURST)) {
        /* one burst of noise when the key goes down, ringing the note and
         * its octave, then fading away (it doesn't repeat) */
        if (cf[CF_BU_T] == 0.0f) {
            cf[CF_BU_T] = 1.0f;
            cf[CF_BU_ENV] = 1.0f;
        }
        cf[CF_BU_ENV] *= expf(-blk_s / 0.9f);
        bc->bu_f1 = svf_coef(f0);
        bc->bu_f2 = svf_coef(f0 * 2.0f);
        bc->bu_amp = BURST_LEVEL * cf[CF_BU_ENV];
    }
    if (layers & LAYER_BIT(VC_CRUSH)) {
        /* the crusher's sample clock sits just off a whole multiple of the
         * note (around 8.8 kHz), so the grit drifts through the wave and
         * pulses. How far off it sits sets the pulse rate: it starts still,
         * then wanders to new rates (about 1..14 Hz) every so often, gliding
         * between them, so the tremolo comes in late and never settles */
        int nmul = (int)(8820.0f / (f0 > 20.0f ? f0 : 20.0f) + 0.5f);
        if (nmul < 1) nmul = 1;
        cf[CF_CR_T] -= blk_s;
        if (cf[CF_CR_T] <= 0.0f) {
            cf[CF_CR_T] = 0.35f + (float)((dice >> 5) % 90u) * 0.01f;          /* next change in 0.35..1.25 s */
            cf[CF_CR_DT] = age < 0.3f ? 0.3f : 1.0f + (float)((dice >> 13) % 130u) * 0.1f;   /* 1..14 Hz */
        }
        cf[CF_CR_D] += 0.03f * (cf[CF_CR_DT] - cf[CF_CR_D]);   /* a slow glide */
        bc->cr_hinc = ((float)nmul * f0 + cf[CF_CR_D]) / (float)FM1_AUDIO_RATE;
        if (bc->cr_hinc > 0.5f) bc->cr_hinc = 0.5f;
    }
    if (layers & LAYER_BIT(VC_WASH)) {
        /* fast blips that swell up and cut off, like tape run backwards,
         * stepping up through the notes you hold; now and then a fifth or
         * an octave instead. Every note shares one clock, so they take turns */
        const int len = 3750;                                  /* 85 ms a blip */
        uint32_t t0 = g_audio_frames - (uint32_t)n;
        uint32_t st = t0 / (uint32_t)len;
        float list[MAX_NOTES]; int m = g_held_n;
        for (int k = 0; k < m; k++) list[k] = g_held_inc[k];
        for (int i = 1; i < m; i++) {                          /* lowest first */
            float v = list[i]; int j = i - 1;
            while (j >= 0 && list[j] > v) { list[j + 1] = list[j]; j--; }
            list[j + 1] = v;
        }
        for (int q = 0; q < 2; q++) {                          /* this blip and the next */
            uint32_t sq = st + (uint32_t)q;
            float g = 0.0f, fi = inc;
            if (m > 0) {
                float want = list[sq % (uint32_t)m];
                if (fabsf(want - inc) < inc * 0.0005f) {
                    uint32_t hsh = (sq * 2654435761u) >> 20;
                    uint32_t r = hsh % 100u;
                    fi = inc * (r < 22u ? 1.5f : (r < 32u ? 2.0f : 1.0f));
                    g = r >= 92u ? 0.0f : WASH_LEVEL;          /* a few rests */
                }
            }
            bc->ws_inc[q] = fi; bc->ws_g[q] = g;
        }
        bc->ws_pos = (int)(t0 % (uint32_t)len);
        bc->ws_len = len; bc->ws_inv = 1.0f / (float)len; bc->ws_k = 0;
        float on = age / 0.01f; if (on > 1.0f) on = 1.0f;
        bc->ws_g[0] *= on; bc->ws_g[1] *= on;
    }
    if (layers & LAYER_BIT(VC_STEPS)) {
        /* "Arc of a Journey", around 4:00: a dark hiss full of short
         * whistling glints, each a high harmonic of your note that swells
         * and fades in 90..300 ms and slides a little */
        for (int q = 0; q < 2; q++) {
            float *G = &cf[CF_ST_G0 + 6 * q];                  /* phase, inc, time, length, level, wait */
            uint32_t h = dice ^ (0x9E3779B9u * (uint32_t)(q + 1));
            h ^= h << 13; h ^= h >> 17; h ^= h << 5;
            float tb = G[2];
            G[2] += blk_s;
            if (G[3] <= 0.0f || G[2] >= G[3] + G[5]) {          /* a new glint */
                /* a fifth or an octave of any note you hold */
                static const float RAT[8] = { 1.5f, 2.0f, 3.0f, 4.0f, 1.5f, 2.0f, 3.0f, 6.0f };
                float src = (g_held_n > 0) ? g_held_inc[(h >> 25) % (uint32_t)g_held_n] : inc;
                float gi = src * RAT[h & 7u];
                while (gi * (float)FM1_AUDIO_RATE > 2400.0f) gi *= 0.5f;
                G[1] = gi * (1.0f + 0.0006f * (float)((int)((h >> 3) % 31u) - 15));
                G[3] = 0.09f + (float)((h >> 8) % 210u) * 0.001f;
                G[4] = 0.5f + (float)((h >> 16) % 50u) * 0.01f;
                G[5] = (age < 0.05f && q == 0) ? 0.0f : (float)((h >> 22) % 250u) * 0.001f;
                G[2] = 0.0f; tb = 0.0f;
                G[0] = (float)((h >> 11) & 255u) * (1.0f / 256.0f);
                if (q == 1 && age < 0.05f) G[5] += 0.12f;
            }
            float u0 = tb / G[3], u1 = G[2] / G[3];
            float e0 = u0 < 1.0f ? fast_sin(0.5f * u0) : 0.0f, e1 = u1 < 1.0f ? fast_sin(0.5f * u1) : 0.0f;
            e0 *= e0 * G[4]; e1 *= e1 * G[4];
            bc->st_e[q] = e0; bc->st_de[q] = (e1 - e0) / (float)n;
            bc->st_inc[q] = G[1] * (1.0f + 0.004f * u0);          /* a little slide up */
        }
        float on = age / 0.15f; if (on > 1.0f) on = 1.0f;
        bc->st_amp = STEPS_LEVEL * on;
    }
    nv->age += blk_s;
}

static inline __attribute__((always_inline)) void render_bank_c(NoteVoice *nv, uint32_t layers, BankCBlock *bc,
                                                                 float *pl, float *pr) {
    float l = 0.0f, r = 0.0f, x;
    float inc = bc->inc;
    float *cf = nv->cf;
    float nz = 0.0f;
    if (layers & (LAYER_BIT(VC_FUZZ) | LAYER_BIT(VC_BURST) | LAYER_BIT(VC_STEPS))) {   /* only these use noise */
        nv->lfsr ^= nv->lfsr << 13; nv->lfsr ^= nv->lfsr >> 17; nv->lfsr ^= nv->lfsr << 5;
        nz = (float)(int32_t)nv->lfsr * 4.6566e-10f;
    }
    if (layers & LAYER_BIT(VC_FUZZ)) {
        /* a triangle (with a little grit) folded through a sine */
        cf[CF_FZ_PH] += inc; if (cf[CF_FZ_PH] >= 1.0f) cf[CF_FZ_PH] -= 1.0f;
        float t = 2.0f * fabsf(2.0f * cf[CF_FZ_PH] - 1.0f) - 1.0f;
        float y = fast_sin(0.25f * bc->fz_k * (t + 0.06f * nz) + 4.0f);
        float o = y - cf[CF_FZ_DC];
        cf[CF_FZ_DC] += 0.004f * o;                         /* DC block */
        x = bc->fz_amp * o;
        l += x * g_pan_l[VC_FUZZ]; r += x * g_pan_r[VC_FUZZ];
    }
    if (layers & LAYER_BIT(VC_GLITCH)) {
        cf[CF_GL_PH] += bc->gl_inc; if (cf[CF_GL_PH] >= 1.0f) cf[CF_GL_PH] -= 1.0f;
        float pp = cf[CF_GL_PH];
        float pq = pp + 0.5f; if (pq >= 1.0f) pq -= 1.0f;
        float sq = pb_saw(pp, bc->gl_inc) - pb_saw(pq, bc->gl_inc);   /* a square */
        if (bc->gl_hold > 1) {
            if (++nv->ci[CI_GL_CNT] >= bc->gl_hold) { nv->ci[CI_GL_CNT] = 0; cf[CF_GL_HELD] = sq; }
            sq = cf[CF_GL_HELD];
        }
        x = bc->gl_amp * sq;
        l += x * g_pan_l[VC_GLITCH]; r += x * g_pan_r[VC_GLITCH];
    }
    if (layers & LAYER_BIT(VC_FLICKER)) {
        /* the synth's own harmonic levels, measured where it plays nearly
         * alone ("Corporeal" 3:07): strongest at the 2nd and 3rd harmonics,
         * falling away above the 5th */
        static const float FLW[16] = { 0.71f, 0.89f, 1.00f, 0.40f, 0.28f, 0.126f, 0.089f, 0.05f,
                                       0.045f, 0.05f, 0.045f, 0.04f, 0.04f, 0.035f, 0.035f, 0.03f };
        rot_step(&bc->fl);
        float h1 = bc->fl.s, tc = 2.0f * bc->fl.c, y;
        if (bc->fl_nh >= 12) {                                 /* the usual case, unrolled */
            float h2 = tc * h1, h3 = tc * h2 - h1, h4 = tc * h3 - h2, h5 = tc * h4 - h3, h6 = tc * h5 - h4;
            float h7 = tc * h6 - h5, h8 = tc * h7 - h6, h9 = tc * h8 - h7, h10 = tc * h9 - h8;
            float h11 = tc * h10 - h9, h12 = tc * h11 - h10;
            y = 0.71f * h1 + 0.89f * h2 + h3 + 0.40f * h4 + 0.28f * h5 + 0.126f * h6 + 0.089f * h7
              + 0.05f * h8 + 0.045f * h9 + 0.05f * h10 + 0.045f * h11 + 0.04f * h12;
        } else {                                               /* very high notes: fewer */
            float hp = 0.0f, hc = h1; y = 0.0f;
            for (int k = 0; k < bc->fl_nh; k++) {
                y += FLW[k] * hc;
                float hn = tc * hc - hp; hp = hc; hc = hn;
            }
        }
        /* worn-out sample playback: resampled with no smoothing (it folds
         * back into grit) and cut to fewer bits, both worse the longer it
         * is held; the level moves in 16 steps, like the SID's volume */
        float *G = &cf[CF_FL_G0];
        G[0] += 0.02f * (G[1] - G[0]);                         /* ~1 ms edges */
        if (++nv->ci[CI_FL_CNT] >= bc->fl_hf) {
            nv->ci[CI_FL_CNT] = 0;
            float gq = (float)(int)(G[0] * 15.0f + 0.5f) * (1.0f / 15.0f);
            float q = bc->fl_q;
            G[4] = gq * (float)((int)(0.38f * y * q + q + 0.5f) - (int)q) * bc->fl_qi;
        }
        x = bc->fl_amp * 2.63f * G[4];
        l += x * g_pan_l[VC_FLICKER]; r += x * g_pan_r[VC_FLICKER];
    }
    if (layers & LAYER_BIT(VC_BURST)) {
        /* mid-band noise (300 Hz..3 kHz) plus the note and its octave ringing in it */
        float bp = nz - cf[CF_BU_HP]; cf[CF_BU_HP] += 0.045f * bp;          /* high-pass ~300 Hz */
        cf[CF_BU_LP] += 0.26f * (bp - cf[CF_BU_LP]);                         /* low-pass ~2 kHz, */
        cf[CF_BU_L1] += bc->bu_f1 * cf[CF_BU_B1];
        cf[CF_BU_B1] += bc->bu_f1 * (nz - cf[CF_BU_L1] - 0.04f * cf[CF_BU_B1]);
        cf[CF_BU_L2] += bc->bu_f2 * cf[CF_BU_B2];
        cf[CF_BU_B2] += bc->bu_f2 * (nz - cf[CF_BU_L2] - 0.04f * cf[CF_BU_B2]);
        cf[CF_BU_LP2] += 0.26f * (cf[CF_BU_LP] - cf[CF_BU_LP2]);           /* ... two poles */
        x = bc->bu_amp * (0.9f * cf[CF_BU_LP2] + 0.07f * (cf[CF_BU_B1] + 0.7f * cf[CF_BU_B2]));
        l += x * g_pan_l[VC_BURST]; r += x * g_pan_r[VC_BURST];
    }
    if (layers & LAYER_BIT(VC_CRUSH)) {
        /* a triangle and a saw, held at a low rate and cut to 3 bits */
        cf[CF_CR_HPH] += bc->cr_hinc;
        if (cf[CF_CR_HPH] >= 1.0f) {
            cf[CF_CR_HPH] -= 1.0f;
            float ph = cf[CF_CR_PH];
            float t = 2.0f * fabsf(2.0f * ph - 1.0f) - 1.0f;
            float sw = 2.0f * ph - 1.0f;
            cf[CF_CR_HELD] = quant(0.6f * t + 0.4f * sw, 4.0f);
        }
        cf[CF_CR_PH] += inc; if (cf[CF_CR_PH] >= 1.0f) cf[CF_CR_PH] -= 1.0f;
        x = CRUSH_LEVEL * cf[CF_CR_HELD];
        l += x * g_pan_l[VC_CRUSH]; r += x * g_pan_r[VC_CRUSH];
    }
    if (layers & LAYER_BIT(VC_WASH)) {
        if (++bc->ws_pos >= bc->ws_len) { bc->ws_pos = 0; bc->ws_k = 1; }
        int k = bc->ws_k;
        float g = bc->ws_g[k];
        if (g > 0.0f) {
            cf[CF_WS_PH] += bc->ws_inc[k]; if (cf[CF_WS_PH] >= 1.0f) cf[CF_WS_PH] -= 1.0f;
            float u = (float)bc->ws_pos * bc->ws_inv;
            float e = u < 0.82f ? u * 1.2195f : (1.0f - u) * 5.5556f;   /* swells, then cuts off */
            e = e * e * e;
            float ph2 = cf[CF_WS_PH] * 2.0f; if (ph2 >= 1.0f) ph2 -= 1.0f;
            float y = fast_sin(cf[CF_WS_PH]) + 0.3f * fast_sin(ph2);
            cf[CF_WS_LP] += 0.5f * (y - cf[CF_WS_LP]);
            x = g * e * cf[CF_WS_LP];
            l += x * g_pan_l[VC_WASH]; r += x * g_pan_r[VC_WASH];
        }
    }
    if (layers & LAYER_BIT(VC_STEPS)) {
        float bp = nz - cf[CF_ST_HP]; cf[CF_ST_HP] += 0.015f * bp;          /* high-pass ~100 Hz */
        cf[CF_ST_LP] += 0.16f * (bp - cf[CF_ST_LP]);                         /* low-pass ~1.2 kHz, */
        cf[CF_ST_LP2] += 0.16f * (cf[CF_ST_LP] - cf[CF_ST_LP2]);           /* two poles */
        float *G0 = &cf[CF_ST_G0], *G1 = &cf[CF_ST_G0 + 6];
        G0[0] += bc->st_inc[0]; if (G0[0] >= 1.0f) G0[0] -= 1.0f;
        G1[0] += bc->st_inc[1]; if (G1[0] >= 1.0f) G1[0] -= 1.0f;
        bc->st_e[0] += bc->st_de[0]; bc->st_e[1] += bc->st_de[1];
        /* soft, fuzzy glints: each tone is driven into a gentle clip, then
         * held at a third of the rate (a soft digital grit) and smoothed */
        float a0 = 2.2f * fast_sin(G0[0]), a1 = 2.2f * fast_sin(G1[0]);
        float gl = bc->st_e[0] * a0 / (1.0f + fabsf(a0)) + bc->st_e[1] * a1 / (1.0f + fabsf(a1));
        cf[CF_ST_FC] += 1.0f;
        if (cf[CF_ST_FC] >= 3.0f) { cf[CF_ST_FC] = 0.0f; cf[CF_ST_FZ] = gl + 0.04f * nz; }
        cf[CF_ST_FL] += 0.35f * (cf[CF_ST_FZ] - cf[CF_ST_FL]);
        x = bc->st_amp * (0.6f * cf[CF_ST_LP2] + 1.0f * cf[CF_ST_FL]);
        l += x * g_pan_l[VC_STEPS]; r += x * g_pan_r[VC_STEPS];
    }
    *pl += l;
    *pr += r;
}

/* ================= Bank E: the wavetable synth ================= */
enum { CE_0 = 8,
       CE_PH = CE_0,                        /* + v: each voice's phase */
       CE_LFO = CE_0 + 8,                   /* + v: each voice's LFO */
       CE_L3 = CE_0 + 16,                  /* LUSH's third saw */
       CE_END };
typedef char ce_fits[(CE_END <= 64) ? 1 : -1];

#define WTE_LEVEL 0.57f
static const float VE_GAIN[NUM_VOICES] = { 0.89f * 0.56f * 2.04f, 0.35f, 1.08f * 0.6f * 1.84f, 0.69f * 0.79f,
    0.9f * 0.56f, 0.94f * 0.6f, 0.78f * 0.56f, 0.535f * 0.75f };   /* (the others turned down 2..5 dB; SAW and SQUARE brought up to bank A's everyday setting) */

typedef struct {
    const int16_t *fa[NUM_VOICES], *fb[NUM_VOICES];      /* the two frames around the position, at the right level */
    float w[NUM_VOICES], dw[NUM_VOICES];                 /* the blend between them */
    float inc[NUM_VOICES], amp[NUM_VOICES], damp[NUM_VOICES];
    int mask[NUM_VOICES];                                /* samples in this level - 1 */
    float sz[NUM_VOICES];
} BankEBlock;


static inline float e_clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

/* the position (0..1) and level each voice wants at a note's age */
static float bank_e_pos(uint32_t v, NoteVoice *nv, float age, float blk_s, float *amp) {
    float *cf = nv->cf;
    float a = 1.0f, pos = 0.0f;
    switch (v) {
    case VE_SQUARE: case VE_MAXFOLD: pos = 1.0f; break;                        /* the table's far end */
    case VE_GROWL: pos = e_clamp01(age / 3.0f); pos = pos * pos * 0.97f; break;   /* meaner the longer you hold */
    case VE_FOLDS:                                                              /* the fold wobbling quickly */
        cf[CE_LFO + v] += 4.8f * blk_s; if (cf[CE_LFO + v] >= 1.0f) cf[CE_LFO + v] -= 1.0f;
        pos = 0.35f + 0.25f * fast_sin(cf[CE_LFO + v]) + 0.2f * e_clamp01(age / 2.0f); break;
    default: pos = 0.0f; break;                                                /* SAW (and SINE, not a table) */
    }
    *amp = a;
    return e_clamp01(pos);
}

static void bank_e_prepare(NoteVoice *nv, uint32_t layers, float inc, int n, BankEBlock *be) {
    float blk_s = (float)n / (float)FM1_AUDIO_RATE;
    float age = nv->age;
    if (age < 0.0001f) {                                       /* a fresh note: different start for each voice */
        for (uint32_t v = 0u; v < NUM_VOICES; v++) nv->cf[CE_LFO + v] = (float)((nv->lfsr >> (v * 3u)) & 7u) * 0.125f;
    }
    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        if (!(layers & LAYER_BIT(v))) continue;
        float a0, a1;
        float p0 = bank_e_pos(v, nv, age, blk_s, &a0);
        float p1 = p0; a1 = a0;
        if (v == VE_GROWL) {                                     /* smooth: look at the block's end */
            float dummy; p1 = bank_e_pos(v, nv, age + blk_s, 0.0f, &dummy); a1 = dummy;
        }
        g_e_pos[v] = p0;
        float vi = inc;
        if (v >= VE_FIRST_ADD) {                               /* 5TH+OCT */
            be->inc[v] = vi;
            float g = WTE_LEVEL * VE_GAIN[v];
            be->amp[v] = g * a0; be->damp[v] = g * (a1 - a0) / (float)n;
            continue;
        }
        /* the level of detail: the most harmonics that stay under half the sample rate */
        int lv = 0;
        /* less shrill: drop the harmonics above about 9 kHz (the next simpler
         * version of the wave), which also keeps high notes clean */
        while (lv < 5 && (float)(64 >> lv) * vi >= 0.204f) lv++;
        float fp = p0 * 15.0f;
        int fa = (int)fp; if (fa > 14) fa = 14;
        float w0 = fp - (float)fa;
        float w1 = p1 * 15.0f - (float)fa;
        w1 = e_clamp01(w1);
        int tb = VE_TABLE[v];
        be->fa[v] = &WTE[tb][fa][WTE_LEVEL_OFF[lv]];
        be->fb[v] = &WTE[tb][fa + 1][WTE_LEVEL_OFF[lv]];
        be->w[v] = w0; be->dw[v] = (w1 - w0) / (float)n;
        be->sz[v] = (float)(256 >> lv);
        be->mask[v] = (256 >> lv) - 1;
        be->inc[v] = vi;
        float g = WTE_LEVEL * VE_GAIN[v] * (1.0f / 32768.0f);
        be->amp[v] = g * a0; be->damp[v] = g * (a1 - a0) / (float)n;
    }
    nv->age += blk_s;
}

static inline __attribute__((always_inline)) void render_bank_e(NoteVoice *nv, uint32_t layers, BankEBlock *be,
                                                                float *pl, float *pr) {
    float *cf = nv->cf;
    float l = 0.0f, r = 0.0f;
    for (uint32_t v = 0u; v < NUM_VOICES; v++) {
        if (!(layers & LAYER_BIT(v))) continue;
        float p = cf[CE_PH + v] + be->inc[v]; if (p >= 1.0f) p -= 1.0f; cf[CE_PH + v] = p;
        be->w[v] += be->dw[v]; be->amp[v] += be->damp[v];
        if (v >= VE_FIRST_ADD) {
            float y;
            {                                                  /* 5TH+OCT: the note (a little), its fifth, its octave */
                float p5 = cf[CE_LFO + v] + 1.5f * be->inc[v]; if (p5 >= 1.0f) p5 -= 1.0f; cf[CE_LFO + v] = p5;
                float p8 = 2.0f * p; p8 -= (float)(int)p8;
                y = 0.35f * fast_sin(p) + 0.75f * fast_sin(p5) + 0.65f * fast_sin(p8);
            }
            y *= be->amp[v];
            l += y * g_pan_l[v]; r += y * g_pan_r[v];
            continue;
        }
        if (v == VE_LUSH) {                                    /* three saws: in tune, 8 cents up (left), 8 down (right) */
            float p2 = cf[CE_LFO + v] + be->inc[v] * 1.00463f; if (p2 >= 1.0f) p2 -= 1.0f; cf[CE_LFO + v] = p2;
            float p3 = cf[CE_L3] + be->inc[v] * 0.99539f; if (p3 >= 1.0f) p3 -= 1.0f; cf[CE_L3] = p3;
            const int16_t *A = be->fa[v];
            float sz = be->sz[v];
            float x1 = p * sz, x2 = p2 * sz, x3 = p3 * sz;
            int i1 = (int)x1, i2 = (int)x2, i3 = (int)x3;
            float y1 = (float)A[i1] + (x1 - (float)i1) * (float)(A[i1 + 1] - A[i1]);
            float y2 = (float)A[i2] + (x2 - (float)i2) * (float)(A[i2 + 1] - A[i2]);
            float y3 = (float)A[i3] + (x3 - (float)i3) * (float)(A[i3 + 1] - A[i3]);
            float a = be->amp[v];
            l += a * (0.6f * y1 + 0.85f * y2 + 0.3f * y3);
            r += a * (0.6f * y1 + 0.3f * y2 + 0.85f * y3);
            continue;
        }
        if (v == VE_SINE) {                                    /* a pure sine, no table */
            float y = be->amp[v] * 29490.0f * fast_sin(p);
            l += y * g_pan_l[v]; r += y * g_pan_r[v];
            continue;
        }
        float x = p * be->sz[v];
        int i = (int)x; float t = x - (float)i;
        const int16_t *A = be->fa[v], *B = be->fb[v];
        float a = (float)A[i] + t * (float)(A[i + 1] - A[i]);   /* the guard sample makes i + 1 safe */
        float b = (float)B[i] + t * (float)(B[i + 1] - B[i]);
        float y = be->amp[v] * (a + be->w[v] * (b - a));
        l += y * g_pan_l[v]; r += y * g_pan_r[v];
    }
    *pl += l;
    *pr += r;
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
    if (g_bank == 4) {
        static BankEBlock be;
        uint32_t le = layers & ~LAYER_MEL;
        bank_e_prepare(nv, le, inc, n, &be);
        for (int i = 0; i < n; i++) {
            float l = 0.0f, r = 0.0f;
            if (le) render_bank_e(nv, le, &be, &l, &r);
            note_mel_env(nv, layers, atk_step, dec_coeff, l, r, &bl[i], &br[i]);
        }
        return;
    }
    if (g_bank == 2) {
        /* bank C: LOWREY is bank B's own code; age is kept by bank C's set-up */
        static BankBBlock bb;
        static BankCBlock bc;
        uint32_t lb = layers & LAYER_BIT(VC_LOWREY);
        uint32_t lc = layers & ~(LAYER_BIT(VC_LOWREY) | LAYER_MEL);
        if (lb) bank_b_prepare(nv, LAYER_BIT(VB_LOWREY), inc, lfo[n / 2], n, &bb);
        bank_c_prepare(nv, lc, inc, lfo[n / 2], n, &bc);
        for (int i = 0; i < n; i++) {
            float l = 0.0f, r = 0.0f;
            if (lb) render_bank_b(nv, LAYER_BIT(VB_LOWREY), lfo[i], &bb, &l, &r);
            if (lc) render_bank_c(nv, lc, &bc, &l, &r);
            note_mel_env(nv, layers, atk_step, dec_coeff, l, r, &bl[i], &br[i]);
        }
        return;
    }
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

/* ---------------- Remembering settings (flash store) ----------------
 * Every bank's voice buttons (it always starts on bank A),
 * the Mellotron flute, the colours and the sequencer pattern are kept in
 * one store slot, so they come back after power-off. A save stops the sound
 * for ~40-50 ms, so it only happens from loop(), 1.5 s after a change
 * and only once nothing has been sounding for 1 s and the sequencer is
 * stopped. (Needs FM1 Studio's newer fm1.h, with fm1_store_save/load.) */
#ifdef FM1_STORE_SLOTS
#define STORE_SLOT  0u
#define STORE_MAGIC 0x31595752u               /* "LWY1" */
#define STORE_VER   4u
typedef struct {
    uint32_t magic, ver;
    int8_t bank, bank_prev, pad0, bank_prev_e;
    int8_t voices[5][NUM_VOICES];
    float rev_mix, rev_decay, dly_mix, dly_time, dly_fb;
    float attack, decay, filter, mel_level, mel_last;
    uint8_t main_theme, btn_theme, seq_len, pad1;
    uint8_t seq[SEQ_STEPS][4];
    int32_t seq_bpm;
    float seq_gate, seq_swing;
    /* added later (older saves end here) */
    uint16_t dseq[DSEQ_STEPS];
    uint8_t dseq_len, pad2[3];
    /* added later again */
    uint8_t midi_multi, pad3;
    int8_t bank_ch[5], pad4;
    /* and again: the MIDI OUT switches */
    uint8_t mo_keys, mo_seq, mo_drums, mo_clock;
    /* and again: USB SPLIT */
    uint8_t usb_split, pad5[3];
    /* and again: POCKET */
    uint8_t pp_mode, pp_vol, pp_wave[7], pp_p1[7], pp_p2[7], pad6[3];
} SavedState;
#define STORE_SPLIT_SIZE ((int)__builtin_offsetof(SavedState, pp_mode))
#define STORE_MO_SIZE ((int)__builtin_offsetof(SavedState, usb_split))
#define STORE_GLOB_SIZE ((int)__builtin_offsetof(SavedState, mo_keys))
#define STORE_OLD_SIZE ((int)__builtin_offsetof(SavedState, dseq))
#define STORE_MID_SIZE ((int)__builtin_offsetof(SavedState, midi_multi))
static SavedState g_saved;                    /* what is in the store now */
static int g_saved_ok = 0;
static uint32_t g_store_changed_ms = 0u, g_store_quiet_ms = 0u;
static int g_store_pending = 0;

static void store_snapshot(SavedState *st) {
    uint8_t *b = (uint8_t *)st;
    for (uint32_t i = 0u; i < sizeof *st; i++) b[i] = 0u;
    st->magic = STORE_MAGIC; st->ver = STORE_VER;
    st->bank = (int8_t)g_bank;
    for (int k = 0; k < 5; k++)
        for (uint32_t v = 0u; v < NUM_VOICES; v++)
            st->voices[k][v] = (int8_t)(k == BANK_RHYTHM ? 0 : (k == (g_bank == BANK_RHYTHM ? g_snd_bank : g_bank) ? g_voice_on[v] : g_bank_voices[k][v]));
    /* reverb, delay, attack, decay and filter always start where they come: not saved */
    st->mel_level = g_mel_level; st->mel_last = g_mel_last;
    st->main_theme = (uint8_t)g_main_theme; st->btn_theme = (uint8_t)g_btn_theme;
    st->seq_len = (uint8_t)g_seq_len;
    for (int i = 0; i < SEQ_STEPS; i++) for (int k = 0; k < 4; k++) st->seq[i][k] = g_seq[i][k];
    for (int i = 0; i < DSEQ_STEPS; i++) st->dseq[i] = g_dseq[i];
    st->dseq_len = (uint8_t)g_dseq_len;
    st->midi_multi = (uint8_t)g_midi_multi;
    for (int k = 0; k < 5; k++) st->bank_ch[k] = g_bank_ch[k];
    st->mo_keys = (uint8_t)g_mo_keys; st->mo_seq = (uint8_t)g_mo_seq;
    st->mo_drums = (uint8_t)g_mo_drums; st->mo_clock = (uint8_t)g_mo_clock;
    st->usb_split = (uint8_t)g_usb_split; st->pad5[0] = st->pad5[1] = st->pad5[2] = 0u;
    st->pp_mode = (uint8_t)g_pp_mode; st->pp_vol = (uint8_t)(g_pp_vol * 255.0f + 0.5f);
    for (int i = 0; i < 7; i++) {
        st->pp_wave[i] = g_pp_wave[i];
        st->pp_p1[i] = (uint8_t)(g_pp_p1[i] * 255.0f + 0.5f); st->pp_p2[i] = (uint8_t)(g_pp_p2[i] * 255.0f + 0.5f);
    }
    st->pad6[0] = 0xA5u;
    st->pad6[1] = (uint8_t)(1.5f + g_beat_vol * 254.0f);    /* the drums' VOL and TONE (0: never saved) */
    st->pad6[2] = (uint8_t)(1.5f + g_beat_tone * 254.0f);
    st->seq_bpm = g_seq_bpm; st->seq_gate = g_seq_gate; st->seq_swing = g_seq_swing;
}

static float store_unit(float x, float dflt) { return (x >= 0.0f && x <= 1.0f) ? x : dflt; }

static void store_load(void) {
    SavedState st;
    int got = fm1_store_load(STORE_SLOT, &st, sizeof st);
    if (got == STORE_OLD_SIZE) {                     /* a save from before the drum sequencer */
        for (int i = 0; i < DSEQ_STEPS; i++) st.dseq[i] = 0u;
        st.dseq_len = 16u; st.pad2[0] = st.pad2[1] = st.pad2[2] = 0u;
    }
    if (got == STORE_OLD_SIZE || got == STORE_MID_SIZE) {   /* ... or from before GLOBAL */
        st.midi_multi = 0u; st.pad3 = 0u; st.pad4 = 0;
        for (int k = 0; k < 5; k++) st.bank_ch[k] = g_bank_ch[k];
    }
    if (got == STORE_OLD_SIZE || got == STORE_MID_SIZE || got == STORE_GLOB_SIZE) {   /* ... or before MIDI OUT */
        st.mo_keys = (uint8_t)g_mo_keys; st.mo_seq = (uint8_t)g_mo_seq;
        st.mo_drums = (uint8_t)g_mo_drums; st.mo_clock = (uint8_t)g_mo_clock;
    }
    if (got == STORE_OLD_SIZE || got == STORE_MID_SIZE || got == STORE_GLOB_SIZE || got == STORE_MO_SIZE) {
        st.usb_split = 0u; st.pad5[0] = st.pad5[1] = st.pad5[2] = 0u;   /* ... or before USB SPLIT */
    }
    if (got == STORE_OLD_SIZE || got == STORE_MID_SIZE || got == STORE_GLOB_SIZE || got == STORE_MO_SIZE ||
        got == STORE_SPLIT_SIZE) {                                      /* ... or before POCKET: its defaults */
        st.pp_mode = (uint8_t)g_pp_mode; st.pp_vol = (uint8_t)(g_pp_vol * 255.0f + 0.5f);
        for (int i = 0; i < 7; i++) {
            st.pp_wave[i] = g_pp_wave[i];
            st.pp_p1[i] = (uint8_t)(g_pp_p1[i] * 255.0f + 0.5f); st.pp_p2[i] = (uint8_t)(g_pp_p2[i] * 255.0f + 0.5f);
        }
    } else if (got != (int)sizeof st) return;
    if (st.magic != STORE_MAGIC || st.ver != STORE_VER) return;
    /* it always starts on the Lowrey bank (bank A); every bank keeps the
     * voice buttons you left on */
    for (int k = 0; k < 5; k++)
        for (uint32_t v = 0u; v < NUM_VOICES; v++) g_bank_voices[k][v] = st.voices[k][v] ? 1 : 0;
    for (uint32_t v = 0u; v < NUM_VOICES; v++) g_voice_on[v] = g_bank_voices[0][v];
    g_mel_level = store_unit(st.mel_level, 0.0f);
    g_mel_last = store_unit(st.mel_last, g_mel_last);
    if (g_mel_last < 0.01f) g_mel_last = 0.90f;
    if (st.main_theme < NUM_THEMES) g_main_theme = st.main_theme;
    if (st.btn_theme < NUM_THEMES) g_btn_theme = st.btn_theme;
    apply_themes();
    if (st.seq_len >= 1u && st.seq_len <= SEQ_STEPS) g_seq_len = st.seq_len;
    for (int i = 0; i < SEQ_STEPS; i++) for (int k = 0; k < 4; k++) g_seq[i][k] = st.seq[i][k] <= 127u ? st.seq[i][k] : 0u;
    if (st.seq_bpm >= 40 && st.seq_bpm <= 300) g_seq_bpm = st.seq_bpm;
    g_seq_gate = (st.seq_gate >= 0.1f && st.seq_gate <= 1.0f) ? st.seq_gate : g_seq_gate;
    g_seq_swing = store_unit(st.seq_swing, g_seq_swing);
    g_seq_ver++;
    for (int i = 0; i < DSEQ_STEPS; i++) g_dseq[i] = (uint16_t)(st.dseq[i] & 0x0FFFu);
    if (st.dseq_len >= 1u && st.dseq_len <= DSEQ_STEPS) g_dseq_len = st.dseq_len > 16u ? 16 : st.dseq_len;
    g_midi_multi = st.midi_multi ? 1 : 0;
    g_mo_keys = st.mo_keys ? 1 : 0; g_mo_seq = st.mo_seq ? 1 : 0;
    g_mo_drums = st.mo_drums ? 1 : 0; g_mo_clock = st.mo_clock ? 1 : 0;
    g_usb_split = 0;                     /* SPLIT always starts off at power-on */
    if (st.pp_mode < PP_MODES) g_pp_mode = st.pp_mode;
    int plain_era = (st.pad6[0] == 0xA5u);  /* saved since PLAIN took Mono FM's place */
    if (st.pad6[1]) g_beat_vol = clamp01((float)(st.pad6[1] - 1u) / 254.0f);
    if (st.pad6[2]) g_beat_tone = clamp01((float)(st.pad6[2] - 1u) / 254.0f);
    g_pp_vol = (float)st.pp_vol / 255.0f;
    for (int i = 0; i < 7; i++) {
        g_pp_wave[i] = (uint8_t)(st.pp_wave[i] & 3u);
        if (pp_is_fm(i)) g_pp_wave[i] &= 1u;
        g_pp_p1[i] = (float)st.pp_p1[i] / 255.0f; g_pp_p2[i] = (float)st.pp_p2[i] / 255.0f;
    }
    if (!plain_era) { g_pp_p1[PP_PLAIN] = 0.0f; g_pp_p2[PP_PLAIN] = 0.8f; g_pp_wave[PP_PLAIN] = 1u; }   /* Mono FM's old knobs don't carry over */
    for (int k = 0; k < 5; k++) if (st.bank_ch[k] >= 0 && st.bank_ch[k] <= 16) g_bank_ch[k] = st.bank_ch[k];
    g_dseq_ver++;
    g_saved = st;
    g_saved_ok = 1;
}

static void store_autosave(void) {
    static uint32_t last_check = 0u;
    if (g_any_note_sounding || g_seq_play || g_dseq_play || g_beat[0] >= 0 || g_beat[1] >= 0) g_store_quiet_ms = g_tick_ms;
    if (g_tick_ms - last_check < 250u) return;               /* look 4 times a second */
    last_check = g_tick_ms;
    static SavedState now;
    store_snapshot(&now);
    const uint8_t *a = (const uint8_t *)&now, *b = (const uint8_t *)&g_saved;
    int differs = !g_saved_ok;
    for (uint32_t i = 0u; i < sizeof now && !differs; i++) differs = a[i] != b[i];
    if (!differs) { g_store_pending = 0; return; }
    if (!g_store_pending) { g_store_pending = 1; g_store_changed_ms = g_tick_ms; }
    if (g_tick_ms - g_store_changed_ms < 1500u) return;     /* let things settle */
    if (g_tick_ms - g_store_quiet_ms < 1000u) return;       /* only in a quiet moment */
    if (g_view == VIEW_SPLASH) return;
    if (fm1_store_save(STORE_SLOT, &now, sizeof now) == 0) {
        g_saved = now;
        g_saved_ok = 1;
        g_store_pending = 0;
    }
}
#else
static void store_load(void) {}
static void store_autosave(void) {}
#endif

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
        g_bank_voices[2][v] = (v == VC_LOWREY) ? 1 : 0;
        g_bank_voices[4][v] = (v == VE_SAW) ? 1 : 0;
        g_prev_voice_on[v] = -1;
        /* a very narrow spread (at most about 1.4 dB between the sides), so it is
         * all but mono; the bass (and whatever shares its button) dead centre */
        float pan = (v == VOICE_BASS) ? 0.0f : ((float)v - 3.0f) / 3.0f;
        g_pan_l[v] = 0.7071f * (1.0f - 0.08f * pan);
        g_pan_r[v] = 0.7071f * (1.0f + 0.08f * pan);
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

    store_load();                                /* what you had set before power-off */

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
    /* on the RHYTHM bank the bank underneath keeps sounding: the audio works
     * as that bank, and only the keys and MIDI notes go to the drums */
    int ui_bank = g_bank;
    g_rhythm_ui = (ui_bank == BANK_RHYTHM);
    if (g_rhythm_ui) g_bank = g_snd_bank;
    update_note_cap();
    sync_keyboard_notes();

    /* MIDI notes are handled here, in the audio interrupt, so they can never
     * change the note pool while it is being rendered (fm1.h: call
     * fm1_midi_take from one place only, audio() or loop()). */
    uint32_t pkt = 0u;
    while (fm1_midi_take(&pkt)) {
        handle_midi_packet(pkt);
    }
    seq_run(frames);
    beat_update();
    /* MIDI OUT: drum note-offs, waiting note-offs, and the clock */
    for (int d = 0; d < NUM_DRUMS; d++)
        if (g_mo_dleft[d] > 0 && (g_mo_dleft[d] -= frames) <= 0) { g_mo_dleft[d] = 0; mo_off(mo_drum_ch(), GM_DRUM[d]); }
    mo_retry();
    mo_transport();
    if (g_mo_clock) {                                   /* 24 clocks to a beat, all the time */
        float per = 60.0f * (float)FM1_AUDIO_RATE / ((float)g_seq_bpm * 24.0f);
        g_mo_clk_acc += (float)frames;
        while (g_mo_clk_acc >= per) { g_mo_clk_acc -= per; mo_send(0xF8u, 0u, 0u); }
    }
    g_audio_frames += (uint32_t)frames;

    /* more voices switched on while notes ring: stop the quietest extras */
    {
        int busy = 0;
        for (uint32_t s = 0u; s < MAX_NOTES; s++) busy += note_busy(&g_pool[s]);
        while (busy > g_max_notes) {
            uint32_t q = 0u;
            float lo = 999.0f;
            int pick = steal_pick();
            if (pick >= 0) q = (uint32_t)pick;
            else
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

    /* MASTER: this firmware sets the volume itself (audio-taper curve). FM1
     * Studio says the FM-1 now does this on its own, but on a real FM-1 it did
     * not (Nov 27 2026), so the volume is kept here; the USB recording follows
     * MASTER too. */
    uint32_t m_val = fm1_master();
    float m_norm = clamp01((float)m_val / (float)FM1_MASTER_MAX);
    float master_gain = (m_norm * m_norm) * MAX_OUTPUT_GAIN;
    int split = g_usb_split;

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
    if (g_bank == BANK_POCKET) {        /* POCKET: its own engine; the organ's notes let go */
        layers = 0u;
        active_layers = 0;
        for (uint32_t s = 0u; s < MAX_NOTES; s++) {
            g_pool[s].active = 0; g_pool[s].sustained = 0; g_pool[s].stage = 2;
        }
    }

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

    /* the rhythm unit: its own bus (mixed in after the filter); first, so the
     * auto bass knows where its kicks fall */
    static float drm_l[FM1_AUDIO_BLOCK], drm_r[FM1_AUDIO_BLOCK];
    for (int i = 0; i < frames; i++) { drm_l[i] = 0.0f; drm_r[i] = 0.0f; }
    render_drums(drm_l, drm_r, frames);
    static float pp_l[FM1_AUDIO_BLOCK], pp_r[FM1_AUDIO_BLOCK];
    int pp_on = (g_bank == BANK_POCKET);
    if (pp_on) {
        for (int i = 0; i < frames; i++) { pp_l[i] = 0.0f; pp_r[i] = 0.0f; }
        pp_render(pp_l, pp_r, frames);
        if (pp_count() > 0) any_held_in_block = 1;       /* (the icons move while POCKET plays) */
        for (int v = 0; v < PP_VOICES; v++)              /* POCKET sounding: no saving to memory now -  */
            if (g_ppv[v].env > 0.0f || g_pp_held[v]) any_active_in_block = 1;   /* a save stalls the sound */
    }

    /* AUTO BASS, as on a Lowrey: on bank A with BASS on and a beat (or the drum
     * sequencer) going, the lowest octave of keys doesn't hold the bass - it
     * plays a short "bum" on every kick, at the lowest of those keys held;
     * on those keys only the mellow voices sound with it (no harpsichord or strings). */
    int auto_bass = (g_bank == 0 && g_voice_on[VOICE_BASS] &&
                     (g_beat[0] >= 0 || g_beat[1] >= 0 || g_dseq_play));
    static int ab_since = 1 << 30;                  /* samples since the last kick */
    static int ab_kick_pitch = -1;                  /* the bass note that kick played (-1: none) */
    if (auto_bass) {
        /* the lowest held note in the bass zone: the FM-1's lowest 12 keys, or a MIDI
         * note below C3 (the bottom octave of a 49-key keyboard) */
        int low = -1, low_real = 999;
        for (uint32_t s2 = 0u; s2 < MAX_NOTES; s2++) {
            NoteVoice *q = &g_pool[s2];
            if (!(q->active || q->sustained) || q->ab || !AB_ZONE(q)) continue;
            int real = q->is_midi ? q->note_idx : 53 + q->note_idx + 12 * g_octave_shift;
            if (real < low_real) { low_real = real; low = ((real - 53) % 12 + 12) % 12; }
        }
        /* it stays in one low range, like a pedal bass: the octave buttons don't move it */
        int ab_midi = (low >= 0) ? 53 + low : -1;
        int starts[AB_MAX_KICKS + 1], ns = 0, late = -1;
        for (int k = 0; k < g_kick_n; k++) {
            if (ab_midi >= 0) starts[ns++] = g_kick_off[k];
            ab_kick_pitch = ab_midi;
            ab_since = frames - g_kick_off[k];
        }
        /* a little late? a note pressed (or changed) up to 0.1 s after a kick
         * still gets its "bum", straight away */
        if (g_kick_n == 0 && ab_midi >= 0 && ab_midi != ab_kick_pitch &&
            ab_since < (int)(0.10f * (float)FM1_AUDIO_RATE)) {
            late = ns;
            starts[ns++] = 0;
            ab_kick_pitch = ab_midi;
        }
        for (int k = 0; k < ns; k++) {
            /* one bass note at a time, like a pedal bass: the last "bum" fades out quickly */
            for (uint32_t s2 = 0u; s2 < MAX_NOTES; s2++) {
                NoteVoice *o = &g_pool[s2];
                if (o->ab && (o->active || o->env > 0.03f)) { o->ab_fade = 1; o->active = 0; o->ab_left = 0; o->stage = 2; }
            }
            g_note_bank = g_bank; g_note_ch = 0;        /* (so it is never turned into a drum) */
            note_on(ab_midi, 1);
            g_note_bank = -1;
            NoteVoice *q = g_last_nv;
            q->ab = 1;
            q->ab_left = (int)(0.20f * (float)FM1_AUDIO_RATE);   /* held for a moment, then let go */
            q->ab_off = starts[k];
            q->sustained = 0;
            if (k == late) {
                /* joined late: put it where it would be had it started on the kick -
                 * its waveform already turning, its hold cut by the time gone - and
                 * let it swell in (12 ms) rather than strike, so it sits in time */
                float inc = g_midi_inc[ab_midi] * 0.5f * (float)ab_since;   /* the bass's half-speed oscillator */
                q->phase_sub = inc - (float)(int)inc;
                q->ab_left -= ab_since;
                if (q->ab_left < 1) q->ab_left = 1;
                q->ab_soft = 1;
                q->env = 0.0f; q->stage = 0;
            }
        }
    }
    if (g_kick_n == 0 && ab_since < (1 << 29)) ab_since += frames;
    float ab_atk = 1.0f / (0.003f * (float)FM1_AUDIO_RATE);
    float ab_atk_soft = 1.0f / (0.012f * (float)FM1_AUDIO_RATE);
    float ab_fade = powf(0.03f, 1.0f / (0.025f * (float)FM1_AUDIO_RATE));   /* the last "bum" gone in 25 ms */

    /* All notes, a whole block at a time */
    static float blk_l[FM1_AUDIO_BLOCK], blk_r[FM1_AUDIO_BLOCK];
    for (int i = 0; i < frames; i++) { blk_l[i] = 0.0f; blk_r[i] = 0.0f; }
    float weight0 = 0.0f, weight1 = 0.0f;
    /* MULTI MIDI: notes of other banks play with those banks' own voices */
    uint32_t bank_layers[5] = { 0u, 0u, 0u, 0u, 0u };
    if (g_midi_multi) {
        for (int k = 0; k < 5; k++) {
            if (k == BANK_RHYTHM) continue;
            const int *von = (k == g_bank) ? g_voice_on : g_bank_voices[k];
            for (uint32_t v = 0u; v < NUM_VOICES; v++) if (von[v]) bank_layers[k] |= LAYER_BIT(v);
            if (g_mel_level > 0.005f) bank_layers[k] |= LAYER_MEL;
        }
    }
    int snd_bank = g_bank;
    for (uint32_t n = 0u; n < MAX_NOTES; n++) {
        NoteVoice *nv = &g_pool[n];
        if (nv->active || nv->sustained || nv->env > 0.03f) {
            weight0 += nv->env;
            if (nv->ab) {                         /* an auto-bass "bum": BASS only, from its kick */
                int off = nv->ab_off;
                if (off > frames - 1) off = frames - 1;
                nv->ab_off = 0;
                if (nv->ab_left > 0) { nv->ab_left -= frames - off; if (nv->ab_left <= 0) nv->active = 0; }
                static float ab_l[FM1_AUDIO_BLOCK], ab_r[FM1_AUDIO_BLOCK];   /* (a little louder than a held bass: it is short) */
                for (int i = 0; i < frames - off; i++) { ab_l[i] = 0.0f; ab_r[i] = 0.0f; }
                render_note_block(nv, LAYER_BIT(VOICE_BASS), lfo_buf, frames - off,
                                  nv->ab_soft ? ab_atk_soft : ab_atk, nv->ab_fade ? ab_fade : dec_coeff, ab_l, ab_r);   /* rings out with the DEC knob */
                if (nv->stage != 0) nv->ab_soft = 0;
                for (int i = 0; i < frames - off; i++) { blk_l[off + i] += 1.78f * ab_l[i]; blk_r[off + i] += 1.78f * ab_r[i]; }
            } else if (auto_bass && AB_ZONE(nv)) {   /* a low key: the bass "bums"; of the rest, only the mellow voices
                                                         hold (flute, horn, reed, vox, vibes, the Mellotron) - no harpsichord or strings */
                render_note_block(nv, layers & ~(LAYER_BIT(VOICE_BASS) | LAYER_BIT(VOICE_HARPSI) | LAYER_BIT(VOICE_STRINGS)),
                                  lfo_buf, frames, atk_step, dec_coeff, blk_l, blk_r);
            } else
            if (g_midi_multi && nv->bank != snd_bank && nv->bank >= 0 && nv->bank < 5 && nv->bank != BANK_RHYTHM) {
                g_bank = nv->bank;                 /* (the bank's own sound code reads g_bank) */
                render_note_block(nv, bank_layers[nv->bank], lfo_buf, frames, atk_step, dec_coeff, blk_l, blk_r);
                g_bank = snd_bank;
            } else
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
        float drum_mono = 0.0f;
        if (split) {                       /* SPLIT: the drums stay out of the organ, its echo and its room */
            drum_mono = 0.5f * (drm_l[i] + drm_r[i]);
            mix_l = g_lp_l;
            mix_r = g_lp_r;
        } else {
            mix_l = g_lp_l + drm_l[i];
            mix_r = g_lp_r + drm_r[i];
        }
        if (pp_on) { mix_l += pp_l[i]; mix_r += pp_r[i]; }   /* POCKET, on the organ's side */

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
        float out_l, out_r;
        if (split) {                       /* left: the organ in mono; right: the drums, dry */
            out_l = output_limit(smooth_limit(0.5f * ((dry_l + dry_r) * dry_k + (rl + rr) * wet)) * master_gain);
            out_r = output_limit(smooth_limit(drum_mono) * master_gain);
        } else {
            out_l = output_limit(smooth_limit(dry_l * dry_k + rl * wet) * master_gain);
            out_r = output_limit(smooth_limit(dry_r * dry_k + rr * wet) * master_gain);
        }

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
    g_bank = ui_bank;
}

void loop(void) {
    /* loop() runs at an uneven rate (about once per audio block, slower while
     * drawing), so take the time from the audio sample count */
    g_tick_ms = (uint32_t)(((uint64_t)g_audio_frames * 1000u) / (uint64_t)FM1_AUDIO_RATE);
    process_controls();
    update_leds();
    render_screen();
    store_autosave();
}