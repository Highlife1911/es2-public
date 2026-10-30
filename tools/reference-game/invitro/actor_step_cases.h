/* actor_step group: actor_step 411cec, state_frame 413eb0 and the update-slot
 * trampolines mech_move 415afc, mech_act 415b38, mech_decide 415b74,
 * flyer_move_call 4217fc, flyer_act 42184c, flyer_decide 421888
 * (docs/reverse-engineering/vtables.md, "`actor_step` and the update-slot
 * trampolines").
 *
 * Direct cases run the original body and the core against logging stubs:
 * the update slots +1Ch/+14h/+18h through two arena vtables,
 * timer_dword_step 467aa8 patched, the state methods as member pointers of
 * arena state records; 413eb0 runs as original code under actor_step (the
 * core computes it). Stubs log the argument and entry EAX/EDX/ECX (stack
 * addresses masked) and change what the originals read again (the state
 * pointer, the vtable, the timer, the skip and once bytes). Pair and arena
 * are compared.
 *
 * Installed 31/a7 probes call the entry with distinct EBX/ESI/EDI and a
 * patterned stack: besides pair, ECX (exact through the shims, except
 * state_frame), trace and arena they compare at every stub entry EBX, ESI,
 * EDI, EBP and ESP relative to the caller and a hash of the 512 bytes
 * below the stub's ESP (what a state method would read uninitialized),
 * and the 512 bytes below the argument after return (the original's
 * frames and every callee's). The real pass enters 411cec with the real
 * MECH or FLYER vtable, so the real trampolines run, with the hook under
 * test original in one run and replaced in the other and the other seven
 * hooks and timer_dword_step in rotating modes; members are logging stubs
 * (a quarter of the cases fill 288 KiB below their entry: aim_fire's gap
 * plus 32 KiB, the budget of the trampoline gaps) or the real
 * timer_dword_step on a timer record. */
typedef struct as_record { uint32_t id,arg,regs[3],saved[5],hash; } as_record;
enum { AS_SIZE=0x1000, AS_ACT=0x100, AS_VT1=0x400, AS_VT2=0x440, AS_ST=0x500, AS_STATES=4, AS_TIMER=0x600,
    AS_MAX=16, AS_IDS=7 };
#define AS_BASE ((uint32_t)(uintptr_t)as_arena)
#define AS_A (AS_BASE+AS_ACT)
#define AS_STATE(k) (AS_BASE+AS_ST+0x40u*(uint32_t)(k))
static BYTE as_arena[AS_SIZE],as_before[AS_SIZE],as_after[AS_SIZE];
static as_record as_log[AS_MAX];
static unsigned as_calls;
static uint32_t as_seed,as_stream;
static int as_track;
/* EBX, ESI, EDI, EBP, ESP at the last stub entry; the caller's ESP before
 * its call; scratch; the 512 bytes below the stub's ESP at entry; the 512
 * bytes below the argument after as_invoke's call; the deep-fill switch
 * and the stub's ESP during the fill. */
__attribute__((used)) static uint32_t as_ecx,as_entry[5],as_origin,as_tmp[3],as_deep,as_deep_esp;
__attribute__((used)) static BYTE as_window[512],as_frame_copy[512];
/* 0 timer_dword_step 467aa8, 1..3 the slots +1Ch, +14h, +18h, 4..6 the
 * members +18h, +24h, +30h. */
static const unsigned as_slots[AS_IDS]={0,0x1c,0x14,0x18,0x18,0x24,0x30};
static uint32_t as_next(void){
    uint32_t z=as_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *as_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int as_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
static uint32_t as_hash(const BYTE *p,unsigned n){uint32_t h=0x811c9dc5u;for(unsigned i=0;i<n;++i)h=(h^p[i])*0x01000193u;return h;}
static void as_swap_vtable(void){
    BYTE *o=as_host(AS_A);uint32_t vt=get32(o);
    if(vt==AS_BASE+AS_VT1)put32(o,AS_BASE+AS_VT2);else if(vt==AS_BASE+AS_VT2)put32(o,AS_BASE+AS_VT1);
}
__attribute__((used,noinline)) static uint64_t __cdecl as_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(as_calls==AS_MAX){fail("actor step call overflow");ExitProcess(1);}
    as_record *v=as_log+as_calls++;memset(v,0,sizeof(*v));v->id=id;
    if(as_track){for(unsigned j=0;j<3;++j)v->saved[j]=as_entry[j];v->saved[3]=as_entry[3]-as_origin;v->saved[4]=as_entry[4]-as_origin;
        v->hash=as_hash(as_window,sizeof(as_window));}
    v->arg=as_stack(a[0])?0:a[0];
    for(unsigned j=0;j<3;++j)v->regs[j]=as_stack(regs[j])?0:regs[j];
    uint32_t eax=as_next(),edx=as_next(),pick=as_next();as_ecx=as_next();
    BYTE *o=as_host(AS_A);
    switch(id){
    case 0: /* timer_dword_step: the timer, the vtable. */
        if(pick%3==0)put32(o+0x52,pick>>8&1?0:as_next());
        if(pick%5==0)as_swap_vtable();
        break;
    case 1: /* decision: a new state (gated or not), the vtable, the timer. */
        if(pick%3==0)put32(o+0x4d,AS_STATE((pick>>4)%AS_STATES));
        if(pick%5==0)as_swap_vtable();
        if(pick%7==0)put32(o+0x52,pick>>12&1?0:as_next());
        break;
    case 2: /* movement: the skip byte, the vtable, the timer. */
        if(pick%4==0)o[0xaf]=(BYTE)(pick>>8&1?0:1+(pick>>12)%255);
        if(pick%5==0)as_swap_vtable();
        if(pick%6==0)put32(o+0x52,as_next());
        break;
    case 3: /* behaviour: AX zero half of the time; the once byte. */
        if(pick%2)eax&=0xffff0000u;else if(!(uint16_t)eax)eax|=1u;
        if(pick%5==0)o[0x83]^=1;
        break;
    default:break;
    }
    return (uint64_t)edx<<32|eax;
}
/* Every stub saves the entry registers and copies the 512 bytes below its
 * ESP before anything writes there; member stubs (deep=1) then fill 288
 * KiB below their entry with 5Ah while as_deep is set. */
#define AS_STUB(n,deep) __attribute__((naked)) static void as_stub_##n(void){__asm__ volatile( \
    "movl %ebx,_as_entry\n\tmovl %esi,_as_entry+4\n\tmovl %edi,_as_entry+8\n\tmovl %ebp,_as_entry+12\n\tmovl %esp,_as_entry+16\n\t" \
    "movl %ecx,_as_tmp\n\tleal -512(%esp),%esi\n\tmovl $_as_window,%edi\n\tmovl $128,%ecx\n\tcld\n\trep movsl\n\t" \
    "movl _as_entry+4,%esi\n\tmovl _as_entry+8,%edi\n\tmovl _as_tmp,%ecx\n\t" \
    ".if " #deep "\n\tcmpl $0,_as_deep\n\tje 2f\n\t" \
    "movl %eax,_as_tmp+4\n\tmovl %edi,_as_tmp+8\n\tmovl %esp,_as_deep_esp\n\tmovl $72,%ecx\n\t" \
    "1: subl $4096,%esp\n\torl $0,(%esp)\n\tloop 1b\n\t" \
    "movl %esp,%edi\n\tmovl $0x5a5a5a5a,%eax\n\tmovl $73728,%ecx\n\trep stosl\n\tmovl _as_deep_esp,%esp\n\t" \
    "movl _as_tmp+4,%eax\n\tmovl _as_tmp+8,%edi\n\tmovl _as_tmp,%ecx\n\t2:\n\t.endif\n\t" \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _as_stub_impl\n\taddl $24,%esp\n\tmovl _as_ecx,%ecx\n\tret\n\t");}
AS_STUB(0,0) AS_STUB(1,0) AS_STUB(2,0) AS_STUB(3,0) AS_STUB(4,1) AS_STUB(5,1) AS_STUB(6,1)
#undef AS_STUB
static void *as_stubs[AS_IDS]={as_stub_0,as_stub_1,as_stub_2,as_stub_3,as_stub_4,as_stub_5,as_stub_6};
static void as_reset(void){as_calls=0;as_stream=as_seed;}
static const unsigned as_members[3]={0x18,0x24,0x30};
static int as_is_member(unsigned index){return index>=ES2_HOOK_INDEX_mech_move&&index<=ES2_HOOK_INDEX_flyer_decide;}
/* One member pointer: empty a quarter of the time, else a stub (or, with
 * real, the real timer_dword_step on a timer record) with a delta and a
 * third dword. A nonzero pointer always has a function (0 would fault). */
static void as_member(uint32_t at,unsigned k,int real){
    BYTE *p=as_host(at);
    if(random32()%4==0){memset(p,0,12);return;}
    static const uint32_t deltas[]={0,4,0xfffffffcu,8,0x10,0xfffffff0u};
    uint32_t delta=random32()%4?deltas[random32()%6]:random32();
    if(real&&random32()%3==0){
        put32(p,(uint32_t)(uintptr_t)(mapped+0x67aa8));put32(p+4,AS_TIMER-AS_ACT+8u*(random32()%8));put32(p+8,0);return;}
    put32(p,(uint32_t)(uintptr_t)as_stubs[4+k]);put32(p+4,delta);put32(p+8,random32()%2?0:random32());
}
/* The arena: actor A (vtable, position, state, timer record A+51h,
 * counter A+56h, bytes A+82h/A+83h, skip byte A+0AFh), two vtables with
 * the slot stubs, four state records (gate byte +8, three members), timer
 * records. real: the actor uses the real MECH or FLYER vtable (class
 * 0/1), the frame-time word is small. */
static void as_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real,unsigned cls){
    as_seed=random32();random_bytes(as_arena,AS_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    for(unsigned v=0;v<2;++v){uint32_t vt=v?AS_VT2:AS_VT1;
        put32(as_arena+vt+0x1c,(uint32_t)(uintptr_t)as_stubs[1]);put32(as_arena+vt+0x14,(uint32_t)(uintptr_t)as_stubs[2]);
        put32(as_arena+vt+0x18,(uint32_t)(uintptr_t)as_stubs[3]);}
    for(unsigned k=0;k<AS_STATES;++k){BYTE *s=as_host(AS_STATE(k));
        s[8]=(BYTE)(random32()%2?0:random32()%2?1:random32());
        for(unsigned j=0;j<3;++j)as_member(AS_STATE(k)+as_members[j],j,real);}
    BYTE *o=as_host(AS_A);
    put32(o,real?(uint32_t)(uintptr_t)(mapped+(cls?0x9a5e0:0x9a282)):AS_BASE+(random32()%2?AS_VT2:AS_VT1));
    put32(o+0x4d,random32()%8==0&&!as_is_member(index)?0:AS_STATE(random32()%AS_STATES));
    put32(o+0x52,random32()%2?0:random32()%2?1+random32()%400:random32());
    o[0xaf]=(BYTE)(random32()%3?0:random32()%2?1:random32());
    o[0x83]=(BYTE)(random32()%3?0:random32()%2?1:random32());
    if(index==ES2_HOOK_INDEX_state_frame&&n%7==0)put32(o+0x56,0xffffffffu);
    if(real)put16(mapped+0xd3b64,(uint16_t)(1+random32()%200));
    a[0]=index==ES2_HOOK_INDEX_state_frame?AS_A+0x4du:AS_A;
}
static int as_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const as_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,as_calls);
    for(unsigned j=0;j<calls&&j<as_calls&&expected;++j)if(memcmp(expected+j,as_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(as_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<AS_SIZE;++j)if(as_after[j]!=as_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,as_after[j],as_arena[j]);break;}
    return 1;
}
static int as_frame_report(const char *what,unsigned n,const BYTE expected[512]){
    fprintf(stderr,"FAIL: %s case %u (%s)\n",current_function,n,what);
    for(unsigned j=0;j<512;++j)if(expected[j]!=as_frame_copy[j]){fprintf(stderr," below the argument -%u: %02x/%02x\n",512-j,expected[j],as_frame_copy[j]);break;}
    return 1;
}
static unsigned as_expected_hits(unsigned index,const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<as_calls;++j){unsigned id=as_log[j].id;
        if(index==ES2_HOOK_INDEX_actor_step&&id>3)continue;
        if(c->event==ES2_COLLIDE_SLOT?id&&as_slots[id]==c->key:!id&&c->key==0x467aa8u)++hits;}
    return hits;
}
static int as_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static as_record expected[AS_MAX];
    memcpy(as_before,as_arena,AS_SIZE);
    as_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=as_calls;memcpy(expected,as_log,calls*sizeof(*expected));
    memcpy(as_after,as_arena,AS_SIZE);
    memcpy(as_arena,as_before,AS_SIZE);as_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[8];
    if(count>8)return fail("actor step counter table too long");
    for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=as_expected_hits(index,c+i))return fail("actor step callee counter delta");
    if(pair[0]!=pair[1]||calls!=as_calls||memcmp(expected,as_log,calls*sizeof(*expected))||memcmp(as_after,as_arena,AS_SIZE))
        return as_report("direct",n,pair,calls,expected);
    return 0;
}
/* Calls target with one slot, EAX/EDX/ECX/EBX = pattern, ESI and EDI
 * distinct, 4 KiB of patterned stack below the argument; returns EAX, EDX
 * and ECX and copies the 512 bytes below the argument after the call. */
__attribute__((naked,noinline)) static void as_invoke(void *target,uint32_t arg,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $4092,%esp\n\t"
        "pushl 12(%ebp)\n\tmovl %esp,_as_origin\n\t"
        "movl 16(%ebp),%eax\n\tmovl %eax,%edx\n\tmovl %eax,%ecx\n\tmovl %eax,%ebx\n\t"
        "movl %eax,%esi\n\txorl $0x5a5a0000,%esi\n\tmovl %eax,%edi\n\txorl $0x00005a5a,%edi\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%ebx\n\tmovl %eax,(%ebx)\n\tmovl %edx,4(%ebx)\n\tmovl %ecx,8(%ebx)\n\t"
        "leal -512(%esp),%esi\n\tmovl $_as_frame_copy,%edi\n\tmovl $128,%ecx\n\tcld\n\trep movsl\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static int as_patch_timer(BYTE saved[5],int on){return actor_patch(0x67aa8,on?as_stubs[0]:NULL,saved,on?TRUE:FALSE);}
static int as_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    const int shim=index!=ES2_HOOK_INDEX_state_frame;as_track=shim;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=poison?0xa7a7a7a7u:0x31313131u;as_build(index,n,a,r,0,0);
        BYTE expected_fpu[108],actual_fpu[108],expected_frame[512];static as_record expected[AS_MAX];
        memcpy(as_before,as_arena,AS_SIZE);
        as_reset();as_invoke(mapped+d->rva,a[0],pattern,expected_out);es2_hook_fpu_save(expected_fpu);memcpy(expected_frame,as_frame_copy,512);
        unsigned calls=as_calls;memcpy(expected,as_log,calls*sizeof(*expected));memcpy(as_after,as_arena,AS_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("actor step poison install");
        memcpy(as_arena,as_before,AS_SIZE);as_reset();as_invoke(mapped+d->rva,a[0],pattern,actual_out);es2_hook_fpu_save(actual_fpu);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||(shim&&expected_out[2]!=actual_out[2])||calls!=as_calls||memcmp(expected,as_log,calls*sizeof(*expected))||
           memcmp(as_after,as_arena,AS_SIZE)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1){
            fprintf(stderr,"ECX %08x/%08x\n",expected_out[2],actual_out[2]);
            status=as_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        }
        else if(shim&&memcmp(expected_frame,as_frame_copy,512))status=as_frame_report(fast?"installed fast frame":"installed preserving frame",n,expected_frame);
        if(!es2_hook_uninstall(&h))return fail("actor step poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));as_track=0;
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, pair/trace/arena%s\n",d->name,total,
        shim?", ECX, EBX/ESI/EDI/EBP/ESP and the 512 bytes below ESP at every callee entry, 512 bytes below the argument":"");
    return status;
}
/* The other seven hooks of the group and timer_dword_step, in an order
 * that gives timer_dword_step and state_frame the same mode (both original
 * in two of the six rotations, where the stack is compared). */
static unsigned as_nested(unsigned index,unsigned nested[8],unsigned position[8]){
    static const unsigned all[]={ES2_HOOK_INDEX_timer_dword_step,ES2_HOOK_INDEX_actor_step,ES2_HOOK_INDEX_mech_move,
        ES2_HOOK_INDEX_state_frame,ES2_HOOK_INDEX_mech_act,ES2_HOOK_INDEX_mech_decide,ES2_HOOK_INDEX_flyer_move_call,
        ES2_HOOK_INDEX_flyer_act,ES2_HOOK_INDEX_flyer_decide};
    unsigned count=0;for(unsigned i=0;i<sizeof(all)/sizeof(all[0]);++i)if(all[i]!=index){position[count]=i;nested[count++]=all[i];}
    return count;
}
static es2_hook_mode as_mode(unsigned mode,unsigned j,unsigned nested){
    es2_hook_mode m=(es2_hook_mode)((mode+j)%3);const es2_hook_extension *e=function_hook_extension(nested);
    return e&&e->compare_unsupported&&m==ES2_HOOK_COMPARE?ES2_HOOK_REPLACED:m;
}
static int as_install_nested(es2_hook hk[9],const unsigned *nested,const unsigned *position,unsigned count,unsigned mode){
    es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
    for(unsigned j=0;j<count;++j){o.mode=as_mode(mode,position[j],nested[j]);
        if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,function_hook_extension(nested[j])))return fail("actor step nested install");}
    return 0;
}
static int as_uninstall(es2_hook *hk,unsigned count,uint64_t *entered){
    int status=0;
    for(unsigned j=0;j<count;++j){if(entered)entered[j]+=hk[j].calls;if(hk[j].mismatches)status=fail("actor step nested compare mismatch");
        if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("actor step nested uninstall");}
    memset(hk,0,sizeof(*hk)*count);return status;
}
/* Real pass: 411cec entered with a real vtable (MECH for the MECH
 * trampolines, FLYER for the FLYER ones, either for actor_step and
 * state_frame), the hook under test original, then replaced, the other
 * hooks in the same rotating mode in both runs (installed afresh for each
 * run). The stack (registers and the 512 bytes below ESP at every stub
 * entry, the 512 bytes below the argument) is compared when
 * timer_dword_step and state_frame are original: a replaced callee's
 * framework leaves its entry flags and call number there, which differ by
 * caller (accepted, as for every replaced leaf). */
static int as_real_pass(unsigned index){
    unsigned nested[8],position[8];const unsigned count=as_nested(index,nested,position);
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,tracked=0,deep=0;uint64_t entered[9]={0};
    static as_record expected[AS_MAX];
    BYTE old_frame_time[2];memcpy(old_frame_time,mapped+0xd3b64,2);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        const unsigned cls=index==ES2_HOOK_INDEX_mech_move||index==ES2_HOOK_INDEX_mech_act||index==ES2_HOOK_INDEX_mech_decide?0:
            index>=ES2_HOOK_INDEX_flyer_move_call&&index<=ES2_HOOK_INDEX_flyer_decide?1:n%2;
        uint32_t a[6],r[3],expected_out[3],actual_out[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;as_build(index,n,a,r,1,cls);
        a[0]=AS_A;
        /* timer_dword_step (position 0) and state_frame (position 3) are
         * original together. */
        as_track=index!=ES2_HOOK_INDEX_state_frame&&mode%3==0;
        as_deep=n%4==3;
        BYTE expected_frame[512],frame_time[2];memcpy(as_before,as_arena,AS_SIZE);memcpy(frame_time,mapped+0xd3b64,2);
        es2_hook hk[9];memset(hk,0,sizeof(hk));
        if(as_install_nested(hk,nested,position,count,mode))return 1;
        as_reset();as_invoke(mapped+0x11cec,a[0],pattern,expected_out);memcpy(expected_frame,as_frame_copy,512);
        unsigned calls=as_calls;memcpy(expected,as_log,calls*sizeof(*expected));memcpy(as_after,as_arena,AS_SIZE);
        if(as_uninstall(hk,count,NULL))return 1;
        if(as_install_nested(hk,nested,position,count,mode))return 1;
        es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+8,d,&o,function_hook_extension(index)))return fail("actor step caller install");
        memcpy(as_arena,as_before,AS_SIZE);memcpy(mapped+0xd3b64,frame_time,2);as_reset();
        as_invoke(mapped+0x11cec,a[0],pattern,actual_out);
        uint64_t pair[2]={(uint64_t)expected_out[1]<<32|expected_out[0],(uint64_t)actual_out[1]<<32|actual_out[0]};
        if(pair[0]!=pair[1]||expected_out[2]!=actual_out[2]||calls!=as_calls||memcmp(expected,as_log,calls*sizeof(*expected))||
           memcmp(as_after,as_arena,AS_SIZE)){
            fprintf(stderr,"real mode=%u ECX %08x/%08x\n",mode,expected_out[2],actual_out[2]);status=as_report("real",n,pair,calls,expected);
        }
        else if(as_track&&memcmp(expected_frame,as_frame_copy,512)){fprintf(stderr,"real mode=%u\n",mode);status=as_frame_report("real frame",n,expected_frame);}
        entered[8]+=hk[8].calls;
        if(as_uninstall(hk,count,entered))return 1;
        if(!es2_hook_uninstall(hk+8))return fail("actor step caller uninstall");
        ++total;tracked+=(unsigned)as_track;deep+=(unsigned)as_deep;
    }
    as_track=0;as_deep=0;memcpy(mapped+0xd3b64,old_frame_time,2);
    if(!status&&!entered[8]){fprintf(stderr,"FAIL: %s real pass never entered the hook\n",d->name);status=1;}
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]&&(nested[j]==ES2_HOOK_INDEX_timer_dword_step||nested[j]==ES2_HOOK_INDEX_actor_step)){
        fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases (%u with the stack compared, %u filling 288 KiB below a member), entered %llu, nested in rotating modes\n",
        d->name,total,tracked,deep,(unsigned long long)entered[8]);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int actor_step_cases(unsigned index,unsigned limit){
    BYTE saved[5];const int timer=index==ES2_HOOK_INDEX_actor_step;
    if(timer&&as_patch_timer(saved,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_ACTOR_STEP_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];as_build(index,n,a,r,0,0);status=as_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=as_installed_probes(index,64);
    if(timer&&as_patch_timer(saved,0))return 1;
    current_case_valid=0;
    if(!limit&&!status)status=as_real_pass(index);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, arena\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
