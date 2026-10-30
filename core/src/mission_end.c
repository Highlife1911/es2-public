#include "es2/core/mission_end.h"
/* The mission end 461ff0 (docs/reverse-engineering/launcher.md,
 * section "the mission end `0x461ff0` as own code"). The register file r
 * follows the original's EAX/EDX/ECX at every game call and at the RET. */
#define R8(a) (*(const uint8_t *)m->host(m->context,(a)))
#define W8(a,v) (*(uint8_t *)m->host(m->context,(a))=(uint8_t)(v))
#define R16(a) m->read16(m->context,(a))
#define R32(a) m->read32(m->context,(a))
#define W32(a,v) m->write32(m->context,(a),(uint32_t)(v))
#define VA(va) (g->image+(uint32_t)(va)-0x400000u)
/* Globals; the run block is the base EBX = 4d24bc of the function. */
#define FLAG VA(0x4d1dd6u)    /* byte, cleared first */
#define DUMP VA(0x4d24deu)    /* block +22h: the dump stream (FILE *) */
#define TAPE VA(0x4d24e2u)    /* block +26h: the tape stream */
#define PLAYER VA(0x4d24e6u)  /* block +2Ah: the object results_writer reads */
#define QUIT VA(0x4d24feu)    /* block +42h: quit word */
#define DEMO VA(0x4d2530u)    /* block +74h: demo word */
#define CODE VA(0x4d27b8u)    /* the process exit code */
#define BANK VA(0x4a9e52u)    /* word read by the exit-code test */
enum { K_FCLOSE=0x496838u, K_RESULTS=0x42412cu };

/* One one-argument game call at the original's registers; the POP ECX that
 * follows it rereads the argument slot. */
static void me_call(const es2_weapon_graph *g,es2_weapon_registers *r,uint32_t va,uint32_t argument) {
    uint32_t a[1]={argument};
    if(g->observe)g->observe(g->observer_context,va,0);
    g->call(g->context,VA(va),a,1,r);
    r->ecx=a[0];
}

/* 461ff0..462099. */
uint64_t es2_mission_end_registers(const es2_guest_memory *m,const es2_weapon_graph *g,
    const uint32_t entry[3],uint32_t *ecx) {
    es2_weapon_registers r={entry[0],entry[1],entry[2]};
    W8(FLAG,0);                                                      /* 461ff6 */
    if(R32(TAPE)) {                                                  /* 461ffd */
        r.eax=R32(DUMP);                                             /* 462003 */
        if(r.eax) {
            me_call(g,&r,K_FCLOSE,r.eax);                            /* 46200b */
            r.edx=0;W32(DUMP,0);                                     /* 462011 */
        }
        r.ecx=R32(TAPE);                                             /* 462016 */
        me_call(g,&r,K_FCLOSE,r.ecx);                                /* 46201a */
        r.eax=0;W32(TAPE,0);                                         /* 462020 */
    }
    r.edx=R32(PLAYER);                                               /* 462025 */
    me_call(g,&r,K_RESULTS,r.edx);                                   /* 462029 */
    uint32_t code;
    if(R16(DEMO))code=6;                                             /* 46202f */
    else if(R16(QUIT)) {                                             /* 462042 */
        r.ecx=0;code=0;
    } else {
        r.eax=R32(PLAYER)+0x92u;                                     /* 462053 */
        code=3;
        if(R8(r.eax+7u)) {                                           /* 46205b */
            const uint32_t zero=R16(BANK)==0;                        /* 462061 */
            r.edx=zero;                                              /* sete dl; and edx,1 */
            if(!zero)code=4;
        }
    }
    W32(CODE,code);
    /* 46208e, 462093: calls of the bare RETs 426514 and 425344 (no effect). */
    *ecx=r.ecx;
    return (uint64_t)r.edx<<32|r.eax;
}
