/* Peak readout on the track headers: the audio core's own meter levels, held,
 * as numbers. The M7 (gate_m7.c) keeps the hold and publishes one byte per
 * channel; the M4 (cue_m4.c) only reads and draws.
 * Published at backup SRAM 0x38800648..0x3880065b: the 24 bytes between the
 * gate bank (ends 0x648) and the cue state (0x660), a line only the M7 writes. */
#ifndef BLUEBOX_PEAK_SHARED_H
#define BLUEBOX_PEAK_SHARED_H
#include <stdint.h>
#define PK_CHANNELS 13u          /* tracks 0..11, master 12: the native meter channel numbers */
#define PK_NONE (-128)           /* nothing to show: below the meters' -60 dB floor */
#define PK_CLIP 1                /* 0 dB or over. Otherwise half-dB steps, -119..0 */
/* status: what is alive on the audio core, shown as one digit after the CPU
 * numbers on TOOLS. 1 the boot-time block exists and meter levels arrive
 * (gate_m7.c); 2 the master compressor found the dynamics state (gr_m7.c);
 * 4 a clipper, model or limiter is switched on. So 3 = ready, 7 = working. */
#define PK_ST_BLOCK 1u
#define PK_ST_MASTER 2u
#define PK_ST_ACTIVE 4u
struct pk_report {uint32_t magic;int8_t v[PK_CHANNELS];uint8_t status,pad[2];};
#define SCB_DCCMVAC_PK (*(volatile uint32_t *)0xe000ef68u)
static inline void pk_status(unsigned set,unsigned clear);
#define PK_REPORT ((volatile struct pk_report *)0x38800648u)
#define PK_MAGIC 0x504b4431u
_Static_assert(0x648+sizeof(struct pk_report)<=0x660,"peak report below the cue state at 0x660");
/* M7 only: the hold, in the SDRAM block behind the master dynamics state. */
struct pk_hold {uint32_t magic;int8_t hold[PK_CHANNELS];uint8_t pad[3];uint16_t age[PK_CHANNELS];};
/* The mixer publishes each channel once per 32-sample block (1500 a second):
 * a peak stays for 1.5 s unless a higher one replaces it. */
#define PK_HOLD_CALLS 2250u
/* Level in dB -> the published byte. */
static inline int pk_code(float db) {
    if(!(db>-60.f))return PK_NONE;           /* also NaN */
    if(db>=0.f)return PK_CLIP;
    return -(int)(-db*2.f+.5f);
}
/* One reading into a channel's hold. Returns the value to show. */
static inline int pk_step(struct pk_hold *h,unsigned ch,int v) {
    if(v>=h->hold[ch]){h->hold[ch]=(int8_t)v;h->age[ch]=0;}
    else if(++h->age[ch]>=PK_HOLD_CALLS){h->hold[ch]=(int8_t)v;h->age[ch]=0;}
    return h->hold[ch];
}
/* M7 only. */
static inline void pk_status(unsigned set,unsigned clear) {
    volatile struct pk_report *r=(volatile struct pk_report *)0x38800648u;
    unsigned now=(r->status&~clear)|set;
    if(now!=r->status) {
        r->status=(uint8_t)now;
        __asm volatile("dsb" ::: "memory");
        SCB_DCCMVAC_PK=(uint32_t)r;
        __asm volatile("dsb\nisb" ::: "memory");
    }
}
#endif
