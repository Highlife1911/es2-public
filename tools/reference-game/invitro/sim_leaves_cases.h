/* sim_leaves group: the last simulation leaves, 17 hooks
 * (docs/reverse-engineering/vtables.md, "simulation leaves"): part_shapes_bind
 * 4030d0, parts_intact 403668, formation_slot 405b9c, explosion_entry 407b20,
 * flat_draw 40991c, projectile_draw 40a120, beam_done 40c2a0, weapon_by_mount
 * 410670, ptr_list_add 411dd4, box_square_init 413a90, twist_sound_latch
 * 41a6d0, pitch_sound_latch 41a994, mech_type_index 41ff00, flyer_type_index
 * 422e80, cockpit_notice 420a68, radio_notice 420a98 and notice_time 426aec.
 * Identifiers sml_/SML_.
 *
 * Direct cases run the original body, the adapter and the core on the same
 * generated state (an arena of 6 KiB with random bytes: the lists, records,
 * objects, child with two vtables, main object, event port with its vtable;
 * the game globals the functions read or write are saved, randomised and
 * compared as windows) against logging stubs for every game callee:
 * terrain_pose 4029d8, shape_actor_matrix 401fe4, timer_word_step 467a48,
 * sfx_playing 462af0 and port_a 433158 (patched over the original), the
 * child's virtual slot +1Ch (two arena vtables, so the table read again is
 * observable) and the event port's slot 0. Each stub logs its id,
 * arguments, the slot count the caller pushed, the entry EAX/EDX/ECX
 * (values that point into the stack, i.e. the event record, are masked) and
 * a hash of the arena and the global windows (so writes before and after a
 * call are ordered), answers EDX:EAX and ECX from a per-case stream (the
 * answers the code tests are planned: a zero word, a zero EAX, a port) and in
 * some cases pokes a word that the original reads again after the call (the
 * child pointer, the child's vtable, the global 4a5b08). The event stub also
 * logs the first six bytes of the record (code word and second dword); the
 * 18 residue bytes the original never writes are not compared (accepted
 * presentation residue, documented). Compared: EDX:EAX of the original, the
 * adapter and the core, ECX of the original and the core, the logged calls of
 * all three, the arena and the global windows, and the adapter's callee
 * counter deltas. Then installed 31/a7 poison probes (fast and preserving
 * dispatch, nonempty x87) and a real-callee pass: timer_word_step with
 * its hook in rotating modes (beam_done), sfx_playing with the sound device
 * absent (both latches) and port_a (radio_notice), the hook original or
 * replaced. flat_draw and projectile_draw have no real pass: their callees
 * 401fe4 and 4029d8 draw through the matrix and terrain code, which faults on
 * random object bytes and is the subject of the shape_draw and node groups.
 * The six hooks that accept compare mode also run the compare
 * probes (compare_probes.h). */
typedef struct sml_record { uint32_t id,args[2],regs[3],hash,count,rec0,rec1; } sml_record;
enum { SML_SIZE=0x1800, SML_O=0x000, SML_C=0x400, SML_C2=0x440, SML_VT1=0x480, SML_VT2=0x4c0, SML_M=0x500, SML_P=0x740, SML_VP=0x750,
    SML_X=0x800, SML_Y=0x1000, SML_MAX=8, SML_IDS=8, SML_GLOBALS=60 };
#define SML_BASE ((uint32_t)(uintptr_t)sml_arena)
static BYTE sml_arena[SML_SIZE],sml_before[SML_SIZE],sml_after[SML_SIZE];
static sml_record sml_log[SML_MAX];
static unsigned sml_calls,sml_poke_at;
static uint32_t sml_seed,sml_stream,sml_plan,sml_poke_address,sml_poke_value,sml_poke_size;
__attribute__((used)) static uint32_t sml_ecx;
/* Stub ids: 0 terrain_pose 4029d8, 1 shape_actor_matrix 401fe4, 2
 * timer_word_step 467a48, 3 sfx_playing 462af0, 4 port_a 433158, 5 and 6 the
 * child's slot +1Ch (vtable 1, vtable 2), 7 the event port's slot 0. */
static const uint32_t sml_addresses[SML_IDS]={0x4029d8,0x401fe4,0x467a48,0x462af0,0x433158,0,0,0};
static const unsigned sml_argc[SML_IDS]={1,1,1,1,0,1,1,2};
/* The game globals the functions touch: the formation cell with its flag
 * (and the dword before it), the projectile word pair, the latch cells, the
 * main object, the formation and explosion tables, the notice flag, the
 * sound device and its slot 21h, the frame time word. */
static const struct { uint32_t va; unsigned len; } sml_windows[]={{0x4975d2,18},{0x4a5b04,8},{0x49a1c8,8},{0x49b088,4},{0x4a95ac,4},
    {0x4a9628,4},{0x4a9e5c,4},{0x49f8ec,4},{0x4d2cfc,4},{0x4d3b64,2}};
#define SML_WINDOWS (sizeof(sml_windows)/sizeof(sml_windows[0]))
/* SML_GLOBALS is the sum of the window lengths (18+8+8+4+4+4+4+4+4+2). */
static BYTE *sml_global(uint32_t va){return mapped+(va-0x400000u);}
static void sml_globals(BYTE *out,int restore){
    unsigned off=0;
    for(unsigned i=0;i<SML_WINDOWS;++i){
        if(restore)memcpy(sml_global(sml_windows[i].va),out+off,sml_windows[i].len);else memcpy(out+off,sml_global(sml_windows[i].va),sml_windows[i].len);
        off+=sml_windows[i].len;
    }
}
static uint32_t sml_next(void){
    uint32_t z=sml_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static uint32_t sml_hash(void){
    uint32_t h=0x811c9dc5u;
    for(unsigned i=0;i<SML_SIZE;i+=4)h=(h^get32(sml_arena+i))*0x01000193u;
    for(unsigned i=0;i<SML_WINDOWS;++i)for(unsigned j=0;j<sml_windows[i].len;++j)h=(h^sml_global(sml_windows[i].va)[j])*0x01000193u;
    return h;
}
static int sml_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
__attribute__((used,noinline)) static uint64_t __cdecl sml_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(sml_calls==SML_MAX){fail("sim_leaves synthetic call overflow");ExitProcess(1);}
    sml_record *v=sml_log+sml_calls++;memset(v,0,sizeof(*v));v->id=id;
    memcpy(v->args,a,4*sml_argc[id]);memcpy(v->regs,regs,12);
    v->count=weapons_bridge_slots(a,sml_argc[id]);v->hash=sml_hash();
    if(id==7){const BYTE *record=(const BYTE *)(uintptr_t)a[1];v->rec0=get16(record);v->rec1=get32(record+2);}
    for(unsigned j=0;j<3;++j)if(sml_stack(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<sml_argc[id];++j)if(sml_stack(v->args[j]))v->args[j]=0;
    uint32_t eax=sml_next(),edx=sml_next();sml_ecx=sml_next();
    if(id==2){if(sml_plan&1u)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;}
    else if(id==3){
        switch(sml_plan>>1&3u){case 0:eax=0;break;case 1:eax=(eax|0x10000u)&0xffff0000u;break;default:eax|=1u;break;}
    } else if(id==4)eax=SML_BASE+SML_P;
    if(sml_poke_address&&sml_poke_at==sml_calls-1){
        if(sml_poke_size==2)put16((BYTE *)(uintptr_t)sml_poke_address,(uint16_t)sml_poke_value);
        else put32((BYTE *)(uintptr_t)sml_poke_address,sml_poke_value);
    }
    return (uint64_t)edx<<32|eax;
}
#define SML_STUB(n) __attribute__((naked)) static void sml_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _sml_stub_impl\n\taddl $24,%esp\n\tmovl _sml_ecx,%ecx\n\tret\n\t");}
SML_STUB(0)
SML_STUB(1)
SML_STUB(2)
SML_STUB(3)
SML_STUB(4)
SML_STUB(5)
SML_STUB(6)
SML_STUB(7)
#undef SML_STUB
static void (*const sml_stubs[SML_IDS])(void)={sml_stub_0,sml_stub_1,sml_stub_2,sml_stub_3,sml_stub_4,sml_stub_5,sml_stub_6,sml_stub_7};
#define SML_IS(name) (index==ES2_HOOK_INDEX_##name)
/* The patched callees of a hook (bit i = stub id i); the real pass leaves
 * the callee that is cheap to run for real unpatched. */
static unsigned sml_mask(unsigned index,int real){
    unsigned m=0;
    if(SML_IS(flat_draw))m=1u<<0|1u<<1;
    else if(SML_IS(projectile_draw))m=1u<<1;
    else if(SML_IS(beam_done))m=1u<<2;
    else if(SML_IS(twist_sound_latch)||SML_IS(pitch_sound_latch))m=1u<<3;
    else if(SML_IS(radio_notice))m=1u<<4;
    if(real){
        if(SML_IS(beam_done))m&=~(1u<<2);
        else if(SML_IS(twist_sound_latch)||SML_IS(pitch_sound_latch))m&=~(1u<<3);
        else if(SML_IS(radio_notice))m&=~(1u<<4);
    }
    return m;
}
static int sml_has_real(unsigned index){
    return SML_IS(beam_done)||SML_IS(twist_sound_latch)||SML_IS(pitch_sound_latch)||SML_IS(radio_notice);
}
static int sml_patch(BYTE saved[SML_IDS][5],int on,unsigned mask){
    for(unsigned i=0;i<SML_IDS;++i){
        if(!sml_addresses[i]||!(mask>>i&1u))continue;
        if(actor_patch(sml_addresses[i]-0x400000u,on?(void *)sml_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    }
    return 0;
}
static void sml_reset(void){sml_calls=0;sml_stream=sml_seed;}
static BYTE *sml_at(uint32_t offset){return sml_arena+offset;}
/* A 32-bit value whose low word runs through the boundary values first, the
 * high word zero or random. */
static uint32_t sml_value(unsigned n){
    static const uint32_t w[]={0,1,2,0xc0,0xc1,0xbf,0xffff,0xff40,0xff3f,0x8000,0x7fff,0x8001,0x100,0xffc0,0xff00,0x80,0xff80,0x7f,3,0xfffe};
    const unsigned count=sizeof(w)/sizeof(w[0]);
    uint32_t v=n<count?w[n]:random32()%4==0?random32():w[random32()%count];
    if(n>=count&&random32()%3==0)v|=random32()&0xffff0000u;
    return v;
}
/* The child of the object at SML_O (flat_draw, projectile_draw): [O+34h] =
 * C, [C] = vtable 1 or 2, [vtable+1Ch] the matching slot stub. */
static void sml_child(void){
    const uint32_t b=SML_BASE;
    put32(sml_at(SML_O+0x34),b+SML_C);put32(sml_at(SML_C),random32()%2?b+SML_VT1:b+SML_VT2);put32(sml_at(SML_C2),b+SML_VT2);
}
/* One state for hook index (case n). real: valid inputs for the real
 * callees and no pokes. */
static void sml_build(unsigned index,unsigned n,int real,uint32_t a[6],uint32_t r[3]){
    static const uint16_t words[]={0,1,2,3,0x7fff,0x8000,0xffff,0xfffe,0x100,0x1234,0x80,0xff80,0x7f,0xc0,0xff40};
    const uint32_t b=SML_BASE;
    sml_seed=random32();sml_plan=random32();random_bytes(sml_arena,SML_SIZE);memset(a,0,24);
    for(unsigned i=0;i<SML_WINDOWS;++i)random_bytes(sml_global(sml_windows[i].va),sml_windows[i].len);
    for(unsigned i=0;i<3;++i)r[i]=n<8?sml_value(n+i):random32();
    sml_poke_address=0;sml_poke_at=0;sml_poke_value=random32();sml_poke_size=4;
    BYTE *O=sml_at(SML_O);
    if(SML_IS(part_shapes_bind)){
        /* A at O, B at O+20h; slots at X, cells at X+40h, source records at
         * Y+200h, B entries at X+80h, destination records at Y+400h, the
         * pool at Y. */
        static const unsigned counts[]={0,1,2,3,4,5,6,0,6,1};
        const unsigned na=n<10?counts[n]:random32()%7;
        unsigned nb=n<10?counts[(n+3)%10]:random32()%8;
        const unsigned bigb=random32()%6;
        BYTE *A=O,*B=O+0x20;
        put32(A,b+SML_X);put16(A+4,(uint16_t)na);put32(B,b+SML_X+0x80);
        put16(B+4,(uint16_t)(bigb==0?0x7fffu:bigb==1?0x8000u:bigb==2?0xffffu:nb));
        for(unsigned i=0;i<6;++i){
            const uint32_t cell=b+SML_X+0x40+8u*(random32()%10==0&&i?i-1:i),record=b+SML_Y+0x200+0x10u*i;
            put32(sml_at(SML_X+4*i),random32()%4==0?0:cell);
            put32(sml_at(cell-b),record);
            put16(sml_at(SML_Y+0x200+0x10*i+4),(uint16_t)random32());put16(sml_at(SML_Y+0x200+0x10*i+6),(uint16_t)random32());
        }
        for(unsigned j=0;j<8;++j){
            uint32_t d=b+SML_Y+0x400+0x10u*j;
            if(random32()%5==0){const unsigned k=random32()%6;d=b+SML_Y+0x200+0x10u*k+(random32()%3==0?0u:random32()%2?2u:0xfffffffeu);}
            put32(sml_at(SML_X+0x80+4*j),random32()%10<4?0:d);
        }
        a[0]=b+SML_O;a[1]=b+SML_O+0x20;a[2]=b+SML_Y;
    } else if(SML_IS(parts_intact)){
        /* [O+1F2h] = header H at X (count word +12h, record array +14h),
         * [O+205h] = the word array at Y (stride 0Bh); records at X+100h
         * (stride 1Eh). */
        static const uint16_t counts[]={0,1,2,3,8,0xffffu,0x8000u,4,5,6,7};
        uint16_t count=n<11?counts[n]:random32()%10==0?counts[5+random32()%2]:(uint16_t)(random32()%9);
        const int ok=random32()%2;
        put32(O+0x1f2,b+SML_X);put32(O+0x205,b+SML_Y);
        put16(sml_at(SML_X+0x12),count);put32(sml_at(SML_X+0x14),b+SML_X+0x100);
        for(unsigned i=0;i<8;++i){
            BYTE *rec=sml_at(SML_X+0x100+0x1e*i),*cur=sml_at(SML_Y+0x0b*i);
            const uint16_t cap=random32()%3?(uint16_t)(random32()%200):words[random32()%15];
            put16(rec,cap);
            put16(rec+6,random32()%3==0?(uint16_t)(0x8000u|random32()):(uint16_t)(random32()&0x7fffu));
            put16(cur,ok&&random32()%8?(uint16_t)(cap+random32()%3):random32()%2?(uint16_t)(cap-1u-random32()%3):words[random32()%15]);
        }
        const unsigned alias=random32()%16;
        if(alias==0)put32(O+0x205,b+SML_O+0x1f0);                  /* the words over the header pointer */
        else if(alias==1)put32(sml_at(SML_X+0x14),b+SML_X);          /* the records over the header */
        a[0]=b+SML_O;
    } else if(SML_IS(formation_slot)){
        /* [4a95ac] = T (X+100h); row slots T+1Ch*row+4 point to entry
         * arrays at Y+100h*(row+2)+80h; the entry is E+0Ah*col-0Ah. */
        const int row=n<6?(int)n-2:(int)(random32()%6)-2,col=n<7?(int)n-2:(int)(random32()%7)-2;
        const uint32_t T=b+SML_X+0x100;
        put32(sml_global(0x4a95ac),T);
        for(int rr=-2;rr<=3;++rr)put32(sml_at(SML_X+0x100+28*rr+4),b+SML_Y+0x100*(uint32_t)(rr+2)+0x80);
        static const BYTE flags[]={0,1,2,0xff,0};
        *sml_global(0x4975e2)=random32()%3?flags[random32()%5]:(BYTE)random32();
        if(random32()%16==0){                                   /* the entry over the cell: the second dword read after the store */
            const uint32_t base=(uint32_t)(uintptr_t)mapped,entry=base+0x4975d2u-0x400000u;
            put32(sml_at(SML_X+0x100+28*row+4),entry-(uint32_t)(10*col-10));
        }
        a[1]=(uint32_t)row|(random32()%2?random32()&0xffff0000u:0);a[2]=(uint32_t)col|(random32()%2?random32()&0xffff0000u:0);
        a[0]=random32();
    } else if(SML_IS(explosion_entry)){
        static const BYTE bytes[]={0,1,0x7f,0x80,0xff,0x40,0xc0};
        O[0x41]=n<7?bytes[n]:random32()%2?bytes[random32()%7]:(BYTE)random32();
        if(random32()%4==0)put32(sml_global(0x4a9628),random32()%2?0xffffffffu:0x7fffffffu);
        a[0]=b+SML_O;
    } else if(SML_IS(flat_draw)||SML_IS(projectile_draw)){
        sml_child();a[0]=b+SML_O;
        if(!real&&random32()%3==0){
            sml_poke_at=random32()%2;
            switch(random32()%3){
            case 0:sml_poke_address=b+SML_O+0x34;sml_poke_value=b+SML_C2;break;                 /* the child pointer */
            case 1:sml_poke_address=b+SML_C;sml_poke_value=get32(sml_at(SML_C))==b+SML_VT1?b+SML_VT2:b+SML_VT1;break;   /* its vtable */
            default:sml_poke_address=SML_IS(projectile_draw)?(uint32_t)(uintptr_t)sml_global(0x4a5b08):b+SML_C2;sml_poke_value=random32();break;
            }
        }
    } else if(SML_IS(beam_done)){
        a[0]=b+SML_O;
        if(real){put16(O+0x5d,(uint16_t)(random32()%4==0?random32():random32()%5));put16(sml_global(0x4d3b64),(uint16_t)(random32()%4==0?random32():random32()%5));}
    } else if(SML_IS(weapon_by_mount)){
        /* L at O: array pointer +4, count word +8; entries at Y (40h each),
         * definitions at Y+400h (byte +17h). */
        static const uint16_t counts[]={0,1,2,3,4,5,6,7,8,0xffffu,0x8000u};
        put32(O+4,b+SML_X);put16(O+8,n<11?counts[n]:random32()%12==0?counts[9+random32()%2]:(uint16_t)(random32()%9));
        uint16_t want=(uint16_t)random32();int matched=0;
        for(unsigned i=0;i<8;++i){
            const uint32_t c=b+SML_Y+0x40u*i,def=b+SML_Y+0x400+0x20u*i;
            put32(sml_at(SML_X+4*i),random32()%5==0?0:random32()%8==0&&i?get32(sml_at(SML_X+4*i-4)):c);
            put32(sml_at(SML_Y+0x40*i+0x18),def);
            if(random32()%3==0&&!matched){matched=1;sml_at(SML_Y+0x400+0x20*i)[0x17]=(BYTE)want;want=(uint16_t)(int16_t)(int8_t)(BYTE)want;}
        }
        a[0]=b+SML_O;a[1]=random32()%4==0?random32():random32()%3==0?(uint32_t)words[random32()%15]:(random32()&0xffff0000u)|want;
        if(n<6)a[1]=(uint32_t)(int32_t)(int8_t)(BYTE)(n*0x30u+0x70u);
    } else if(SML_IS(ptr_list_add)){
        /* L at O+100h: array pointer +0, index word +6; array at X+20h. */
        static const int idx[]={0,1,2,3,-1,-2,4,5,6};
        const int c=n<9?idx[n]:(int)(random32()%9)-2;
        BYTE *L=sml_at(SML_O+0x100);uint32_t arr=b+SML_X+0x20,p=b+SML_Y+0x10u*(random32()%8);
        put16(L+6,(uint16_t)c);
        const unsigned alias=random32()%16;
        if(alias==0)p=b+SML_O+0x100+6-0x4b;                        /* the index word written over itself */
        else if(alias==1)arr=b+SML_O+0x100+6-4u*(uint32_t)c;       /* the stored pointer over the index word */
        put32(L,arr);
        a[0]=b+SML_O+0x100;a[1]=p;
    } else if(SML_IS(box_square_init)){
        a[0]=b+SML_O+random32()%0x40;a[1]=sml_value(n);
    } else if(SML_IS(twist_sound_latch)||SML_IS(pitch_sound_latch)){
        const int twist=SML_IS(twist_sound_latch);
        const uint32_t flag=twist?0x49a1c9u:0x49a1ccu,word=twist?0x49a1cau:0x49a1ceu,offset=twist?0x298u:0x29au;
        a[0]=b+SML_O;a[1]=sml_value(n);
        put16(O+offset,random32()%4==0?words[random32()%15]:(uint16_t)random32());
        if(random32()%3==0)put16(sml_global(word),get16(O+offset));
        else if(random32()%4==0)put16(sml_global(word),words[random32()%15]);
        {const unsigned f=random32()%4;*sml_global(flag)=f==0?0:f==1?1:f==2?(BYTE)random32():0;}
        if(real){put32(sml_global(0x49f8ec),random32()%2?0:random32()|1u);put32(sml_global(0x4d2cfc),0xffffffffu);}
    } else if(SML_IS(mech_type_index)||SML_IS(flyer_type_index)){
        a[0]=sml_value(n);
    } else if(SML_IS(cockpit_notice)||SML_IS(radio_notice)){
        const uint32_t m=b+SML_M,p=b+SML_P;
        put32(sml_global(0x49b088),m);put32(sml_at(SML_M+0x20b),p);put32(sml_at(SML_M+0x207),p);
        put32(sml_at(SML_P),b+SML_VP);
        if(SML_IS(cockpit_notice))a[0]=sml_value(n);
        else {
            static const BYTE bytes[]={0,1,0xff,0x80,0};
            a[0]=b+SML_O;a[1]=sml_value(n+3);a[2]=n<6?(n==0?0u:n==1?1u:n==2?0x10000u:n==3?0xffff0000u:n==4?0x8000u:0xffffu):random32()%2?0:random32();
            O[0x99]=n<5?bytes[n]:random32()%2?bytes[random32()%5]:(BYTE)random32();
        }
    } else {
        /* notice_time */
        static const uint16_t values[]={1,0,2,0xffff,0x8000,3};
        put16(sml_global(0x4a9e5e),n<6?values[n]:random32()%2?values[random32()%6]:(uint16_t)random32());
    }
    put32(sml_at(SML_VT1+0x1c),(uint32_t)(uintptr_t)sml_stub_5);put32(sml_at(SML_VT2+0x1c),(uint32_t)(uintptr_t)sml_stub_6);
    put32(sml_at(SML_VP),(uint32_t)(uintptr_t)sml_stub_7);
}
/* The original with the four argument slots and EAX/EDX/ECX = r; out
 * receives EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void sml_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t sml_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;BYTE scratch[128]={0};
    es2_weapon_graph g=weapons_graph(mapped,scratch,index);const uint32_t image=(uint32_t)(uintptr_t)mapped;
    if(SML_IS(part_shapes_bind))return es2_part_shapes_bind_registers(m,a[0],a[1],a[2],r,ecx);
    if(SML_IS(parts_intact))return es2_parts_intact_registers(m,a[0],r,ecx);
    if(SML_IS(formation_slot))return es2_formation_slot_registers(m,image,a[1],a[2],r,ecx);
    if(SML_IS(explosion_entry))return es2_explosion_entry_registers(m,image,a[0],r,ecx);
    if(SML_IS(flat_draw))return es2_flat_draw_registers(m,&g,a[0],r,ecx);
    if(SML_IS(projectile_draw))return es2_projectile_draw_registers(m,&g,a[0],r,ecx);
    if(SML_IS(beam_done))return es2_beam_done_registers(m,&g,a[0],r,ecx);
    if(SML_IS(weapon_by_mount))return es2_weapon_by_mount_registers(m,a[0],a[1],r,ecx);
    if(SML_IS(ptr_list_add))return es2_ptr_list_add_registers(m,a[0],a[1],r,ecx);
    if(SML_IS(box_square_init))return es2_box_square_init_registers(m,a[0],a[1],r,ecx);
    if(SML_IS(twist_sound_latch))return es2_twist_sound_latch_registers(m,&g,a[0],a[1],r,ecx);
    if(SML_IS(pitch_sound_latch))return es2_pitch_sound_latch_registers(m,&g,a[0],a[1],r,ecx);
    if(SML_IS(mech_type_index))return es2_mech_type_index_registers(m,image,a[0],r,ecx);
    if(SML_IS(flyer_type_index))return es2_flyer_type_index_registers(m,image,a[0],r,ecx);
    if(SML_IS(cockpit_notice))return es2_cockpit_notice_registers(m,&g,a[0],r,ecx);
    if(SML_IS(radio_notice))return es2_radio_notice_registers(m,&g,a[0],a[1],a[2],r,ecx);
    return es2_notice_time_registers(m,image,r,ecx);
}
/* The calls of stub id (key 1Ch = both child slots, the two call sites of
 * the event port = its slot 0). */
static uint64_t sml_expected(const sml_record *log,unsigned calls,uint32_t key){
    uint64_t count=0;
    for(unsigned j=0;j<calls;++j){
        const uint32_t id=log[j].id;
        if(key==0x1cu?id==5||id==6:key==0x420a8du||key==0x420acbu?id==7:sml_addresses[id]==key)++count;
    }
    return count;
}
static int sml_report(const char *what,unsigned n,const uint64_t pair[3],unsigned calls,const sml_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],(unsigned long long)pair[2],calls,sml_calls);
    for(unsigned j=0;j<calls&&j<sml_calls&&expected;++j)if(memcmp(expected+j,sml_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(sml_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u word%u %08x/%08x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<SML_SIZE;++j)if(sml_after[j]!=sml_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,sml_after[j],sml_arena[j]);break;}
    return 1;
}
static int sml_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[SML_GLOBALS],after[SML_GLOBALS],actual[SML_GLOBALS];static sml_record expected[SML_MAX],adapter[SML_MAX];
    static BYTE adapter_arena[SML_SIZE];
    uint32_t want[3],core_ecx=0;uint64_t pair[3];
    memcpy(sml_before,sml_arena,SML_SIZE);sml_globals(before,0);
    sml_reset();sml_invoke(mapped+d->rva,a,r,want);
    const unsigned calls=sml_calls;memcpy(expected,sml_log,calls*sizeof(*expected));
    memcpy(sml_after,sml_arena,SML_SIZE);sml_globals(after,0);
    /* the adapter */
    memcpy(sml_arena,sml_before,SML_SIZE);sml_globals(before,1);sml_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    const unsigned adapter_calls=sml_calls;memcpy(adapter,sml_log,adapter_calls*sizeof(*adapter));
    memcpy(adapter_arena,sml_arena,SML_SIZE);sml_globals(actual,0);
    for(unsigned i=0;i<count&&i<4;++i)
        if(c[i].count-counts[i]!=sml_expected(expected,calls,(uint32_t)c[i].key))return fail("sim_leaves callee counter delta");
    pair[0]=(uint64_t)want[1]<<32|want[0];
    int bad=pair[0]!=pair[1]||calls!=adapter_calls||memcmp(expected,adapter,calls*sizeof(*expected))||
        memcmp(sml_after,adapter_arena,SML_SIZE)||memcmp(after,actual,SML_GLOBALS);
    /* the core */
    memcpy(sml_arena,sml_before,SML_SIZE);sml_globals(before,1);sml_reset();
    pair[2]=sml_core(index,a,r,&core_ecx);sml_globals(actual,0);
    const int ecx_known=!rng_hooks[index].installed;
    bad=bad||pair[2]!=pair[0]||calls!=sml_calls||memcmp(expected,sml_log,calls*sizeof(*expected))||
        memcmp(sml_after,sml_arena,SML_SIZE)||memcmp(after,actual,SML_GLOBALS)||(ecx_known&&want[2]!=core_ecx);
    if(bad){
        if(ecx_known)fprintf(stderr," ecx %08x/%08x\n",want[2],core_ecx);
        return sml_report("direct",n,pair,calls,expected);
    }
    sml_globals(before,1);
    return 0;
}
static int sml_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;sml_build(index,n,0,a,r);
        BYTE before[SML_GLOBALS],after[SML_GLOBALS],actual[SML_GLOBALS],expected_fpu[108],actual_fpu[108];static sml_record expected[SML_MAX];
        memcpy(sml_before,sml_arena,SML_SIZE);sml_globals(before,0);
        sml_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=sml_calls;memcpy(expected,sml_log,calls*sizeof(*expected));memcpy(sml_after,sml_arena,SML_SIZE);sml_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("sim_leaves poison install");
        memcpy(sml_arena,sml_before,SML_SIZE);sml_globals(before,1);sml_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);sml_globals(actual,0);
        uint64_t pair[3]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0],0};
        if(pair[0]!=pair[1]||calls!=sml_calls||memcmp(expected,sml_log,calls*sizeof(*expected))||memcmp(sml_after,sml_arena,SML_SIZE)||
           memcmp(after,actual,SML_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=sml_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("sim_leaves poison uninstall");sml_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX/trace/arena/globals\n",d->name,total);
    return status;
}
/* The nested hook that runs as original code under the caller in the real
 * pass. */
static unsigned sml_nested(unsigned index,unsigned out[1]){
    if(SML_IS(beam_done)){out[0]=ES2_HOOK_INDEX_timer_word_step;return 1;}
    return 0;
}
static int sml_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;uint64_t entered=0,hooked=0,replaced=0;
    unsigned nested[1];const unsigned count=sml_nested(index,nested);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;sml_build(index,n,1,a,r);
        BYTE before[SML_GLOBALS],after[SML_GLOBALS],actual[SML_GLOBALS];static sml_record expected[SML_MAX];
        memcpy(sml_before,sml_arena,SML_SIZE);sml_globals(before,0);
        sml_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        const unsigned calls=sml_calls;memcpy(expected,sml_log,calls*sizeof(*expected));
        memcpy(sml_after,sml_arena,SML_SIZE);sml_globals(after,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        es2_hook_mode nested_mode=ES2_HOOK_ORIGINAL;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if((!e||e->compare_unsupported)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            nested_mode=o.mode;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("sim_leaves nested install");}
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+1,d,&o,function_hook_extension(index)))return fail("sim_leaves real install");
        memcpy(sml_arena,sml_before,SML_SIZE);sml_globals(before,1);sml_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);sml_globals(actual,0);
        /* A replaced nested hook returns EAX only: EDX is the callee's residue
         * (timer_word_step leaves the scaled word in DX), not beam_done's. */
        if(count&&nested_mode==ES2_HOOK_REPLACED)actual_pair[1]=expected_pair[1];
        if(memcmp(expected_pair,actual_pair,8)||calls!=sml_calls||memcmp(expected,sml_log,calls*sizeof(*expected))||
           memcmp(sml_after,sml_arena,SML_SIZE)||memcmp(after,actual,SML_GLOBALS)){
            uint64_t pair[3]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0],0};
            fprintf(stderr,"real mode=%u\n",mode);status=sml_report("real",n,pair,calls,expected);
        }
        for(unsigned j=0;j<count;++j){entered+=hk[j].calls;if(hk[j].mismatches)status=fail("sim_leaves nested compare mismatch");
            if(!es2_hook_uninstall(hk+j))return fail("sim_leaves real uninstall");}
        if(!es2_hook_uninstall(hk+1))return fail("sim_leaves real uninstall");
        hooked+=hk[1].calls;replaced+=mode>=3;
        sml_globals(before,1);++total;
    }
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    if(!status&&count&&!entered){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[0])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases%s, hook original/replaced, 31/a7 stack, EDX:EAX, trace, arena and globals\n",d->name,total,
        count?", nested hook in rotating modes":"");
        if(count)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[0])->name,(unsigned long long)entered);}
    return status;
}
static void sml_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){sml_build(index,n,0,a,r);}
static void sml_compare_state(BYTE *s,int restore){
    if(restore){memcpy(sml_arena,s,SML_SIZE);sml_globals(s+SML_SIZE,1);}else{memcpy(s,sml_arena,SML_SIZE);sml_globals(s+SML_SIZE,0);}
}
static int sim_leaves_cases(unsigned index,unsigned limit){
    BYTE saved[SML_IDS][5],old_globals[SML_GLOBALS];
    const int compares=compare_probe_capable(index);
    if(limit&&compares&&compare_probe_installed(index))
        return compare_probe_run(index,limit,sml_compare_build,sml_compare_state,SML_SIZE+SML_GLOBALS);
    sml_globals(old_globals,0);
    if(sml_patch(saved,1,sml_mask(index,0)))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_SIM_LEAVES_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];sml_build(index,n,0,a,r);status=sml_compare(index,n,a,r);sml_globals(old_globals,1);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=sml_installed_probes(index,64);
    if(sml_patch(saved,0,sml_mask(index,0)))return 1;
    current_case_valid=0;
    if(!limit&&!status&&sml_has_real(index)){
        BYTE real_saved[SML_IDS][5];const unsigned mask=sml_mask(index,1);
        if(sml_patch(real_saved,1,mask))return 1;
        status=sml_real_pass(index);
        if(sml_patch(real_saved,0,mask))return 1;
    }
    sml_globals(old_globals,1);
    if(!limit&&!status&&compares)status=compare_probe_run(index,0,sml_compare_build,sml_compare_state,SML_SIZE+SML_GLOBALS);
    if(!status)printf("PASS: %-20s %u cases; EDX:EAX of the original, adapter and core, ECX (core), callee calls with entry registers and arena hash, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
