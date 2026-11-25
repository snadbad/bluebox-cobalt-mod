/* Where the master dynamics state lives, shared by the M7 payloads that touch
 * it: delay_m7.c allocates it once at boot (md_allocate) and gr_m7.c and
 * gate_m7.c use it.
 * The native allocator (0x080412a4) is the one the Granular and Gazer buffers
 * come from, but it does not put everything in the SDRAM arena: a request of
 * up to 0x42cc bytes is served from a bump area in DTCM (0x20000000 +
 * cursor, up to 0x1f800) first, then from the heap (0x0806868c), and only then
 * from the arena. This block is about 1 KB, so on the device it is in DTCM.
 * (The first hardware builds only accepted an arena address and so never ran.)
 * The 16 bytes at 0x38800f90 are the unused second half of the saturation
 * state's backup-SRAM line (sat_state is 16 bytes at 0xf80); only the M7
 * touches this line. */
#ifndef BLUEBOX_MASTER_DYN_LINK_H
#define BLUEBOX_MASTER_DYN_LINK_H
#include "master_dyn.h"
#include "peak_shared.h"
struct md_link {uint32_t magic;struct master_dyn *state;};
#define MD_LINK ((volatile struct md_link *)0x38800f90u)
#define MD_LINK_MAGIC 0x4d444c31u
#define MD_ALLOC (((unsigned)sizeof(struct master_dyn)+31u)&~31u)
/* The peak readout's hold (peak_shared.h, used by gate_m7.c) sits right behind it. */
#define PK_ALLOC 64u
#define MD_TOTAL (MD_ALLOC+PK_ALLOC)
_Static_assert(sizeof(struct pk_hold)<=PK_ALLOC,"peak hold fits its block");
#define MD_ARENA_LO 0xc0000000u
#define MD_ARENA_HI 0xc3400000u
/* Any RAM the allocator can hand out: DTCM, AXI SRAM, SRAM1-3, SDRAM. */
static inline int md_ram_ok(uint32_t a,uint32_t n) {
    if(a&3u)return 0;
    return (a>=0x20000000u && a<=0x20020000u-n) || (a>=0x24000000u && a<=0x24080000u-n) ||
           (a>=0x30000000u && a<=0x30048000u-n) || (a>=0xc0000000u && a<=0xc4000000u-n);
}
/* The block, or 0 when it was not allocated at boot. */
static inline struct master_dyn *md_block(void) {
    if(MD_LINK->magic!=MD_LINK_MAGIC)return 0;
    struct master_dyn *d=MD_LINK->state;
    return md_ram_ok((uint32_t)d,MD_TOTAL)?d:0;
}
_Static_assert(0xf90+sizeof(struct md_link)<=0xfa0,"dynamics link below the PC transport line at 0xfa0");
_Static_assert(sizeof(struct master_dyn)<=2048,"master dynamics state stays small");
#endif
