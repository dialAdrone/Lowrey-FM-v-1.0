/* Lowrey FM in the browser: the FM-1's API (fm1.h) for main.c, compiled together to
 * WebAssembly. The page (sim.js) draws the screen, plays the audio, and sends the keys,
 * buttons, knobs, the MASTER slider and MIDI in. */
#include <stdint.h>
#include <string.h>
#include "fm1.h"

volatile fm1_in_t fm1_in;
static uint16_t fb[240 * 240];
static uint8_t leds[FM1_NLED];
static int32_t enc[FM1_NENC];
static uint32_t master = 800u;
static uint32_t btn_seen = 0, note_seen = 0;
static uint32_t midi_q[256]; static unsigned mq_r = 0, mq_w = 0;
static uint8_t store[FM1_STORE_SLOTS][FM1_STORE_MAX + 4];   /* +4: the length */
static int store_dirty = 0;
static int32_t abuf[2 * FM1_AUDIO_BLOCK];
static float fout[2 * FM1_AUDIO_BLOCK];

int fm1_lcd_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) {
    if (x + w > 240u || y + h > 240u) return -2;
    for (uint32_t j = y; j < y + h; j++) for (uint32_t i = x; i < x + w; i++) fb[j * 240u + i] = c;
    return 0;
}
int fm1_lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p, uint32_t n) {
    if (x + w > 240u || y + h > 240u || n < w * h) return -2;
    for (uint32_t j = 0; j < h; j++) memcpy(&fb[(y + j) * 240u + x], &p[j * w], w * 2u);
    return 0;
}
uint32_t fm1_input_edges(uint32_t *released) {
    uint32_t now = fm1_in.buttons, down = now & ~btn_seen, up = btn_seen & ~now;
    btn_seen = now; if (released) *released = up; return down;
}
uint32_t fm1_input_note_edges(void) {
    uint32_t now = fm1_in.notes, down = now & ~note_seen; note_seen = now; return down;
}
void fm1_led_key(uint32_t id, int on) { if (id < FM1_NLED) leds[id] = on ? FM1_LED_ON : FM1_LED_OFF; }
void fm1_led_level(uint32_t id, int level) { if (id < FM1_NLED) leds[id] = (uint8_t)level; }
void fm1_led_dim_low(int low) { (void)low; }
int32_t fm1_enc_take(uint32_t e) { if (e >= FM1_NENC) return 0; int32_t d = enc[e]; enc[e] = 0; return d; }
uint32_t fm1_master(void) { return master; }
int fm1_midi_take(uint32_t *p) { if (mq_r == mq_w) return 0; *p = midi_q[mq_r]; mq_r = (mq_r + 1u) & 255u; return 1; }
int fm1_midi_send(uint32_t p) { (void)p; return 0; }
int fm1_store_load(uint32_t s, void *d, uint32_t m) {
    if (s >= FM1_STORE_SLOTS) return -1;
    uint32_t len; memcpy(&len, store[s], 4);
    if (len == 0u || len > FM1_STORE_MAX) return -1;
    if (len > m) len = m;
    memcpy(d, store[s] + 4, len); return (int)len;
}
int fm1_store_save(uint32_t s, const void *d, uint32_t l) {
    if (s >= FM1_STORE_SLOTS || l > FM1_STORE_MAX) return -1;
    memcpy(store[s], &l, 4); memcpy(store[s] + 4, d, l); store_dirty = 1; return 0;
}
int fm1_battery(void) { return FM1_BATTERY_USB; }

/* ---- for the page ---- */
__attribute__((export_name("sim_setup"))) void sim_setup(void) { setup(); }
__attribute__((export_name("sim_block"))) float *sim_block(void) {     /* one audio block, then loop() */
    audio(abuf, FM1_AUDIO_BLOCK);
    loop();
    for (int i = 0; i < 2 * FM1_AUDIO_BLOCK; i++) fout[i] = (float)abuf[i] * (1.0f / 8388608.0f);
    return fout;
}
__attribute__((export_name("sim_fb"))) uint16_t *sim_fb(void) { return fb; }
__attribute__((export_name("sim_leds"))) uint8_t *sim_leds(void) { return leds; }
__attribute__((export_name("sim_store"))) uint8_t *sim_store(void) { return &store[0][0]; }
__attribute__((export_name("sim_store_size"))) int sim_store_size(void) { return (int)sizeof store; }
__attribute__((export_name("sim_store_dirty"))) int sim_store_dirty(void) { int d = store_dirty; store_dirty = 0; return d; }
__attribute__((export_name("sim_keys"))) void sim_keys(uint32_t notes, uint32_t buttons) { fm1_in.notes = notes; fm1_in.buttons = buttons; }
__attribute__((export_name("sim_enc"))) void sim_enc(uint32_t e, int32_t d) { if (e < FM1_NENC) enc[e] += d; }
__attribute__((export_name("sim_master"))) void sim_master(uint32_t m) { master = m > FM1_MASTER_MAX ? FM1_MASTER_MAX : m; }
__attribute__((export_name("sim_midi"))) void sim_midi(uint32_t st, uint32_t d1, uint32_t d2) {
    unsigned n = (mq_w + 1u) & 255u; if (n == mq_r) return;
    midi_q[mq_w] = FM1_MIDI_PACKET(st, d1, d2); mq_w = n;
}
