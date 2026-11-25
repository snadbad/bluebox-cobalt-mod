/* Master dynamics per-sample loops, built at -O2 like the settled Tape loop.
 * Kept apart from gr_m7.c so everything Cobalt already had keeps its -Os code. */
#include "master_dyn.h"
void md_clip_o2(struct master_dyn *d,float *l,float *r,unsigned n) {
    md_clip_block(&d->c,d->clipper,(float)d->clip*0.001f,(float)d->shape*0.001f,l,r,n);
}
float md_comp_o2(struct master_dyn *d,float *l,float *r,unsigned n) {
    return md_comp_block(&d->k,d->model,d->thresh,d->gain,d->wet,l,r,n);
}
void md_lim_o2(struct master_dyn *d,float *l,float *r,unsigned n) {
    md_lim_block(&d->l,d->ldrive,d->lceil,l,r,n);
}
