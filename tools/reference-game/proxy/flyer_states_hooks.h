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
 * flyer_states_pending (it writes no stack and changes no register), and the adapter writes them below ESP at the
 * callee's offsets before that call (flyer_states_stack_call, the
 * flyer_control technique). The record belongs to the invocation only if
 * the dword above its ESP is this call's first argument; without it (direct
 * calls) the plain bridge is used. flyer_decision and flyer_engage have no
 * residue reader below them and no shim. No game instruction is embedded. */
#include "es2/core/flyer_states.h"
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
/* One stack call: each planted byte at the callee's entry ESP - offset. */
typedef es2_machine_plant flyer_states_plant;
static void __cdecl flyer_states_stack_call(uint32_t target,const uint32_t *args,unsigned count,
    es2_weapon_registers *registers,const flyer_states_plant *plant,unsigned plants) {
    weapons_machine_call_at(target,args,count,registers,NULL,0,plant,plants,NULL);
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
 * flyer_states_capture into the record (no stack write, registers kept),
 * then continue into the framework's dispatch. */
static const unsigned flyer_states_shim_bytes[FS_SHIMS]={12,4,8,8,8,8};
__attribute__((unused)) static const uint32_t flyer_states_shim_rva[FS_SHIMS]={0x22ca8u,0x22598u,0x22a50u,0x22a80u,0x22b34u,0x22bdcu};
static int flyer_states_entry(es2_machine *m,unsigned k) {
    flyer_states_residue *p=flyer_states_pending+k;p->esp=m->esp;
    for(unsigned j=0;j<flyer_states_shim_bytes[k];++j)p->bytes[j]=(BYTE)shim_r8(m->esp-flyer_states_capture[k][j]);
    return 0;
}
#define FS_ENTRY(name,k,rva) \
    static int flyer_states_##name##_entry(es2_machine *m,uint32_t bridge) {(void)bridge;return flyer_states_entry(m,k);} \
    ES2_SHIM(flyer_states_##name##_shim,rva,flyer_states_##name##_entry,0);
FS_ENTRY(attack,FS_ATTACK,0x22ca8u)
FS_ENTRY(follow,FS_FOLLOW,0x22598u)
FS_ENTRY(formation,FS_FORMATION,0x22a50u)
FS_ENTRY(search,FS_SEARCH,0x22a80u)
FS_ENTRY(patrol,FS_PATROL,0x22b34u)
FS_ENTRY(scout,FS_SCOUT,0x22bdcu)
#undef FS_ENTRY
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
