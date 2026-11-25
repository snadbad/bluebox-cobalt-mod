/* Master dynamics for the bluebox master bus: clipper, compressor models and
 * limiter. No firmware calls in this file: tests/master_dyn_host.c builds the
 * same code for the host with sanitizers.
 *
 * Ported from JOEBOX / blackbox-mod by Justin Joe
 * (https://github.com/j3threejay/blackbox-mod, src/clip.c and src/compm.c),
 * used with the author's permission and under its MIT licence:
 *
 *   MIT License. Copyright (c) 2026 Justin Joe
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to permit
 *   persons to whom the Software is furnished to do so, subject to the
 *   following conditions: The above copyright notice and this permission
 *   notice shall be included in all copies or substantial portions of the
 *   Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 *   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * What differs from JOEBOX:
 *  - Three separate stages whose order is a setting (Flow). On JOEBOX the
 *    limiter is one of the compressor models and the clipper is always last.
 *  - The limiter is JOEBOX's Limit model with its fixed +6 dB push and
 *    -0.3 dBFS ceiling turned into two settings (Lim Drive, Lim Ceil).
 *  - Glue, Punch, Opto and Squash are JOEBOX's presets. Their threshold moves
 *    with the stock Thresh row (the stock default, -20 dB, is the JOEBOX
 *    voicing), and the stock Gain and Wet/Dry rows keep working. They do not
 *    end in a hard clamp at -0.3 dBFS: that is what the clipper and limiter
 *    stages are for.
 *  - The clipper's DSP is JOEBOX's clip.c; the only addition is that a NaN or
 *    runaway input sample is kept out of its state (md_sane).
 */
#ifndef BLUEBOX_MASTER_DYN_H
#define BLUEBOX_MASTER_DYN_H
#include <stdint.h>
#include "master_dyn_params.h"

enum {MD_CLIP_OFF,MD_CLIP_CLEAN,MD_CLIP_DIODE,MD_CLIP_TYPES};
enum {MD_STOCK,MD_GLUE,MD_PUNCH,MD_OPTO,MD_SQUASH,MD_MODELS};
enum {MD_FLOW_CLIP_COMP_LIM,MD_FLOW_COMP_CLIP_LIM,MD_FLOW_COMP_LIM_CLIP,MD_FLOWS};
#define MD_BLOCK 32u
#define MD_LA 72u                 /* limiter look-ahead: 1.5 ms at 48 kHz */
#define MD_CLIP_CEILING 0.9661f   /* -0.3 dBFS, as JOEBOX */
#define MD_CLIP_LATENCY 5u
#define MD_MAGIC 0x4d44594eu      /* "MDYN" */

struct md_clip {
    uint32_t on,pos;
    float gain;                  /* the push, smoothed across blocks */
    float in[2][8];              /* the last eight pushed samples, per channel */
    float cut[2][16];            /* what the curve took off, at 96 kHz */
    float dc_in[2],dc_out[2];    /* Diode: blocker for the offset an uneven clip leaves */
};
struct md_comp {
    uint32_t model;              /* model the state belongs to; 0 = start over */
    float gr,ms;                 /* gain reduction in dB (<= 0), RMS detector mean square */
    float out,wet;               /* Gain and Wet/Dry rows, smoothed across blocks */
};
struct md_lim {
    uint32_t on,pos;
    float gr,pre;                /* gain reduction in dB (<= 0), the drive smoothed across blocks */
    float la[MD_LA][2];
};
struct master_dyn {
    uint32_t magic;
    int32_t model,clipper,clip,shape,limit,ldrive,lceil,flow;   /* our settings */
    int32_t thresh,gain,wet;                                     /* stock compressor rows we follow */
    struct md_clip c;
    struct md_comp k;
    struct md_lim l;
    uint32_t dry_n;              /* frames held in dry[] for this block */
    uint32_t comp_event;         /* this block carried a Compressor on / off event */
    float dry[2][MD_BLOCK];      /* the bus before the stock compressor, while a model replaces it */
};

static inline void md_clip_reset(struct md_clip *s) {s->on=0;}
static inline void md_comp_reset(struct md_comp *s) {s->model=0;}
static inline void md_lim_reset(struct md_lim *s) {s->on=0;}
static inline void md_reset_dsp(struct master_dyn *d) {
    md_clip_reset(&d->c);md_comp_reset(&d->k);md_lim_reset(&d->l);
}
static inline void md_init(struct master_dyn *d) {
    d->model=MD_STOCK;d->clipper=MD_CLIP_OFF;d->clip=MD_DEF_CLIP;d->shape=MD_DEF_SHAPE;
    d->limit=0;d->ldrive=MD_DEF_LDRIVE;d->lceil=MD_DEF_LCEIL;d->flow=MD_FLOW_CLIP_COMP_LIM;
    d->thresh=-20000;d->gain=0;d->wet=1000;d->dry_n=0;d->comp_event=0;
    md_reset_dsp(d);
    d->magic=MD_MAGIC;
}
static inline int32_t md_range(int32_t v,int32_t lo,int32_t hi) {return v<lo?lo:v>hi?hi:v;}
/* One parameter event from the master queue. Out-of-range values are clamped;
 * an unknown list entry falls back to the stage being off / stock. */
static inline void md_event(struct master_dyn *d,unsigned id,int32_t v) {
    switch(id) {
    case MD_P_MODEL:d->model=v>=0 && v<MD_MODELS?v:MD_STOCK;break;
    case MD_P_CLIPPER:d->clipper=v>=0 && v<MD_CLIP_TYPES?v:MD_CLIP_OFF;break;
    case MD_P_CLIP:d->clip=md_range(v,0,1000);break;
    case MD_P_SHAPE:d->shape=md_range(v,0,1000);break;
    case MD_P_LIMIT:d->limit=v==1;break;
    case MD_P_LDRIVE:d->ldrive=md_range(v,0,18000);break;
    case MD_P_LCEIL:d->lceil=md_range(v,-12000,0);break;
    case MD_P_FLOW:d->flow=v>=0 && v<MD_FLOWS?v:MD_FLOW_CLIP_COMP_LIM;break;
    case MD_P_THRESH:d->thresh=md_range(v,-48000,0);break;
    case MD_P_GAIN:d->gain=md_range(v,-36000,36000);break;
    case MD_P_WET:d->wet=md_range(v,0,1000);break;
    case MD_P_COMP:d->comp_event=1;break;
    default:break;
    }
}
static inline int md_wanted(const struct master_dyn *d) {
    return d->clipper!=MD_CLIP_OFF || d->limit || d->model!=MD_STOCK;
}

/* log2 / exp2, bit tricks with quadratic correction (error ~0.005 / ~0.3 %), as JOEBOX compm.c */
static inline __attribute__((always_inline)) float md_log2(float x) {
    union {float f;uint32_t u;} v={x};
    float e=(float)((int)((v.u>>23)&0xff)-128);
    v.u=(v.u&0x007fffffu)|0x3f800000u;
    float m=v.f;
    return e+(-0.34484843f*m+2.02466578f)*m-0.67487759f;
}
static inline __attribute__((always_inline)) float md_exp2(float x) {
    if(x<-30.0f)return 0.0f;
    if(x>30.0f)x=30.0f;
    int i=(int)x-(x<0.0f);
    float f=x-(float)i;
    float p=1.0f+f*(0.6565f+f*0.3435f);                 /* 2^f on [0, 1) */
    union {float f;uint32_t u;} v={p};
    v.u+=(uint32_t)i<<23;
    return v.f;
}
#define MD_DB2LOG 0.16610f       /* dB -> log2 amplitude */
static inline float md_abs(float x) {return x<0.0f?-x:x;}
/* Keeps a NaN or a runaway value out of the filter and detector state. */
static inline float md_sane(float x) {return x>64.0f?64.0f:x<-64.0f?-64.0f:x==x?x:0.0f;}

/* ---- Clipper: JOEBOX src/clip.c ------------------------------------------
 *   Clip    how hard the mix is pushed into the ceiling: 0 .. +18 dB. At 0 it only catches what is already over.
 *   Shape   where the rounding starts: 0 % = from the bottom (soft), 100 % = not at all (flat top, hard).
 *   Clean   the same curve both ways.
 *   Diode   the top half rounds earlier (half the Shape) and stops at 0.75 of the ceiling: even harmonics;
 *           a blocker takes off the offset it leaves.
 * The curve runs at 96 kHz (11-tap half-band interpolator). Only what the curve TOOK OFF is brought back down and
 * added to the input, five samples late: below the knee the output is the input, bit for bit. A plain clamp at the
 * ceiling (-0.3 dBFS) finishes. */
struct md_half {float t,top,inv;};   /* knee start (of this half's ceiling), this half's ceiling, 1 / it */
static inline float md_cut(float u,const struct md_half *h) {
    const struct md_half *k=&h[u<0.0f];
    float a=(u<0.0f?-u:u)*k->inv,y;
    if(a<=k->t)return 0.0f;
    if(a>=2.0f-k->t) {
        y=1.0f;
    } else {
        float d=a-k->t;
        y=a-d*d/(4.0f*(1.0f-k->t));
    }
    y*=k->top;
    return (u<0.0f?-y:y)-u;
}
/* One block, in place. amount and shape are the knobs, 0..1. r may be 0.
 * The arithmetic is JOEBOX's, sample for sample (tests/master_dyn_host.c holds
 * it against the original). What differs is the bookkeeping: JOEBOX indexes
 * two ring buffers per sample; here each channel's block is laid out in a
 * straight line behind its history (the last 5 pushed samples, the last 10
 * values of the cut at 96 kHz) and done in three plain passes, which costs the
 * M7 about 20 % fewer instructions (7 900 a block against 10 000). */
#define MD_CLIP_IN 5u
#define MD_CLIP_D 10u
static inline void md_clip_chunk(struct md_clip *s,int type,const struct md_half *h,float g0,float dg,float *x,int c,unsigned n) {
    float in[MD_CLIP_IN+MD_BLOCK],d[MD_CLIP_D+2u*MD_BLOCK];
    float *hin=s->in[c],*hd=s->cut[c];
    for(unsigned i=0;i<MD_CLIP_IN;i++)in[i]=hin[i];
    for(unsigned i=0;i<MD_CLIP_D;i++)d[i]=hd[i];
    float gi=g0;
    for(unsigned i=0;i<n;i++) {
        gi+=dg;
        in[MD_CLIP_IN+i]=md_sane(x[i]*gi);
    }
    /* 96 kHz: the point half way between in[-3] and in[-2], then in[-2] itself */
    for(unsigned i=0;i<n;i++) {
        const float *q=in+i;                            /* q[5] is the newest sample, q[0] five back */
        float mid=(3.0f*(q[5]+q[0])-25.0f*(q[4]+q[1])+150.0f*(q[3]+q[2]))*(1.0f/256.0f);
        d[MD_CLIP_D+2u*i]=md_cut(mid,h);
        d[MD_CLIP_D+2u*i+1u]=md_cut(q[3],h);
    }
    /* back to 48 kHz, centred on in[-5] */
    float dc_in=s->dc_in[c],dc_out=s->dc_out[c];
    for(unsigned i=0;i<n;i++) {
        const float *e=d+2u*i;                          /* e[10] is this sample's mid-point cut */
        float off=0.5f*e[5]+(3.0f*(e[10]+e[0])-25.0f*(e[8]+e[2])+150.0f*(e[6]+e[4]))*(1.0f/512.0f);
        float out=in[i]+off;
        if(type==MD_CLIP_DIODE) {                       /* about 5 Hz */
            dc_out=out-dc_in+0.99935f*dc_out;
            dc_in=out;
            out=dc_out;
        }
        x[i]=(out>1.0f?1.0f:out<-1.0f?-1.0f:out)*MD_CLIP_CEILING;
    }
    s->dc_in[c]=dc_in;
    s->dc_out[c]=dc_out;
    for(unsigned i=0;i<MD_CLIP_IN;i++)hin[i]=in[n+i];
    for(unsigned i=0;i<MD_CLIP_D;i++)hd[i]=d[2u*n+i];
}
static inline void md_clip_block(struct md_clip *s,int type,float amount,float shape,float *l,float *r,unsigned n) {
    if(type<=MD_CLIP_OFF || type>=MD_CLIP_TYPES || !n) {s->on=0;return;}
    amount=amount<0.0f?0.0f:amount>1.0f?1.0f:amount;
    shape=shape<0.0f?0.0f:shape>0.995f?0.995f:shape;    /* 0.995: a corner 0.5 % wide, never a divide by 0 */
    /* 10^(18 x amount / 20) = e^(2.0723 x amount): a short series, squared twice; within 0.2 % */
    float e=2.0723f*amount*0.25f,g=1.0f+e*(1.0f+e*(0.5f+e*(1.0f/6.0f)));
    g*=g;
    g*=g;
    g*=1.0f/MD_CLIP_CEILING;                            /* work with the ceiling at 1 */
    if(!s->on) {
        for(unsigned i=0;i<sizeof *s/4;i++)((uint32_t *)s)[i]=0;
        s->gain=g;
        s->on=1;
    }
    struct md_half h[2]={{shape,1.0f,1.0f},{shape,1.0f,1.0f}};
    if(type==MD_CLIP_DIODE) {
        h[0].t=0.5f*shape;
        h[0].top=0.75f;
        h[0].inv=1.0f/0.75f;
    }
    float g0=s->gain,dg=(g-g0)/(float)n;
    for(unsigned at=0;at<n;at+=MD_BLOCK) {
        unsigned m=n-at<MD_BLOCK?n-at:MD_BLOCK;
        float ga=g0+dg*(float)at;
        md_clip_chunk(s,type,h,ga,dg,l+at,0,m);
        if(r)md_clip_chunk(s,type,h,ga,dg,r+at,1,m);
    }
    s->pos+=(uint32_t)n;
    s->gain=g;
}

/* ---- Compressor models: JOEBOX src/compm.c -------------------------------
 * One per-sample, stereo-linked feed-forward compressor: soft-knee gain
 * computer, attack / release smoothing of the gain reduction in dB, optional
 * two-stage "auto" release, optional parallel blend. Modelled loosely on an
 * SSL bus compressor (Glue), a slow-attack drum compressor (Punch), an LA-2A
 * (Opto) and an 1176 with all buttons in, in parallel (Squash). */
/* exp(-1 / (ms x 0.001 x 48000)): one-pole smoothing coefficients for 48 kHz */
#define MD_C_0P5MS 0.959189457f
#define MD_C_10MS 0.997918835f
#define MD_C_20MS 0.998958876f
#define MD_C_30MS 0.999305797f
#define MD_C_60MS 0.999652838f
#define MD_C_80MS 0.999739617f
#define MD_C_100MS 0.999791688f
#define MD_C_600MS 0.999965278f
#define MD_C_1500MS 0.999986111f
struct md_model {
    float thresh,ratio,knee;                 /* dB, x:1, dB */
    float att,rel_fast,rel_slow,auto_db;     /* smoothing coefficients; slow release once within auto_db of target */
    float rms;                               /* RMS detector coefficient, 0 = peak */
    float makeup,mix;                        /* linear output gain, wet share */
};
static const struct md_model md_models[MD_MODELS-1]={
    /* Glue: 2:1, 6 dB knee, 10 ms attack, auto release 100 -> 600 ms, RMS, +3 dB */
    {-18.0f,2.0f,6.0f,MD_C_10MS,MD_C_100MS,MD_C_600MS,2.0f,MD_C_10MS,1.4125f,1.0f},
    /* Punch: 4:1, 3 dB knee, 30 ms attack lets the hit through, 80 ms release, peak, +4 dB */
    {-20.0f,4.0f,3.0f,MD_C_30MS,MD_C_80MS,MD_C_80MS,0.0f,0.0f,1.5849f,1.0f},
    /* Opto: 3:1, 10 dB knee, 10 ms attack, 60 ms then 1.5 s release, RMS, +4 dB */
    {-22.0f,3.0f,10.0f,MD_C_10MS,MD_C_60MS,MD_C_1500MS,3.0f,MD_C_20MS,1.5849f,1.0f},
    /* Squash: 20:1, hard, 0.5 ms attack, 60 ms release, peak, +12 dB wet, blended 50/50 with dry */
    {-35.0f,20.0f,0.0f,MD_C_0P5MS,MD_C_60MS,MD_C_60MS,0.0f,0.0f,3.9811f,0.5f},
};
static inline float md_computer(const struct md_model *m,float thresh,float level_db) {
    float over=level_db-thresh,slope=1.0f/m->ratio-1.0f;
    if(m->knee>0.0f && 2.0f*over>-m->knee && 2.0f*over<m->knee) {
        float k=over+0.5f*m->knee;
        return slope*k*k/(2.0f*m->knee);
    }
    return over>0.0f?slope*over:0.0f;
}
/* One block of model `model` (MD_GLUE..MD_SQUASH), in place. r may be 0.
 * thresh_mdb is the stock Thresh row: its stock default, -20 dB, leaves the
 * model's own threshold alone and each dB moves it by one (JOEBOX's Comp
 * Thresh, -24..+12 dB). gain_mdb and wet are the stock Gain and Wet/Dry rows.
 * Returns the deepest gain reduction of the block in dB (>= 0). */
static inline float md_comp_block(struct md_comp *s,int model,int32_t thresh_mdb,int32_t gain_mdb,int32_t wet,float *l,float *r,unsigned n) {
    if(model<=MD_STOCK || model>=MD_MODELS || !n) {s->model=0;return 0.0f;}
    const struct md_model *m=&md_models[model-1];
    float off=(float)(thresh_mdb+20000)*0.001f;
    off=off<-24.0f?-24.0f:off>12.0f?12.0f:off;
    float thresh=m->thresh+off;
    float out1=md_exp2((float)gain_mdb*0.001f*MD_DB2LOG),wet1=(float)wet*0.001f;
    if(s->model!=(uint32_t)model) {
        s->gr=0.0f;s->ms=0.0f;s->out=out1;s->wet=wet1;
        s->model=(uint32_t)model;
    }
    float gr=s->gr,ms=s->ms,deepest=0.0f;
    float out=s->out,dout=(out1-out)/(float)n,w=s->wet,dw=(wet1-w)/(float)n;
    const float att=m->att,rel_fast=m->rel_fast,rel_slow=m->rel_slow,auto_db=m->auto_db,rms=m->rms;
    const float makeup=m->makeup,mix=m->mix;
    for(unsigned i=0;i<n;i++) {
        float xl=md_sane(l[i]),xr=r?md_sane(r[i]):xl;
        float level;
        if(rms>0.0f) {
            float p=0.5f*(xl*xl+xr*xr);
            ms=p+rms*(ms-p);
            level=3.0103f*md_log2(ms+1e-12f);           /* 10 log10 */
        } else {
            float a=md_abs(xl),b=md_abs(xr);
            level=6.0206f*md_log2((a>b?a:b)+1e-9f);     /* 20 log10 */
        }
        float target=md_computer(m,thresh,level);
        if(target<gr)gr=target+att*(gr-target);
        else gr=target+(target-gr<auto_db?rel_slow:rel_fast)*(gr-target);
        if(gr<deepest)deepest=gr;
        float g=md_exp2(gr*MD_DB2LOG)*makeup;           /* dB -> linear */
        float yl=xl*g,yr=xr*g;
        if(mix<1.0f) {
            yl=mix*yl+(1.0f-mix)*xl;
            yr=mix*yr+(1.0f-mix)*xr;
        }
        out+=dout;w+=dw;
        l[i]=xl+w*(yl*out-xl);
        if(r)r[i]=xr+w*(yr*out-xr);
    }
    s->gr=gr;s->ms=ms;s->out=out1;s->wet=wet1;
    return -deepest;
}

/* ---- Limiter: JOEBOX's Limit model ---------------------------------------
 * Infinite ratio above the ceiling, 0.5 ms attack on a 1.5 ms look-ahead,
 * 60 ms release, stereo linked, then a plain clamp at the ceiling: nothing
 * leaves above it. Drive pushes the mix into it. The stage is 72 samples
 * (1.5 ms) late. r may be 0. */
static inline void md_lim_block(struct md_lim *s,int32_t drive_mdb,int32_t ceil_mdb,float *l,float *r,unsigned n) {
    if(!n)return;
    const float att=MD_C_0P5MS,rel=MD_C_60MS;
    float ceil_db=(float)ceil_mdb*0.001f,ceiling=md_exp2(ceil_db*MD_DB2LOG);
    float pre1=md_exp2((float)drive_mdb*0.001f*MD_DB2LOG);
    if(!s->on) {
        for(unsigned i=0;i<sizeof *s/4;i++)((uint32_t *)s)[i]=0;
        s->pre=pre1;
        s->on=1;
    }
    float gr=s->gr,pre=s->pre,dpre=(pre1-pre)/(float)n;
    uint32_t pos=s->pos;
    for(unsigned i=0;i<n;i++) {
        pre+=dpre;
        float xl=md_sane(l[i]*pre),xr=r?md_sane(r[i]*pre):xl;
        float a=md_abs(xl),b=md_abs(xr);
        float over=6.0206f*md_log2((a>b?a:b)+1e-9f)-ceil_db;
        float target=over>0.0f?-over:0.0f;
        gr=target+(target<gr?att:rel)*(gr-target);
        float g=md_exp2(gr*MD_DB2LOG);
        float dl=s->la[pos][0],dr=s->la[pos][1];
        s->la[pos][0]=xl;
        s->la[pos][1]=xr;
        if(++pos>=MD_LA)pos=0;
        float yl=dl*g,yr=dr*g;
        l[i]=yl>ceiling?ceiling:yl<-ceiling?-ceiling:yl;
        if(r)r[i]=yr>ceiling?ceiling:yr<-ceiling?-ceiling:yr;
    }
    s->gr=gr;s->pre=pre1;s->pos=pos;
}
#endif
