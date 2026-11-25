/* Master dynamics on the master compressor node: firmware glue for
 * master_dyn.h. Included by gr_m7.c after saturation.h (FW_* helpers).
 *
 * Order on the master bus, by the Flow setting (the compressor is the stock
 * one or the model that replaces it; saturation stays right after it):
 *   0  clipper, compressor, saturation, limiter
 *   1  compressor, saturation, clipper, limiter
 *   2  compressor, saturation, limiter, clipper
 * With the clipper and limiter off and Comp Model on Stock nothing here
 * touches the audio and gr_process runs exactly as in Cobalt 1.2.0. */
#include "master_dyn_link.h"
void md_clip_o2(struct master_dyn *d,float *l,float *r,unsigned n);
float md_comp_o2(struct master_dyn *d,float *l,float *r,unsigned n);
void md_lim_o2(struct master_dyn *d,float *l,float *r,unsigned n);
#define MD_MASTER_QUEUE 12u      /* the master cell: its compressor's queue index */

/* The state, with this block's parameter events applied. 0 unless this is
 * the master compressor and the state was allocated at boot. */
static struct master_dyn *md_get(uint8_t *obj,void *ctx) {
    if(*(uint32_t *)(obj+0x18)!=MD_MASTER_QUEUE)return 0;
    struct master_dyn *d=md_block();
    if(!d)return 0;
    if(d->magic!=MD_MAGIC)md_init(d);
    d->comp_event=0;
    void *q=FW_QUEUE(ctx,MD_MASTER_QUEUE);
    struct sat_event ev;
    for(unsigned i=0;FW_EVENT(q,i,&ev);i++)
        if(ev.type==0x39)md_event(d,ev.id,ev.value);
    return d;
}
/* The master bus as two channels (r = 0 when both are one buffer). 0 when
 * there is nothing to process: the stages then start clean next time, so the
 * look-ahead never replays audio from before a silence. */
static unsigned md_bus(struct master_dyn *d,uint8_t *obj,void *ctx,float **l,float **r) {
    typedef unsigned (*port_fn)(uint8_t *);
    port_fn port=(*(port_fn **)obj)[0x54/4];
    uint8_t *bus=FW_BUS(ctx,port(obj));
    unsigned n=0;
    *l=*r=0;
    if(bus && !bus[16]) {
        n=FW_FRAMES(bus);if(n>MD_BLOCK)n=MD_BLOCK;
        if(n)FW_STEREO(bus,l,r);
        if(!*l)n=0;
        if(*r==*l)*r=0;
    }
    if(!n)md_reset_dsp(d);
    return n;
}
/* Before the stock compressor: the clipper when it comes first, and a copy of
 * the bus for a model to work from. */
static void md_before(struct master_dyn *d,uint8_t *obj,void *ctx) {
    float *l,*r;
    unsigned n=md_bus(d,obj,ctx,&l,&r);
    d->dry_n=0;
    if(!n)return;
    if(d->flow==MD_FLOW_CLIP_COMP_LIM && d->clipper!=MD_CLIP_OFF)md_clip_o2(d,l,r,n);
    if(d->model!=MD_STOCK) {
        for(unsigned i=0;i<n;i++){d->dry[0][i]=l[i];d->dry[1][i]=r?r[i]:l[i];}
        d->dry_n=n;
    }
}
/* After the stock compressor's turn: with a model picked and the compressor
 * on, the model works from the copy of the bus taken before it (gr_process
 * keeps stock bypassed meanwhile, so normally stock has not touched it). Returns the model's gain reduction in dB, or a
 * negative value when stock's reading stands. */
static float md_model(struct master_dyn *d,uint8_t *obj,void *ctx,unsigned comp_on) {
    if(d->model==MD_STOCK || !comp_on){md_comp_reset(&d->k);return -1.0f;}
    float *l,*r;
    unsigned n=md_bus(d,obj,ctx,&l,&r);
    if(!n || n!=d->dry_n){md_comp_reset(&d->k);return 0.0f;}
    for(unsigned i=0;i<n;i++){l[i]=d->dry[0][i];if(r)r[i]=d->dry[1][i];}
    return md_comp_o2(d,l,r,n);
}
/* After the compressor and saturation. */
static void md_after(struct master_dyn *d,uint8_t *obj,void *ctx) {
    unsigned clip=d->flow!=MD_FLOW_CLIP_COMP_LIM && d->clipper!=MD_CLIP_OFF;
    if(d->clipper==MD_CLIP_OFF)md_clip_reset(&d->c);
    if(!d->limit)md_lim_reset(&d->l);
    if(!clip && !d->limit)return;
    float *l,*r;
    unsigned n=md_bus(d,obj,ctx,&l,&r);
    if(!n)return;
    if(clip && d->flow==MD_FLOW_COMP_CLIP_LIM)md_clip_o2(d,l,r,n);
    if(d->limit)md_lim_o2(d,l,r,n);
    if(clip && d->flow==MD_FLOW_COMP_LIM_CLIP)md_clip_o2(d,l,r,n);
}
