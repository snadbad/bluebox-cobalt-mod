/* Host build of the master dynamics loops (src/dyn_fast_m7.c, the file the
 * firmware builds) as a shared library, so tests/test_master_dyn.py can hold
 * the emulated firmware against the same C on the host. */
#include <stddef.h>
#include "../src/dyn_fast_m7.c"
unsigned md_host_size(void) {return (unsigned)sizeof(struct master_dyn);}
void md_host_init(struct master_dyn *d) {md_init(d);}
void md_host_event(struct master_dyn *d,unsigned id,int value) {md_event(d,id,value);}
unsigned md_host_off_clip_on(void) {return (unsigned)offsetof(struct master_dyn,c.on);}
unsigned md_host_off_comp_model(void) {return (unsigned)offsetof(struct master_dyn,k.model);}
unsigned md_host_off_lim_on(void) {return (unsigned)offsetof(struct master_dyn,l.on);}
void md_host_comp_reset(struct master_dyn *d) {md_comp_reset(&d->k);}
