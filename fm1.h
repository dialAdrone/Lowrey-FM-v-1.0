/* fm1.h - the API between an FM-1 program and the hardware.
 *
 * A program defines three functions: setup(), audio() and loop().
 * In the browser, the simulator calls them. On a real FM-1, an adapter on top
 * of fm1-base and felucca-bsp calls them (the folder firmware of FM-1 Studio's
 * repository builds a program with it).
 *
 * The screen and input functions have the names and signatures of felucca-bsp
 * (fm1_lcd.h, fm1_input.h). The button and encoder names after OCT_UP are ours.
 *
 * ---- The project file fm1project.json (optional) ----
 *
 * Without it, each .c and .cpp file of the project is compiled. With it, a
 * project can say how it is built. All its parts are optional:
 *
 *   {
 *     "sources": ["main.c"],         compile only these files
 *     "include": ["firmware/src", "firmware/gen"], more folders for #include
 *     "define":  ["APP_VER=\"1.0\""],              as #define APP_VER "1.0"
 *     "flags":   ["-Wno-unused-function"],         more options for clang
 *     "stack":   1048576,                          stack size in bytes (default 65536)
 *     "web":     { },                              the program has the web_ interface
 *     "device":  { "build": "dinghy", ... }        its own build for a real FM-1
 *   }
 *
 * "web" is for firmware that has a browser emulator made as Felucca's
 * (web/emu): it does not use this file, it has its own FM-1 inside and the
 * functions web_boot, web_render, web_keys, web_buttons, web_enc, web_master,
 * web_midi, web_screen and web_screen_draws. "web" can have:
 *
 *   "buttons":  [12, 13, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]
 *       the bit in web_buttons() for each button below, FM1_BTN_OCT_DOWN first
 *       (default: the numbers below)
 *   "encoders": [0, 1, 3, 4, 5, 6, 2]
 *       the number in web_enc() for each encoder below, FM1_ENC_SELECT first
 *       (default: the numbers below)
 *   "screen":   "swapped"
 *       web_screen() gives the two bytes of each pixel high byte first
 *
 * The examples for "buttons", "encoders" and "screen" are those of Felucca.
 *
 * "device" is for such a firmware too: the firmware compiler (DOWNLOAD, INSTALL)
 * then makes its package for a real FM-1 with the steps of its own build, from
 * the files of the project (with your changes). Without it, DOWNLOAD and INSTALL
 * are off for a project with "web". "device" has:
 *
 *   "build":   "dinghy"                  the steps of tools/build.py of AURORA,
 *                                        CS-1, FM-106 and GR!MM; "felucca": of
 *                                        Felucca, FMINI-1, SLOOP, SKIFF and JIANT
 *                                        (the only two builds known now)
 *   "name":    "aurora"                  the package is aurora.fwsc
 *   "product": "FM-1_4747"               the identity of the package
 *   "sources": ["firmware/src/aurora.c"] the C files of the firmware
 *   "define":  ["APP_VER=\"0.9.1\""]     more #defines for those files
 *   "flags":   ["-mcpu=r3"]              more options for each C file
 *   "loaderFlags": ["-Oz"]               more options for the C file of the
 *                                        update loader, after those
 *   "loaderMax": 7936                    the most bytes that the update loader
 *                                        (ota.bin) can have
 *   "dualCore": 0                        "dinghy": one core (default 1: two)
 *   "stub":    true                      "dinghy": a boot stub in front of the app
 *   "ramFree": 4096                      bytes of RAM that must stay free (default:
 *                                        0 for "dinghy", 4096 for "felucca")
 *   "size":    ["ui.c", "menu.c"]        "felucca": the files (from firmware/src)
 *                                        whose functions are built for size, as
 *                                        tools/size_fns.py does (without "size":
 *                                        all at -Os, as FELUCCA_SIZE=0)
 *   "giants":  ["draw_graph", "ui_input"] with "size": a function that only one
 *                                        of these calls (one time) also gets
 *                                        noinline, as SKIFF's size_fns.py does
 *   "keepInline": ["midi_pc_poll"]       with "giants": but not these
 *   "sizeRoots": ["fm1_alnk0_irq"]       with "size": the functions where an
 *                                        interrupt starts (default: those two of
 *                                        fm1_isr.S, the sound and the key scan)
 *   "sizeGlobals": true                  with "size": a global that interrupt code
 *                                        names counts as what it holds (a table of
 *                                        functions), as JIANT's size_fns.py does
 *   "sizeAttributes": true               with "size": __attribute__((..)) before
 *                                        a function's name is skipped (JIANT)
 *   "sizeEvents": ["midi_", "seq_step"]  with "size": the start of the names of
 *                                        event functions, built for size also
 *                                        when an interrupt reaches them (JIANT)
 *   "sizeEventKeep": ["mo_block"]        with "sizeEvents": but not these
 *   "hot":     ["mix_block"]             with "size": these functions run from RAM
 *                                        (.hot_text), and what an interrupt
 *                                        reaches goes first in the image (JIANT)
 *   "appSlot": 584336                    "felucca": the room for the app in the
 *                                        flash, as app.ld's XIP region (default
 *                                        581564: 0x8DFBC)
 *   "hal":     ["firmware/hal", "firmware/bsp/chip"]
 *                                        "felucca": the folders of the hardware
 *                                        layer, in the sequence of the build
 *                                        (default: firmware/hal)
 *
 * "dinghy" also takes these files of the project: firmware/crt0.S,
 * firmware/hal/fm1_vec.S, firmware/hal/fm1_isr.S, firmware/app.ld and the
 * update loader in firmware/loader, with the headers in firmware/hal,
 * firmware/src and firmware/gen. With "stub", also the boot stub in
 * firmware/stub, as FM-106 has it. "felucca" takes the same files, and also
 * fm1_cpu1.S (the second core) when the project has it, with the headers in
 * build/gen in place of firmware/gen. With "hal", it takes the assembly files
 * from the first folder of "hal" that has them. Both builds link the compiler's helper
 * routines.
 */
#ifndef FM1_H
#define FM1_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Program ---- */

/* Called one time, before the first audio block. */
void setup(void);

/* Called for each audio block. Write `frames` stereo pairs to `out`: L R L R ...
 * On a real FM-1 this is an interrupt and must return in less than 5.8 ms.
 * Do not draw on the screen from here. */
void audio(int32_t *out, int frames);

/* Called again and again for work that is not audio (controls, screen). */
void loop(void);

/* ---- Audio ---- */

#define FM1_AUDIO_RATE     44100    /* nominal; a real FM-1 runs at about 44118 Hz */
#define FM1_AUDIO_CHANNELS 2
#define FM1_AUDIO_BLOCK    256      /* frames in each audio() call */
#define FM1_AUDIO_MAX      8388607  /* full scale: samples are signed 24-bit (Q23) */

/* ---- Screen: 240 x 240 pixels, RGB565 colours, (0, 0) is top left ----
 *
 * Both functions return 0 when done and -2 when the rectangle is not fully on
 * the screen (there is no clipping). On a real FM-1 they are slow: a full
 * screen takes about 77 ms. Draw only what changed. */

#define FM1_LCD_WIDTH  240u
#define FM1_LCD_HEIGHT 240u

int fm1_lcd_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t rgb);

/* `pixels` holds w * h colours, row after row. `pixel_count` is its length. */
int fm1_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                 const uint16_t *pixels, uint32_t pixel_count);

/* ---- Keys and buttons ----
 *
 * Key ids: 0 to 13 are the buttons, 14 to 40 are the 27 note keys.
 * Note key 0 is F3, note key 26 is G5. */

#define FM1_NKEY      41u
#define FM1_NOTE_KEYS 27u
#define FM1_KEY_NOTE0 14u  /* key id of note key 0 */

enum {
  FM1_BTN_OCT_DOWN = 0, FM1_BTN_OCT_UP = 1,
  FM1_BTN_FX = 2, FM1_BTN_SCL = 3, FM1_BTN_ENV = 4, FM1_BTN_LFO = 5,
  FM1_BTN_EDIT = 6, FM1_BTN_GLO = 7, FM1_BTN_HOME = 8, FM1_BTN_SAVE = 9,
  FM1_BTN_ARP = 10, FM1_BTN_SEQ = 11, FM1_BTN_PLAY = 12, FM1_BTN_REC = 13
};

typedef struct {
  uint32_t notes;    /* bit n is 1 while note key n is down */
  uint32_t buttons;  /* bit i is 1 while button i is down */
} fm1_in_t;
extern volatile fm1_in_t fm1_in;

/* The buttons pressed since the last call, one bit for each button.
 * `released` (can be 0) gets the buttons released since the last call. */
uint32_t fm1_input_edges(uint32_t *released);

/* The note keys pressed since the last call, one bit for each note key. */
uint32_t fm1_input_note_edges(void);

/* ---- LEDs ----
 *
 * Each key and button has an LED, with the id of the key or button. An LED lights
 * the legend of its key in white; REC's is red. PLAY has two LEDs: a green one,
 * with the id FM1_BTN_PLAY, and a red one, with the id FM1_LED_PLAY_RED. With one
 * of them on, PLAY and STOP have its colour; with both, PLAY is green and STOP
 * orange.
 *
 * An LED has one of five levels. These are the levels of a real FM-1 (fm1-base
 * and felucca-bsp), as parts of the time that a fully lit LED is on:
 *   FM1_LED_OFF     dark
 *   FM1_LED_GLOW    a dim glow, about 1/30 (labels can be read in the dark)
 *   FM1_LED_MID     a mid level, about 1/8, over the glow
 *   FM1_LED_ON      fully lit
 *   FM1_LED_BREATH  fades from dark to about 60 % and back in about 1.1 s. All
 *                   LEDs that breathe do it at the same time. A breath that
 *                   starts when no LED breathes starts at its brightest.
 * The eye sees the glow brighter than these numbers: about a fifth of the light
 * of a lit LED, the mid level about half. */

#define FM1_LED_PLAY_RED 41u  /* the id of PLAY's red LED */
#define FM1_NLED         42u  /* LED ids: the key ids 0 to 40, and FM1_LED_PLAY_RED */

enum { FM1_LED_OFF = 0, FM1_LED_GLOW = 1, FM1_LED_MID = 2, FM1_LED_ON = 3, FM1_LED_BREATH = 4 };

/* Sets LED `id` fully on (on is not 0) or off. */
void fm1_led_key(uint32_t id, int on);

/* Sets LED `id` to `level`, one of FM1_LED_OFF to FM1_LED_BREATH. */
void fm1_led_level(uint32_t id, int level);

/* With low not 0, the glow, the mid level and the breath of all LEDs are darker:
 * the glow about 1/60, the mid level about 1/12, the breath to about 30 %, as the
 * setting "DIM LO" of Felucca. 0 (the start) gives the levels above. */
void fm1_led_dim_low(int low);

/* ---- Encoders ---- */

#define FM1_NENC 7u

enum {
  FM1_ENC_SELECT = 0, FM1_ENC_ALGORITHM = 1,
  FM1_ENC_KNOB1 = 2, FM1_ENC_KNOB2 = 3, FM1_ENC_KNOB3 = 4, FM1_ENC_KNOB4 = 5,
  FM1_ENC_PRESETS = 6
};

/* The clicks of encoder `e` since the last call. Clockwise is positive. */
int32_t fm1_enc_take(uint32_t e);

/* ---- MASTER pot ----
 *
 * fm1_master is our own function: felucca-bsp gives this value as
 * fm1_an.master or fm1_adc_read(FM1_ADC_MASTER).
 *
 * The pot is an input only. It changes the volume when the program uses it. */

#define FM1_MASTER_MAX 1023u

/* The position of the MASTER pot: 0 is fully left, FM1_MASTER_MAX is fully right. */
uint32_t fm1_master(void);

/* ---- MIDI input ----
 *
 * fm1_midi_take is our own function: felucca-bsp has a queue of such packets
 * (usb.c) but no function to read it.
 *
 * A message is one 32-bit packet in the USB-MIDI form. Use the macros to read
 * it. The status byte holds the message type and the channel, for example
 * 0x90 = note on, channel 1. You get the channel messages (notes, controllers,
 * program change, pressure, pitch bend) and clock, start, continue and stop. */

#define FM1_MIDI_STATUS(packet) (((packet) >> 8) & 0xFFu)
#define FM1_MIDI_DATA1(packet)  (((packet) >> 16) & 0xFFu)
#define FM1_MIDI_DATA2(packet)  (((packet) >> 24) & 0xFFu)

/* Gets the next MIDI message. Returns 1 and writes `*packet`, or returns 0
 * when there is no message. Call it from one place only: audio() or loop(). */
int fm1_midi_take(uint32_t *packet);

#ifdef __cplusplus
}
#endif

#endif
