/* Stock compressor and GR reading retained; optional saturation before its next node.
 * Master dynamics (clipper, compressor models, limiter): master_dyn_m7.h. */
#include "shared.h"
#include "meter_shared.h"
typedef unsigned (*process_fn)(uint8_t *,void *);
#define STOCK_COMP ((process_fn)0x08042649u)
struct gr_acc { uint32_t magic, blocks; float peak; };
#define GR_ACC ((volatile struct gr_acc *)0x38800f60u)
#define GR_ACC_MAGIC 0x44474132u
#include "saturation.h"
#include "master_dyn_m7.h"
unsigned gr_process(uint8_t *obj,void *ctx) {
    backup_enable();
    PWR_CR1 |= 1u<<8;
    sat_events(obj,ctx);
    /* Only split the chain while processing nonzero saturation or its fade-out.
     * The original next pointer is restored before downstream execution. */
    struct master_dyn *dyn=md_get(obj,ctx);
    if(dyn)pk_status(md_wanted(dyn)?PK_ST_MASTER|PK_ST_ACTIVE:PK_ST_MASTER,md_wanted(dyn)?0:PK_ST_ACTIVE);
    if(dyn && !md_wanted(dyn)){md_reset_dsp(dyn);dyn=0;}
    unsigned split=SAT_STATE->drive!=0 || SAT_STATE->mix>0.f || dyn;
    uint8_t **pnext=(uint8_t **)(obj+8),*next=*pnext;
    if(dyn)md_before(dyn,obj,ctx);
    /* A model replaces the stock compressor: stock still takes its parameter
     * events, but through its own bypass, so its DSP is not run for nothing.
     * Its process reads the flag (obj+0x28) once, after its event loop, and
     * only a Compressor on / off event writes it (0x08042c3a): with no such
     * event in this block the flag is put back as it was. */
    unsigned held=dyn && dyn->model!=MD_STOCK;
    uint8_t bypass=obj[0x28];
    if(held)obj[0x28]=1;
    if(split)*pnext=0;
    unsigned result=STOCK_COMP(obj,ctx);
    if(split)*pnext=next;
    if(held && !dyn->comp_event)obj[0x28]=bypass;
    float model_gr=dyn?md_model(dyn,obj,ctx,obj[0x28]==0):-1.f;
    if(GR_ACC->magic!=GR_ACC_MAGIC) {
        GR_ACC->blocks=0;
        GR_ACC->peak=0.f;
        GR_ACC->magic=GR_ACC_MAGIC;
        GR_REPORT->magic=0;
        GR_REPORT->seq=0;
    }
    unsigned on=obj[0x28]==0;
    if(on) {
        /* Gains include makeup, and are log2 amplitude values. */
        float left=*(const float *)(obj+0xc0), right=*(const float *)(obj+0xd8);
        float makeup=*(const float *)(obj+0xfc);
        /* The stock gain fields begin at a sentinel and may stay there for
         * silent channels. Ignore channels whose detector is effectively silent
         * (below -120 dBFS), instead of displaying that sentinel as reduction. */
        float env_left=*(const float *)(obj+0xbc), env_right=*(const float *)(obj+0xd4);
        float gr_left=env_left>1e-6f ? makeup-left : 0.f;
        float gr_right=env_right>1e-6f ? makeup-right : 0.f;
        float gr=gr_left>gr_right ? gr_left : gr_right;
        if(model_gr>=0.f) gr=model_gr*(1.f/6.0206f);   /* a model replaced stock: its reduction, in log2 */
        if(gr>GR_ACC->peak) GR_ACC->peak=gr;
    } else {
        GR_ACC->peak=0.f;
    }
    /* Report bypass transitions immediately, otherwise every 32 blocks. */
    unsigned transition=GR_REPORT->magic==GR_MAGIC && GR_REPORT->on!=on;
    if(++GR_ACC->blocks>=32u || transition) {
        float tenths=on ? GR_ACC->peak*60.206f : 0.f;
        if(!(tenths>0.f)) tenths=0.f;
        if(tenths>1200.f) tenths=1200.f;
        GR_REPORT->gr=(uint16_t)(tenths+0.5f);
        GR_REPORT->on=(uint8_t)on;
        GR_REPORT->reserved=0;
        GR_REPORT->seq++;
        GR_REPORT->magic=GR_MAGIC;
        __asm volatile("dsb" ::: "memory");
        SCB_DCCMVAC=(uint32_t)GR_REPORT;
        __asm volatile("dsb\nisb" ::: "memory");
        GR_ACC->blocks=0;
        GR_ACC->peak=0.f;
    }
    if(split) {
        saturate(obj,ctx);
        if(dyn)md_after(dyn,obj,ctx);
        return next ? (*(process_fn **)next)[3](next,ctx) : 1u;
    }
    return result;
}
