#ifndef BLUEBOX_MASTER_DYN_PARAMS_H
#define BLUEBOX_MASTER_DYN_PARAMS_H
/* Master dynamics (clipper, compressor models, limiter). Desktop 1.5.2
 * parameter ids 0x1c0..0x1c7: unused in stock and in Cobalt 1.2.0 (checked by
 * tests/test_master_dyn.py), and clear of the Phrase Looper fork's 0x1ba..0x1bd. */
#define MD_P_MODEL 0x1c0u    /* Comp Model: Stock, Glue, Punch, Opto, Squash */
#define MD_P_CLIPPER 0x1c1u  /* Clipper: Off, Clean, Diode */
#define MD_P_CLIP 0x1c2u     /* Clip: push into the clipper, 0..1000 = 0..+18 dB */
#define MD_P_SHAPE 0x1c3u    /* Clip Shape: 0 soft .. 1000 hard */
#define MD_P_LIMIT 0x1c4u    /* Limiter: Off, On */
#define MD_P_LDRIVE 0x1c5u   /* Lim Drive: 0..18000 milli-dB */
#define MD_P_LCEIL 0x1c6u    /* Lim Ceil: -12000..0 milli-dB */
#define MD_P_FLOW 0x1c7u     /* Flow: order of clipper, compressor and limiter */
#define MD_P_FIRST MD_P_MODEL
#define MD_P_LAST MD_P_FLOW
/* Stock compressor rows the models keep using. */
#define MD_P_COMP 0xb5u       /* Compressor on / off */
#define MD_P_THRESH 0x132u   /* Thresh, milli-dB, stock default -20000 */
#define MD_P_WET 0x13au      /* Wet/Dry, 0..1000 */
#define MD_P_GAIN 0x13bu     /* Gain, milli-dB */
#define MD_DEF_CLIP 0
#define MD_DEF_SHAPE 800
#define MD_DEF_LDRIVE 0
#define MD_DEF_LCEIL (-300)
#endif
