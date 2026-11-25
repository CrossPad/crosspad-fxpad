// SPDX-License-Identifier: GPL-3.0-or-later
//
// The FX Pad's effects as the screen has them: names, the parameter on each
// axis of the XY surface, its range and curve, how the effect stops. No LVGL
// here, so the host tests read the same table and the same mapping from the
// finger to the engine as the screen does.
#pragma once

#define CP_FX_COUNT 32   /* two banks */
#define CP_FX_BANK 16    /* effects a bank, a pad each */
#define CP_FX_SLOTS 2

enum cp_fx_cat_t { CP_FX_CAT_DLY, CP_FX_CAT_REV, CP_FX_CAT_FLT, CP_FX_CAT_MOD, CP_FX_CAT_GLT, CP_FX_CAT_DST };
enum cp_fx_release_t { CP_FX_REL_TAIL, CP_FX_REL_SNAP };

struct cp_fx_def_t {
    const char *name;
    cp_fx_cat_t cat;
    const char *px, *py;     /* parameter names on X and Y, as the surface has them now */
    float x_lo, x_hi;        /* usable range of X, 0..1 of the full parameter */
    float y_lo, y_hi;
    bool exp_curve;          /* the effect's first parameter is squared, whichever axis it is on */
    cp_fx_release_t release; /* TAIL only where the effect can ring out (FxEngine::ringsOut) */
    bool swap;               /* SWAP X/Y: the surface's X is the effect's second parameter */
};

/* The CP_FX_COUNT effects in pad order, as fxpad::Fx has them. */
cp_fx_def_t *cp_fx_table(void);

/* A finger position 0..1 on one axis of the surface, as its parameter's value. */
static inline float cp_fx_curve(const cp_fx_def_t *d, float v, bool isx) {
    const float lo = isx ? d->x_lo : d->y_lo, hi = isx ? d->x_hi : d->y_hi;
    if (d->exp_curve && isx != d->swap) v = v * v;
    return lo + (hi - lo) * v;
}
/* The finger at x, y as the effect's two parameters in the engine's order
 * (FxEngine::setSlot): swapped, the surface's Y is the first one. */
static inline void cp_fx_map(const cp_fx_def_t *d, float x, float y, float *p1, float *p2) {
    const float a = cp_fx_curve(d, x, true), b = cp_fx_curve(d, y, false);
    *p1 = d->swap ? b : a;
    *p2 = d->swap ? a : b;
}
/* SWAP X/Y: the two parameters change axes, each with its name and range. */
static inline void cp_fx_swap(cp_fx_def_t *d) {
    const char *t = d->px; d->px = d->py; d->py = t;
    const float lo = d->x_lo, hi = d->x_hi;
    d->x_lo = d->y_lo; d->x_hi = d->y_hi; d->y_lo = lo; d->y_hi = hi;
    d->swap = !d->swap;
}
