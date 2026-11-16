// SPDX-License-Identifier: GPL-3.0-or-later
#include "FxDef.hpp"

/* In pad order, as fxpad::Fx has them. TAIL only on the effects that add their
 * wet signal to the dry (FxEngine::ringsOut): the others have nothing to ring out. */
static cp_fx_def_t FX[CP_FX_COUNT] = {
    {"ECHO",CP_FX_CAT_DLY,"TIME","FDBK",0,1,0,.95f,false,CP_FX_REL_TAIL,false},
    {"TAPE ECHO",CP_FX_CAT_DLY,"TIME","WOW",0,1,0,1,false,CP_FX_REL_TAIL,false},
    {"PING PONG",CP_FX_CAT_DLY,"TIME","FDBK",0,1,0,.95f,false,CP_FX_REL_TAIL,false},
    {"ROLL",CP_FX_CAT_GLT,"RATE","DECAY",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"HALL",CP_FX_CAT_REV,"SIZE","DAMP",0,1,0,1,false,CP_FX_REL_TAIL,false},
    {"SHIMMER",CP_FX_CAT_REV,"SIZE","PITCH",0,1,0,1,false,CP_FX_REL_TAIL,false},
    {"FREEZE",CP_FX_CAT_REV,"TONE","LEVEL",0,1,0,1,false,CP_FX_REL_TAIL,false},
    {"GATE",CP_FX_CAT_GLT,"RATE","DEPTH",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"LP FILTER",CP_FX_CAT_FLT,"CUTOFF","RES",0,1,0,.9f,true,CP_FX_REL_SNAP,false},
    {"HP FILTER",CP_FX_CAT_FLT,"CUTOFF","RES",0,1,0,.9f,true,CP_FX_REL_SNAP,false},
    {"BAND SWEEP",CP_FX_CAT_FLT,"FREQ","WIDTH",0,1,0,1,true,CP_FX_REL_SNAP,false},
    {"WAH",CP_FX_CAT_FLT,"FREQ","RES",0,1,0,.9f,true,CP_FX_REL_SNAP,false},
    {"FLANGER",CP_FX_CAT_MOD,"SPEED","DEPTH",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"PHASER",CP_FX_CAT_MOD,"SPEED","FDBK",0,1,0,.9f,false,CP_FX_REL_SNAP,false},
    {"BITCRUSH",CP_FX_CAT_DST,"BITS","RATE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"DRIVE",CP_FX_CAT_DST,"GAIN","TONE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    /* Bank B: the ones that cut the sound up, all on the last seconds of the input. */
    {"STUTTER",CP_FX_CAT_GLT,"SLICE","GATE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"BEAT RPT",CP_FX_CAT_GLT,"LEN","STEP",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"GLITCH",CP_FX_CAT_GLT,"SLICE","CHAOS",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"SLICER",CP_FX_CAT_GLT,"PATTERN","DEPTH",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"REVERSE",CP_FX_CAT_GLT,"LEN","TONE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"TAPE STOP",CP_FX_CAT_GLT,"TIME","CURVE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"SPINBACK",CP_FX_CAT_GLT,"TIME","BACK",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"SCRATCH",CP_FX_CAT_GLT,"POSITION","LEN",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"GRAIN",CP_FX_CAT_MOD,"SIZE","DENSITY",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"PITCH",CP_FX_CAT_MOD,"SHIFT","FDBK",0,1,0,.9f,false,CP_FX_REL_SNAP,false},
    {"AUTO FLT",CP_FX_CAT_FLT,"RATE","RES",0,1,0,.9f,false,CP_FX_REL_SNAP,false},
    {"PUMP",CP_FX_CAT_GLT,"RATE","DEPTH",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"LOFI",CP_FX_CAT_DST,"AGE","WOBBLE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"RING MOD",CP_FX_CAT_DST,"FREQ","SHAPE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"ECHO FRZ",CP_FX_CAT_DLY,"LEN","FADE",0,1,0,1,false,CP_FX_REL_SNAP,false},
    {"PAN CHOP",CP_FX_CAT_GLT,"SLICE","WIDTH",0,1,0,1,false,CP_FX_REL_SNAP,false},
};

cp_fx_def_t *cp_fx_table(void) { return FX; }
