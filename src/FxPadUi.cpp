// SPDX-License-Identifier: GPL-3.0-or-later
#include "FxPadUi.hpp"

#include <cmath>
#include <mutex>
#include <cstdio>
#include <cstring>

#define C_BG    0x16181B
#define C_PANEL 0x20242A
#define C_WELL  0x1B1E22
#define C_LINE  0x2C3138
#define C_TEXT  0xE8EAED
#define C_DIM   0x8A9099
#define C_LIME  0xC6FF47
#define C_RED   0xFF5C5C
#define C_INK   0x06120C
#define F10 (&lv_font_montserrat_10)
#define F14 (&lv_font_montserrat_14)

#define XY_X 4
#define XY_Y 40
#define XY_W 312
#define XY_H 120
#define TRAIL_N 14

static const uint32_t CAT_COL[] = { 0x4EA6D3, 0xA77BF0, 0xC6FF47, 0x4FE0B0, 0xFF5C5C, 0xFFC857 };
static const char *CAT_TAG[] = { "DLY", "REV", "FLT", "MOD", "GLT", "DST" };

/* In pad order, as fxpad::Fx has them. */
static cp_fx_def_t FX[CP_FX_COUNT] = {
    {"ECHO",CP_FX_CAT_DLY,"TIME","FDBK",0,1,0,.95f,false,CP_FX_REL_TAIL},
    {"TAPE ECHO",CP_FX_CAT_DLY,"TIME","WOW",0,1,0,1,false,CP_FX_REL_TAIL},
    {"PING PONG",CP_FX_CAT_DLY,"TIME","FDBK",0,1,0,.95f,false,CP_FX_REL_TAIL},
    {"ROLL",CP_FX_CAT_GLT,"RATE","DECAY",0,1,0,1,false,CP_FX_REL_SNAP},
    {"HALL",CP_FX_CAT_REV,"SIZE","DAMP",0,1,0,1,false,CP_FX_REL_TAIL},
    {"SHIMMER",CP_FX_CAT_REV,"SIZE","PITCH",0,1,0,1,false,CP_FX_REL_TAIL},
    {"FREEZE",CP_FX_CAT_REV,"TONE","LEVEL",0,1,0,1,false,CP_FX_REL_TAIL},
    {"GATE",CP_FX_CAT_GLT,"RATE","DEPTH",0,1,0,1,false,CP_FX_REL_SNAP},
    {"LP FILTER",CP_FX_CAT_FLT,"CUTOFF","RES",0,1,0,.9f,true,CP_FX_REL_SNAP},
    {"HP FILTER",CP_FX_CAT_FLT,"CUTOFF","RES",0,1,0,.9f,true,CP_FX_REL_SNAP},
    {"BAND SWEEP",CP_FX_CAT_FLT,"FREQ","WIDTH",0,1,0,1,true,CP_FX_REL_SNAP},
    {"WAH",CP_FX_CAT_FLT,"FREQ","RES",0,1,0,.9f,true,CP_FX_REL_SNAP},
    {"FLANGER",CP_FX_CAT_MOD,"SPEED","DEPTH",0,1,0,1,false,CP_FX_REL_TAIL},
    {"PHASER",CP_FX_CAT_MOD,"SPEED","FDBK",0,1,0,.9f,false,CP_FX_REL_TAIL},
    {"BITCRUSH",CP_FX_CAT_DST,"BITS","RATE",0,1,0,1,false,CP_FX_REL_SNAP},
    {"DRIVE",CP_FX_CAT_DST,"GAIN","TONE",0,1,0,1,false,CP_FX_REL_SNAP},
};
static const char *SRC_NAME[] = { "IN 1/2", "IN 3/4", "MIC", "USB 1/2" };
static const char *SRC_SUB[]  = { "Line", "Line", "Built-in", "Host" };
static const char *OUT_NAME[] = { "OUT 1/2", "OUT 3/4", "USB" };
static const char *OUT_SUB[]  = { "Phones", "Line", "Rec" };
static const int OUT_Y[] = { 4, 53, 102 };
static const char *TAB_NAME[] = { "PAD", "FX", "EDIT", "ROUTE" };

/* Soft keys: each tab's own, spread over the width. */
enum sk_t { SK_HOLD, SK_SYNC, SK_WET, SK_TAP, SK_KILL, SK_LATCH, SK_CLEAR, SK_EXIT,
            SK_SWAP, SK_LIN, SK_EXP, SK_SNAP, SK_TAIL, SK_DRY, SK_PRE, SK_POST, SK_MONO, SK_RCLR, SK_COUNT };
static const char *SK_NAME[SK_COUNT] = { "HOLD","SYNC","WET","TAP","KILL","LATCH","CLEAR","MAIN FW",
                                         "SWAP X/Y","LIN","EXP","SNAP","TAIL","THRU","PRE","POST","MONO","CLEAR" };
static const sk_t SK_PAD[]   = { SK_HOLD, SK_SYNC, SK_WET, SK_TAP, SK_KILL };
static const sk_t SK_FX[]    = { SK_LATCH, SK_CLEAR, SK_EXIT };
static const sk_t SK_EDIT[]  = { SK_SWAP, SK_LIN, SK_EXP, SK_SNAP, SK_TAIL };
static const sk_t SK_ROUTE[] = { SK_DRY, SK_PRE, SK_POST, SK_MONO, SK_RCLR };
static const sk_t *const SK_TAB[4] = { SK_PAD, SK_FX, SK_EDIT, SK_ROUTE };
static const int SK_N[4] = { 5, 3, 5, 5 };

static struct {
    lv_obj_t *root, *tab[4], *page[4], *sk[SK_COUNT];
    /* PAD */
    lv_obj_t *xy, *vline, *hline, *halo, *dot, *trail, *chip[CP_FX_SLOTS], *chipl[CP_FX_SLOTS], *name[CP_FX_SLOTS], *hint, *axx, *axy, *axw;
    lv_point_precise_t tp[TRAIL_N]; int tn;
    /* FX */
    lv_obj_t *tile[CP_FX_COUNT], *tname[CP_FX_COUNT], *ttag[CP_FX_COUNT];
    /* EDIT */
    lv_obj_t *erow[4], *ebar[2], *erng[2], *eval[4], *ehead, *echip;
    int esel;
    /* ROUTE */
    lv_obj_t *src[CP_FX_SRC_COUNT], *sdot[CP_FX_SRC_COUNT], *sbar[CP_FX_SRC_COUNT], *sfill[CP_FX_SRC_COUNT], *sval[CP_FX_SRC_COUNT], *sln[CP_FX_SRC_COUNT];
    lv_obj_t *out[CP_FX_OUT_COUNT], *oln[CP_FX_OUT_COUNT];
    lv_point_precise_t sp[CP_FX_SRC_COUNT][2], op[CP_FX_OUT_COUNT][2];
    uint8_t send[CP_FX_SRC_COUNT]; bool avail[CP_FX_SRC_COUNT]; bool outon[CP_FX_OUT_COUNT]; int ssel; cp_fx_tap_t tap; bool mono;
    cp_fx_tab_t tabsel;
    int held[CP_FX_SLOTS]; int nheld; uint8_t vel[CP_FX_COUNT];
    int active;                  /* last pressed / tapped fx, edited in EDIT */
    float x, y;                  /* 0..1, y = 0 at bottom */
    bool hold, sync, latch, monitor, fullwet;
    bool dirty;                  /* the pads changed what is held: redraw at the next tick */
    cp_fxpad_param_cb_t cb; void *user;
    cp_fxpad_action_cb_t acb; void *auser;
    cp_fxpad_changed_cb_t ccb; void *cuser;
} P;

/* The pads change what is held from their own task, the screen reads it on
 * LVGL's: every entry point takes this, briefly. Nothing under it waits. */
static std::recursive_mutex s_mu;
#define LOCKED std::lock_guard<std::recursive_mutex> lk_(s_mu); defaults()

/* Line 1/2 and USB sent, phones and USB out, THRU, SYNC: until the app
 * restores what the player left. */
static void defaults(void) {
    static bool done = false;
    if (done) return;
    done = true;
    P.send[CP_FX_SRC_IN12] = 80; P.send[CP_FX_SRC_USB] = 100;
    P.outon[CP_FX_OUT_12] = true; P.outon[CP_FX_OUT_USB] = true;
    for (bool &a : P.avail) a = true;
    P.sync = true; P.monitor = true; P.tap = CP_FX_TAP_POST;
}

static lv_color_t hx(uint32_t c) { return lv_color_hex(c); }
static lv_obj_t *box(lv_obj_t *p, int x, int y, int w, int h, int r, uint32_t c, lv_opa_t o) {
    lv_obj_t *b = lv_obj_create(p); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y); lv_obj_set_size(b, w, h); lv_obj_set_style_radius(b, r, 0);
    lv_obj_set_style_bg_color(b, hx(c), 0); lv_obj_set_style_bg_opa(b, o, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}
static lv_obj_t *txt(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y, const char *s) {
    lv_obj_t *l = lv_label_create(p); lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, hx(c), 0); lv_label_set_text(l, s); lv_obj_set_pos(l, x, y);
    return l;
}
static void set_bg(lv_obj_t *o, uint32_t c) { lv_obj_set_style_bg_color(o, hx(c), 0); }
static void set_fg(lv_obj_t *o, uint32_t c) { lv_obj_set_style_text_color(o, hx(c), 0); }
static void *ud(int i) { return (void *)(uintptr_t)i; }
static int ud_of(lv_event_t *e) { return (int)(uintptr_t)lv_event_get_user_data(e); }
static void changed(void) { if (P.ccb) P.ccb(P.cuser); }

static float curve(const cp_fx_def_t *d, float v, bool isx) {
    float lo = isx ? d->x_lo : d->y_lo, hi = isx ? d->x_hi : d->y_hi;
    if (d->exp_curve && isx) v = v * v;
    return lo + (hi - lo) * v;
}
/* A parameter's value as the engine hears it (FxEngine.cpp's mappings). */
static void fmt(char *b, size_t n, const char *p, float v) {
    static const char *T[] = { "1/32", "1/16", "1/8", "1/8 D", "1/4", "1/2" };
    if (!strcmp(p, "TIME")) {
        if (P.sync) snprintf(b, n, "%s", T[(int)lroundf(v * 5)]);
        else snprintf(b, n, "%d ms", (int)(20 * powf(75, v)));
    } else if (!strcmp(p, "RATE") && !P.sync) snprintf(b, n, "%d ms", (int)(30 * powf(16.7f, v)));
    else if (!strcmp(p, "RATE")) snprintf(b, n, "%s", T[(int)lroundf(v * 4)]);
    else if (!strcmp(p, "SPEED")) snprintf(b, n, "%.2f Hz", (double)(0.05f * powf(80, v)));
    else if (!strcmp(p, "CUTOFF")) snprintf(b, n, "%d Hz", (int)(80 * powf(200, v)));
    else if (!strcmp(p, "FREQ")) snprintf(b, n, "%d Hz", (int)(200 * powf(20, v)));
    else if (!strcmp(p, "BITS")) snprintf(b, n, "%d bit", (int)lroundf(16 - v * 12));
    else if (!strcmp(p, "PITCH")) snprintf(b, n, "+%d st", (int)lroundf(v * 24));
    else snprintf(b, n, "%d%%", (int)lroundf(v * 100));
}

static float wet_of(int f) { return P.fullwet ? 1.0f : P.vel[f] / 127.f; }

static void emit(void) {
    if (!P.cb) return;
    for (int s = 0; s < CP_FX_SLOTS; s++) {
        if (s < P.nheld) { int f = P.held[s]; P.cb(s, f, curve(&FX[f], P.x, true), curve(&FX[f], P.y, false), wet_of(f), P.user); }
        else P.cb(s, -1, 0, 0, 0, P.user);
    }
}

/* ---------- PAD view ---------- */
static void draw_pad(void) {
    int px = (int)(P.x * XY_W), py = (int)((1 - P.y) * XY_H);
    bool act = P.nheld > 0;
    uint32_t c = act ? CAT_COL[FX[P.held[P.nheld - 1]].cat] : C_DIM;
    lv_obj_set_style_border_color(P.xy, hx(c), 0); lv_obj_set_style_border_width(P.xy, act ? 2 : 0, 0);
    lv_obj_set_x(P.vline, px); set_bg(P.vline, c);
    lv_obj_set_y(P.hline, py); set_bg(P.hline, c);
    lv_obj_set_pos(P.halo, px - 16, py - 16); set_bg(P.halo, c);
    if (act) lv_obj_remove_flag(P.halo, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(P.halo, LV_OBJ_FLAG_HIDDEN);
    int r = act ? 9 : 6;
    lv_obj_set_size(P.dot, r * 2, r * 2); lv_obj_set_pos(P.dot, px - r, py - r);
    lv_obj_set_style_bg_opa(P.dot, act ? LV_OPA_COVER : LV_OPA_TRANSP, 0); set_bg(P.dot, c);
    lv_obj_set_style_border_color(P.dot, hx(c), 0);
    lv_obj_set_style_line_color(P.trail, hx(c), 0);
    lv_line_set_points(P.trail, P.tp, P.tn);

    int cx = 6;
    for (int s = 0; s < CP_FX_SLOTS; s++) {
        bool on = s < P.nheld;
        if (!on) { lv_obj_add_flag(P.chip[s], LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(P.name[s], LV_OBJ_FLAG_HIDDEN); continue; }
        int f = P.held[s]; char b[12]; snprintf(b, sizeof b, "%d", f + 1);
        lv_obj_remove_flag(P.chip[s], LV_OBJ_FLAG_HIDDEN); lv_obj_remove_flag(P.name[s], LV_OBJ_FLAG_HIDDEN);
        set_bg(P.chip[s], CAT_COL[FX[f].cat]); lv_label_set_text(P.chipl[s], b);
        lv_obj_set_x(P.chip[s], cx); lv_label_set_text(P.name[s], FX[f].name);
        lv_obj_update_layout(P.name[s]); lv_obj_set_x(P.name[s], cx + 22);
        cx += 22 + lv_obj_get_width(P.name[s]) + 8;
    }
    if (act) lv_obj_add_flag(P.hint, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(P.hint, LV_OBJ_FLAG_HIDDEN);

    char a[32], v[16];
    if (act) {
        int f = P.held[P.nheld - 1];
        const cp_fx_def_t *d = &FX[f];
        fmt(v, sizeof v, d->px, curve(d, P.x, true)); snprintf(a, sizeof a, "%s  %s", d->px, v); lv_label_set_text(P.axx, a);
        fmt(v, sizeof v, d->py, curve(d, P.y, false)); snprintf(a, sizeof a, "%s  %s", d->py, v); lv_label_set_text(P.axy, a);
        snprintf(a, sizeof a, "WET  %d%%", (int)lroundf(wet_of(f) * 100)); lv_label_set_text(P.axw, a);
        lv_obj_remove_flag(P.axw, LV_OBJ_FLAG_HIDDEN);
    } else {
        snprintf(a, sizeof a, "X  %d", (int)lroundf(P.x * 100)); lv_label_set_text(P.axx, a);
        snprintf(a, sizeof a, "Y  %d", (int)lroundf(P.y * 100)); lv_label_set_text(P.axy, a);
        lv_obj_add_flag(P.axw, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_align(P.axx, LV_ALIGN_BOTTOM_RIGHT, -6, -4);
}

static void on_xy(lv_event_t *e) {
    LOCKED;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) return;
    lv_point_t pt; lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a; lv_obj_get_coords(P.xy, &a);
    float x = (float)(pt.x - a.x1) / XY_W, y = 1.f - (float)(pt.y - a.y1) / XY_H;
    P.x = x < 0 ? 0 : x > 1 ? 1 : x; P.y = y < 0 ? 0 : y > 1 ? 1 : y;
    if (code == LV_EVENT_PRESSED) P.tn = 0;
    lv_point_precise_t q;
    q.x = (lv_value_precise_t)(P.x * XY_W); q.y = (lv_value_precise_t)((1 - P.y) * XY_H);
    if (P.tn < TRAIL_N) P.tp[P.tn++] = q; else { memmove(P.tp, P.tp + 1, sizeof(P.tp[0]) * (TRAIL_N - 1)); P.tp[TRAIL_N - 1] = q; }
    draw_pad(); emit();
}

static void build_pad(lv_obj_t *pg) {
    P.xy = box(pg, 0, 0, XY_W, XY_H, 6, C_WELL, LV_OPA_COVER);
    lv_obj_set_style_border_opa(P.xy, LV_OPA_COVER, 0); lv_obj_set_style_clip_corner(P.xy, true, 0);
    for (int i = 1; i < 8; i++) box(P.xy, i * XY_W / 8, 0, 1, XY_H, 0, C_LINE, LV_OPA_60);
    for (int i = 1; i < 4; i++) box(P.xy, 0, i * XY_H / 4, XY_W, 1, 0, C_LINE, LV_OPA_60);
    P.trail = lv_line_create(P.xy);
    lv_obj_set_style_line_width(P.trail, 3, 0); lv_obj_set_style_line_rounded(P.trail, true, 0); lv_obj_set_style_line_opa(P.trail, LV_OPA_50, 0);
    P.vline = box(P.xy, 0, 0, 1, XY_H, 0, C_DIM, LV_OPA_50);
    P.hline = box(P.xy, 0, 0, XY_W, 1, 0, C_DIM, LV_OPA_50);
    P.halo = box(P.xy, 0, 0, 32, 32, LV_RADIUS_CIRCLE, C_DIM, LV_OPA_20);
    P.dot = box(P.xy, 0, 0, 12, 12, LV_RADIUS_CIRCLE, C_DIM, LV_OPA_TRANSP);
    lv_obj_set_style_border_width(P.dot, 2, 0);
    for (int s = 0; s < CP_FX_SLOTS; s++) {
        P.chip[s] = box(P.xy, 6, 5, 18, 16, 3, C_LIME, LV_OPA_COVER);
        P.chipl[s] = txt(P.chip[s], F10, C_INK, 0, 0, "1"); lv_obj_center(P.chipl[s]);
        P.name[s] = txt(P.xy, F14, C_TEXT, 28, 4, "");
    }
    P.hint = box(P.xy, 6, 5, 160, 16, 0, 0, LV_OPA_TRANSP);
    lv_obj_t *dry = box(P.hint, 0, 0, 30, 16, 3, C_LINE, LV_OPA_COVER);
    lv_obj_t *dl = txt(dry, F10, C_DIM, 0, 0, "DRY"); lv_obj_center(dl);
    txt(P.hint, F10, C_DIM, 36, 2, "HOLD A PAD");
    P.axx = txt(P.xy, F10, C_TEXT, 0, 0, "");
    P.axy = txt(P.xy, F10, C_TEXT, 6, XY_H - 16, "");
    P.axw = txt(P.xy, F10, C_TEXT, 0, 6, ""); lv_obj_align(P.axw, LV_ALIGN_TOP_RIGHT, -6, 6);
    lv_obj_add_flag(P.xy, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(P.xy, LV_OBJ_FLAG_GESTURE_BUBBLE);
    const lv_event_code_t ev[] = { LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST };
    for (int i = 0; i < 4; i++) lv_obj_add_event_cb(P.xy, on_xy, ev[i], NULL);
}

static bool held_of(int idx);

/* ---------- FX view ---------- */
static void draw_fx(void) {
    for (int i = 0; i < CP_FX_COUNT; i++) {
        bool on = i == P.active, held = held_of(i);
        set_bg(P.tile[i], held ? CAT_COL[FX[i].cat] : on ? C_LIME : C_PANEL);
        set_fg(P.tname[i], (on || held) ? C_INK : C_TEXT); set_fg(P.ttag[i], (on || held) ? C_INK : C_DIM);
    }
}
static void on_tile(lv_event_t *e) { LOCKED; P.active = ud_of(e); draw_fx(); }
static void build_fx(lv_obj_t *pg) {
    const int W = 75, H = 27, G = 4;
    for (int i = 0; i < CP_FX_COUNT; i++) {
        int col = i % 4, row = 3 - i / 4;
        lv_obj_t *t = box(pg, col * (W + G), row * (H + G), W, H, 5, C_PANEL, LV_OPA_COVER);
        box(t, 0, 0, 3, H, 0, CAT_COL[FX[i].cat], LV_OPA_COVER);
        P.tname[i] = txt(t, F10, C_TEXT, 7, 2, FX[i].name);
        char b[16]; snprintf(b, sizeof b, "%d \xC2\xB7 %s", i + 1, CAT_TAG[FX[i].cat]);
        P.ttag[i] = txt(t, F10, C_DIM, 7, 14, b);
        lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(t, on_tile, LV_EVENT_SHORT_CLICKED, ud(i));
        P.tile[i] = t;
    }
}

/* ---------- EDIT view ---------- */
static void draw_edit(void) {
    cp_fx_def_t *d = &FX[P.active]; char b[48], lo[12], hi[12];
    set_bg(P.echip, CAT_COL[d->cat]);
    snprintf(b, sizeof b, "%d", P.active + 1); lv_label_set_text(lv_obj_get_child(P.echip, 0), b);
    lv_label_set_text(P.ehead, d->name);
    for (int r = 0; r < 4; r++) {
        bool s = r == P.esel;
        set_bg(P.erow[r], s ? C_LIME : C_PANEL);
        for (uint32_t k = 0; k < lv_obj_get_child_count(P.erow[r]); k++) {
            lv_obj_t *ch = lv_obj_get_child(P.erow[r], k);
            if (lv_obj_check_type(ch, &lv_label_class)) set_fg(ch, s ? C_INK : C_TEXT);
        }
    }
    for (int a = 0; a < 2; a++) {
        float l = a ? d->y_lo : d->x_lo, h = a ? d->y_hi : d->x_hi;
        const char *p = a ? d->py : d->px;
        fmt(lo, sizeof lo, p, l); fmt(hi, sizeof hi, p, h);
        snprintf(b, sizeof b, "%s  %s\xE2\x80\x93%s", p, lo, hi); lv_label_set_text(P.eval[a], b);
        bool s = a == P.esel;
        set_bg(P.ebar[a], s ? C_INK : C_WELL); lv_obj_set_style_bg_opa(P.ebar[a], s ? LV_OPA_20 : LV_OPA_COVER, 0);
        lv_obj_set_x(P.erng[a], (int)(l * 120)); lv_obj_set_width(P.erng[a], (int)((h - l) * 120) + 1);
        set_bg(P.erng[a], s ? C_INK : CAT_COL[d->cat]);
    }
    if (P.fullwet) snprintf(b, sizeof b, "100%%");
    else snprintf(b, sizeof b, "%d%% (pad)", (int)lroundf(wet_of(P.active) * 100));
    lv_label_set_text(P.eval[2], b);
    lv_label_set_text(P.eval[3], d->release == CP_FX_REL_TAIL ? "TAIL" : "SNAP");
    lv_obj_align(P.eval[2], LV_ALIGN_RIGHT_MID, -8, 0); lv_obj_align(P.eval[3], LV_ALIGN_RIGHT_MID, -8, 0);
}
static void on_erow(lv_event_t *e) { LOCKED; P.esel = ud_of(e); draw_edit(); }
static void build_edit(lv_obj_t *pg) {
    P.echip = box(pg, 0, 1, 18, 16, 3, C_LIME, LV_OPA_COVER); lv_obj_center(txt(P.echip, F10, C_INK, 0, 0, "1"));
    P.ehead = txt(pg, F14, C_TEXT, 24, 0, "");
    static const char *L[] = { "X", "Y", "WET", "RELEASE" };
    for (int r = 0; r < 4; r++) {
        int y = r < 2 ? 22 + r * 32 : 86 + (r - 2) * 17, h = r < 2 ? 28 : 15;
        lv_obj_t *row = box(pg, 0, y, 312, h, 5, C_PANEL, LV_OPA_COVER);
        lv_obj_t *l = txt(row, F10, C_TEXT, 8, 0, L[r]); lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
        if (r < 2) {
            P.eval[r] = txt(row, F10, C_TEXT, 0, 0, ""); lv_obj_align(P.eval[r], LV_ALIGN_LEFT_MID, 24, 0);
            P.ebar[r] = box(row, 180, 11, 120, 6, 3, C_WELL, LV_OPA_COVER);
            P.erng[r] = box(P.ebar[r], 0, 0, 120, 6, 3, C_LIME, LV_OPA_COVER);
        } else P.eval[r] = txt(row, F10, C_TEXT, 0, 0, "");
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_erow, LV_EVENT_SHORT_CLICKED, ud(r));
        P.erow[r] = row;
    }
}

/* ---------- ROUTE view ---------- */
#define RH 26
#define RP 30
static void draw_route(void) {
    char b[8];
    for (int i = 0; i < CP_FX_SRC_COUNT; i++) {
        bool s = i == P.ssel, on = P.send[i] > 0;
        lv_obj_t *r = P.src[i];
        set_bg(r, s ? C_LIME : C_PANEL);
        lv_obj_set_style_opa(r, (on || s) && P.avail[i] ? LV_OPA_COVER : LV_OPA_50, 0);
        for (uint32_t k = 0; k < lv_obj_get_child_count(r); k++) { lv_obj_t *ch = lv_obj_get_child(r, k); if (lv_obj_check_type(ch, &lv_label_class)) set_fg(ch, s ? C_INK : C_TEXT); }
        set_bg(P.sdot[i], s ? C_INK : C_LIME); lv_obj_set_style_bg_opa(P.sdot[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(P.sdot[i], hx(s ? C_INK : C_DIM), 0); lv_obj_set_style_border_width(P.sdot[i], on ? 0 : 2, 0);
        set_bg(P.sbar[i], s ? C_INK : C_WELL); lv_obj_set_style_bg_opa(P.sbar[i], s ? LV_OPA_20 : LV_OPA_COVER, 0);
        lv_obj_set_width(P.sfill[i], P.send[i] * 32 / 100); set_bg(P.sfill[i], s ? C_INK : C_LIME);
        if (!P.avail[i]) snprintf(b, sizeof b, "off");
        else if (on) snprintf(b, sizeof b, "%d", P.send[i]);
        else snprintf(b, sizeof b, "\xE2\x80\x94");
        lv_label_set_text(P.sval[i], b); lv_obj_align(P.sval[i], LV_ALIGN_RIGHT_MID, -6, 0);
        if (on) lv_obj_remove_flag(P.sln[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(P.sln[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int o = 0; o < CP_FX_OUT_COUNT; o++) {
        bool on = P.outon[o];
        lv_obj_set_style_border_width(P.out[o], on ? 2 : 0, 0); lv_obj_set_style_opa(P.out[o], on ? LV_OPA_COVER : LV_OPA_60, 0);
        if (on) lv_obj_remove_flag(P.oln[o], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(P.oln[o], LV_OBJ_FLAG_HIDDEN);
    }
}
static void on_src(lv_event_t *e) {
    LOCKED;
    int i = ud_of(e);
    if (P.ssel == i) P.send[i] = P.send[i] ? 0 : 80;   /* second tap toggles */
    P.ssel = i; draw_route(); changed();
}
static void on_out(lv_event_t *e) { LOCKED; int o = ud_of(e); P.outon[o] = !P.outon[o]; draw_route(); changed(); }
static lv_obj_t *mkline(lv_obj_t *p, lv_point_precise_t *pts) {
    lv_obj_t *l = lv_line_create(p); lv_line_set_points(l, pts, 2);
    lv_obj_set_style_line_width(l, 2, 0); lv_obj_set_style_line_color(l, hx(C_LIME), 0); lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}
static void build_route(lv_obj_t *pg) {
    for (int i = 0; i < CP_FX_SRC_COUNT; i++) {
        int y = i * RP;
        P.sp[i][0].x = 186; P.sp[i][0].y = y + RH / 2; P.sp[i][1].x = 200; P.sp[i][1].y = 60;
        P.sln[i] = mkline(pg, P.sp[i]);
        lv_obj_t *r = box(pg, 0, y, 186, RH, 5, C_PANEL, LV_OPA_COVER);
        P.sdot[i] = box(r, 6, 8, 9, 9, LV_RADIUS_CIRCLE, C_LIME, LV_OPA_COVER);
        lv_obj_t *n = txt(r, F10, C_TEXT, 20, 0, SRC_NAME[i]); lv_obj_align(n, LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_t *s = txt(r, F10, C_DIM, 0, 0, SRC_SUB[i]); lv_obj_align(s, LV_ALIGN_LEFT_MID, 76, 0);
        P.sbar[i] = box(r, 126, 10, 32, 5, 2, C_WELL, LV_OPA_COVER);
        P.sfill[i] = box(P.sbar[i], 0, 0, 0, 5, 2, C_LIME, LV_OPA_COVER);
        P.sval[i] = txt(r, F10, C_TEXT, 0, 0, "");
        lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(r, on_src, LV_EVENT_SHORT_CLICKED, ud(i));
        P.src[i] = r;
    }
    for (int o = 0; o < CP_FX_OUT_COUNT; o++) {
        P.op[o][0].x = 246; P.op[o][0].y = 60; P.op[o][1].x = 256; P.op[o][1].y = OUT_Y[o] + 18;
        P.oln[o] = mkline(pg, P.op[o]);
    }
    lv_obj_t *fx = box(pg, 200, 39, 46, 42, 6, C_LIME, LV_OPA_COVER);
    lv_obj_t *f1 = txt(fx, F14, C_INK, 0, 0, "FX"); lv_obj_align(f1, LV_ALIGN_CENTER, 0, -6);
    lv_obj_t *f2 = txt(fx, F10, C_INK, 0, 0, "PAD"); lv_obj_align(f2, LV_ALIGN_CENTER, 0, 9);
    for (int o = 0; o < CP_FX_OUT_COUNT; o++) {
        lv_obj_t *b = box(pg, 256, OUT_Y[o], 56, 36, 5, C_PANEL, LV_OPA_COVER);
        lv_obj_set_style_border_color(b, hx(C_LIME), 0);
        lv_obj_t *l = txt(b, F10, C_TEXT, 0, 0, OUT_NAME[o]); lv_obj_align(l, LV_ALIGN_CENTER, 0, -6);
        lv_obj_t *s = txt(b, F10, C_DIM, 0, 0, OUT_SUB[o]); lv_obj_align(s, LV_ALIGN_CENTER, 0, 7);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, on_out, LV_EVENT_SHORT_CLICKED, ud(o));
        P.out[o] = b;
    }
}

/* ---------- Soft keys + tabs ---------- */
static bool sk_on(sk_t k) {
    cp_fx_def_t *d = &FX[P.active];
    switch (k) {
        case SK_HOLD: return P.hold; case SK_SYNC: return P.sync; case SK_WET: return P.fullwet; case SK_LATCH: return P.latch;
        case SK_LIN: return !d->exp_curve; case SK_EXP: return d->exp_curve;
        case SK_SNAP: return d->release == CP_FX_REL_SNAP; case SK_TAIL: return d->release == CP_FX_REL_TAIL;
        case SK_DRY: return P.monitor; case SK_PRE: return P.tap == CP_FX_TAP_PRE; case SK_POST: return P.tap == CP_FX_TAP_POST; case SK_MONO: return P.mono;
        default: return false;
    }
}
static void draw_sk(void) {
    for (int k = 0; k < SK_COUNT; k++) {
        lv_obj_t *b = P.sk[k];
        bool on = sk_on((sk_t)k);
        set_bg(b, k == SK_KILL ? C_RED : on ? C_LIME : C_PANEL);
        set_fg(lv_obj_get_child(b, 0), (k == SK_KILL || on) ? C_INK : C_TEXT);
    }
}
static void release_all(void) { P.nheld = 0; emit(); }
static void redraw(void) {
    draw_sk(); draw_pad();
    if (P.tabsel == CP_FX_TAB_FX) draw_fx();
    if (P.tabsel == CP_FX_TAB_EDIT) draw_edit();
    if (P.tabsel == CP_FX_TAB_ROUTE) draw_route();
}
static void on_sk(lv_event_t *e) {
    LOCKED;
    sk_t k = (sk_t)ud_of(e); cp_fx_def_t *d = &FX[P.active];
    switch (k) {
        case SK_HOLD: P.hold = !P.hold; if (!P.hold) release_all(); break;
        case SK_SYNC: P.sync = !P.sync; changed(); break;
        case SK_WET: P.fullwet = !P.fullwet; emit(); changed(); break;
        case SK_TAP: if (P.acb) P.acb(CP_FX_ACT_TAP, P.auser); break;
        case SK_KILL: release_all(); if (P.acb) P.acb(CP_FX_ACT_KILL, P.auser); break;
        case SK_LATCH: P.latch = !P.latch; break;
        case SK_CLEAR: release_all(); break;
        case SK_EXIT: if (P.acb) P.acb(CP_FX_ACT_EXIT, P.auser); break;
        case SK_SWAP: { const char *t = d->px; d->px = d->py; d->py = t; float a = d->x_lo, b = d->x_hi; d->x_lo = d->y_lo; d->x_hi = d->y_hi; d->y_lo = a; d->y_hi = b; emit(); changed(); } break;
        case SK_LIN: d->exp_curve = false; emit(); changed(); break;
        case SK_EXP: d->exp_curve = true; emit(); changed(); break;
        case SK_SNAP: d->release = CP_FX_REL_SNAP; changed(); break;
        case SK_TAIL: d->release = CP_FX_REL_TAIL; changed(); break;
        case SK_DRY: P.monitor = !P.monitor; changed(); break;
        case SK_PRE: P.tap = CP_FX_TAP_PRE; changed(); break;
        case SK_POST: P.tap = CP_FX_TAP_POST; changed(); break;
        case SK_MONO: P.mono = !P.mono; changed(); break;
        case SK_RCLR: memset(P.send, 0, sizeof P.send); changed(); break;
        default: break;
    }
    redraw();
}

static void set_tab(cp_fx_tab_t t) {
    P.tabsel = t;
    for (int i = 0; i < 4; i++) {
        bool s = i == (int)t;
        set_bg(P.tab[i], s ? C_LIME : C_PANEL); set_fg(lv_obj_get_child(P.tab[i], 0), s ? C_INK : C_DIM);
        if (s) lv_obj_remove_flag(P.page[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(P.page[i], LV_OBJ_FLAG_HIDDEN);
        for (int k = 0; k < SK_N[i]; k++) {
            if (s) lv_obj_remove_flag(P.sk[SK_TAB[i][k]], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(P.sk[SK_TAB[i][k]], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (t == CP_FX_TAB_FX) draw_fx(); else if (t == CP_FX_TAB_EDIT) draw_edit(); else if (t == CP_FX_TAB_ROUTE) draw_route(); else draw_pad();
    draw_sk();
}
void cp_fxpad_set_tab(cp_fx_tab_t t) { LOCKED; set_tab(t); }
static void on_tab(lv_event_t *e) { LOCKED; set_tab((cp_fx_tab_t)ud_of(e)); }

lv_obj_t *cp_fxpad_create(lv_obj_t *parent) {
    LOCKED;
    /* What the app restored before creating the screen stays: routes, sends,
     * the effects' ranges, SYNC, THRU, MONO. Everything else starts fresh. */
    P.root = nullptr; P.nheld = 0; P.tabsel = CP_FX_TAB_PAD; P.active = 0; P.esel = 0; P.ssel = 0; P.tn = 0;
    P.x = .5f; P.y = .5f; P.hold = false; P.latch = false; P.dirty = false;
    for (int i = 0; i < CP_FX_COUNT; i++) P.vel[i] = 76;   /* 60 % until the pad reports pressure */
    P.root = box(parent, 0, 0, 320, 200, 0, C_BG, LV_OPA_COVER);
    for (int t = 0; t < 4; t++) {
        lv_obj_t *b = box(P.root, 4 + t * 79, 4, 75, 32, 5, C_PANEL, LV_OPA_COVER);
        lv_obj_t *l = txt(b, F14, C_DIM, 0, 0, TAB_NAME[t]); lv_obj_center(l);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, on_tab, LV_EVENT_SHORT_CLICKED, ud(t));
        P.tab[t] = b;
        P.page[t] = box(P.root, XY_X, XY_Y, XY_W, XY_H, 0, 0, LV_OPA_TRANSP);
        const int n = SK_N[t], gap = 4, w = (312 - (n - 1) * gap) / n;
        for (int i = 0; i < n; i++) {
            const sk_t k = SK_TAB[t][i];
            lv_obj_t *kb = box(P.root, 4 + i * (w + gap), 164, w, 32, 5, C_PANEL, LV_OPA_COVER);
            lv_obj_t *kl = txt(kb, F10, C_TEXT, 0, 0, SK_NAME[k]); lv_obj_center(kl);
            lv_obj_add_flag(kb, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(kb, on_sk, LV_EVENT_SHORT_CLICKED, ud(k));
            P.sk[k] = kb;
        }
    }
    build_pad(P.page[0]); build_fx(P.page[1]); build_edit(P.page[2]); build_route(P.page[3]);
    set_tab(CP_FX_TAB_PAD);
    return P.root;
}


/* ---------- Hardware ---------- */
void cp_fxpad_pad(int idx, bool down, uint8_t velocity) {
    if (idx < 0 || idx >= CP_FX_COUNT) return;
    LOCKED;
    int at = -1; for (int s = 0; s < P.nheld; s++) if (P.held[s] == idx) at = s;
    if (down) {
        P.vel[idx] = velocity ? velocity : P.vel[idx];
        if (at < 0) {
            if (!P.latch || P.nheld >= CP_FX_SLOTS) { /* without LATCH one effect; with it two, oldest drops */
                if (!P.latch) P.nheld = 0; else { memmove(P.held, P.held + 1, sizeof(int) * (CP_FX_SLOTS - 1)); P.nheld--; }
            }
            P.held[P.nheld++] = idx; P.active = idx;
        }
    } else if (at >= 0 && !P.hold) {
        memmove(P.held + at, P.held + at + 1, sizeof(int) * (P.nheld - at - 1)); P.nheld--;
    }
    P.dirty = true;
    emit();
}

bool cp_fxpad_tick(void) {
    LOCKED;
    if (!P.dirty || !P.root) return false;
    P.dirty = false;
    if (P.tabsel == CP_FX_TAB_PAD) draw_pad(); else if (P.tabsel == CP_FX_TAB_FX) draw_fx(); else if (P.tabsel == CP_FX_TAB_EDIT) draw_edit();
    return true;
}
static bool held_of(int idx) { for (int s = 0; s < P.nheld; s++) if (P.held[s] == idx) return true; return false; }
bool cp_fxpad_pad_held(int idx) { LOCKED; return held_of(idx); }
uint32_t cp_fxpad_pad_color(int idx) { return (idx >= 0 && idx < CP_FX_COUNT) ? CAT_COL[FX[idx].cat] : 0; }

void cp_fxpad_encoder(int delta) {
    LOCKED;
    if (P.tabsel == CP_FX_TAB_EDIT) { P.esel = ((P.esel + delta) % 4 + 4) % 4; draw_edit(); }
    else if (P.tabsel == CP_FX_TAB_ROUTE) { int v = P.send[P.ssel] + delta * 5; P.send[P.ssel] = v < 0 ? 0 : v > 100 ? 100 : v; draw_route(); changed(); }
    else if (P.tabsel == CP_FX_TAB_FX) { P.active = ((P.active + delta) % 16 + 16) % 16; draw_fx(); }
}
void cp_fxpad_encoder_click(void) {
    LOCKED;
    if (P.tabsel == CP_FX_TAB_PAD) set_tab(CP_FX_TAB_FX);
    else if (P.tabsel == CP_FX_TAB_FX) set_tab(CP_FX_TAB_EDIT);
    else if (P.tabsel == CP_FX_TAB_EDIT) set_tab(CP_FX_TAB_PAD);
    else { P.send[P.ssel] = P.send[P.ssel] ? 0 : 80; draw_route(); changed(); }
}

void cp_fxpad_forget(void) { LOCKED; P.root = nullptr; }
void cp_fxpad_set_param_cb(cp_fxpad_param_cb_t cb, void *user) { P.cb = cb; P.user = user; }
void cp_fxpad_set_action_cb(cp_fxpad_action_cb_t cb, void *user) { P.acb = cb; P.auser = user; }
void cp_fxpad_set_changed_cb(cp_fxpad_changed_cb_t cb, void *user) { P.ccb = cb; P.cuser = user; }
cp_fx_def_t *cp_fxpad_def(int idx) { return (idx >= 0 && idx < CP_FX_COUNT) ? &FX[idx] : NULL; }
uint8_t cp_fxpad_send(cp_fx_src_t s) { LOCKED; return s < CP_FX_SRC_COUNT ? P.send[s] : 0; }
void cp_fxpad_set_send(cp_fx_src_t s, uint8_t v) { LOCKED; if (s < CP_FX_SRC_COUNT) { P.send[s] = v > 100 ? 100 : v; if (P.root && P.tabsel == CP_FX_TAB_ROUTE) draw_route(); } }
void cp_fxpad_set_available(cp_fx_src_t s, bool a) {
    LOCKED;
    if (s >= CP_FX_SRC_COUNT || P.avail[s] == a) return;
    P.avail[s] = a;
    if (P.root && P.tabsel == CP_FX_TAB_ROUTE) draw_route();
}
bool cp_fxpad_out(cp_fx_out_t o) { LOCKED; return o < CP_FX_OUT_COUNT && P.outon[o]; }
void cp_fxpad_set_out(cp_fx_out_t o, bool on) { LOCKED; if (o < CP_FX_OUT_COUNT) P.outon[o] = on; }
cp_fx_tap_t cp_fxpad_tap(void) { LOCKED; return P.tap; }
void cp_fxpad_set_tap(cp_fx_tap_t t) { LOCKED; P.tap = t; }
bool cp_fxpad_dry_thru(void) { LOCKED; return P.monitor; }
void cp_fxpad_set_dry_thru(bool on) { LOCKED; P.monitor = on; }
bool cp_fxpad_mono(void) { LOCKED; return P.mono; }
void cp_fxpad_set_mono(bool on) { LOCKED; P.mono = on; }
bool cp_fxpad_sync(void) { LOCKED; return P.sync; }
void cp_fxpad_set_sync(bool on) { LOCKED; P.sync = on; }
bool cp_fxpad_full_wet(void) { LOCKED; return P.fullwet; }
