// SPDX-License-Identifier: GPL-3.0-or-later
//
// The FX Pad's screen on host LVGL: a 320 x 240 buffer for a display, a
// scripted finger. What a player can see and reach, not the sound.
#include <catch2/catch_test_macros.hpp>

#include "FxPadUi.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

constexpr int kW = 320, kH = 240;
constexpr int kBar = 40;             /* the board's top bar: the app's 320 x 200 sits under it */

uint32_t g_ms = 0;
uint32_t tick_cb() { return g_ms; }
uint16_t g_fb[kW * kH];
bool g_down = false;
lv_point_t g_at = { 0, 0 };

void flush_cb(lv_display_t* d, const lv_area_t* a, uint8_t* px)
{
    const uint16_t* s = (const uint16_t*)px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++) g_fb[y * kW + x] = *s++;
    lv_display_flush_ready(d);
}
void read_cb(lv_indev_t*, lv_indev_data_t* data)
{
    data->point = g_at;
    data->state = g_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void run(int ms)
{
    for (int i = 0; i < ms; i += 5) { g_ms += 5; lv_timer_handler(); }
}
void tap(int x, int y)
{
    g_at = { (int32_t)x, (int32_t)y };
    g_down = true; run(60);
    g_down = false; run(60);
}

/* One screen for the whole run: the screen's state is the module's own. */
lv_obj_t* screen()
{
    static lv_obj_t* root = nullptr;
    if (root) return root;
    lv_init();
    lv_tick_set_cb(tick_cb);
    static uint16_t buf[kW * kH];
    lv_display_t* d = lv_display_create(kW, kH);
    lv_display_set_buffers(d, buf, nullptr, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t* in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, read_cb);
    lv_obj_t* host = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(host);
    lv_obj_set_pos(host, 0, kBar);
    lv_obj_set_size(host, kW, kH - kBar);
    root = cp_fxpad_create(host);
    run(50);
    return root;
}

std::string what(lv_obj_t* o)
{
    char b[160];
    lv_area_t a; lv_obj_get_coords(o, &a);
    const char* t = "";
    if (lv_obj_check_type(o, &lv_label_class)) t = lv_label_get_text(o);
    else if (lv_obj_get_child_count(o) && lv_obj_check_type(lv_obj_get_child(o, 0), &lv_label_class))
        t = lv_label_get_text(lv_obj_get_child(o, 0));
    snprintf(b, sizeof b, "'%s' x %d..%d y %d..%d", t, (int)a.x1, (int)a.x2, (int)a.y1, (int)a.y2);
    return b;
}

/* Every object drawn now that reaches past the object it sits in: LVGL cuts
 * it there, so that part is neither seen nor reached by a finger. */
void cut_by_parent(lv_obj_t* o, std::vector<std::string>& out)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    lv_area_t a, p; lv_obj_get_coords(o, &a);
    lv_obj_t* par = lv_obj_get_parent(o);
    lv_obj_get_coords(par, &p);
    if (a.x1 < p.x1 || a.y1 < p.y1 || a.x2 > p.x2 || a.y2 > p.y2)
        out.push_back(what(o) + " in " + what(par));
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) cut_by_parent(lv_obj_get_child(o, i), out);
}
std::vector<std::string> cut(lv_obj_t* root)
{
    lv_obj_update_layout(root);
    std::vector<std::string> out;
    cut_by_parent(root, out);
    return out;
}
std::string joined(const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& e : v) s += e + "\n";
    return s;
}

/* FXPAD_UI_SHOTS=<dir>: the frame as <dir>/<name>.ppm, to look at. */
void shot(const char* name)
{
    const char* dir = getenv("FXPAD_UI_SHOTS");
    if (!dir) return;
    lv_obj_invalidate(lv_screen_active());
    run(40);
    FILE* f = fopen((std::string(dir) + "/" + name + ".ppm").c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", kW, kH);
    for (int i = 0; i < kW * kH; i++) {
        const uint16_t c = g_fb[i];
        const uint8_t rgb[3] = { (uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

lv_obj_t* find_text(lv_obj_t* o, const char* text)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(o, &lv_label_class) && !strcmp(lv_label_get_text(o), text)) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        if (lv_obj_t* r = find_text(lv_obj_get_child(o, i), text)) return r;
    return nullptr;
}

}  // namespace

TEST_CASE("PAD: nothing is cut by what it sits in", "[ui]")
{
    lv_obj_t* root = screen();
    cp_fxpad_set_tab(CP_FX_TAB_PAD);
    INFO(joined(cut(root)));
    CHECK(cut(root).empty());
    shot("pad");
}

TEST_CASE("FX: every tile shows its whole name, in both banks", "[ui]")
{
    lv_obj_t* root = screen();
    cp_fxpad_set_tab(CP_FX_TAB_FX);
    for (int bank = 0; bank < 2; bank++) {
        cp_fxpad_set_bank(bank);
        cp_fxpad_tick();
        INFO("bank " << (bank ? 'B' : 'A') << "\n" << joined(cut(root)));
        CHECK(cut(root).empty());
        shot(bank ? "fx_b" : "fx_a");
    }
    cp_fxpad_set_bank(0);
    cp_fxpad_tick();
}

TEST_CASE("EDIT: nothing is cut, whichever effect is edited", "[ui]")
{
    lv_obj_t* root = screen();
    for (int f = 0; f < CP_FX_COUNT; f++) {
        /* The effect a pad started is the one EDIT shows. */
        cp_fxpad_set_bank(f / CP_FX_BANK);
        cp_fxpad_pad(f % CP_FX_BANK, true, 100);
        cp_fxpad_pad(f % CP_FX_BANK, false, 0);
        cp_fxpad_set_tab(CP_FX_TAB_EDIT);
        INFO(cp_fxpad_def(f)->name << "\n" << joined(cut(root)));
        CHECK(cut(root).empty());
    }
    shot("edit");
    cp_fxpad_set_bank(0);
    cp_fxpad_tick();
}

TEST_CASE("ROUTE: the three outputs are whole, on the page, and a finger reaches each", "[ui]")
{
    lv_obj_t* root = screen();
    cp_fxpad_set_tab(CP_FX_TAB_ROUTE);
    INFO(joined(cut(root)));
    CHECK(cut(root).empty());
    shot("route");

    static const char* OUT[] = { "OUT 1/2", "OUT 3/4", "USB" };
    for (int o = 0; o < CP_FX_OUT_COUNT; o++) {
        lv_obj_t* l = find_text(root, OUT[o]);
        REQUIRE(l);
        lv_area_t a; lv_obj_get_coords(lv_obj_get_parent(l), &a);
        const bool was = cp_fxpad_out((cp_fx_out_t)o);
        tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
        INFO("output " << OUT[o]);
        CHECK(cp_fxpad_out((cp_fx_out_t)o) != was);
        cp_fxpad_set_out((cp_fx_out_t)o, was);
    }
}
