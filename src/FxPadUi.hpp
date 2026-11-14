// SPDX-License-Identifier: GPL-3.0-or-later
//
// The FX Pad's screen: a Kaoss-style performance multi-effect. 320 x 200
// under the top bar.
//   Tabs PAD / FX / EDIT / ROUTE  y 4  h 32
//   Body                          y 40 h 120
//   Soft keys                     y 164 h 32
// Play: a pad held = its effect on, a finger on the XY surface = its two
// parameters. Up to two pads at once (LATCH) share one XY point; the pad's
// pressure sets WET. Fonts: Montserrat 10 / 14 / 16.
#pragma once

#include "lvgl.h"
#include <cstdint>

#define CP_FX_COUNT 16
#define CP_FX_SLOTS 2

enum cp_fx_cat_t { CP_FX_CAT_DLY, CP_FX_CAT_REV, CP_FX_CAT_FLT, CP_FX_CAT_MOD, CP_FX_CAT_GLT, CP_FX_CAT_DST };
enum cp_fx_tab_t { CP_FX_TAB_PAD, CP_FX_TAB_FX, CP_FX_TAB_EDIT, CP_FX_TAB_ROUTE };
/// The sources, in the order crosspad::FxSource has them.
enum cp_fx_src_t { CP_FX_SRC_IN12, CP_FX_SRC_IN34, CP_FX_SRC_MIC, CP_FX_SRC_USB, CP_FX_SRC_COUNT };
/// The outputs, in the order crosspad::FxOutput has them.
enum cp_fx_out_t { CP_FX_OUT_12, CP_FX_OUT_34, CP_FX_OUT_USB, CP_FX_OUT_COUNT };
enum cp_fx_tap_t { CP_FX_TAP_PRE, CP_FX_TAP_POST };   /* send taken before / after the source fader */
enum cp_fx_release_t { CP_FX_REL_TAIL, CP_FX_REL_SNAP };

struct cp_fx_def_t {
    const char *name;
    cp_fx_cat_t cat;
    const char *px, *py;     /* parameter names on X and Y */
    float x_lo, x_hi;        /* usable range of X, 0..1 of the full parameter */
    float y_lo, y_hi;
    bool exp_curve;
    cp_fx_release_t release;
};

/* Audio side: called whenever an effect slot changes.
 * fx = -1 means the slot is empty (dry). x, y already mapped through range + curve, 0..1. */
typedef void (*cp_fxpad_param_cb_t)(int slot, int fx, float x, float y, float wet, void *user);
/* A soft key the screen has no audio for of its own: TAP, KILL, and MAIN FW (back to the other firmware). */
enum cp_fx_action_t { CP_FX_ACT_TAP, CP_FX_ACT_KILL, CP_FX_ACT_EXIT };
typedef void (*cp_fxpad_action_cb_t)(cp_fx_action_t a, void *user);
/* Anything a player would expect to find as they left it changed: a route,
 * a send, an effect's range, curve or release, SYNC, THRU, MONO. */
typedef void (*cp_fxpad_changed_cb_t)(void *user);

lv_obj_t *cp_fxpad_create(lv_obj_t *parent);
/* The screen is gone (its root deleted): nothing is drawn until the next create. */
void cp_fxpad_forget(void);
void cp_fxpad_set_param_cb(cp_fxpad_param_cb_t cb, void *user);
void cp_fxpad_set_action_cb(cp_fxpad_action_cb_t cb, void *user);
void cp_fxpad_set_changed_cb(cp_fxpad_changed_cb_t cb, void *user);

/* Hardware pads: idx 0..15 (pad 1 bottom left), velocity 0..127. Call on press, aftertouch and release,
 * from the pads' own task: the effect starts at once, the screen follows at cp_fxpad_tick(). */
void cp_fxpad_pad(int idx, bool down, uint8_t velocity);

/* LED colour for a pad (RGB 0xRRGGBB) and whether it is held, for the pad LED driver. */
uint32_t cp_fxpad_pad_color(int idx);
bool cp_fxpad_pad_held(int idx);

/* Encoder: EDIT row / ROUTE send level of the selected source. Click: PAD → FX → EDIT → PAD, ROUTE → source on/off. */
void cp_fxpad_encoder(int delta);
void cp_fxpad_encoder_click(void);

void cp_fxpad_set_tab(cp_fx_tab_t t);
cp_fx_def_t *cp_fxpad_def(int idx);
/* ROUTE: several sources can feed the effect at once. send 0..100, 0 = not routed. */
uint8_t cp_fxpad_send(cp_fx_src_t s);
void cp_fxpad_set_send(cp_fx_src_t s, uint8_t send);
/* A source the board cannot hear now (USB off, a third analog input): drawn dim. */
void cp_fxpad_set_available(cp_fx_src_t s, bool available);
bool cp_fxpad_out(cp_fx_out_t o);
void cp_fxpad_set_out(cp_fx_out_t o, bool on);
cp_fx_tap_t cp_fxpad_tap(void);
void cp_fxpad_set_tap(cp_fx_tap_t t);
bool cp_fxpad_dry_thru(void);      /* pass sources dry when no pad is held */
void cp_fxpad_set_dry_thru(bool on);
bool cp_fxpad_mono(void);
void cp_fxpad_set_mono(bool on);
bool cp_fxpad_sync(void);
void cp_fxpad_set_sync(bool on);
/* WET from the pads' pressure, or always full. */
bool cp_fxpad_full_wet(void);
/* From an LVGL timer: redraws what the pads changed since the last call;
 * true when it did, so the caller repaints the pad LEDs too. */
bool cp_fxpad_tick(void);
