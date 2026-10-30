#include "es2/core/key_axes.h"
/* The keyboard axes 45a4cc (docs/reverse-engineering/input.md,
 * "keyboard axes 45a4cc"). No game call; every access goes through
 * the guest memory in the original's order. */
#define R32(a) m->read32(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(v))
#define R16(a) m->read16(m->context,(a))
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define VA(x) (image+(uint32_t)(x)-0x400000u)
enum { KEYS_AXES=14, KEYS_ALL=15, PAD_KEYS=8, SHIFT=7 };
/* movsx of a table byte, shifted by CL = 7 (mov bx,7 at 45a4fd). */
static uint32_t step(uint8_t b) { return (b<0x80u ? (uint32_t)b : (uint32_t)b|0xffffff00u)<<SHIFT; }
static uint64_t pair(uint32_t eax,uint32_t edx) { return (uint64_t)edx<<32|eax; }

/* Original: DBSIM.EXE 0x45a4cc */
uint64_t es2_key_axes_registers(const es2_guest_memory *m,uint32_t image,uint32_t block,uint32_t flag) {
    const uint32_t down=VA(0x4d2394u),released=VA(0x4d23a3u),seen=VA(0x4d23b2u);
    const uint32_t exclusive=VA(0x49eb3du),table=VA(0x49eb45u);
    /* The flag only reaches TEST SI,SI at 45a510, whose flags CMP EAX,8
     * overwrites at once. */
    (void)flag;
    /* 45a4da..45a4ea: the four axis dwords from the top, then +11h, +10h. */
    W32(block+0xcu,0);W32(block+8u,0);W32(block+4u,0);W32(block,0);
    W8(block+0x11u,0);W8(block+0x10u,0);
    for(uint32_t i=0;i<KEYS_AXES;++i) {
        if(!R8(down+i))continue;
        /* 45a501..45a50e: the Alt word and i are compared, but every
         * outcome reaches 45a513. */
        (void)R16(VA(0x4d23d6u));
        W8(seen+i,1);
        if(i>=PAD_KEYS) {
            /* 45a55c: the first held twist/pitch key ends the scan. */
            uint32_t d=step(R8(table+2u*i));W32(block+8u,R32(block+8u)+d);
            d=step(R8(table+2u*i+1u));W32(block+0xcu,R32(block+0xcu)+d);
            break;
        }
        /* 45a51f: an exclusive pad key restarts turn and throttle. */
        if(R8(exclusive+i)) { W32(block+4u,0);W32(block,0); }
        uint32_t d=step(R8(table+2u*i));W32(block,R32(block)+d);
        d=step(R8(table+2u*i+1u));W32(block+4u,R32(block+4u)+d);
        /* 45a54c rereads the flag after the adds; EAX <- 7, then the
         * increment ends the pad keys. */
        if(R8(exclusive+i))i=PAD_KEYS-1;
    }
    /* 45a586: 4d23a2 is the last down byte (Space), 4d23a3 the first
     * released byte. */
    if(R8(VA(0x4d23a2u)))W8(block+0x10u,1);
    if(R8(VA(0x4d23a3u)))W8(block+0x11u,1);
    W8(block+0x13u,0);W8(block+0x12u,0);
    /* 45a5bb: a key that is down and was released before a frame saw it
     * is dropped now that it has been seen; the released byte stays. */
    for(uint32_t j=0;j<KEYS_ALL;++j) {
        if(!R8(down+j))continue;
        if(!R8(released+j))continue;
        W8(seen+j,0);W8(down+j,0);
    }
    /* EAX = the counter, EDX = the down pointer after 15 steps. */
    return pair(KEYS_ALL,down+KEYS_ALL);
}
