#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "es2/core/mission_end.h"
/* Synthetic state for the core (the mission end 461ff0). The image
 * base is placed so that the globals 4a9e52, 4d1dd6, the run block
 * 4d24bc..4d2530 and the exit code 4d27b8 fall inside the arena; the object
 * that results_writer receives (and whose byte +99h the exit code reads) is
 * placed above them. A logging call function stands in for both game calls
 * (target, argument, entry EAX/EDX/ECX, and the three globals the original
 * would have written by then); it returns fixed registers and, in two
 * tests, changes a global like results_writer could. Expected values are
 * worked out by hand from the contract in launcher.md. */
#define BASE UINT32_C(0x90000000)
#define IMAGE (BASE+0x1000u-0x4a9e52u+0x400000u)
#define VA(x) (IMAGE+(uint32_t)(x)-0x400000u)
#define PLAYER_OBJECT (BASE+0x2a0000u)
static uint8_t arena[0x2a1000];static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"mission_end line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){(void)c;CHECK(a-BASE<sizeof(arena));return arena+(a-BASE);}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
#define G32(va) r32(0,VA(va))
typedef struct call { uint32_t va,count,arg,eax,edx,ecx,flag,dump,tape; } call;
static call calls[8];static unsigned call_count,observed,observed_slots;
static uint32_t on_results_quit;      /* nonzero: results_writer sets the quit word to it */
static uint32_t on_results_player;    /* nonzero: results_writer repoints the object dword */
static void fake_call(void *c,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)c;CHECK(call_count<8);call *k=calls+call_count++;memset(k,0,sizeof(*k));
    k->va=f-IMAGE+0x400000u;k->count=n;k->arg=a[0];k->eax=r->eax;k->edx=r->edx;k->ecx=r->ecx;
    k->flag=*(uint8_t *)host(0,VA(0x4d1dd6u));k->dump=G32(0x4d24deu);k->tape=G32(0x4d24e2u);
    if(k->va==0x42412cu){
        if(on_results_quit)w16(0,VA(0x4d24feu),(uint16_t)on_results_quit);
        if(on_results_player)w32(0,VA(0x4d24e6u),on_results_player);
    }
    r->eax=0xa0u+call_count;r->edx=0xd0u+call_count;r->ecx=0xc0u+call_count;
}
static void fake_observe(void *c,uint32_t f,unsigned slot){(void)c;(void)f;++observed;if(slot)++observed_slots;}
static const es2_weapon_graph graph={IMAGE,0,0,0,0,fake_call,0,fake_observe,0};
static const uint32_t entry[3]={0xe0,0xe1,0xe2};
/* State: tape/dump stream words, demo, quit, the object's byte +99h, bank. */
static void setup(uint32_t tape,uint32_t dump,unsigned demo,unsigned quit,unsigned live,unsigned bank){
    memset(arena,0xcc,sizeof(arena));call_count=observed=observed_slots=0;on_results_quit=on_results_player=0;
    *(uint8_t *)host(0,VA(0x4d1dd6u))=0x77;
    w32(0,VA(0x4d24deu),dump);w32(0,VA(0x4d24e2u),tape);w32(0,VA(0x4d24e6u),PLAYER_OBJECT);
    w16(0,VA(0x4d24feu),(uint16_t)quit);w16(0,VA(0x4d2530u),(uint16_t)demo);
    *(uint8_t *)host(0,PLAYER_OBJECT+0x99u)=(uint8_t)live;
    w16(0,VA(0x4a9e52u),(uint16_t)bank);w32(0,VA(0x4d27b8u),0x55555555u);
}
static uint64_t run(uint32_t *ecx){return es2_mission_end_registers(&memory,&graph,entry,ecx);}

static void test_no_tape_exit_codes(void){
    /* demo, quit, live, bank -> code; EAX/EDX/ECX after results_writer's
     * registers (0xa1/0xd1/0xc1): demo keeps them, quit clears ECX, the
     * third path sets EAX = object + 92h and, with the byte set, EDX = bank
     * == 0, and byte clear leaves EDX. */
    for(unsigned bits=0;bits<16;++bits){
        const unsigned demo=bits&1,quit=bits>>1&1,live=bits>>2&1,bank=bits>>3&1;
        setup(0,0x1234,demo,quit,live?0x40:0,bank?0x9000:0);
        uint32_t ecx;uint64_t v=run(&ecx);
        uint32_t want=demo?6:quit?0:live&&bank?4:3;
        CHECK(G32(0x4d27b8u)==want);
        CHECK(call_count==1 && calls[0].va==0x42412cu && calls[0].count==1 && calls[0].arg==PLAYER_OBJECT);
        CHECK(calls[0].eax==0xe0u && calls[0].edx==PLAYER_OBJECT && calls[0].ecx==0xe2u);
        CHECK(calls[0].flag==0 && calls[0].dump==0x1234u && calls[0].tape==0);   /* no tape: the dump word stays */
        CHECK(G32(0x4d24deu)==0x1234u);
        CHECK(observed==1 && observed_slots==0);
        const uint32_t eax=0xa1,edx=0xd1;
        if(demo){ CHECK((uint32_t)v==eax && (uint32_t)(v>>32)==edx); CHECK(ecx==PLAYER_OBJECT); }
        else if(quit){ CHECK((uint32_t)v==eax && (uint32_t)(v>>32)==edx); CHECK(ecx==0); }
        else {
            CHECK((uint32_t)v==PLAYER_OBJECT+0x92u);
            CHECK((uint32_t)(v>>32)==(live?(bank?0u:1u):edx));
            CHECK(ecx==PLAYER_OBJECT);
        }
    }
}
static void test_tape_and_dump(void){
    setup(0x2000,0x3000,0,0,1,1);
    uint32_t ecx;uint64_t v=run(&ecx);
    CHECK(call_count==3 && observed==3);
    /* fclose(dump): EAX = the dump stream, EDX/ECX the entry's; the flag is already clear. */
    CHECK(calls[0].va==0x496838u && calls[0].count==1 && calls[0].arg==0x3000u);
    CHECK(calls[0].eax==0x3000u && calls[0].edx==0xe1u && calls[0].ecx==0xe2u);
    CHECK(calls[0].flag==0 && calls[0].dump==0x3000u && calls[0].tape==0x2000u);
    /* fclose(tape): the dump word is already zero, EDX = 0, EAX = the first call's. */
    CHECK(calls[1].va==0x496838u && calls[1].arg==0x2000u);
    CHECK(calls[1].eax==0xa1u && calls[1].edx==0u && calls[1].ecx==0x2000u);
    CHECK(calls[1].dump==0 && calls[1].tape==0x2000u);
    /* results_writer: EAX = 0, EDX = the object, ECX = the slot after the second close; both words zero. */
    CHECK(calls[2].va==0x42412cu && calls[2].arg==PLAYER_OBJECT);
    CHECK(calls[2].eax==0u && calls[2].edx==PLAYER_OBJECT && calls[2].ecx==0x2000u);
    CHECK(calls[2].dump==0 && calls[2].tape==0);
    CHECK(G32(0x4d24deu)==0 && G32(0x4d24e2u)==0 && G32(0x4d27b8u)==4u);
    CHECK((uint32_t)v==PLAYER_OBJECT+0x92u && (uint32_t)(v>>32)==0u && ecx==PLAYER_OBJECT);
}
static void test_tape_without_dump(void){
    setup(0x2000,0,0,0,0,0);
    uint32_t ecx;uint64_t v=run(&ecx);
    CHECK(call_count==2 && calls[0].va==0x496838u && calls[0].arg==0x2000u);
    /* The dump test leaves EAX = 0 (the loaded dword); EDX and ECX are the entry's / the tape word. */
    CHECK(calls[0].eax==0u && calls[0].edx==0xe1u && calls[0].ecx==0x2000u);
    CHECK(calls[1].va==0x42412cu && calls[1].eax==0u && calls[1].edx==PLAYER_OBJECT);
    CHECK(G32(0x4d24e2u)==0 && G32(0x4d27b8u)==3u);
    CHECK((uint32_t)v==PLAYER_OBJECT+0x92u && (uint32_t)(v>>32)==0xd2u && ecx==PLAYER_OBJECT);
}
static void test_exit_code_read_after_results_writer(void){
    /* The quit word and the object dword are read after results_writer. */
    setup(0,0,0,0,1,1);on_results_quit=1;
    uint32_t ecx;run(&ecx);
    CHECK(G32(0x4d27b8u)==0 && ecx==0);
    setup(0,0,0,0,0,1);on_results_player=PLAYER_OBJECT+0x100u;
    *(uint8_t *)host(0,PLAYER_OBJECT+0x100u+0x99u)=1;
    uint64_t v=run(&ecx);
    CHECK(G32(0x4d27b8u)==4u && (uint32_t)v==PLAYER_OBJECT+0x100u+0x92u && calls[0].arg==PLAYER_OBJECT);
}
static void test_word_widths(void){
    /* The tests are on words (demo, quit, bank) and a dword (tape, dump): bytes above do not count. */
    setup(0,0,0,0,1,0);w32(0,VA(0x4d2530u),0x10000u);w32(0,VA(0x4d24feu),0x20000u);w32(0,VA(0x4a9e52u),0x30000u);
    uint32_t ecx;run(&ecx);
    CHECK(G32(0x4d27b8u)==3u);          /* bank word 0 -> the byte-set path answers 3 */
    setup(0x10000,0,0,0,0,0);
    run(&ecx);CHECK(call_count==2);     /* the tape dword (not its low word) decides */
}
int main(void){
    test_no_tape_exit_codes();test_tape_and_dump();test_tape_without_dump();
    test_exit_code_read_after_results_writer();test_word_widths();
    printf("PASS: mission_end %u checks\n",checks);
    return 0;
}
