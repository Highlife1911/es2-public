#ifndef ES2_FLYER_STATES_HOOKS_H
#define ES2_FLYER_STATES_HOOKS_H
/* FLYER AI state methods (docs/reverse-engineering/flyer.md, section
 * "the FLYER AI state methods"): flyer_attacking 422ca8, flyer_follow 422598, flyer_formation 422a50,
 * flyer_search 422a80, flyer_patrol 422b34, flyer_scout 422bdc,
 * flyer_decision 422d00 and flyer_engage 422bf0. Every game callee goes
 * through the weapons guest bridge and callee observer (weapons_graph) with
 * the original's registers, so all eight refuse compare mode. flyer_follow's
 * frame (P, the zero dword, the distance) is the adapter's scratch.
 *
 * Stack residue: below these methods two callees read uninitialized stack
 * at fixed depths under their own entry ESP: flyer_steer 4222fc (the four
 * bytes that reach the flight model's pose byte, flyer.md "flyer control") and
 * target_emit 4226a0 (its control dword at entry ESP-14h, weapons.md). In
 * an all-original call those positions are not written between the entry
 * of the outer method and the reading call (depth table in flyer.md), so
 * each entry shim copies the bytes from its own entry offsets into
 * flyer_states_pending (it writes no stack and changes no register; EAX is
 * parked in a static), and the adapter writes them below ESP at the
 * callee's offsets before that call (flyer_states_stack_call, the
 * flyer_control technique). The record belongs to the invocation only if
 * the dword above its ESP is this call's first argument; without it (direct
 * calls) the plain bridge is used. flyer_decision and flyer_engage have no
 * residue reader below them and no shim. The shims are chained from
 * mech_combat_capture_entry. No game instruction is embedded. */
#include "es2/core/flyer_states.h"
#include "sim_bridge.h"
static callee_counter flyer_attacking_counters[]={
    {"call_4226a0",ES2_COLLIDE_CALL,0x4226a0,0},
    {"call_422598",ES2_COLLIDE_CALL,0x422598,0},
};
static callee_counter flyer_follow_counters[]={
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"slot_78",ES2_COLLIDE_SLOT,0x78,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_422108",ES2_COLLIDE_CALL,0x422108,0},
    {"call_4222fc",ES2_COLLIDE_CALL,0x4222fc,0},
    {"call_422260",ES2_COLLIDE_CALL,0x422260,0},
};
static callee_counter flyer_formation_counters[]={
    {"call_4224c4",ES2_COLLIDE_CALL,0x4224c4,0},
    {"call_422598",ES2_COLLIDE_CALL,0x422598,0},
};
static callee_counter flyer_search_counters[]={
    {"call_422a50",ES2_COLLIDE_CALL,0x422a50,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_423918",ES2_COLLIDE_CALL,0x423918,0},
    {"call_422bf0",ES2_COLLIDE_CALL,0x422bf0,0},
};
static callee_counter flyer_patrol_counters[]={
    {"call_422a50",ES2_COLLIDE_CALL,0x422a50,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_422bf0",ES2_COLLIDE_CALL,0x422bf0,0},
};
static callee_counter flyer_scout_counters[]={
    {"call_422a50",ES2_COLLIDE_CALL,0x422a50,0},
};
static callee_counter flyer_decision_counters[]={
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
};
static callee_counter flyer_engage_counters[]={
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_422bf0",ES2_COLLIDE_CALL,0x422bf0,0},
};
static callee_counter *flyer_states_hook_counters(unsigned hook,unsigned *count) {
#define FLYER_STATES_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { \
        *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
    FLYER_STATES_COUNTERS(flyer_attacking)
    FLYER_STATES_COUNTERS(flyer_follow)
    FLYER_STATES_COUNTERS(flyer_formation)
    FLYER_STATES_COUNTERS(flyer_search)
    FLYER_STATES_COUNTERS(flyer_patrol)
    FLYER_STATES_COUNTERS(flyer_scout)
    FLYER_STATES_COUNTERS(flyer_decision)
    FLYER_STATES_COUNTERS(flyer_engage)
#undef FLYER_STATES_COUNTERS
    *count=0;return NULL;
}
/* Residue records, one per shimmed hook: the entry ESP and up to twelve
 * bytes read at the offsets below (each 16 bytes, the shims address them
 * as flyer_states_pending + 16k). */
typedef struct flyer_states_residue { uint32_t esp; BYTE bytes[12]; } flyer_states_residue;
enum { FS_ATTACK, FS_FOLLOW, FS_FORMATION, FS_SEARCH, FS_PATROL, FS_SCOUT, FS_SHIMS };
_Static_assert(sizeof(flyer_states_residue)==16,"shim record layout");
__attribute__((used)) static flyer_states_residue flyer_states_pending[FS_SHIMS];
__attribute__((used)) static uint32_t flyer_states_eax;
/* Offsets below the reading callee's entry ESP: flyer_steer 4222fc reads
 * -229h/-23Dh (the 42215c path) and -20Dh/-221h (the 4221a8 path);
 * target_emit 4226a0 reads the dword at -14h. Depth of each reading call
 * below the entry of its caller: 422598 calls 4222fc at 38h, 4224c4 at 30h,
 * 4226a0 at 60h; 422a50 calls 4224c4/422598 at 10h; 422ca8 calls
 * 4226a0/422598 at 14h; 422a80, 422b34, 422bdc call 422a50 at 14h, 18h, 0Ch. */
#define FS_STEER(d) (d)+0x229u,(d)+0x23du,(d)+0x20du,(d)+0x221u
#define FS_FORMATION_SET(d) FS_STEER((d)+0x40u),FS_STEER((d)+0x48u)
static const uint32_t flyer_states_capture[FS_SHIMS][12]={
    {0x28u,0x27u,0x26u,0x25u,FS_STEER(0x14u+0x60u),FS_STEER(0x14u+0x38u)},
    {FS_STEER(0x38u)},
    {FS_FORMATION_SET(0)},
    {FS_FORMATION_SET(0x14u)},
    {FS_FORMATION_SET(0x18u)},
    {FS_FORMATION_SET(0x0cu)},
};
/* One stack call: push the slots, write each planted byte at callee-entry
 * ESP - offset (the CALL pushes the return address at entry ESP, so ESP
 * after the pushes is entry + 4), load EAX/EDX/ECX, call, store them back.
 * Caller cleanup. */
typedef struct flyer_states_plant { uint32_t offset,byte; } flyer_states_plant;
__attribute__((naked,noinline)) static void __cdecl flyer_states_stack_call(uint32_t target,const uint32_t *args,
    unsigned count,es2_weapon_registers *registers,const flyer_states_plant *plant,unsigned plants) {
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        ES2_SIM_BRIDGE_ARG8
        "movl 12(%ebp),%esi\n\tmovl 16(%ebp),%ebx\n\t"
        "testl %ebx,%ebx\n\tjz 2f\n\t1: decl %ebx\n\tpushl (%esi,%ebx,4)\n\ttestl %ebx,%ebx\n\tjnz 1b\n\t"
        "2: movl 24(%ebp),%esi\n\tmovl 28(%ebp),%ecx\n\ttestl %ecx,%ecx\n\tjz 4f\n\t"
        "3: movl %esp,%eax\n\tsubl (%esi),%eax\n\tmovb 4(%esi),%dl\n\tmovb %dl,-4(%eax)\n\taddl $8,%esi\n\tdecl %ecx\n\tjnz 3b\n\t"
        "4: movl 20(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Up to two planting callees per hook. */
typedef struct flyer_states_site { uint32_t target; unsigned count; flyer_states_plant plant[8]; } flyer_states_site;
typedef struct flyer_states_stack { flyer_states_site site[2]; } flyer_states_stack;
static void flyer_states_guest_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    const flyer_states_stack *s=context;
    for(unsigned i=0;i<2;++i)if(s->site[i].count&&s->site[i].target==f) {
        flyer_states_stack_call(f,a,n,r,s->site[i].plant,s->site[i].count);return;
    }
    weapons_machine_call(f,a,n,r);
}
/* Site i: callee RVA, `count` bytes from `first` of the record, planted at
 * the given offsets below the callee's entry ESP. */
static void flyer_states_site_set(flyer_states_site *s,void *image,uint32_t rva,const BYTE *bytes,unsigned first,
    const uint32_t *offsets,unsigned count) {
    s->target=(uint32_t)(uintptr_t)image+rva;s->count=count;
    for(unsigned j=0;j<count;++j) { s->plant[j].offset=offsets[j];s->plant[j].byte=bytes[first+j]; }
}
/* The shim's record, if it belongs to this invocation (consumed either way). */
static int flyer_states_take(unsigned k,uint32_t first_argument,BYTE out[12]) {
    flyer_states_residue *p=flyer_states_pending+k;const uint32_t esp=p->esp;p->esp=0;
    if(!esp||math_native_read32(NULL,esp+4u)!=first_argument)return 0;
    memcpy(out,p->bytes,12);return 1;
}
/* The planting sites of each shimmed hook, from the record's bytes. */
static int flyer_states_sites(unsigned k,uint32_t first_argument,void *image,flyer_states_stack *s) {
    BYTE b[12];memset(s,0,sizeof(*s));
    if(!flyer_states_take(k,first_argument,b))return 0;
    static const uint32_t steer[4]={FS_STEER(0)},follow[4]={FS_STEER(0x38u)},route[4]={FS_STEER(0x30u)},
        emit[8]={0x14u,0x13u,0x12u,0x11u,FS_STEER(0x60u)},formation[8]={FS_FORMATION_SET(0)};
    switch(k) {
    case FS_ATTACK:
        flyer_states_site_set(s->site,image,0x226a0u,b,0,emit,8);
        flyer_states_site_set(s->site+1,image,0x22598u,b,8,follow,4);break;
    case FS_FOLLOW:flyer_states_site_set(s->site,image,0x222fcu,b,0,steer,4);break;
    case FS_FORMATION:
        flyer_states_site_set(s->site,image,0x224c4u,b,0,route,4);
        flyer_states_site_set(s->site+1,image,0x22598u,b,4,follow,4);break;
    default:flyer_states_site_set(s->site,image,0x22a50u,b,0,formation,8);break;
    }
    return 1;
}
#define FLYER_STATES_GRAPH(name,k,scratch) \
    flyer_states_stack stack;es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_##name); \
    if(flyer_states_sites(k,a[0],u,&stack)) { g.call=flyer_states_guest_call;g.context=&stack; }
static uint64_t __cdecl flyer_states_flyer_attacking(const uint32_t *a,void *u,const uint32_t *r) {
    FLYER_STATES_GRAPH(flyer_attacking,FS_ATTACK,NULL)
    return es2_flyer_attacking_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl flyer_states_flyer_follow(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_FLYER_FOLLOW_SCRATCH]={0};
    FLYER_STATES_GRAPH(flyer_follow,FS_FOLLOW,frame)
    return es2_flyer_follow_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl flyer_states_flyer_formation(const uint32_t *a,void *u,const uint32_t *r) {
    FLYER_STATES_GRAPH(flyer_formation,FS_FORMATION,NULL)
    return es2_flyer_formation_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl flyer_states_flyer_search(const uint32_t *a,void *u,const uint32_t *r) {
    FLYER_STATES_GRAPH(flyer_search,FS_SEARCH,NULL)
    return es2_flyer_search_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl flyer_states_flyer_patrol(const uint32_t *a,void *u,const uint32_t *r) {
    FLYER_STATES_GRAPH(flyer_patrol,FS_PATROL,NULL)
    return es2_flyer_patrol_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl flyer_states_flyer_scout(const uint32_t *a,void *u,const uint32_t *r) {
    FLYER_STATES_GRAPH(flyer_scout,FS_SCOUT,NULL)
    return es2_flyer_scout_registers(&math_native_memory,&g,a[0],r);
}
#undef FLYER_STATES_GRAPH
static uint64_t __cdecl flyer_states_flyer_decision(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flyer_decision);
    return es2_flyer_decision_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl flyer_states_flyer_engage(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_flyer_engage);
    return es2_flyer_engage_registers(&math_native_memory,&g,a[0],r);
}
/* Entry shims: copy the residue bytes at the offsets of
 * flyer_states_capture into the record (no stack write, flags and
 * registers kept), then continue into the framework's dispatch. */
__attribute__((used)) static void *flyer_states_bridge[FS_SHIMS];
#define FS_STR2(x) #x
#define FS_STR(x) FS_STR2(x)
#define FS_BYTE(k,i,o) "movb -" FS_STR(o) "(%esp),%al\n\tmovb %al,_flyer_states_pending+" FS_STR(16*k+4+i) "\n\t"
#define FS_SHIM_HEAD(k) "movl %eax,_flyer_states_eax\n\tmovl %esp,_flyer_states_pending+" FS_STR(16*k) "\n\t"
#define FS_SHIM_TAIL(k) "movl _flyer_states_eax,%eax\n\tjmp *_flyer_states_bridge+" FS_STR(4*k) "\n\t"
#define FS_SHIM4(k,o0,o1,o2,o3) FS_BYTE(k,0,o0) FS_BYTE(k,1,o1) FS_BYTE(k,2,o2) FS_BYTE(k,3,o3)
#define FS_SHIM8(k,o0,o1,o2,o3,o4,o5,o6,o7) FS_SHIM4(k,o0,o1,o2,o3) FS_BYTE(k,4,o4) FS_BYTE(k,5,o5) FS_BYTE(k,6,o6) FS_BYTE(k,7,o7)
/* The literal offsets repeat flyer_states_capture (checked at install). */
__attribute__((naked)) static void flyer_states_attack_entry(void) { __asm__ volatile(FS_SHIM_HEAD(0)
    FS_SHIM8(0,0x28,0x27,0x26,0x25,0x29d,0x2b1,0x281,0x295) FS_BYTE(0,8,0x275) FS_BYTE(0,9,0x289) FS_BYTE(0,10,0x259)
    FS_BYTE(0,11,0x26d) FS_SHIM_TAIL(0)); }
__attribute__((naked)) static void flyer_states_follow_entry(void) { __asm__ volatile(FS_SHIM_HEAD(1)
    FS_SHIM4(1,0x261,0x275,0x245,0x259) FS_SHIM_TAIL(1)); }
__attribute__((naked)) static void flyer_states_formation_entry(void) { __asm__ volatile(FS_SHIM_HEAD(2)
    FS_SHIM8(2,0x269,0x27d,0x24d,0x261,0x271,0x285,0x255,0x269) FS_SHIM_TAIL(2)); }
__attribute__((naked)) static void flyer_states_search_entry(void) { __asm__ volatile(FS_SHIM_HEAD(3)
    FS_SHIM8(3,0x27d,0x291,0x261,0x275,0x285,0x299,0x269,0x27d) FS_SHIM_TAIL(3)); }
__attribute__((naked)) static void flyer_states_patrol_entry(void) { __asm__ volatile(FS_SHIM_HEAD(4)
    FS_SHIM8(4,0x281,0x295,0x265,0x279,0x289,0x29d,0x26d,0x281) FS_SHIM_TAIL(4)); }
__attribute__((naked)) static void flyer_states_scout_entry(void) { __asm__ volatile(FS_SHIM_HEAD(5)
    FS_SHIM8(5,0x275,0x289,0x259,0x26d,0x27d,0x291,0x261,0x275) FS_SHIM_TAIL(5)); }
static void *const flyer_states_entries[FS_SHIMS]={flyer_states_attack_entry,flyer_states_follow_entry,
    flyer_states_formation_entry,flyer_states_search_entry,flyer_states_patrol_entry,flyer_states_scout_entry};
static const uint32_t flyer_states_shim_rva[FS_SHIMS]={0x22ca8u,0x22598u,0x22a50u,0x22a80u,0x22b34u,0x22bdcu};
static const unsigned flyer_states_shim_bytes[FS_SHIMS]={12,4,8,8,8,8};
/* The shims' literal offsets must equal flyer_states_capture. The shim is
 * a fixed sequence: movl %eax,moffs (A3 imm32), movl %esp,mem (89 25
 * imm32), then per byte movb disp(%esp),%al (8A 44 24 disp8 or 8A 84 24
 * disp32) and movb %al,moffs (A2 imm32). */
static BOOL flyer_states_shim_offsets_ok(unsigned k) {
    const BYTE *p=(const BYTE *)flyer_states_entries[k];
    if(p[0]!=0xa3||p[5]!=0x89||p[6]!=0x25)return FALSE;
    p+=11;
    for(unsigned j=0;j<flyer_states_shim_bytes[k];++j) {
        uint32_t offset;
        if(p[0]==0x8a&&p[1]==0x44&&p[2]==0x24) { offset=0u-(uint32_t)(int32_t)(int8_t)p[3];p+=4; }
        else if(p[0]==0x8a&&p[1]==0x84&&p[2]==0x24) { offset=0u-math_native_read32(NULL,(uint32_t)(uintptr_t)(p+3));p+=7; }
        else return FALSE;
        if(offset!=flyer_states_capture[k][j]||p[0]!=0xa2)return FALSE;
        p+=5;
    }
    return TRUE;
}
/* Called by mech_combat_capture_entry after the framework installed its
 * jump: redirect it to the shim and keep h->patch consistent, so uninstall
 * still validates the exact patch. The chain continues in
 * mech_travel_capture_entry (mech_travel_hooks.h). */
static BOOL mech_travel_capture_entry(es2_hook *h);
static BOOL flyer_states_capture_entry(es2_hook *h) {
    unsigned k=0;
    while(k<FS_SHIMS&&flyer_states_shim_rva[k]!=h->descriptor->rva)++k;
    if(k==FS_SHIMS)return mech_travel_capture_entry(h);
    if(!flyer_states_shim_offsets_ok(k))return FALSE;
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    flyer_states_bridge[k]=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)flyer_states_entries[k]-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static const BYTE flyer_states_attack_sig[]={0x55,0x8b,0xec,0x56,0x57,0x8b,0x45,0x08};
static const BYTE flyer_states_follow_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE flyer_states_formation_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
static const BYTE flyer_states_search_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE flyer_states_patrol_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE flyer_states_scout_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08};
static const BYTE flyer_states_decision_sig[]={0x55,0x8b,0xec,0x53,0x8b,0x55,0x08};
static const BYTE flyer_states_engage_sig[]={0x55,0x8b,0xec,0x51,0x53,0x56,0x8b,0x5d,0x08};
static const BYTE flyer_states_push_lens[]={1,2,1,1},flyer_states_frame_lens[]={1,2,3},flyer_states_load_lens[]={1,2,1,3};
#define FLYER_STATES_EXT(name) \
    static const es2_hook_extension flyer_states_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,flyer_states_##name,TRUE}; \
    static uint32_t __cdecl flyer_states_##name##32(const uint32_t *a,void *u) {return (uint32_t)flyer_states_##name(a,u,weapons_no_registers);}
FLYER_STATES_EXT(flyer_attacking)
FLYER_STATES_EXT(flyer_follow)
FLYER_STATES_EXT(flyer_formation)
FLYER_STATES_EXT(flyer_search)
FLYER_STATES_EXT(flyer_patrol)
FLYER_STATES_EXT(flyer_scout)
FLYER_STATES_EXT(flyer_decision)
FLYER_STATES_EXT(flyer_engage)
#undef FLYER_STATES_EXT
static es2_hook_descriptor flyer_states_hook_descriptors[]={
    {ES2_HOOK_ID(flyer_attacking),flyer_states_attack_sig,8,5,flyer_states_push_lens,4,1,flyer_states_flyer_attacking32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_follow),flyer_states_follow_sig,12,6,flyer_states_frame_lens,3,1,flyer_states_flyer_follow32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_formation),flyer_states_formation_sig,7,7,flyer_states_load_lens,4,1,flyer_states_flyer_formation32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_search),flyer_states_search_sig,8,5,flyer_states_push_lens,4,1,flyer_states_flyer_search32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_patrol),flyer_states_patrol_sig,9,5,flyer_states_push_lens,4,1,flyer_states_flyer_patrol32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_scout),flyer_states_scout_sig,6,6,flyer_states_frame_lens,3,1,flyer_states_flyer_scout32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_decision),flyer_states_decision_sig,7,7,flyer_states_load_lens,4,1,flyer_states_flyer_decision32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(flyer_engage),flyer_states_engage_sig,9,5,flyer_states_push_lens,4,1,flyer_states_flyer_engage32,NULL,NULL,NULL,0},
};
#endif
