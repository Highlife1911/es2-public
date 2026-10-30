#ifndef ES2_BULLET_CTOR_HOOKS_H
#define ES2_BULLET_CTOR_HOOKS_H
#include "guest_native.h"
/* Bullet constructor (docs/reverse-engineering/weapons.md, section
 * "bullet constructor"): bullet_create 40b43c (called by turret,
 * weapon_fire, emit, target_emit and the original wrapper 40b5a0), its
 * object constructor bullet_init 40af6c and the definition lookup
 * def_lookup 40ffc8 (also called by impact, the projectile constructor
 * 40a948 and the weapon factory 40fff8). Every game callee goes through
 * the weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so the nested hooks (pool_alloc, object_init,
 * bullet_def, rng_next, sfx_play_at and this group's own) keep their modes
 * and the runtime frame setup 49630c and the memset thunk 49681a run as
 * original code. bullet_create and bullet_init refuse compare mode;
 * def_lookup (a walk of the table [4a98fch] bounded by [4a9900h], no store;
 * EAX the record or 0, EDX the index) accepts it with the default
 * comparison. No game instruction is embedded (the shim only loads and
 * stores registers and moves ESP).
 *
 * bullet_create has an entry shim like projectile_create
 * (lc_base_hooks.h): it keeps the original's EBP, stores the caller's
 * EBX/ESI/EDI in the original's save slots (EBP-30h/-34h/-38h), reserves a
 * guard-probed gap of 24 pages (96 KiB: the callees' subtrees, including
 * the 64 KiB comparison storage of a nested hook in compare mode; inside
 * aim_fire's 256 KiB the emitters' 128 KiB, their host frames and this
 * gap still fit) and leaves {EBP, outgoing ESP = EBP-38h} for the adapter, whose frame is then
 * the original one; its game calls run through ai_behaviour_stack_call at
 * the original stack coordinate with the EBX/ESI/EDI the core names and
 * the original's EBP and return addresses. So the runtime record lies at
 * the original EBP-24h (49630c runs with the original EBP) and the frame
 * and the callees' area hold the original bytes for later readers of
 * stale stack. Without the shim (direct and legacy calls) the plain bridge
 * and a synthetic EBP in the adapter's scratch are used
 * (weapons_impact_guest_call). The adapter stores FS:0 from the returned
 * EDX (the record's saved head), as the original's epilogue does. The
 * shim also returns the ECX the original returns (sfx_play_at's, which
 * the original wrapper 40b5a0 keeps in its high word): the adapter leaves
 * it in a static and the shim loads it after the framework's dispatch if
 * the replacement ran. bullet_init and def_lookup have no shim: their
 * ECX (O and the category) is dead at every live caller (weapons.md). */
__attribute__((used)) static ai_behaviour_stack *bullet_ctor_create_pending;
__attribute__((used)) static uint32_t bullet_ctor_create_ecx,bullet_ctor_create_ecx_valid;
/* The core's per-call EBX/ESI/EDI/return address, handed to the stack
 * bridge of ai_behaviour_hooks.h. */
typedef struct bullet_ctor_guest { ai_behaviour_guest stack; es2_weapon_frame frame; } bullet_ctor_guest;
static void bullet_ctor_stack_call(void *context,uint32_t f,const uint32_t *a,unsigned n,es2_weapon_registers *r) {
    bullet_ctor_guest *c=context;
    c->stack.frame.ebx=c->frame.ebx;c->stack.frame.esi=c->frame.esi;c->stack.frame.edi=c->frame.edi;c->stack.frame.ret=c->frame.ret;
    ai_behaviour_stack_guest_call(&c->stack,f,a,n,r);
}
static uint64_t __cdecl bullet_ctor_bullet_create(const uint32_t *a,void *u,const uint32_t *r) {
    const ai_behaviour_stack *s=ai_behaviour2_take(&bullet_ctor_create_pending,a,4);
    BYTE scratch[ES2_BULLET_CREATE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_bullet_create);
    bullet_ctor_guest c={{s,{0,0,0,0}},{0,0,0,0}};uint32_t ebx=0,esi=0,edi=0,ecx=0;
    weapons_impact_context x={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_BULLET_CREATE_SCRATCH)};
    if(s) { g.scratch=s->ebp-ES2_BULLET_CREATE_SCRATCH;g.call=bullet_ctor_stack_call;g.context=&c;
        ebx=math_native_read32(NULL,s->ebp-0x30u);esi=math_native_read32(NULL,s->ebp-0x34u);edi=math_native_read32(NULL,s->ebp-0x38u); }
    else { g.call=weapons_impact_guest_call;g.context=&x; }
    uint64_t result=es2_bullet_create_registers(&math_native_memory,&g,s?&c.frame:NULL,ebx,esi,edi,a[0],a[1],a[2],a[3],r,&ecx);
    uint32_t head=(uint32_t)(result>>32);es2_machine_set_fs0(head);
    bullet_ctor_create_ecx=ecx;bullet_ctor_create_ecx_valid=1;
    return result;
}
static uint64_t __cdecl bullet_ctor_bullet_init(const uint32_t *a,void *u,const uint32_t *r) {
    es2_weapon_graph g=weapons_graph(u,NULL,ES2_HOOK_INDEX_bullet_init);
    return es2_bullet_init_registers(&math_native_memory,&g,a[0],a[1],r);
}
static uint64_t __cdecl bullet_ctor_def_lookup(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_def_lookup_registers(&math_native_memory,(uint32_t)(uintptr_t)u,a[0],a[1]);
}
/* The entry shim: frame 2Ch, save slots EBP-30h/-34h/-38h (ebx, esi, edi;
 * the outgoing ESP is the EDI slot), 24 guard-probed pages, the four
 * argument slots copied for the framework's dispatch; after it, the core's
 * ECX if the replacement ran (a fallback to the original keeps its own). */
static int bullet_ctor_create_entry(es2_machine *m,uint32_t bridge) {
    uint32_t ebp;const uint32_t context=shim_saved_gap(m,bridge,&ebp,-0x30,24u);
    bullet_ctor_create_pending=es2_guest_span(context,sizeof(ai_behaviour_stack));bullet_ctor_create_ecx_valid=0;
    const int ran=shim_gap_call(m,bridge,ebp,context,4);
    bullet_ctor_create_pending=NULL;
    if(bullet_ctor_create_ecx_valid) { m->ecx=bullet_ctor_create_ecx;bullet_ctor_create_ecx_valid=0; }
    return ran;
}
ES2_SHIM(bullet_ctor_create_shim,0xb43cu,bullet_ctor_create_entry,0);
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook is
 * non-original. Keys: the callee VA, or the vtable offset for the slot;
 * def_lookup has no game calls. */
static callee_counter bullet_create_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_40af6c",ES2_COLLIDE_CALL,0x40af6c,0},
    {"call_40adc0",ES2_COLLIDE_CALL,0x40adc0,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"slot_38",ES2_COLLIDE_SLOT,0x38,0},
    {"call_4628e0",ES2_COLLIDE_CALL,0x4628e0,0},
};
static callee_counter bullet_init_counters[]={
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_40ffc8",ES2_COLLIDE_CALL,0x40ffc8,0},
    {"call_40adc0",ES2_COLLIDE_CALL,0x40adc0,0},
};
static callee_counter *bullet_ctor_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_bullet_create) {
        *count=sizeof(bullet_create_counters)/sizeof(bullet_create_counters[0]);return bullet_create_counters; }
    if(hook==ES2_HOOK_INDEX_bullet_init) {
        *count=sizeof(bullet_init_counters)/sizeof(bullet_init_counters[0]);return bullet_init_counters; }
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; add esp,-2Ch; mov eax,498549h (40b43c) / push
 * ebx; push esi; push edi; mov ecx,[4a96f8h] (40af6c) / push ebx; mov
 * eax,[4a98fch] (40ffc8). */
static const BYTE bullet_ctor_create_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd4,0xb8,0x49,0x85,0x49,0x00};
static const BYTE bullet_ctor_init_sig[]={0x55,0x8b,0xec,0x53,0x56,0x57,0x8b,0x0d,0xf8,0x96,0x4a,0x00};
static const BYTE bullet_ctor_lookup_sig[]={0x55,0x8b,0xec,0x53,0xa1,0xfc,0x98,0x4a,0x00};
static const BYTE bullet_ctor_create_lens[]={1,2,3},bullet_ctor_init_lens[]={1,2,1,1},bullet_ctor_lookup_lens[]={1,2,1,5};
#define BULLET_CTOR_EXT(name,refuse) \
    static const es2_hook_extension bullet_ctor_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,bullet_ctor_##name,refuse}; \
    static uint32_t __cdecl bullet_ctor_##name##32(const uint32_t *a,void *u) {return (uint32_t)bullet_ctor_##name(a,u,weapons_no_registers);}
BULLET_CTOR_EXT(bullet_create,TRUE)
BULLET_CTOR_EXT(bullet_init,TRUE)
BULLET_CTOR_EXT(def_lookup,FALSE)
#undef BULLET_CTOR_EXT
static es2_hook_descriptor bullet_ctor_hook_descriptors[]={
    {ES2_HOOK_ID(bullet_create),bullet_ctor_create_sig,11,6,bullet_ctor_create_lens,3,4,bullet_ctor_bullet_create32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bullet_init),bullet_ctor_init_sig,12,5,bullet_ctor_init_lens,4,2,bullet_ctor_bullet_init32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(def_lookup),bullet_ctor_lookup_sig,9,9,bullet_ctor_lookup_lens,4,2,bullet_ctor_def_lookup32,NULL,NULL,NULL,0},
};
#endif
