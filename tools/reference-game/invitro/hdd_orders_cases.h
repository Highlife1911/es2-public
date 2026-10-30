/* hdd_orders group: the HDD map order controls map_areas 44d450, map_mode
 * 44d9f4, wingman_select 44da98 and order_cancel 44dc10
 * (docs/reverse-engineering/cockpit.md, "HDD map order controls"). An 8 KiB
 * arena holds the HDDMap object M, its eight mode buttons (rectangle at +4),
 * two map gadgets G/G2 and their holders P/P2, the graphics context C with its
 * offsets object B, and the HDD gauge with its button 0Dh and that button's
 * vtable; the image globals 49d6b0 (P), 49b088 (C) and the text table
 * 4d1638..4d1647 are set per case, compared and restored. Per case the event
 * gadget and the two gadget codes, the event point around a chosen button's
 * edges (-1/0/+1), the rectangles (also reversed, extreme or inside M), the map
 * mode (pick modes, the button's own mode, -1, any), the wingman index (0..2,
 * -1, or another index whose record byte stays in the arena), the flag bytes, G
 * placed inside M in some cases, the mode/index arguments and the entry
 * EAX/EDX/ECX vary. The callees 438d9c, 486e7c, 44dc6c, 433e3c, 44dd14, 44ede0,
 * 44ee00, 44ee24 are logging stubs (ordered calls with their arguments, the
 * bridge's slot count and entry EAX/EDX/ECX, stack values masked, the two
 * dwords behind 486e7c's point; results from a per-case stream) that also apply
 * a side effect from the same stream to M, P or B in about one call of four
 * (map mode, index, map-gadget code, flag bytes, point, rectangle, the holder's
 * gadget, the graphics offsets), so a replacement that caches a value the
 * original rereads differs. In-group callees (44d9f4 under
 * map_areas/wingman_select/order_cancel, 44da98 under order_cancel) run as
 * original code in the direct cases. The full EDX:EAX pair, the call log, the
 * arena and the globals are compared, and for the first 4,096 cases the callee
 * counters against the logged calls. Then installed 31/a7 poison probes and
 * real-callee passes against the all-original run: map_areas with map_mode and
 * button_press 438d9c (real, with the HDD button's vtable slot +8 and 4530bc as
 * stubs), wingman_select with map_mode, order_cancel with wingman_select and
 * map_mode, each hook in original or replaced mode in every combination;
 * map_mode under its real callers 44ee48/44ee88 (next/previous mode) in both
 * modes. In the real passes the logged ECX of 44ede0/44ee24 is not compared (a
 * replaced map_mode returns another ECX, which 44da98 hands on; both stubs
 * write ECX before reading it). Identifiers: hd_/HD_. */
enum { HD_SIZE=0x2000, HD_M=0x100, HD_BUTTONS=0x600, HD_G=0x800, HD_G2=0xa00, HD_P=0xc00, HD_P2=0xc40, HD_C=0xc80,
    HD_B=0xd00, HD_HDD=0x1000, HD_W=0x1100, HD_VT=0x1180, HD_IDS=10, HD_MAX=16, HD_GLOBALS=4+4+16,
    HD_ALL=0xff, HD_REAL=0x1fe };
#define HD_BASE ((uint32_t)(uintptr_t)hd_arena)
#define HD_ADDR(o) (HD_BASE+(uint32_t)(o))
static BYTE hd_arena[HD_SIZE],hd_before[HD_SIZE],hd_after[HD_SIZE];
typedef struct hd_record { uint32_t id,slots,a[3],regs[3],local[2]; } hd_record;
static hd_record hd_log[HD_MAX];
static unsigned hd_calls,hd_mask_ecx,hd_paths[8];
static uint32_t hd_seed,hd_stream;
__attribute__((used)) static uint32_t hd_ecx;
/* ids 0..8 are patched callees; 9 is the HDD button's vtable slot +8. */
static const uint32_t hd_addr[HD_IDS]={0x438d9c,0x486e7c,0x44dc6c,0x433e3c,0x44dd14,0x44ede0,0x44ee00,0x44ee24,0x4530bc,0};
static const unsigned hd_arity[HD_IDS]={3,1,3,1,1,2,1,1,2,2};
static uint32_t hd_next(void){
    uint32_t z=hd_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static void hd_reset(void){hd_calls=0;hd_stream=hd_seed;}
static int hd_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static void hd_globals(BYTE *out,int restore){
    static const struct { uint32_t rva,size; } r[]={{0x9d6b0,4},{0x9b088,4},{0xd1638,16}};
    BYTE *p=out;for(unsigned i=0;i<3;++i){if(restore)memcpy(mapped+r[i].rva,p,r[i].size);else memcpy(p,mapped+r[i].rva,r[i].size);p+=r[i].size;}
}
/* A wingman index whose record byte M+2Ah*i+A6h stays in the arena. */
static uint32_t hd_index(void){
    switch(hd_next()%8){
    case 0:return 0xffffffffu;
    case 1:return 3u+hd_next()%60u;
    case 2:return 0u-2u-hd_next()%5u;
    default:return hd_next()%3u;
    }
}
static uint32_t hd_mode(void){
    static const uint32_t modes[]={0xa,0xb,0xc,0xd,0xe,0xf,0x10,0x11,0xb,0xc,0xd,0xe,0xffffffffu,0x9,0x12,0x10b};
    return hd_next()%16?modes[hd_next()%16]:hd_next();
}
/* One callee side effect (about one call in four), drawn from the stream. */
static void hd_effect(void){
    uint32_t pick=hd_next();if(pick%4)return;
    BYTE *M=hd_arena+HD_M;
    switch(pick/4%10){
    case 0:put32(M+0x5d,hd_mode());break;
    case 1:put32(M+0x105,hd_index());break;
    case 2:put32(M+0x39,get32(M+0x14e));break;
    case 3:M[0x15a]=(BYTE)(hd_next()%3);M[0x15b]=(BYTE)(hd_next()%2);break;
    case 4:M[0x104]=(BYTE)(hd_next()%3);break;
    case 5:put32(M+0x152,hd_next()%256u);put32(M+0x156,hd_next()%256u);break;
    case 6:put32(M+0x71,hd_next());put32(M+0x79,hd_next()%640u);break;
    case 7:put32(hd_arena+HD_P+8,HD_ADDR(hd_next()%2?HD_G2:HD_G));break;
    case 8:put32(hd_arena+HD_B+0x210+4*(hd_next()%6),hd_next());break;
    default:put32(mapped+0x9d6b0,HD_ADDR(hd_next()%2?HD_P2:HD_P));break;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl hd_log_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(hd_calls==HD_MAX){fail("hdd_orders synthetic call overflow");ExitProcess(1);}
    hd_record *v=hd_log+hd_calls++;memset(v,0,sizeof(*v));v->id=id;v->slots=weapons_bridge_slots(a,hd_arity[id]);
    for(unsigned j=0;j<hd_arity[id];++j)v->a[j]=hd_stack(a[j])?0u:a[j];
    memcpy(v->regs,regs,12);for(unsigned j=0;j<3;++j)if(hd_stack(v->regs[j]))v->regs[j]=0;
    if((id==5||id==7)&&hd_mask_ecx)v->regs[2]=0;
    if(id==1){v->local[0]=get32((const BYTE *)(uintptr_t)a[0]);v->local[1]=get32((const BYTE *)(uintptr_t)a[0]+4);}
    uint32_t eax=hd_next(),edx=hd_next();hd_ecx=hd_next();
    hd_effect();
    return (uint64_t)edx<<32|eax;
}
#define HD_STUB(n) __attribute__((naked)) static void hd_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _hd_log_impl\n\taddl $24,%esp\n\tmovl _hd_ecx,%ecx\n\tret\n\t");}
HD_STUB(0) HD_STUB(1) HD_STUB(2) HD_STUB(3) HD_STUB(4) HD_STUB(5) HD_STUB(6) HD_STUB(7) HD_STUB(8) HD_STUB(9)
#undef HD_STUB
static void *hd_stubs[HD_IDS]={hd_stub_0,hd_stub_1,hd_stub_2,hd_stub_3,hd_stub_4,hd_stub_5,hd_stub_6,hd_stub_7,hd_stub_8,hd_stub_9};
static int hd_patch(BYTE saved[HD_IDS][5],int active[HD_IDS],unsigned which,int on){
    for(unsigned i=0;i<HD_IDS;++i){
        if(!hd_addr[i]){active[i]=0;continue;}
        if(on){active[i]=0;if(!(which>>i&1))continue;if(actor_patch(hd_addr[i]-0x400000u,hd_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(hd_addr[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
/* Coordinates: screen-like, small negative, the signed extremes. */
static uint32_t hd_coordinate(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,0x7ffffffeu,0x80000001u};
    switch(hd_next()%10){
    case 0:return edges[hd_next()%7];
    case 1:return hd_next();
    case 2:return hd_next()%64u-32u;
    default:return hd_next()%640u;
    }
}
/* One coordinate near the interval lo..hi of a chosen button. */
static uint32_t hd_near(uint32_t lo,uint32_t hi){
    switch(hd_next()%8){
    case 0:return lo+hd_next()%3u-1u;
    case 1:return hi+hd_next()%3u-1u;
    case 2:return hd_coordinate();
    default:return lo+(hi-lo)/2u+hd_next()%3u-1u;
    }
}
/* Builds one state. a[0] = M; a[1] = the mode (map_mode) or the index
 * (wingman_select). */
static void hd_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    hd_seed=random32();hd_stream=hd_seed^n;random_bytes(hd_arena,HD_SIZE);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=hd_next();
    BYTE *M=hd_arena+HD_M;const uint32_t Ma=HD_ADDR(HD_M);
    /* Globals, holders, gadgets, graphics, text table. */
    put32(mapped+0x9d6b0,HD_ADDR(HD_P));put32(hd_arena+HD_P+8,HD_ADDR(HD_G));put32(hd_arena+HD_P2+8,HD_ADDR(HD_G2));
    /* G inside M, except where G+B0h would rewrite the index M+105h (a
     * nonzero high byte would put the record byte outside the arena). */
    if(hd_next()%12==0){uint32_t k=hd_next()%0x2a0u;if(k-0x55u<4u)k+=4u;put32(hd_arena+HD_P+8,Ma+k);}
    put32(mapped+0x9b088,HD_ADDR(HD_C));put32(hd_arena+HD_C+4,HD_ADDR(HD_B));
    for(unsigned k=0;k<4;++k)put32(mapped+0xd1638+4*k,hd_next());
    /* The HDD gauge and its button 0Dh (for the real button_press). */
    put32(M+8,HD_ADDR(HD_HDD));put32(hd_arena+HD_HDD+0x18+4*0xd,HD_ADDR(HD_W));
    put16(hd_arena+HD_W+0x1b,hd_next()%4?1:2);put32(hd_arena+HD_W+0x17,HD_ADDR(HD_VT));put32(hd_arena+HD_VT+8,(uint32_t)(uintptr_t)hd_stub_9);
    put32(hd_arena+HD_C,HD_ADDR(HD_HDD+0x80));
    /* The eight mode buttons; a few lie inside M. */
    for(unsigned i=0;i<8;++i){
        uint32_t btn=HD_ADDR(HD_BUTTONS+0x20u*i);
        if(hd_next()%32==0)btn=Ma+0x100u+hd_next()%0x240u;
        put32(M+0x3d+4*i,btn);
        BYTE *rect=(BYTE *)(uintptr_t)btn+4;
        uint32_t x0=hd_coordinate(),y0=hd_coordinate();
        uint32_t x1=hd_next()%6?x0+1u+hd_next()%40u:hd_coordinate(),y1=hd_next()%6?y0+1u+hd_next()%30u:hd_coordinate();
        put32(rect,x0);put32(rect+4,y0);put32(rect+8,x1);put32(rect+12,y1);
    }
    /* Event: gadget codes, the event point near a chosen button. */
    uint32_t panel=hd_next(),area=hd_next()%8?hd_next():panel;
    put32(M+0x35,panel);put32(M+0x39,area);
    switch(hd_next()%5){case 0:put32(M+0x14e,area);break;case 1:put32(M+0x14e,hd_next());break;default:put32(M+0x14e,panel);break;}
    unsigned target=hd_next()%8;BYTE *tr=(BYTE *)(uintptr_t)get32(M+0x3d+4*target)+4;
    uint32_t px=hd_near(get32(tr),get32(tr+8)),py=hd_near(get32(tr+4),get32(tr+12));
    put32(M+0x152,px);put32(M+0x156,py);
    /* Map mode, wingman, flags, the map rectangle. */
    put32(M+0x5d,hd_next()%2?target+0xau:hd_mode());
    put32(M+0x105,hd_index());
    M[0x15a]=(BYTE)(hd_next()%3?hd_next()%2:hd_next());M[0x15b]=(BYTE)(hd_next()%2?0:hd_next()|1u);M[0x104]=(BYTE)(hd_next()%2?0:hd_next()|1u);
    put32(M+0x71,hd_coordinate());put32(M+0x75,hd_coordinate());put32(M+0x79,hd_coordinate());put32(M+0x7d,hd_coordinate());
    a[0]=Ma;
    if(index==ES2_HOOK_INDEX_map_mode){a[1]=hd_next()%3==0?get32(M+0x5d):hd_mode();if(hd_next()%8==0)put32(M+0x5d,0xffffffffu);}
    if(index==ES2_HOOK_INDEX_wingman_select){a[1]=hd_index();if(hd_next()%4==0)put32(M+0x5d,0xffffffffu);}
    if(index==ES2_HOOK_INDEX_order_cancel&&hd_next()%4==0)put32(M+0x5d,0xffffffffu);
    hd_stream=hd_seed;   /* the stubs' stream restarts here in both runs */
}
static unsigned hd_hits(uint32_t key){
    unsigned h=0;for(unsigned j=0;j<hd_calls;++j)if(hd_addr[hd_log[j].id]==key)++h;
    return h;
}
/* The calls a hook makes itself, from the log of its replacement run: every
 * stub id it calls directly is logged only for its own calls, except
 * 44dc6c under map_areas (also called by the nested 44d9f4, which calls
 * 433e3c once and then 44dc6c or 44dd14; map_areas never calls 433e3c or
 * 44dd14 itself); order_cancel calls 44d9f4 and 44da98 once each. */
static int hd_counter_check(unsigned index,const callee_counter *c,uint64_t delta){
    uint64_t want;
    if(index==ES2_HOOK_INDEX_order_cancel)want=1;
    else if(c->key==0x44d9f4u)want=hd_hits(0x433e3cu);
    else if(index==ES2_HOOK_INDEX_map_areas&&c->key==0x44dc6cu)want=hd_hits(0x44dc6cu)-(hd_hits(0x433e3cu)-hd_hits(0x44dd14u));
    else want=hd_hits(c->key);
    return delta!=want;
}
static int hd_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const hd_record *e,const BYTE *want,const BYTE *got){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,hd_calls);
    for(unsigned j=0;j<calls&&j<hd_calls&&e;++j)if(memcmp(e+j,hd_log+j,sizeof(*e))){
        const uint32_t *x=(const uint32_t*)(e+j),*y=(const uint32_t*)(hd_log+j);
        for(unsigned k=0;k<sizeof(*e)/4;++k)if(x[k]!=y[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,e[j].id,k,x[k],y[k]);break;}
    for(unsigned j=0;j<HD_SIZE;++j)if(hd_after[j]!=hd_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,hd_after[j],hd_arena[j]);break;}
    for(unsigned j=0;j<HD_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);break;}
    return 1;
}
/* Statistics: logged calls per stub id over the direct cases. */
static void hd_count(unsigned calls,const hd_record *e){
    for(unsigned j=0;j<calls;++j)hd_paths[e[j].id<8?e[j].id:7]++;
}
static int hd_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[HD_GLOBALS],after[HD_GLOBALS],actual[HD_GLOBALS];static hd_record expected[HD_MAX];
    memcpy(hd_before,hd_arena,HD_SIZE);hd_globals(before,0);
    hd_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);
    unsigned calls=hd_calls;memcpy(expected,hd_log,calls*sizeof(*expected));memcpy(hd_after,hd_arena,HD_SIZE);hd_globals(after,0);
    hd_count(calls,expected);
    memcpy(hd_arena,hd_before,HD_SIZE);hd_globals(before,1);hd_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8]={0};
    for(unsigned i=0;i<count&&i<8;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);hd_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count&&i<8;++i)if(hd_counter_check(index,c+i,c[i].count-counts[i]))return fail("hdd_orders callee counter delta");
    if(pair[0]!=pair[1]||calls!=hd_calls||memcmp(expected,hd_log,calls*sizeof(*expected))||memcmp(hd_after,hd_arena,HD_SIZE)||
       memcmp(after,actual,HD_GLOBALS))
        return hd_report("direct",n,pair,calls,expected,after,actual);
    hd_globals(before,1);
    return 0;
}
static int hd_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;hd_build(index,n*41u+3u,a,r);
        BYTE before[HD_GLOBALS],after[HD_GLOBALS],actual[HD_GLOBALS],expected_fpu[108],actual_fpu[108];static hd_record expected[HD_MAX];
        memcpy(hd_before,hd_arena,HD_SIZE);hd_globals(before,0);
        hd_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=hd_calls;memcpy(expected,hd_log,calls*sizeof(*expected));memcpy(hd_after,hd_arena,HD_SIZE);hd_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("hdd_orders poison install");
        memcpy(hd_arena,hd_before,HD_SIZE);hd_globals(before,1);hd_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);hd_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=hd_calls||memcmp(expected,hd_log,calls*sizeof(*expected))||memcmp(hd_after,hd_arena,HD_SIZE)||
           memcmp(after,actual,HD_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=hd_report(fast?"installed fast":"installed preserving",n,pair,calls,expected,after,actual);
        if(!es2_hook_uninstall(&h))return fail("hdd_orders poison uninstall");hd_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/calls/arena/globals\n",d->name,total);
    return status;
}
/* Real-callee pass: the hooks of `set` installed in every original/
 * replaced combination over the entry `target` (the hook itself, or for
 * map_mode its callers 44ee48/44ee88), against the all-original run with
 * the same stubs (the HDD button's slot and 4530bc stubbed, 438d9c real
 * when button_press is in the set). */
static int hd_real(unsigned index){
    unsigned set[3],k=0;uint32_t targets[2]={function_hook_descriptor(index)->rva,0};unsigned ntargets=1;
    set[k++]=index;
    if(index==ES2_HOOK_INDEX_map_areas){set[k++]=ES2_HOOK_INDEX_map_mode;set[k++]=ES2_HOOK_INDEX_button_press;}
    else if(index==ES2_HOOK_INDEX_wingman_select)set[k++]=ES2_HOOK_INDEX_map_mode;
    else if(index==ES2_HOOK_INDEX_order_cancel){set[k++]=ES2_HOOK_INDEX_wingman_select;set[k++]=ES2_HOOK_INDEX_map_mode;}
    else {targets[0]=0x4ee48u;targets[1]=0x4ee88u;ntargets=2;}
    const unsigned modes=1u<<k;
    BYTE saved[HD_IDS][5];int active[HD_IDS];if(hd_patch(saved,active,HD_REAL&~1u,1))return 1;
    int status=0;unsigned total=0,button=0;uint64_t entered=0,nested=0;hd_mask_ecx=1;
    for(unsigned t=0;t<ntargets&&!status;++t)for(unsigned mode=0;mode<modes&&!status;++mode)for(unsigned n=0;n<256u*4u/ntargets/(modes>4?2u:1u)&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;hd_build(index,n*67u+mode*7u+t,a,r);
        BYTE before[HD_GLOBALS],after[HD_GLOBALS],actual[HD_GLOBALS];static hd_record expected[HD_MAX];
        memcpy(hd_before,hd_arena,HD_SIZE);hd_globals(before,0);
        hd_reset();sg_poison_call(mapped+targets[t],a,pattern,expected_pair);
        unsigned calls=hd_calls;memcpy(expected,hd_log,calls*sizeof(*expected));memcpy(hd_after,hd_arena,HD_SIZE);hd_globals(after,0);
        es2_hook hk[3];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<k;++j){o.mode=(mode>>j&1)?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(set[j]),&o,function_hook_extension(set[j])))return fail("hdd_orders real install");}
        memcpy(hd_arena,hd_before,HD_SIZE);hd_globals(before,1);hd_reset();
        sg_poison_call(mapped+targets[t],a,pattern,actual_pair);hd_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=hd_calls||memcmp(expected,hd_log,calls*sizeof(*expected))||memcmp(hd_after,hd_arena,HD_SIZE)||
           memcmp(after,actual,HD_GLOBALS)){
            fprintf(stderr,"real target=%x mode=%u\n",targets[t]+0x400000u,mode);status=hd_report("real",n,pair,calls,expected,after,actual);}
        if(mode&1)entered+=hk[0].calls;
        for(unsigned j=1;j<k;++j)if(mode>>j&1)nested+=hk[j].calls;
        for(unsigned j=0;j<calls;++j)if(expected[j].id>=8)++button;
        for(unsigned j=0;j<k;++j)if(!es2_hook_uninstall(hk+j))return fail("hdd_orders real uninstall");
        hd_globals(before,1);++total;
    }
    hd_mask_ecx=0;
    if(hd_patch(saved,active,0,0))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass: never entered replaced\n",current_function);status=1;}
    if(!status)printf("PASS: %s %u real-callee cases (%u hooks original/replaced in all %u combinations%s; %llu replaced entries, %llu replaced nested entries, %u button_press callee calls), 31/a7 stack, pair/calls/arena/globals\n",
        current_function,total,k,modes,ntargets==2?" under 44ee48/44ee88":"",(unsigned long long)entered,(unsigned long long)nested,button);
    return status;
}
static int hdd_orders_cases(unsigned index,unsigned limit){
    BYTE old_globals[HD_GLOBALS];hd_globals(old_globals,0);
    BYTE saved[HD_IDS][5];int active[HD_IDS];
    if(hd_patch(saved,active,HD_ALL,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_HDD_ORDERS_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(hd_paths,0,sizeof(hd_paths));hd_mask_ecx=0;
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];hd_build(index,n,a,r);status=hd_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);current_case_valid=0;
    hd_globals(old_globals,1);
    if(!limit&&!status)status=hd_installed_probes(index,64);
    hd_globals(old_globals,1);
    if(hd_patch(saved,active,HD_ALL,0))return 1;
    if(!limit&&!status)status=hd_real(index);
    hd_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases (stub calls 438d9c %u, 486e7c %u, 44dc6c %u, 433e3c %u, 44dd14 %u, 44ede0 %u, 44ee00 %u, 44ee24 %u); full return pair, ordered calls with entry registers, arena and globals\n",
        function_hook_descriptor(index)->name,cases,hd_paths[0],hd_paths[1],hd_paths[2],hd_paths[3],hd_paths[4],hd_paths[5],hd_paths[6],hd_paths[7]);
    return status;
}
