#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "es2/core/object_shapes.h"
/* Synthetic guest: an arena at 90000000h (objects, shapes, the frame
 * scratch) and a synthetic image base I whose globals 0x6c5830 (heap),
 * 0x6c56a8 (the global rotation) and 0x6cbee4 (the counter pointer) live
 * in a small block. Game calls are logged and answered by a stub: 49630c
 * writes the record's saved head, memset and memcpy act and return their
 * CW3220 registers (rtl_imports.md), every other call returns the next
 * value of an answer queue (or 1111xxxxh) in EAX and fixed EDX/ECX.
 * Expectations worked out by hand from the reference listing
 * (mission_load.md, section "object shape set-up"). */
#define ARENA UINT32_C(0x90000000)
#define I UINT32_C(0x10000000)
#define GLOBALS (I+0x2c5600u)
static uint8_t arena[0x2000],globals[0x6900];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"object_shapes line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *host(void *c,uint32_t a){
    (void)c;
    if(a-ARENA<sizeof(arena))return arena+(a-ARENA);
    if(a-GLOBALS<sizeof(globals))return globals+(a-GLOBALS);
    fprintf(stderr,"object_shapes: unmapped %08x\n",a);exit(1);
}
static uint16_t r16(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t r32(void *c,uint32_t a){uint8_t *p=host(c,a);return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void w16(void *c,uint32_t a,uint16_t v){uint8_t *p=host(c,a);p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(void *c,uint32_t a,uint32_t v){uint8_t *p=host(c,a);for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(i*8));}
static const es2_guest_memory memory={0,r32,r16,w32,w16,host};
static const es2_guest_memory *m=&memory;
#define VA(x) (I+(x)-0x400000u)
typedef struct call_record { uint32_t target,args[5],regs[3];unsigned n; } call_record;
static call_record log_[16];static unsigned calls,queued,taken;
static uint32_t queue[8];
static const uint32_t SCRATCH=ARENA+0x1f00;
#define FRAME (SCRATCH+ES2_OBJECT_SHAPES_FRAME)
static void stub(void *context,uint32_t target,const uint32_t *a,unsigned n,es2_weapon_registers *r){
    (void)context;CHECK(calls<16);call_record *v=log_+calls++;
    v->target=target;v->n=n;memcpy(v->args,a,4*n);v->regs[0]=r->eax;v->regs[1]=r->edx;v->regs[2]=r->ecx;
    if(target==VA(0x49630c)){w32(0,FRAME-0x24,0xfeedf00du);w16(0,FRAME-0x14,0);r->eax=0xfeedf00du;return;}
    if(target==VA(0x49681a)){for(uint32_t i=0;i<a[2];++i)*(uint8_t *)host(0,a[0]+i)=(uint8_t)a[1];r->eax=a[0];r->edx=a[2];r->ecx=0;return;}
    if(target==VA(0x49680e)){for(uint32_t i=0;i<a[2];++i)*(uint8_t *)host(0,a[0]+i)=*(uint8_t *)host(0,a[1]+i);
        r->eax=a[0];r->edx=0x2d2d0000u;r->ecx=0x3c3c0000u;return;}
    r->eax=taken<queued?queue[taken++]:0x11110000u+calls;r->edx=0x22220000u+calls;r->ecx=0x33330000u+calls;
}
static const es2_weapon_graph graph={I,SCRATCH,0,0,0,stub,NULL,NULL,NULL};
static const uint32_t entry[3]={0xabcd1234u,0x5678ef01u,0x0badf00du};
#define EAX(p) ((uint32_t)(p))
#define EDX(p) ((uint32_t)((p)>>32))
static void reset(void){memset(arena,0x5a,sizeof(arena));memset(globals,0,sizeof(globals));calls=queued=taken=0;
    w32(0,VA(0x6cbee4),ARENA+0x10);w32(0,ARENA+0x10,100);}
static void answers(unsigned n,const uint32_t *v){memcpy(queue,v,4*n);queued=n;taken=0;}
static uint32_t count(void){return r32(0,ARENA+0x10);}

/* ts_obj_ctor with a shape: the pose block zeroed, the vtables, new(42h),
 * anim_instance_ctor with the allocation, count +1 +1 -3 +1 = 100. */
static void ts_obj(void){
    const uint32_t O=ARENA+0x100,S=ARENA+0x400,X=ARENA+0x600;uint32_t ecx=0;
    reset();answers(1,&X);
    uint64_t p=es2_ts_obj_ctor_registers(m,&graph,O,S,entry,&ecx);
    CHECK(EAX(p)==O&&EDX(p)==ARENA+0x10&&ecx==0xfeedf00du&&calls==4);
    CHECK(log_[0].target==VA(0x49630c)&&log_[0].regs[0]==VA(0x497145)&&log_[0].regs[1]==entry[1]&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==VA(0x47749c)&&log_[1].n==1&&log_[1].args[0]==0x42&&log_[1].regs[0]==ARENA+0x10&&log_[1].regs[1]==0&&log_[1].regs[2]==0);
    CHECK(log_[2].target==VA(0x478838)&&log_[2].n==2&&log_[2].args[0]==X&&log_[2].args[1]==S);
    CHECK(log_[2].regs[0]==X&&log_[2].regs[1]==S&&log_[2].regs[2]==X);
    CHECK(log_[3].target==VA(0x49681a)&&log_[3].args[0]==O+0x38&&log_[3].args[2]==8&&log_[3].regs[0]==O+0x38&&log_[3].regs[1]==X);
    CHECK(log_[3].regs[2]==0x33330003u);
    CHECK(r32(0,O)==VA(0x4a0b80)&&r32(0,O+0xc)==0&&r16(0,O+0x10)==0&&r32(0,O+0x26)==0&&r32(0,O+0x2a)==0&&r32(0,O+0x2e)==0);
    CHECK(r16(0,O+0x32)==0&&r32(0,O+0x34)==X&&r32(0,O+0x38)==0x100&&r32(0,O+0x3c)==0&&count()==100);
    CHECK(r16(0,O+0x12)==0x5a5a&&r16(0,O+0x24)==0x5a5a&&r16(0,FRAME-0x14)==8&&r32(0,FRAME-4)==X);
    /* no shape: no allocation, O+34h = 0, count +3. */
    reset();p=es2_ts_obj_ctor_registers(m,&graph,O,0,entry,&ecx);
    CHECK(calls==2&&r32(0,O+0x34)==0&&count()==103&&log_[1].regs[0]==O+0x38&&log_[1].regs[1]==0&&log_[1].regs[2]==0);
    /* a failed allocation: O+34h = 0, no context word, ECX 42h at memset. */
    const uint32_t zero=0;reset();answers(1,&zero);
    p=es2_ts_obj_ctor_registers(m,&graph,O,S,entry,&ecx);
    CHECK(EAX(p)==O&&calls==3&&r32(0,O+0x34)==0&&count()==103&&ecx==0xfeedf00du);
    CHECK(log_[2].target==VA(0x49681a)&&log_[2].regs[1]==0&&log_[2].regs[2]==0x42&&r16(0,FRAME-0x14)==0);
}
/* The three DET_ANIM_OBJ constructors. */
static void det_anim(void){
    const uint32_t O=ARENA+0x100,S=ARENA+0x400,R=ARENA+0x500;uint32_t ecx=1;
    reset();uint64_t p=es2_det_anim_shape_registers(m,&graph,O,S,entry,&ecx);
    CHECK(EAX(p)==O&&EDX(p)==ARENA+0x10&&ecx==0&&calls==1&&log_[0].target==VA(0x402278)&&log_[0].args[0]==O&&log_[0].args[1]==S);
    CHECK(log_[0].regs[0]==S&&log_[0].regs[1]==entry[1]&&log_[0].regs[2]==entry[2]);
    CHECK(r32(0,O)==VA(0x4973cc)&&r32(0,O+0x41)==0&&count()==102);
    reset();ecx=1;p=es2_det_anim_static_registers(m,&graph,O,S,entry,&ecx);
    CHECK(EAX(p)==O&&ecx==0&&log_[0].target==VA(0x402188)&&r32(0,O)==VA(0x4973cc)&&count()==102);
    reset();w32(0,R,ARENA+0x520);w32(0,ARENA+0x520,S);
    p=es2_det_anim_record_registers(m,&graph,O,R,entry,&ecx);
    CHECK(EAX(p)==O&&EDX(p)==ARENA+0x10&&ecx==ARENA+0x10&&log_[0].target==VA(0x402278)&&log_[0].args[1]==S&&log_[0].regs[0]==S);
    CHECK(r32(0,O)==VA(0x4973cc)&&r32(0,O+0x41)==R&&count()==102);
}
/* anim_instance_ctor with a two-node shape: node 0 takes pose entry 1,
 * node 1 entry 0 of the shape's pose table. */
static void anim_instance(void){
    const uint32_t X=ARENA+0x100,S=ARENA+0x400,T=ARENA+0x500,P=ARENA+0x580,F=ARENA+0x600,A=ARENA+0x700,B=ARENA+0x800;
    uint32_t ecx=0;reset();
    w32(0,X+4,S);w32(0,S+0x2a,T);w16(0,T+0x16,2);w32(0,T+0x18,ARENA+0x5f0);w32(0,T+0x12,P);
    w16(0,ARENA+0x5f0,1);w16(0,ARENA+0x5f2,0);
    for(unsigned i=0;i<6;++i)w32(0,P+4*i,0x70000000u+i);
    const uint32_t blocks[4]={0x0f0f0f0fu,F,A,B};answers(4,blocks);   /* shape_instance, new[], 49667c twice */
    uint64_t p=es2_anim_instance_ctor_registers(m,&graph,X,S,entry,&ecx);
    CHECK(EAX(p)==X&&EDX(p)==ARENA+0x10&&ecx==0&&calls==7&&count()==101);
    CHECK(log_[0].target==VA(0x490b64)&&log_[0].args[0]==X&&log_[0].args[1]==S&&log_[0].regs[0]==S&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==VA(0x49681a)&&log_[1].args[0]==VA(0x6c56a8)&&log_[1].args[2]==0x20&&log_[1].regs[0]==0x0f0f0f0fu&&log_[1].regs[1]==0x22220001u);
    CHECK(r16(0,VA(0x6c56a8))==0x4000&&r16(0,VA(0x6c56ae))==0x4000&&r16(0,VA(0x6c56b8))==0x4000&&r16(0,VA(0x6c56aa))==0);
    CHECK(log_[2].target==VA(0x4774f0)&&log_[2].args[0]==2&&log_[2].regs[0]==2&&log_[2].regs[1]==T&&log_[2].regs[2]==0);
    CHECK(log_[3].target==VA(0x49667c)&&log_[3].n==5&&log_[3].args[1]==0xc&&log_[3].args[2]==2&&log_[3].args[4]==VA(0x4922e6));
    CHECK(log_[3].regs[0]==F&&log_[3].regs[1]==2&&log_[3].regs[2]==2);
    CHECK(log_[4].target==VA(0x49667c)&&log_[4].args[1]==0x20&&log_[4].args[4]==VA(0x495132)&&log_[4].regs[0]==A&&log_[4].regs[1]==0x22220004u);
    CHECK(log_[5].target==VA(0x478c64)&&log_[5].args[0]==X&&log_[5].regs[0]==2&&log_[5].regs[1]==F&&log_[5].regs[2]==2);
    CHECK(log_[6].target==VA(0x49681a)&&log_[6].args[0]==B&&log_[6].regs[2]==X);
    for(unsigned i=0;i<10;++i)CHECK(r32(0,X+0x1a+4*i)==0);
    CHECK(r32(0,X)==VA(0x4a14c4)&&r16(0,X+0xc)==2&&r32(0,X+0xe)==F&&r32(0,X+0x12)==A&&r32(0,X+0x16)==B);
    CHECK(r32(0,A)==0x70000003u&&r32(0,A+4)==0x70000004u&&r32(0,A+8)==0x70000005u&&r32(0,A+0xc)==0x70000000u&&r32(0,A+0x14)==0x70000002u);
    CHECK(arena[0x600]==1&&arena[0x601]==1&&arena[0x602]==0x5a);
    CHECK(r16(0,B)==0x4000&&r16(0,B+6)==0x4000&&r16(0,B+0x10)==0x4000&&r16(0,B+2)==0&&r32(0,B+0x1c)==0);
    /* no nodes: the loop does not run, node_matrices gets 49667c's EDX. */
    reset();w32(0,X+4,S);w32(0,S+0x2a,T);w16(0,T+0x16,0);answers(4,blocks);
    p=es2_anim_instance_ctor_registers(m,&graph,X,S,entry,&ecx);
    CHECK(calls==7&&log_[5].regs[0]==0&&log_[5].regs[1]==0x22220005u&&log_[5].regs[2]==0&&log_[2].args[0]==0);
}
/* object_track: the thread, its constructor with the shape tree, the clip
 * with the word in EAX's low half, the rate, the slot insert. */
static void object_track(void){
    const uint32_t O=ARENA+0x100,X=ARENA+0x300,S=ARENA+0x400,M=ARENA+0x500,T=ARENA+0x600;uint32_t ecx=0;
    reset();w32(0,O+0x34,X);w32(0,X+4,S);w32(0,S+0x2a,M);
    const uint32_t thread[1]={T};answers(1,thread);
    uint64_t p=es2_object_track_registers(m,&graph,O,0xffff0007u,entry,&ecx);
    CHECK(EAX(p)==T&&EDX(p)==0xfeedf00du&&ecx==0x33330006u&&calls==6);
    CHECK(log_[0].target==VA(0x49630c)&&log_[0].regs[0]==VA(0x497175));
    CHECK(log_[1].target==VA(0x47749c)&&log_[1].args[0]==0x52&&log_[1].regs[0]==0xfeedf00du&&log_[1].regs[1]==entry[1]);
    CHECK(log_[2].target==VA(0x478e48)&&log_[2].args[0]==T&&log_[2].args[1]==M&&log_[2].regs[0]==M&&log_[2].regs[1]==T&&log_[2].regs[2]==S);
    CHECK(log_[3].target==VA(0x4792ac)&&log_[3].n==4&&log_[3].args[0]==T&&log_[3].args[1]==0x11110007u&&log_[3].args[2]==0&&log_[3].args[3]==0);
    CHECK(log_[3].regs[0]==0x11110007u&&log_[3].regs[1]==0x22220003u);
    CHECK(log_[4].target==VA(0x4796d8)&&log_[4].args[0]==T&&log_[4].args[1]==0&&log_[4].regs[2]==0x33330004u);
    CHECK(log_[5].target==VA(0x478a3c)&&log_[5].args[0]==X&&log_[5].args[1]==T&&log_[5].regs[1]==X&&log_[5].regs[0]==0x11110005u);
    CHECK(r16(0,FRAME-0x14)==8&&count()==100);
    /* a failed allocation: no constructor, the null thread to the clip. */
    const uint32_t zero=0;reset();w32(0,O+0x34,X);answers(1,&zero);
    p=es2_object_track_registers(m,&graph,O,3,entry,&ecx);
    CHECK(EAX(p)==0&&calls==5&&log_[2].target==VA(0x4792ac)&&log_[2].args[0]==0&&log_[2].args[1]==3&&log_[2].regs[2]==0x52);
}
/* thread_add: the first free slot, then the sort; ten taken slots. */
static void thread_add(void){
    const uint32_t X=ARENA+0x300,T=ARENA+0x600;uint32_t ecx=0;
    reset();for(unsigned i=0;i<10;++i)w32(0,X+0x1a+4*i,i<3?ARENA+0x700+i:0);
    uint64_t p=es2_thread_add_registers(m,&graph,X,T,entry,&ecx);
    CHECK(EAX(p)==1&&EDX(p)==0x22220001u&&ecx==X&&calls==1&&r32(0,X+0x26)==T&&r32(0,X+0x2a)==0);
    CHECK(log_[0].target==VA(0x4789d0)&&log_[0].args[0]==X&&log_[0].regs[0]==X+0x26&&log_[0].regs[1]==3&&log_[0].regs[2]==X);
    reset();for(unsigned i=0;i<10;++i)w32(0,X+0x1a+4*i,ARENA+0x700+i);
    p=es2_thread_add_registers(m,&graph,X,T,entry,&ecx);
    CHECK(EAX(p)==0&&EDX(p)==10&&ecx==X&&calls==0);
}
/* thread_ctor: the manager, the clip, the cleared state with the three
 * diagonal words. */
static void thread_ctor(void){
    const uint32_t T=ARENA+0x600,M=ARENA+0x500;uint32_t ecx=0;
    reset();uint64_t p=es2_thread_ctor_registers(m,&graph,T,M,entry,&ecx);
    CHECK(EAX(p)==T&&EDX(p)==0x20&&ecx==0x4000&&calls==2&&r32(0,T)==M);
    CHECK(log_[0].target==VA(0x4792ac)&&log_[0].n==4&&log_[0].args[0]==T&&log_[0].args[1]==0&&log_[0].regs[0]==T+0x2e);
    CHECK(log_[0].regs[1]==M&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==VA(0x49681a)&&log_[1].args[0]==T+0x2e&&log_[1].regs[0]==0x11110001u);
    CHECK(r16(0,T+0x2e)==0x4000&&r16(0,T+0x34)==0x4000&&r16(0,T+0x3e)==0x4000&&r16(0,T+0x30)==0&&r32(0,T+0x4a)==0&&r16(0,T+0x4e)==0x5a5a);
}
/* shape_word_block: three words copied into an arena block; an empty
 * array; no shape. */
static void word_block(void){
    const uint32_t R=ARENA+0x100,S=ARENA+0x400,W=ARENA+0x480,C=ARENA+0x700;uint32_t ecx=0;
    reset();w32(0,VA(0x6c5830),0x12345678u);w16(0,S+0x24,3);w32(0,S+0x1c,W);
    for(unsigned i=0;i<3;++i)w16(0,W+2*i,(uint16_t)(0x100+i));
    answers(1,&C);
    uint64_t p=es2_shape_word_block_registers(m,&graph,R,S,entry,&ecx);
    CHECK(EAX(p)==R&&EDX(p)==ARENA+0x10&&ecx==0x3c3c0000u&&calls==2&&count()==101);
    CHECK(log_[0].target==VA(0x47a288)&&log_[0].args[0]==6&&log_[0].args[1]==0x12345678u&&log_[0].regs[0]==0x12345678u);
    CHECK(log_[0].regs[1]==6&&log_[0].regs[2]==entry[2]);
    CHECK(log_[1].target==VA(0x49680e)&&log_[1].args[0]==C&&log_[1].args[1]==W&&log_[1].args[2]==6&&log_[1].regs[0]==S&&log_[1].regs[2]==W);
    CHECK(r32(0,R)==S&&r32(0,R+4)==C&&r16(0,C)==0x100&&r16(0,C+4)==0x102&&r16(0,C+6)==0x5a5a);
    reset();w16(0,S+0x24,0);p=es2_shape_word_block_registers(m,&graph,R,S,entry,&ecx);
    CHECK(calls==0&&r32(0,R+4)==0&&ecx==entry[2]&&count()==101);
    reset();p=es2_shape_word_block_registers(m,&graph,R,0,entry,&ecx);
    CHECK(calls==0&&r32(0,R)==0&&r32(0,R+4)==0x5a5a5a5au&&EAX(p)==R);
}
int main(void){
    ts_obj();det_anim();anim_instance();object_track();thread_add();thread_ctor();word_block();
    printf("PASS: object_shapes core, %u checks\n",checks);
    return 0;
}
