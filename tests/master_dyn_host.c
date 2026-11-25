/* Host checks for src/master_dyn.h, built with ASan/UBSan by run_checks.py.
 * The clipper is also compared with JOEBOX's own clip.c (references/joebox,
 * MIT, unmodified) so the port is shown to be the same DSP. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/master_dyn.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#include "references/joebox/clip.c"
#pragma GCC diagnostic pop

static int checks,fails;
static void check(int ok,const char *msg) {
    printf("%s %s\n",ok?"PASS":"FAIL",msg);checks++;if(!ok)fails++;
}
static uint32_t rng=0x1234567u;
static float noise(void) {rng=rng*1664525u+1013904223u;return (float)(int32_t)rng*(1.0f/2147483648.0f);}
#define N 24000
static float il[N],ir[N],ol[N],or_[N],pl[N],pr[N];
static void fill(float amp,float hz_l,float hz_r,float noise_amp) {
    for(int i=0;i<N;i++) {
        il[i]=amp*sinf(6.2831853f*hz_l*(float)i/48000.f)+noise_amp*noise();
        ir[i]=amp*sinf(6.2831853f*hz_r*(float)i/48000.f+.7f)+noise_amp*noise();
    }
}
static float peak(const float *x,int from,int to) {float p=0;for(int i=from;i<to;i++)if(fabsf(x[i])>p)p=fabsf(x[i]);return p;}
static int finite_all(const float *x) {for(int i=0;i<N;i++)if(!isfinite(x[i]))return 0;return 1;}
static float db(float x) {return 20.f*log10f(x);}

static void run_clip(struct md_clip *s,int type,float amount,float shape,int mono) {
    memcpy(ol,il,sizeof ol);memcpy(or_,ir,sizeof or_);
    for(int i=0;i<N;i+=32)md_clip_block(s,type,amount,shape,ol+i,mono?0:or_+i,32);
}
static void run_lim(struct md_lim *s,int drive,int ceil,int mono) {
    memcpy(ol,il,sizeof ol);memcpy(or_,ir,sizeof or_);
    for(int i=0;i<N;i+=32)md_lim_block(s,drive,ceil,ol+i,mono?0:or_+i,32);
}
static float run_comp(struct md_comp *s,int model,int thresh,int gain,int wet,int mono) {
    float deepest=0;
    memcpy(ol,il,sizeof ol);memcpy(or_,ir,sizeof or_);
    for(int i=0;i<N;i+=32){float g=md_comp_block(s,model,thresh,gain,wet,ol+i,mono?0:or_+i,32);if(g>deepest)deepest=g;}
    return deepest;
}

int main(void) {
    char msg[200];
    /* ---- settings ---- */
    struct master_dyn d;memset(&d,0xa5,sizeof d);md_init(&d);
    check(d.magic==MD_MAGIC && !md_wanted(&d) && d.model==0 && d.clipper==0 && d.limit==0 && d.flow==0 &&
          d.clip==0 && d.shape==800 && d.ldrive==0 && d.lceil==-300 && d.thresh==-20000 && d.gain==0 && d.wet==1000,
          "defaults: every stage off, Stock model, Clip 0, Shape 80 %, ceiling -0.3 dB, stock compressor defaults");
    md_event(&d,MD_P_CLIP,5000);md_event(&d,MD_P_SHAPE,-3);md_event(&d,MD_P_LDRIVE,99999);md_event(&d,MD_P_LCEIL,500);
    md_event(&d,MD_P_THRESH,-99999);md_event(&d,MD_P_GAIN,99999);md_event(&d,MD_P_WET,-1);
    check(d.clip==1000 && d.shape==0 && d.ldrive==18000 && d.lceil==0 && d.thresh==-48000 && d.gain==36000 && d.wet==0,
          "out-of-range values are clamped to each row's range");
    md_event(&d,MD_P_MODEL,7);md_event(&d,MD_P_CLIPPER,-1);md_event(&d,MD_P_FLOW,3);md_event(&d,MD_P_LIMIT,2);
    check(d.model==MD_STOCK && d.clipper==MD_CLIP_OFF && d.flow==0 && d.limit==0 && !md_wanted(&d),
          "unknown list entries fall back to Stock / Off / default order");
    md_event(&d,0x43,700);md_event(&d,0x1b6,1);md_event(&d,0x130,5);
    check(!md_wanted(&d),"other master parameters are ignored");
    md_event(&d,MD_P_MODEL,MD_OPTO);check(md_wanted(&d),"a model asks for the stage");md_event(&d,MD_P_MODEL,0);
    md_event(&d,MD_P_CLIPPER,MD_CLIP_DIODE);check(md_wanted(&d),"the clipper asks for the stage");md_event(&d,MD_P_CLIPPER,0);
    md_event(&d,MD_P_LIMIT,1);check(md_wanted(&d),"the limiter asks for the stage");

    /* ---- clipper ---- */
    struct md_clip c;memset(&c,0,sizeof c);
    fill(.5f,440.f,1000.f,.05f);
    run_clip(&c,MD_CLIP_CLEAN,0.f,1.f,0);
    {float worst=0;for(int i=5;i<N;i++){float e=fabsf(ol[i]-il[i-5]);if(e>worst)worst=e;e=fabsf(or_[i]-ir[i-5]);if(e>worst)worst=e;}
     snprintf(msg,sizeof msg,"clipper below the knee passes the input, five samples late (worst %.2g)",worst);check(worst<2e-7f,msg);}
    {int ok=1;float worst=0;
     for(int type=MD_CLIP_CLEAN;type<MD_CLIP_TYPES;type++)for(int a=0;a<=10;a+=5)for(int sh=0;sh<=10;sh+=5) {
        fill(1.8f,97.f,3100.f,.6f);il[100]=NAN;ir[200]=INFINITY;il[300]=-INFINITY;il[400]=1e30f;
        memset(&c,0,sizeof c);run_clip(&c,type,(float)a*.1f,(float)sh*.1f,0);
        float p=peak(ol,0,N);if(peak(or_,0,N)>p)p=peak(or_,0,N);
        if(p>worst)worst=p;if(!(p<=MD_CLIP_CEILING) || !finite_all(ol) || !finite_all(or_))ok=0;
     }
     snprintf(msg,sizeof msg,"clipper never leaves the -0.3 dBFS ceiling: all types, pushes and shapes, with NaN and infinite input (peak %.4f)",worst);check(ok,msg);}
    {int ok=1;float worst=0;
     for(int type=MD_CLIP_CLEAN;type<MD_CLIP_TYPES;type++)for(int a=0;a<=10;a+=2)for(int sh=0;sh<=10;sh+=2) {
        fill(.9f,220.f,1500.f,.3f);
        memset(&c,0,sizeof c);run_clip(&c,type,(float)a*.1f,(float)sh*.1f,0);
        struct clip_state j;memset(&j,0,sizeof j);
        memcpy(pl,il,sizeof pl);memcpy(pr,ir,sizeof pr);
        for(int i=0;i<N;i+=32)clip_block(&j,type,(float)a*.1f,(float)sh*.1f,pl+i,pr+i,32);
        for(int i=0;i<N;i++){float e=fabsf(pl[i]-ol[i]);if(e>worst)worst=e;e=fabsf(pr[i]-or_[i]);if(e>worst)worst=e;}
     }
     ok=worst==0.f;
     snprintf(msg,sizeof msg,"clipper port is sample-identical to JOEBOX clip.c on finite input (36 settings x 2 types, worst %.2g)",worst);check(ok,msg);}
    fill(.9f,220.f,220.f,0.f);memset(&c,0,sizeof c);run_clip(&c,MD_CLIP_CLEAN,.5f,1.f,0);
    {float p=peak(ol,4800,N);snprintf(msg,sizeof msg,"hard clip of a pushed sine sits at the ceiling (%.4f)",p);check(p>MD_CLIP_CEILING-1e-4f && p<=MD_CLIP_CEILING,msg);}
    fill(.9f,220.f,220.f,0.f);memset(&c,0,sizeof c);run_clip(&c,MD_CLIP_DIODE,.5f,.8f,0);
    {float hi=0,lo=0;for(int i=4800;i<N;i++){if(ol[i]>hi)hi=ol[i];if(ol[i]<lo)lo=ol[i];}
     snprintf(msg,sizeof msg,"Diode clips the two halves unevenly (+%.3f / %.3f)",hi,lo);check(hi<-lo-.05f && -lo<=MD_CLIP_CEILING,msg);}
    fill(.9f,220.f,500.f,.1f);memset(&c,0,sizeof c);run_clip(&c,MD_CLIP_CLEAN,.4f,.6f,1);
    memcpy(pl,ol,sizeof pl);
    check(memcmp(or_,ir,sizeof ir)==0,"mono bus: the missing right channel is not written");
    memset(&c,0,sizeof c);run_clip(&c,MD_CLIP_CLEAN,.4f,.6f,0);
    check(memcmp(pl,ol,sizeof pl)==0,"mono bus: the left channel is processed exactly as in stereo");
    run_clip(&c,MD_CLIP_OFF,.4f,.6f,0);
    check(!c.on && memcmp(ol,il,sizeof il)==0,"clipper Off leaves the audio alone and restarts clean next time");

    /* ---- limiter ---- */
    struct md_lim m;memset(&m,0,sizeof m);
    fill(.4f,330.f,2000.f,.05f);run_lim(&m,0,-300,0);
    check(memcmp(ol+MD_LA,il,(N-MD_LA)*sizeof(float))==0 && memcmp(or_+MD_LA,ir,(N-MD_LA)*sizeof(float))==0 && peak(ol,0,MD_LA)==0.f,
          "limiter below the ceiling is the input, bit for bit, 72 samples (1.5 ms) late");
    {int ok=1;float worst=-99;
     for(int drive=0;drive<=18000;drive+=6000)for(int ceil=-12000;ceil<=0;ceil+=3000) {
        fill(1.5f,60.f,5000.f,.8f);il[100]=NAN;ir[200]=INFINITY;il[300]=-INFINITY;il[400]=1e30f;
        memset(&m,0,sizeof m);run_lim(&m,drive,ceil,0);
        float p=peak(ol,0,N);if(peak(or_,0,N)>p)p=peak(or_,0,N);
        float over=db(p)-(float)ceil*.001f;if(over>worst)worst=over;
        if(!(over<=.03f) || !finite_all(ol) || !finite_all(or_))ok=0;
     }
     snprintf(msg,sizeof msg,"limiter output never passes the ceiling: every drive and ceiling, NaN and infinite input (worst %+.3f dB, within the 0.03 dB of the fast dB conversion)",worst);check(ok,msg);}
    fill(.9f,440.f,440.f,0.f);memset(&m,0,sizeof m);run_lim(&m,12000,-6000,0);
    {float p=db(peak(ol,4800,N));snprintf(msg,sizeof msg,"limiter holds a driven sine at the ceiling (%.2f dB for -6 dB)",p);check(p<=-6.f+.03f && p>-6.6f,msg);}
    {/* how much of the work the gain does, rather than the final clamp */
     fill(0.f,1.f,1.f,0.f);for(int i=0;i<N;i++)il[i]=ir[i]=(i>=2400 && i<2400+480)?.9f*sinf(6.2831853f*1000.f*(float)i/48000.f):.05f*sinf(6.2831853f*1000.f*(float)i/48000.f);
     memset(&m,0,sizeof m);run_lim(&m,12000,-300,0);
     float tail=peak(ol,2400+480+MD_LA+16800,2400+480+MD_LA+16800+480),want=.05f*3.981f;
     snprintf(msg,sizeof msg,"limiter releases after a burst: back to unity gain 0.35 s later (%.3f of %.3f)",tail,want);check(fabsf(tail-want)<want*.02f,msg);}
    fill(.2f,330.f,330.f,0.f);memset(&m,0,sizeof m);run_lim(&m,6000,0,0);
    {float g=peak(ol,4800,N)/.2f;snprintf(msg,sizeof msg,"Lim Drive +6 dB doubles a quiet signal (x%.3f)",g);check(fabsf(g-1.995f)<.02f,msg);}
    fill(.9f,220.f,500.f,.1f);memset(&m,0,sizeof m);run_lim(&m,9000,-1000,1);memcpy(pl,ol,sizeof pl);
    check(memcmp(or_,ir,sizeof ir)==0,"limiter mono bus: the missing right channel is not written");
    memcpy(ir,il,sizeof ir);memset(&m,0,sizeof m);run_lim(&m,9000,-1000,0);
    check(memcmp(pl,ol,sizeof pl)==0 && memcmp(ol,or_,sizeof ol)==0,"limiter mono bus equals stereo with both channels alike");

    /* ---- compressor models ---- */
    struct md_comp k;
    static const char *const names[]={"Stock","Glue","Punch","Opto","Squash"};
    for(int model=MD_GLUE;model<MD_MODELS;model++) {
        const struct md_model *mm=&md_models[model-1];
        /* constant level: quadrature sines for the RMS detectors, DC for the peak ones */
        float amp=.5f;
        for(int i=0;i<N;i++) {
            if(mm->rms>0.f){il[i]=amp*sinf(6.2831853f*1000.f*(float)i/48000.f);ir[i]=amp*cosf(6.2831853f*1000.f*(float)i/48000.f);}
            else il[i]=ir[i]=amp;
        }
        float level=mm->rms>0.f?10.f*log10f(amp*amp*.5f):20.f*log10f(amp);
        memset(&k,0,sizeof k);
        float deepest=0;
        for(int rep=0;rep<12;rep++)deepest=run_comp(&k,model,-20000,0,1000,0);   /* 6 s: the slow releases settle */
        float over=level-mm->thresh,slope=1.f/mm->ratio-1.f,gr;
        if(mm->knee>0.f && 2.f*over>-mm->knee && 2.f*over<mm->knee)gr=slope*(over+.5f*mm->knee)*(over+.5f*mm->knee)/(2.f*mm->knee);
        else gr=over>0.f?slope*over:0.f;
        float wet=powf(10.f,gr/20.f)*mm->makeup,want=mm->mix*wet+(1.f-mm->mix);
        float got=mm->rms>0.f?sqrtf(ol[N-1]*ol[N-1]+or_[N-1]*or_[N-1])/amp:ol[N-1]/amp;
        snprintf(msg,sizeof msg,"%s: settled gain at -6 dBFS matches its curve (%.2f dB, expected %.2f dB; reduction %.1f dB)",names[model],db(got),db(want),-gr);
        check(fabsf(db(got)-db(want))<.15f,msg);
        snprintf(msg,sizeof msg,"%s: reported reduction is the gain computer's (%.2f dB)",names[model],deepest);
        check(fabsf(deepest+gr)<.15f,msg);
        /* Thresh: +6 dB on the row raises the threshold by 6 dB */
        memset(&k,0,sizeof k);for(int rep=0;rep<12;rep++)deepest=run_comp(&k,model,-14000,0,1000,0);
        float over2=over-6.f,gr2;
        if(mm->knee>0.f && 2.f*over2>-mm->knee && 2.f*over2<mm->knee)gr2=slope*(over2+.5f*mm->knee)*(over2+.5f*mm->knee)/(2.f*mm->knee);
        else gr2=over2>0.f?slope*over2:0.f;
        snprintf(msg,sizeof msg,"%s: Thresh +6 dB compresses less (%.2f dB, expected %.2f dB)",names[model],deepest,-gr2);
        check(fabsf(deepest+gr2)<.15f,msg);
        /* Gain and Wet/Dry rows */
        memset(&k,0,sizeof k);for(int rep=0;rep<12;rep++)run_comp(&k,model,-20000,6000,1000,0);
        float got6=mm->rms>0.f?sqrtf(ol[N-1]*ol[N-1]+or_[N-1]*or_[N-1])/amp:ol[N-1]/amp;
        snprintf(msg,sizeof msg,"%s: Gain +6 dB adds 6 dB (%.2f dB)",names[model],db(got6)-db(got));
        check(fabsf(db(got6)-db(got)-6.f)<.05f,msg);
        fill(.7f,300.f,900.f,.1f);memset(&k,0,sizeof k);run_comp(&k,model,-20000,3000,0,0);
        snprintf(msg,sizeof msg,"%s: Wet/Dry at 0 %% is the dry input, bit for bit",names[model]);
        check(memcmp(ol,il,sizeof il)==0 && memcmp(or_,ir,sizeof ir)==0,msg);
        fill(1.5f,60.f,5000.f,.8f);il[100]=NAN;ir[200]=INFINITY;il[300]=-INFINITY;memset(&k,0,sizeof k);run_comp(&k,model,-48000,36000,1000,0);
        snprintf(msg,sizeof msg,"%s: NaN and infinite input leave finite output and state",names[model]);
        check(finite_all(ol) && finite_all(or_) && isfinite(k.gr) && isfinite(k.ms),msg);
        fill(.7f,300.f,300.f,0.f);memcpy(ir,il,sizeof ir);memset(&k,0,sizeof k);run_comp(&k,model,-20000,0,1000,0);memcpy(pl,ol,sizeof pl);
        memset(&k,0,sizeof k);run_comp(&k,model,-20000,0,1000,1);
        snprintf(msg,sizeof msg,"%s: mono bus equals stereo with both channels alike, right channel untouched",names[model]);
        check(memcmp(pl,ol,sizeof pl)==0 && memcmp(or_,ir,sizeof ir)==0,msg);
    }
    memset(&k,0,sizeof k);fill(.7f,300.f,900.f,.1f);
    check(run_comp(&k,MD_STOCK,-20000,0,1000,0)==0.f && memcmp(ol,il,sizeof il)==0 && !k.model,"Stock model: this code leaves the audio to the stock compressor");
    {/* Punch lets the hit through: first 5 ms of a burst are louder than Glue's settled level would suggest */
     for(int i=0;i<N;i++)il[i]=ir[i]=i<480?0.f:.8f;
     memset(&k,0,sizeof k);run_comp(&k,MD_PUNCH,-20000,0,1000,0);float early=ol[480+48],late=ol[N-1];
     snprintf(msg,sizeof msg,"Punch: 30 ms attack lets the transient through (%.3f at 1 ms, %.3f settled)",early,late);check(early>late*1.8f,msg);}
    printf("%d master dynamics host checks, %d failed. Host arithmetic is not device CPU timing.\n",checks,fails);
    return fails!=0;
}
