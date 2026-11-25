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
#include "FxDef.hpp"
#include <cstdint>

enum cp_fx_tab_t { CP_FX_TAB_PAD, CP_FX_TAB_FX, CP_FX_TAB_EDIT, CP_FX_TAB_ROUTE };
/// The sources, in the order crosspad::FxSource has them.
enum cp_fx_src_t { CP_FX_SRC_IN12, CP_FX_SRC_IN34, CP_FX_SRC_MIC, CP_FX_SRC_USB, CP_FX_SRC_COUNT };
/// The outputs, in the order crosspad::FxOutput has them.
enum cp_fx_out_t { CP_FX_OUT_12, CP_FX_OUT_34, CP_FX_OUT_USB, CP_FX_OUT_COUNT };
enum cp_fx_tap_t { CP_FX_TAP_PRE, CP_FX_TAP_POST };   /* send taken before / after the source fader */

/* Audio side: called whenever an effect slot changes.
 * fx = -1 means the slot is empty (dry). x, y are the effect's first and second parameter (the engine's
 * order, whichever axis of the surface each is on), already mapped through range + curve, 0..1. */
typedef void (*cp_fxpad_param_cb_t)(int slot, int fx, float x, float y, float wet, void *user);
/* A soft key the screen has no audio for of its own: TAP, KILL, MAIN FW (back to the other firmware)
 * and USB REC (USB to its audio profile, so a phone or a computer records the FX). */
enum cp_fx_action_t { CP_FX_ACT_TAP, CP_FX_ACT_KILL, CP_FX_ACT_EXIT, CP_FX_ACT_USB };
typedef void (*cp_fxpad_action_cb_t)(cp_fx_action_t a, void *user);
/* Anything a player would expect to find as they left it changed: a route,
 * a send, an effect's axes, range, curve or release, SYNC, THRU, MONO.
 * Called from cp_fxpad_tick() on LVGL's thread, outside the lock the pads
 * take: a save never holds up a pad. */
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

/* LED colour for a pad of the bank on the pads (RGB 0xRRGGBB) and whether its effect is held. */
uint32_t cp_fxpad_pad_color(int idx);
bool cp_fxpad_pad_held(int idx);

/* Every held effect let go (HOLD and LATCH included), as KILL does; the
 * caller then cuts the engine's tails. */
void cp_fxpad_kill(void);

/* Encoder: EDIT row / ROUTE send level of the selected source. Click: PAD → FX → EDIT → PAD, ROUTE → source on/off. */
void cp_fxpad_encoder(int delta);
void cp_fxpad_encoder_click(void);

void cp_fxpad_set_tab(cp_fx_tab_t t);
/* The bank on the pads: 0 = A, 1 = B. */
int cp_fxpad_bank(void);
void cp_fxpad_set_bank(int bank);
/* Whether USB is in its audio profile, for USB REC's key. */
void cp_fxpad_set_usb_rec(bool on);
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
