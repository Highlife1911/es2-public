/* weapon_system group: select_step 41074c, fire_group 410ae4, weapons_v3c
 * 410d04 and group_toggle 4110ac (weapons.md, "weapon-system routines below the
 * weapon keys"). Direct cases run the original body and the core against
 * logging stubs for every game callee of the hook under test (410708, 41074c,
 * 4610ad, 441f90 and 410cd0 patched in the private image; the weapon virtuals
 * +38h/+3Ch/+48h through two arena vtables): ordered arguments, entry
 * EAX/EDX/ECX, and the ESI that 41074c receives from 410ae4. The originals are
 * called with a chosen ESI (41074c reads the caller's high word) and their EAX,
 * EDX and ECX are captured; ECX is compared for group_toggle (exact through its
 * entry shim), the pair for all four. Stubs return edge values and change what
 * the originals read again after a call (E's vtable before V48/V3C, list
 * entries ahead of 410d04's loop, the cached list pointer and count). Then
 * installed 31/a7 probes through the entry shims, and real-callee passes:
 * 410ae4 over the real 41074c, 410708, 4610ad and 441f90 with select_step
 * nested in both modes, and the real weapon_keys 410f78 (original or replaced)
 * over all four hooks in rotating modes with the real 410708, 410cd0 and HUD
 * calls. */
typedef struct ws_record { uint32_t id,args[2],regs[3],esi,eax,edx; } ws_record;
enum { WS_SIZE=0x1500, WS_W=0x300, WS_LIST=0x660, WS_T=0x704, WS_TSTRIDE=0x120, WS_E=0xb80, WS_D=0xe80, WS_C=0xf40,
    WS_VT1=0x10c0, WS_VT2=0x1120, WS_H=0x1180, WS_X=0x1280, WS_WEAPONS=6, WS_SLOTS=24, WS_MAX=64, WS_IDS=12,
    WS_REAL_MAX=8192, WS_BIG=0x10000, WS_GLOBALS=16 };
#define WS_BASE ((uint32_t)(uintptr_t)ws_arena)
#define WS_ADDR(o) (WS_BASE+(uint32_t)(o))
static BYTE ws_arena[WS_SIZE],ws_before[WS_SIZE],ws_after[WS_SIZE];
static uint32_t ws_big[WS_BIG];
static ws_record ws_log[WS_MAX];
static uint32_t ws_real_log[WS_REAL_MAX];
static unsigned ws_calls,ws_real_count,ws_index;
static int ws_real,ws_big_mode;
static uint32_t ws_seed,ws_stream;
__attribute__((used)) static uint32_t ws_ecx,ws_esi;
/* 0 410708, 1 41074c, 2 4610ad, 3 441f90, 4 410cd0, 5 unused; 6/7 VT1
 * +3Ch/+48h, 8/9 VT2 +3Ch/+48h, 10/11 VT1/VT2 +38h. */
static const uint32_t ws_addresses[WS_IDS]={0x410708,0x41074c,0x4610ad,0x441f90,0x410cd0,0,0,0,0,0,0,0};
static const uint32_t ws_slots[WS_IDS]={0,0,0,0,0,0,0x3c,0x48,0x3c,0x48,0x38,0x38};
static const unsigned ws_counts[WS_IDS]={2,2,1,2,2,0,1,1,1,1,2,2};
static uint32_t ws_next(void){
    uint32_t z=ws_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void ws_reset(void){ws_calls=ws_real_count=0;ws_stream=ws_seed;}
static int ws_inside(uint32_t a,uint32_t n){return a-WS_BASE<=(uint32_t)WS_SIZE-n;}
static uint32_t ws_e(unsigned k){return WS_ADDR(WS_E+0x80u*k);}
static uint32_t ws_list(int i){return WS_ADDR(WS_LIST+4*i);}
static uint32_t ws_table(unsigned k){return WS_ADDR(WS_T+WS_TSTRIDE*k);}
static BYTE *ws_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static void ws_globals(BYTE *out,int restore){
    if(restore){memcpy(mapped+0x9b088,out,4);memcpy(mapped+0xd2520,out+4,12);}
    else {memcpy(out,mapped+0x9b088,4);memcpy(out+4,mapped+0xd2520,12);}
}
/* The weapon a stub sees through W's list slot i, or 0 when outside. */
static uint32_t ws_slot_weapon(uint32_t i){
    uint32_t list=get32(ws_arena+WS_W+4),a=list+4u*(uint32_t)(int32_t)(int16_t)(uint16_t)i;
    if(!ws_inside(a,4))return 0;
    uint32_t e=get32(ws_host(a));return ws_inside(e,0x80)?e:0;
}
static void ws_swap_vtable(uint32_t e,uint32_t pick){
    if(e&&ws_inside(e,4))put32(ws_host(e),pick&1?WS_ADDR(WS_VT2):WS_ADDR(WS_VT1));
}
static uint64_t ws_real_stub(unsigned id,const uint32_t *a,uint32_t eax,uint32_t edx,uint32_t pick){
    if(ws_real_count+4>WS_REAL_MAX){fail("weapon_system real stub overflow");ExitProcess(1);}
    uint32_t *o=ws_real_log+ws_real_count;o[0]=id;unsigned k=1;
    for(unsigned j=0;j<ws_counts[id];++j)o[k++]=a[j];
    if(id==7||id==9){if(pick%2)eax&=0xffff0000u;if(pick%8==3)ws_swap_vtable(a[0],pick>>3);}
    ws_real_count+=k;return (uint64_t)edx<<32|eax;
}
__attribute__((used,noinline)) static uint64_t __cdecl ws_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    uint32_t eax=ws_next(),edx=ws_next(),pick=ws_next();ws_ecx=ws_next();
    if(ws_real)return ws_real_stub(id,a,eax,edx,pick);
    if(ws_calls==WS_MAX){fail("weapon_system synthetic call overflow");ExitProcess(1);}
    ws_record *v=ws_log+ws_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<ws_counts[id];++k)v->args[k]=a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=regs[j];
    switch(id){
    case 1:v->esi=ws_esi;break;
    case 2:if(pick%2)eax=WS_ADDR(WS_H);break;
    case 4:if(pick%4==0)ws_swap_vtable(ws_slot_weapon(a[1]),pick>>2);break;
    case 6:case 8:
        /* 410d04 reads each slot when it gets there; list and count are cached. */
        if(ws_index==ES2_HOOK_INDEX_weapons_v3c&&!ws_big_mode){
            if(pick%4==0)put32(ws_arena+WS_LIST+4*(ws_next()%WS_SLOTS),ws_next()%3?ws_e(ws_next()%WS_WEAPONS):0);
            if(pick%8==1)put16(ws_arena+WS_W+8,(uint16_t)ws_next());
            if(pick%8==2)put32(ws_arena+WS_W+4,WS_ADDR(WS_LIST+4));
        }
        break;
    case 7:case 9:
        if(pick%2)eax&=0xffff0000u;                /* AX = 0: V3C follows */
        if(pick%8==3)ws_swap_vtable(a[0],pick>>3);    /* the vtable is read again */
        break;
    default:break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define WS_STUB(n) __attribute__((naked)) static void ws_stub_##n(void){__asm__ volatile( \
    "movl %esi,_ws_esi\n\tpushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _ws_stub_impl\n\taddl $24,%esp\n\tmovl _ws_ecx,%ecx\n\tret\n\t");}
WS_STUB(0) WS_STUB(1) WS_STUB(2) WS_STUB(3) WS_STUB(4) WS_STUB(6) WS_STUB(7) WS_STUB(8) WS_STUB(9) WS_STUB(10) WS_STUB(11)
#undef WS_STUB
static void *ws_stubs[WS_IDS]={ws_stub_0,ws_stub_1,ws_stub_2,ws_stub_3,ws_stub_4,NULL,ws_stub_6,ws_stub_7,ws_stub_8,ws_stub_9,
    ws_stub_10,ws_stub_11};
/* Calls a target with two slots, chosen EAX/EDX/ECX and ESI; returns
 * EAX, EDX and ECX. */
__attribute__((naked)) static void ws_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t esi,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tmovl 20(%ebp),%esi\n\t"
        "call *8(%ebp)\n\tmovl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The same from a stack filled with a pattern, EAX = EDX = ECX = pattern. */
__attribute__((naked)) static void ws_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t esi,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl 20(%ebp),%esi\n\tcall *8(%ebp)\n\t"
        "movl 24(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint8_t ws_group_byte(void){
    /* 0..2 mostly; other groups whose record does not overlap W's header. */
    switch(random32()%8){
    case 0:return (uint8_t)(3u+random32()%125u);
    case 1:return (uint8_t)(0x80u+random32()%123u);   /* -128..-6 */
    default:return (uint8_t)(random32()%3u);
    }
}
static void ws_record_table(uint8_t group){put32(ws_arena+WS_W+0x1f+6*(int8_t)group,ws_table(random32()%4));}
/* Termination of 41074c's loop for step d on the current state, at most
 * 200 rounds (generator filter only; reads the arena like the original). */
static int ws_step_ends(uint16_t d){
    const int8_t sel=(int8_t)ws_arena[WS_W+0x1d];if(sel<0)return 1;
    const uint16_t count=get16(ws_arena+WS_W+8);uint16_t x=(uint16_t)sel;
    for(unsigned k=0;k<200;++k){
        x=(uint16_t)(x+d);
        if((int16_t)x<0)x=(uint16_t)(count-1u);else if(!((int16_t)count>(int16_t)x))x=0;
        const int idx=(int16_t)x;const uint32_t slot=get32(ws_arena+WS_W+4)+4u*(uint32_t)idx;
        if(!ws_inside(slot,4))return 0;
        const uint32_t e=get32(ws_host(slot));
        if(e){
            if(!ws_inside(e,0x80))return 0;
            const uint32_t def=get32(ws_host(e+0x18));
            if(ws_host(e)[0x4c]&&(!ws_host(e)[0x4b]||(ws_inside(def,0x20)&&(int8_t)ws_host(def)[0x16]>0))){
                const uint32_t t=get32(ws_arena+WS_W+0x1f+6*(int8_t)ws_arena[WS_W+0x1c])+2u*(uint32_t)idx;
                if(!ws_inside(t,2))return 0;
                if(get16(ws_host(t)))return 1;
            }
        }
        if(idx==sel)return 1;
    }
    return 0;
}
/* A weapon system W with a list of WS_SLOTS slots (index -1 included), six
 * weapons on two vtables, their definitions and carriers, four group
 * tables and the HUD objects. sane: count 1..16, selection -1..count-1,
 * groups 0..2, the selected slot filled (real passes). */
static void ws_build(unsigned index,unsigned n,uint32_t a[2],uint32_t r[3],uint32_t *esi,int sane){
    ws_seed=random32();random_bytes(ws_arena,WS_SIZE);a[0]=a[1]=0;ws_big_mode=0;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    *esi=random32();
    BYTE *w=ws_arena+WS_W;
    for(unsigned v=0;v<2;++v){uint32_t t=v?WS_VT2:WS_VT1;
        put32(ws_arena+t+0x38,(uint32_t)(uintptr_t)ws_stubs[10+v]);put32(ws_arena+t+0x3c,(uint32_t)(uintptr_t)ws_stubs[6+2*v]);
        put32(ws_arena+t+0x48,(uint32_t)(uintptr_t)ws_stubs[7+2*v]);}
    static const BYTE defs[]={1,0,0x7f,0x80,0xff,2};
    static const BYTE numbers[]={0,1,2,3,4,5,9,0xff,0x80};
    static const uint32_t lives[]={1,0,0xffffffffu,0x7fffffffu,0x80000000u,2};
    for(unsigned k=0;k<WS_WEAPONS;++k){
        uint32_t e=ws_e(k);BYTE *p=ws_host(e),*d=ws_arena+WS_D+0x20*k,*c=ws_arena+WS_C+0x40*k;
        put32(p,random32()%2?WS_ADDR(WS_VT1):WS_ADDR(WS_VT2));put32(p+0x18,WS_ADDR(WS_D+0x20*k));put32(p+0x1c,WS_ADDR(WS_C+0x40*k));
        p[0x4b]=(BYTE)(random32()%2?0:1u+random32()%255u);p[0x4c]=(BYTE)(random32()%4?1u+random32()%255u:0);
        d[0x16]=random32()%4?defs[random32()%6]:(BYTE)random32();d[7]=random32()%4?numbers[random32()%9]:(BYTE)random32();
        put32(c+0x30,random32()%4?lives[random32()%6]:random32());
    }
    put32(w+4,WS_ADDR(WS_LIST));
    unsigned count=sane?1u+random32()%16u:random32()%16==0?0:1u+random32()%20u;
    put16(w+8,(uint16_t)count);
    for(int i=-1;i<WS_SLOTS;++i)put32(ws_host(ws_list(i)),random32()%4==0?0:ws_e(random32()%WS_WEAPONS));
    for(unsigned k=0;k<4;++k)for(int i=-2;i<0x88;++i){
        static const uint16_t words[]={1,7,0x8000,0xffff,0x100};
        put16(ws_host(ws_table(k)+2u*(uint32_t)i),random32()%2?0:random32()%4?words[random32()%5]:(uint16_t)random32());
    }
    int sel=random32()%8==0?-1:(int)(random32()%(count?count:1u));
    if(!sane&&random32()%8==0){static const int odd[]={-128,-2,127,23,20};sel=odd[random32()%5];}
    w[0x1d]=(BYTE)sel;
    uint8_t group=sane?(uint8_t)(random32()%3u):ws_group_byte();w[0x1c]=group;
    for(uint8_t g=0;g<3;++g)ws_record_table(g);
    ws_record_table(group);
    BYTE *x=ws_arena+WS_X;put32(x+0x1e1,WS_ADDR(WS_H));
    if(sane){if(sel>=0)put32(ws_host(ws_list(sel)),ws_e(random32()%WS_WEAPONS));return;}
    a[0]=WS_ADDR(WS_W);
    if(index==ES2_HOOK_INDEX_select_step){
        static const uint16_t steps[]={1,0xffff,1,0xffff,0,2,0xfffe,0x8000};
        uint16_t d=random32()%8?steps[random32()%8]:(uint16_t)random32();
        if(!ws_step_ends(d)){d=random32()%2?1:0xffff;if(!count)w[0x1d]=0xff;else w[0x1d]=(BYTE)(random32()%count);}
        if(!ws_step_ends(d)){fail("weapon_system step filter");ExitProcess(1);}
        a[1]=(random32()%2?random32()&0xffff0000u:0)|d;
        /* EAX's high bytes are returned when nothing is selected. */
        (void)n;return;
    }
    if(index==ES2_HOOK_INDEX_fire_group){
        uint8_t g=random32()%4?(uint8_t)(random32()%3u):random32()%2?group:ws_group_byte();
        ws_record_table(g);
        uint32_t high=random32()%2?((int8_t)g<0?0xff00u:0):random32()&0xff00u;
        a[1]=(random32()%2?random32()&0xffff0000u:0)|high|g;
        return;
    }
    if(index==ES2_HOOK_INDEX_weapons_v3c){
        if(random32()%32==0)put32(w+4,0);
        if(n%512==511){
            /* An unsigned count over a large list, three weapons in it. */
            ws_big_mode=1;memset(ws_big,0,sizeof(ws_big));put32(w+4,(uint32_t)(uintptr_t)ws_big);
            uint16_t big=(uint16_t)(0x8000u+random32()%0x8000u);put16(w+8,big);
            for(unsigned j=0;j<3;++j)ws_big[random32()%big]=ws_e(random32()%WS_WEAPONS);
        }
        return;
    }
    static const uint16_t wanted[]={0,1,2,3,4,5,9,0xffff,0xff80};
    a[1]=(random32()&0xffff0000u)|(random32()%8?wanted[random32()%9]:(uint16_t)random32());
}
static int ws_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3],unsigned calls,const ws_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x calls %u/%u\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2],calls,ws_calls);
    for(unsigned j=0;j<calls&&j<ws_calls&&expected;++j)if(memcmp(expected+j,ws_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(ws_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<WS_SIZE;++j)if(ws_after[j]!=ws_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ws_after[j],ws_arena[j]);break;}
    return 1;
}
static unsigned ws_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<ws_calls;++j){unsigned id=ws_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?ws_slots[id]&&ws_slots[id]==c->key:!ws_slots[id]&&ws_addresses[id]==c->key)++hits;}
    return hits;
}
static int ws_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r,uint32_t esi){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static ws_record expected[WS_MAX];
    memcpy(ws_before,ws_arena,WS_SIZE);
    uint32_t want[3],got[3]={0,0,0};ws_reset();ws_machine(mapped+d->rva,a,r,esi,want);
    unsigned calls=ws_calls;memcpy(expected,ws_log,calls*sizeof(*expected));
    memcpy(ws_after,ws_arena,WS_SIZE);memcpy(ws_arena,ws_before,WS_SIZE);ws_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    /* select_step's adapter takes the caller's ESI in the EDX slot (shim). */
    const uint32_t entry[3]={r[0],index==ES2_HOOK_INDEX_select_step?esi:r[1],r[2]};
    /* group_toggle's shim computes ECX on the entry state. */
    got[2]=index==ES2_HOOK_INDEX_group_toggle?es2_group_toggle_ecx(&math_native_memory,a[0],a[1]):want[2];
    uint64_t pair=x->replacement_context(a,d->user,entry);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);
    if(ws_big_mode)memset(ws_big,0,sizeof(ws_big));
    if(n<4096)for(unsigned i=0;i<count&&i<4;++i)if(c[i].count-counts[i]!=ws_expected_hits(c+i))return fail("weapon_system callee counter delta");
    if(memcmp(want,got,sizeof(want))||calls!=ws_calls||memcmp(expected,ws_log,calls*sizeof(*expected))||memcmp(ws_after,ws_arena,WS_SIZE))
        return ws_report("direct",n,want,got,calls,expected);
    return 0;
}
static unsigned ws_which(unsigned index){
    switch(index){
    case ES2_HOOK_INDEX_select_step:return 0x1u;
    case ES2_HOOK_INDEX_fire_group:return 0xeu;
    case ES2_HOOK_INDEX_group_toggle:return 0x10u;
    default:return 0;
    }
}
static int ws_patch(BYTE saved[WS_IDS][5],int active[WS_IDS],unsigned which,int on){
    for(unsigned i=0;i<WS_IDS;++i){
        if(on){active[i]=0;if(!(which>>i&1)||!ws_addresses[i])continue;if(actor_patch(ws_addresses[i]-0x400000u,ws_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]){if(actor_patch(ws_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;active[i]=0;}
    }
    return 0;
}
/* Installed probes: the original from a 31/a7 stack against the installed
 * hook (through the entry shims); EAX, EDX, the trace and the arena, and
 * ECX for group_toggle. */
static int ws_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[2],r[3],esi,want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u;ws_build(index,n,a,r,&esi,0);
        BYTE expected_fpu[108],actual_fpu[108];static ws_record expected[WS_MAX];
        memcpy(ws_before,ws_arena,WS_SIZE);
        ws_reset();ws_poison_call(mapped+d->rva,a,pattern,esi,want);es2_hook_fpu_save(expected_fpu);
        unsigned calls=ws_calls;memcpy(expected,ws_log,calls*sizeof(*expected));memcpy(ws_after,ws_arena,WS_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("weapon_system poison install");
        memcpy(ws_arena,ws_before,WS_SIZE);ws_reset();ws_poison_call(mapped+d->rva,a,pattern,esi,got);
        es2_hook_fpu_save(actual_fpu);
        if(index!=ES2_HOOK_INDEX_group_toggle)got[2]=want[2];
        if(ws_big_mode)memset(ws_big,0,sizeof(ws_big));
        if(memcmp(want,got,sizeof(want))||calls!=ws_calls||memcmp(expected,ws_log,calls*sizeof(*expected))||memcmp(ws_after,ws_arena,WS_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=ws_report(fast?"installed fast":"installed preserving",n,want,got,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("weapon_system poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX%s/trace/arena\n",d->name,total,
        index==ES2_HOOK_INDEX_group_toggle?"/ECX":"");
    return status;
}
static int ws_real_report(const char *what,unsigned n,unsigned mode,const uint32_t want[3],const uint32_t got[3],unsigned logged){
    fprintf(stderr,"FAIL: %s case %u (%s mode %u) eax %08x/%08x edx %08x/%08x ecx %08x/%08x stub log %u/%u\n",current_function,n,what,mode,
        want[0],got[0],want[1],got[1],want[2],got[2],logged,ws_real_count);
    for(unsigned j=0;j<WS_SIZE;++j)if(ws_after[j]!=ws_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,ws_after[j],ws_arena[j]);break;}
    return 1;
}
/* fire_group's real callees: 41074c (select_step original or replaced),
 * 410708, 4610ad and 441f90 on the arena HUD; fire_group original or
 * replaced. The HUD root 49b088 and the word 4d2528 are planted, compared
 * and restored. */
static int ws_real_fire(void){
    const unsigned outer=ES2_HOOK_INDEX_fire_group,nested=ES2_HOOK_INDEX_select_step;
    int status=0;unsigned total=0;uint64_t entered[2]={0,0};static uint32_t expected_log[WS_REAL_MAX];
    BYTE saved_globals[WS_GLOBALS],after_globals[WS_GLOBALS],actual_globals[WS_GLOBALS];ws_globals(saved_globals,0);
    ws_real=1;ws_index=outer;
    for(unsigned mode=0;mode<4&&!status;++mode)for(unsigned n=0;n<768&&!status;++n){
        uint32_t a[2],r[3],esi,want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;ws_build(outer,n,a,r,&esi,1);
        a[0]=WS_ADDR(WS_W);a[1]=(random32()&0xffff0000u)|(random32()%4?random32()%3u:ws_arena[WS_W+0x1c]);
        put32(mapped+0x9b088,WS_ADDR(WS_X));put16(mapped+0xd2528,(uint16_t)random32());
        BYTE globals[WS_GLOBALS];ws_globals(globals,0);memcpy(ws_before,ws_arena,WS_SIZE);
        ws_reset();ws_poison_call(mapped+0x10ae4,a,pattern,esi,want);
        unsigned logged=ws_real_count;memcpy(expected_log,ws_real_log,4*logged);memcpy(ws_after,ws_arena,WS_SIZE);ws_globals(after_globals,0);
        es2_hook hk[2];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk,function_hook_descriptor(nested),&o,function_hook_extension(nested)))return fail("weapon_system nested install");
        o.mode=mode&2?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+1,function_hook_descriptor(outer),&o,function_hook_extension(outer)))return fail("weapon_system caller install");
        memcpy(ws_arena,ws_before,WS_SIZE);ws_globals(globals,1);ws_reset();
        ws_poison_call(mapped+0x10ae4,a,pattern,esi,got);ws_globals(actual_globals,0);got[2]=want[2];
        if(memcmp(want,got,sizeof(want))||logged!=ws_real_count||memcmp(expected_log,ws_real_log,4*logged)||memcmp(ws_after,ws_arena,WS_SIZE)||
           memcmp(after_globals,actual_globals,WS_GLOBALS))status=ws_real_report("real fire_group",n,mode,want,got,logged);
        entered[0]+=hk[0].calls;entered[1]+=hk[1].calls;
        for(unsigned j=0;j<2;++j)if(!es2_hook_uninstall(hk+j))return fail("weapon_system real uninstall");
        ++total;
    }
    ws_real=0;ws_globals(saved_globals,1);
    if(!status&&(!entered[0]||!entered[1])){fprintf(stderr,"FAIL: fire_group real pass entered %llu/%llu\n",
        (unsigned long long)entered[0],(unsigned long long)entered[1]);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases over the real 41074c/410708/4610ad/441f90 (select_step nested, %llu replaced entries)\n",
        current_function,total,(unsigned long long)entered[0]);
    return status;
}
/* The real weapon_keys 410f78 (original or replaced) over the four hooks in
 * rotating modes, the real 410708, 410cd0, 4610ad and 441f90; the weapon
 * virtuals +38h/+3Ch/+48h stay stubs (inputs compared in order). EDX is
 * compared when weapon_keys is original (its replaced form hands 41074c the
 * bridge's ESI, weapons.md, "weapon-system routines below the weapon keys"),
 * ECX after Alt+1..Alt+0 likewise. */
static int ws_real_keys(unsigned index){
    static const unsigned mine[4]={ES2_HOOK_INDEX_select_step,ES2_HOOK_INDEX_fire_group,ES2_HOOK_INDEX_weapons_v3c,ES2_HOOK_INDEX_group_toggle};
    static const uint16_t codes[]={0x11,0x211,0x2c,0x30,0x202,0x203,0x204,0x205,0x20b,0x0c,0x0d,0x4a,0x4e,0x11,0x211,0x30,0x10,0x20c};
    int status=0;unsigned total=0;uint64_t entered[4]={0,0,0,0};static uint32_t expected_log[WS_REAL_MAX];
    BYTE saved_globals[WS_GLOBALS],after_globals[WS_GLOBALS],actual_globals[WS_GLOBALS];ws_globals(saved_globals,0);
    ws_real=1;ws_index=index;
    for(unsigned mode=0;mode<8&&!status;++mode)for(unsigned n=0;n<384&&!status;++n){
        uint32_t a[2],r[3],esi,want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;ws_build(index,n,a,r,&esi,1);
        const uint16_t code=codes[random32()%(sizeof(codes)/sizeof(codes[0]))];
        a[0]=WS_ADDR(WS_W);a[1]=(random32()%2?random32()&0xffff0000u:0)|code;
        put32(mapped+0x9b088,WS_ADDR(WS_X));put16(mapped+0xd2528,(uint16_t)random32());
        BYTE globals[WS_GLOBALS];ws_globals(globals,0);memcpy(ws_before,ws_arena,WS_SIZE);
        ws_reset();ws_poison_call(mapped+0x10f78,a,pattern,esi,want);
        unsigned logged=ws_real_count;memcpy(expected_log,ws_real_log,4*logged);memcpy(ws_after,ws_arena,WS_SIZE);ws_globals(after_globals,0);
        es2_hook hk[5];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<4;++j){o.mode=((mode+j)>>(j&1))&1?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(mine[j]),&o,function_hook_extension(mine[j])))return fail("weapon_system nested install");}
        const int keys_replaced=mode>=4;o.mode=keys_replaced?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(hk+4,function_hook_descriptor(ES2_HOOK_INDEX_weapon_keys),&o,function_hook_extension(ES2_HOOK_INDEX_weapon_keys)))
            return fail("weapon_system weapon_keys install");
        memcpy(ws_arena,ws_before,WS_SIZE);ws_globals(globals,1);ws_reset();
        ws_poison_call(mapped+0x10f78,a,pattern,esi,got);ws_globals(actual_globals,0);
        if(keys_replaced||code<0x202||code>0x20b)got[2]=want[2];
        if(memcmp(want,got,sizeof(want))||logged!=ws_real_count||memcmp(expected_log,ws_real_log,4*logged)||memcmp(ws_after,ws_arena,WS_SIZE)||
           memcmp(after_globals,actual_globals,WS_GLOBALS))status=ws_real_report("real weapon_keys",n,mode,want,got,logged);
        for(unsigned j=0;j<4;++j)entered[j]+=hk[j].calls;
        for(unsigned j=0;j<5;++j)if(!es2_hook_uninstall(hk+j))return fail("weapon_system real uninstall");
        ++total;
    }
    ws_real=0;ws_globals(saved_globals,1);
    for(unsigned j=0;j<4&&!status;++j)if(!entered[j]){
        fprintf(stderr,"FAIL: %s real weapon_keys pass never entered %s\n",current_function,function_hook_descriptor(mine[j])->name);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under 410f78 (original/replaced), four hooks in rotating modes, entries %llu/%llu/%llu/%llu\n",
        current_function,total,(unsigned long long)entered[0],(unsigned long long)entered[1],(unsigned long long)entered[2],
        (unsigned long long)entered[3]);
    return status;
}
static int weapon_system_cases(unsigned index,unsigned limit){
    ws_index=index;ws_real=0;
    BYTE saved[WS_IDS][5];int active[WS_IDS];
    if(ws_patch(saved,active,ws_which(index),1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WEAPON_SYSTEM_CASES");
    if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[2],r[3],esi;ws_build(index,n,a,r,&esi,0);status=ws_compare(index,n,a,r,esi);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=ws_installed_probes(index,64);
    if(ws_patch(saved,active,0,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&index==ES2_HOOK_INDEX_fire_group)status=ws_real_fire();
    if(!limit&&!status)status=ws_real_keys(index);
    if(!status)printf("PASS: %-20s %u cases; EAX/EDX%s, ordered calls with entry registers and ESI, arena\n",
        function_hook_descriptor(index)->name,cases,index==ES2_HOOK_INDEX_group_toggle?"/ECX":"");
    return status;
}
