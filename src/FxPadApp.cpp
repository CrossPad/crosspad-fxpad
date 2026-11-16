// SPDX-License-Identifier: GPL-3.0-or-later
//
// FX Pad: the board as a performance multi-effect. What comes in -- line
// 1/2, line 3/4, the mics, the computer over USB -- goes through the effects
// the pads hold and out to the phones, the line out and the computer. The
// one app of the fxpad firmware kit (no sampler, no song: the render is the
// inputs, the FX bus and the outputs).
//
// The pads start an effect from their own task, straight into the engine's
// atomics -- an effect is on within the block after the hit, not after the
// next screen refresh. The screen and the pad LEDs follow from a timer.

#include <cstdio>
#include <memory>

#include <crosspad/app/AppRegistrar.hpp>
#include <crosspad/audio/IFxBus.hpp>
#include <crosspad/led/RgbColor.hpp>
#include <crosspad/pad/IPadLogicHandler.hpp>
#include <crosspad/pad/PadManager.hpp>
#include <crosspad/platform/IClock.hpp>
#include <crosspad/platform/PlatformServices.hpp>
#include <crosspad/settings/CrosspadSettings.hpp>
#include <crosspad/settings/IKeyValueStore.hpp>
#include <crosspad/transport/Transport.hpp>

#include "crosspad-gui/components/app_icon.h"
#include "crosspad-gui/components/power_gesture.h"
#include "crosspad-gui/fw/IFwViewService.h"
#include "crosspad-gui/platform/IGuiPlatform.h"
#include "crosspad-gui/widgets/gui_helpers.h"

#include "lvgl.h"

#include "FxEngine.hpp"
#include "FxPadUi.hpp"

namespace {

constexpr const char* kPadLogicName = "FxPad";
constexpr const char* kNs = "fxpad";
constexpr uint32_t kTickMs = 30;
/* The screen's refresh while FX Pad runs: a finger on the XY pad is drawn
 * every 16 ms instead of LVGL's 33 -- the pad redraws only the cursor's few
 * small objects, so the SPI has the time. */
constexpr uint32_t kRefreshMs = 16;

fxpad::FxEngine s_engine;      // lives as long as the firmware: the render may hold it a block after detach

crosspad::IFxBus* bus() { return crosspad::getPlatformServices().fxBus; }
crosspad::IKeyValueStore* store() { return crosspad::getPlatformServices().kvStore; }

/* ── The pads (their own task) ───────────────────────────────────────── */

class FxPadLogic : public crosspad::IPadLogicHandler {
public:
    void onActivate(crosspad::PadManager&) override {}
    void onDeactivate(crosspad::PadManager& pm) override
    {
        for (uint8_t i = 0; i < 16; ++i) pm.setPadColor(i, crosspad::RgbColor(0, 0, 0));
    }
    void onPadPress(crosspad::PadManager&, uint8_t pad, uint8_t velocity) override { cp_fxpad_pad(pad, true, velocity); }
    void onPadRelease(crosspad::PadManager&, uint8_t pad) override { cp_fxpad_pad(pad, false, 0); }
    void onPadPressure(crosspad::PadManager&, uint8_t pad, uint8_t pressure) override
    {
        if (pressure && cp_fxpad_pad_held(pad)) cp_fxpad_pad(pad, true, pressure);
    }
};
std::shared_ptr<FxPadLogic> s_padLogic;

/* ── Settings: the screen, the engine and the bus agree, and the card keeps them ── */

void applyAll()
{
    s_engine.setSync(cp_fxpad_sync());
    s_engine.setThru(cp_fxpad_dry_thru());
    s_engine.setMono(cp_fxpad_mono());
    for (int f = 0; f < CP_FX_COUNT; ++f) s_engine.setSnap(f, cp_fxpad_def(f)->release == CP_FX_REL_SNAP);
    crosspad::IFxBus* b = bus();
    if (!b) return;
    for (int s = 0; s < CP_FX_SRC_COUNT; ++s) b->setSend((crosspad::FxSource)s, cp_fxpad_send((cp_fx_src_t)s) / 100.0f);
    for (int o = 0; o < CP_FX_OUT_COUNT; ++o) b->setReturn((crosspad::FxOutput)o, cp_fxpad_out((cp_fx_out_t)o));
    b->setPreFader(cp_fxpad_tap() == CP_FX_TAP_PRE);
}

void save()
{
    crosspad::IKeyValueStore* kv = store();
    if (!kv) return;
    char key[8];
    for (int s = 0; s < CP_FX_SRC_COUNT; ++s) {
        std::snprintf(key, sizeof key, "s%d", s);
        kv->saveU8(kNs, key, cp_fxpad_send((cp_fx_src_t)s));
    }
    for (int o = 0; o < CP_FX_OUT_COUNT; ++o) {
        std::snprintf(key, sizeof key, "o%d", o);
        kv->saveBool(kNs, key, cp_fxpad_out((cp_fx_out_t)o));
    }
    kv->saveBool(kNs, "pre", cp_fxpad_tap() == CP_FX_TAP_PRE);
    kv->saveBool(kNs, "thru", cp_fxpad_dry_thru());
    kv->saveBool(kNs, "mono", cp_fxpad_mono());
    kv->saveBool(kNs, "sync", cp_fxpad_sync());
    kv->saveU8(kNs, "bank", (uint8_t)cp_fxpad_bank());
    for (int f = 0; f < CP_FX_COUNT; ++f) {
        std::snprintf(key, sizeof key, "r%d", f);
        kv->saveBool(kNs, key, cp_fxpad_def(f)->release == CP_FX_REL_SNAP);
        std::snprintf(key, sizeof key, "e%d", f);
        kv->saveBool(kNs, key, cp_fxpad_def(f)->exp_curve);
        std::snprintf(key, sizeof key, "w%d", f);
        kv->saveBool(kNs, key, cp_fxpad_def(f)->swap);
    }
}

void restore()
{
    crosspad::IKeyValueStore* kv = store();
    if (!kv) return;
    char key[8];
    for (int s = 0; s < CP_FX_SRC_COUNT; ++s) {
        std::snprintf(key, sizeof key, "s%d", s);
        cp_fxpad_set_send((cp_fx_src_t)s, kv->readU8(kNs, key, cp_fxpad_send((cp_fx_src_t)s)));
    }
    for (int o = 0; o < CP_FX_OUT_COUNT; ++o) {
        std::snprintf(key, sizeof key, "o%d", o);
        cp_fxpad_set_out((cp_fx_out_t)o, kv->readBool(kNs, key, cp_fxpad_out((cp_fx_out_t)o)));
    }
    cp_fxpad_set_tap(kv->readBool(kNs, "pre", cp_fxpad_tap() == CP_FX_TAP_PRE) ? CP_FX_TAP_PRE : CP_FX_TAP_POST);
    cp_fxpad_set_dry_thru(kv->readBool(kNs, "thru", cp_fxpad_dry_thru()));
    cp_fxpad_set_mono(kv->readBool(kNs, "mono", cp_fxpad_mono()));
    cp_fxpad_set_sync(kv->readBool(kNs, "sync", cp_fxpad_sync()));
    cp_fxpad_set_bank(kv->readU8(kNs, "bank", 0));
    for (int f = 0; f < CP_FX_COUNT; ++f) {
        cp_fx_def_t* d = cp_fxpad_def(f);
        std::snprintf(key, sizeof key, "r%d", f);
        /* TAIL kept from 0.2.1 for an effect that cannot ring out reads as SNAP: that is how it stopped. */
        const bool snap = kv->readBool(kNs, key, d->release == CP_FX_REL_SNAP) || !fxpad::FxEngine::ringsOut(f);
        d->release = snap ? CP_FX_REL_SNAP : CP_FX_REL_TAIL;
        std::snprintf(key, sizeof key, "e%d", f);
        d->exp_curve = kv->readBool(kNs, key, d->exp_curve);
        std::snprintf(key, sizeof key, "w%d", f);
        if (kv->readBool(kNs, key, d->swap) != d->swap) cp_fx_swap(d);
    }
}

void onChanged(void*)
{
    applyAll();
    save();
}

void onParam(int slot, int fx, float x, float y, float wet, void*) { s_engine.setSlot(slot, fx, x, y, wet); }

/* TAP: the tempo from the last taps, 0.2 .. 2 s apart, averaged over up to
 * three intervals; the top bar's BPM shows it. */
int64_t s_taps[4] = {};
int s_tapCount = 0;

void tapTempo()
{
    const int64_t now = crosspad::getClock().getTimeUs();
    if (s_tapCount && (now - s_taps[s_tapCount - 1] > 2000000 || now - s_taps[s_tapCount - 1] < 200000)) s_tapCount = 0;
    if (s_tapCount == 4) {
        for (int i = 0; i < 3; ++i) s_taps[i] = s_taps[i + 1];
        s_tapCount = 3;
    }
    s_taps[s_tapCount++] = now;
    if (s_tapCount < 2) return;
    const float sec = (float)(s_taps[s_tapCount - 1] - s_taps[0]) / 1e6f / (float)(s_tapCount - 1);
    crosspad::transportSetTempo(60.0f / sec);
    crosspad::transportCommitTempo();
}

void onAction(cp_fx_action_t a, void*)
{
    switch (a) {
    case CP_FX_ACT_TAP:  tapTempo(); break;
    case CP_FX_ACT_KILL: s_engine.kill(); break;
    case CP_FX_ACT_EXIT:
        /* Back to the full firmware in the other slot; nothing happens where
         * there is none. */
        s_engine.kill();
        crosspad_gui::fwViewService().flowSwitchAndRestart(false);
        break;
    case CP_FX_ACT_USB: {
        /* USB REC: the USB audio profile (UAC2, class compliant), so a phone
         * or a computer records what comes out of the effects; again, back to
         * the data profile. The profile persists (usb_config_manager). */
        crosspad::CrosspadSettings* cfg = crosspad::CrosspadSettings::getInstance();
        const bool on = !(cfg && cfg->usbAudioEnabled);
        if (on) {
            cp_fxpad_set_out(CP_FX_OUT_USB, true);
            onChanged(nullptr);
        }
        crosspad_gui::getGuiPlatform().setUsbAudioEnabled(on);
        break;
    }
    }
}

/* ── LVGL thread ─────────────────────────────────────────────────────── */

lv_timer_t* s_timer = nullptr;
lv_group_t* s_group = nullptr;
lv_group_t* s_prevGroup = nullptr;
lv_obj_t* s_input = nullptr;
uint32_t s_backToken = 0;

lv_indev_t* navEncoder() { return crosspad_gui::getGuiPlatform().getNavigationEncoder(); }

/* A held pad in its effect's colour, the others a glow of it. */
void paintPads()
{
    crosspad::PadManager& pm = crosspad::getPadManager();
    for (uint8_t i = 0; i < CP_FX_BANK; ++i) {
        const uint32_t c = cp_fxpad_pad_color(i);
        const int k = cp_fxpad_pad_held(i) ? 255 : 28;
        pm.setPadColor(i, crosspad::RgbColor((uint8_t)(((c >> 16) & 0xFF) * k / 255),
                                             (uint8_t)(((c >> 8) & 0xFF) * k / 255), (uint8_t)((c & 0xFF) * k / 255)));
    }
}

void tick(lv_timer_t*)
{
    const float bpm = crosspad::transportTempo();
    if (bpm > 0.0f) s_engine.setTempo(bpm);
    if (crosspad::IFxBus* b = bus()) {
        for (int s = 0; s < CP_FX_SRC_COUNT; ++s) {
            cp_fxpad_set_available((cp_fx_src_t)s, b->sourceAvailable((crosspad::FxSource)s));
        }
    }
    crosspad::CrosspadSettings* cfg = crosspad::CrosspadSettings::getInstance();
    cp_fxpad_set_usb_rec(cfg && cfg->usbAudioEnabled);
    if (cp_fxpad_tick()) paintPads();
}

lv_timer_t* refreshTimer()
{
    lv_display_t* d = lv_display_get_default();
    return d ? lv_display_get_refr_timer(d) : nullptr;
}

void onEncoderKey(lv_event_t* e)
{
    const uint32_t k = lv_event_get_key(e);
    if (k != LV_KEY_LEFT && k != LV_KEY_RIGHT && k != LV_KEY_ENTER) return;
    lv_event_stop_processing(e);
    if (k == LV_KEY_ENTER) cp_fxpad_encoder_click();
    else cp_fxpad_encoder(k == LV_KEY_RIGHT ? 1 : -1);
}

/* The power button's click: every effect off, tails cut. */
bool onPowerBack(void*)
{
    s_engine.kill();
    return true;
}

lv_obj_t* lv_CreateFxPad(lv_obj_t* parent, App* app)
{
    (void)app;
    restore();
    cp_fxpad_set_param_cb(onParam, nullptr);
    cp_fxpad_set_action_cb(onAction, nullptr);
    cp_fxpad_set_changed_cb(onChanged, nullptr);
    lv_obj_t* root = cp_fxpad_create(parent);

    /* The encoder's group: one object always in edit mode, so a turn is a
     * key here rather than a move of focus. */
    s_input = lv_obj_create(root);
    lv_obj_remove_style_all(s_input);
    lv_obj_set_size(s_input, 1, 1);
    s_group = lv_group_create();
    lv_group_add_obj(s_group, s_input);
    lv_obj_add_event_cb(s_input, onEncoderKey, static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS),
                        nullptr);
    if (lv_indev_t* nav = navEncoder()) {
        s_prevGroup = lv_indev_get_group(nav);
        lv_indev_set_group(nav, s_group);
    }
    lv_group_focus_obj(s_input);
    lv_group_set_editing(s_group, true);
    s_backToken = crosspad_gui::ui_back_push(onPowerBack, nullptr);

    if (!s_padLogic) {
        s_padLogic = std::make_shared<FxPadLogic>();
        crosspad::getPadManager().registerPadLogic(kPadLogicName, s_padLogic);
    }
    applyAll();
    if (crosspad::IFxBus* b = bus()) b->attach(&s_engine);
    paintPads();
    s_timer = lv_timer_create(tick, kTickMs, nullptr);
    if (lv_timer_t* t = refreshTimer()) lv_timer_set_period(t, kRefreshMs);
    return root;
}

void lv_DestroyFxPad(lv_obj_t* root)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    if (lv_timer_t* t = refreshTimer()) lv_timer_set_period(t, LV_DEF_REFR_PERIOD);
    if (crosspad::IFxBus* b = bus()) b->detach();
    s_engine.kill();
    crosspad_gui::ui_back_remove(s_backToken);
    s_backToken = 0;
    if (lv_indev_t* nav = navEncoder()) lv_indev_restore_group(nav, s_prevGroup);
    if (s_group) {
        lv_group_delete(s_group);
        s_group = nullptr;
    }
    s_input = nullptr;
    cp_fxpad_forget();
    if (root) lv_obj_delete_async(root);
}

} // namespace

REGISTER_APP_PL(FxPad, nullptr, crosspad_gui::resolveAppIcon("fxpad.png"), lv_CreateFxPad, lv_DestroyFxPad, nullptr,
                nullptr, nullptr, 0, kPadLogicName)
