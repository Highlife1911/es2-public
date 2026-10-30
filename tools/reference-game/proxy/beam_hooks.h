#ifndef ES2_BEAM_HOOKS_H
#define ES2_BEAM_HOOKS_H
/* beam group (docs/reverse-engineering/weapons.md, section "beam
 * constructor 40b804"): beam_create 40b804, the BEAM constructor
 * below impact 40bf74. Every game callee (object_init, dist_gate, the
 * runtime's array helper 49667c, set_length, rng_next) goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers, so the four nested hooks keep their modes and
 * 49667c (RTL, not a hook) runs as original code. The frame buffer stands
 * for the original's 28h-byte frame. No entry shim (no local or entry
 * register is read before it is written); ECX after the hook is dead at
 * all three call sites in impact (weapons.md). Refuses compare mode. No
 * game instruction is embedded. */
static uint64_t __cdecl beam_beam_create(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[ES2_BEAM_CREATE_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_beam_create);
    return es2_beam_create_registers(&math_native_memory,&g,a[0],a[1],a[2],a[3],a[4],r);
}
/* Callee counters (hk_beam_create_<suffix>), incremented only while the
 * hook is non-original. */
static callee_counter beam_create_counters[]={
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_49667c",ES2_COLLIDE_CALL,0x49667c,0},
    {"call_4927f0",ES2_COLLIDE_CALL,0x4927f0,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
};
static callee_counter *beam_hook_counters(unsigned hook,unsigned *count) {
    if(hook==ES2_HOOK_INDEX_beam_create) { *count=sizeof(beam_create_counters)/sizeof(beam_create_counters[0]);return beam_create_counters; }
    *count=0;return NULL;
}
static const BYTE beam_create_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x57};
static const BYTE beam_frame_lens[]={1,2,3};
static const es2_hook_extension beam_beam_create_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,beam_beam_create,TRUE};
static uint32_t __cdecl beam_beam_create32(const uint32_t *a,void *u) {return (uint32_t)beam_beam_create(a,u,weapons_no_registers);}
static es2_hook_descriptor beam_hook_descriptors[]={
    {ES2_HOOK_ID(beam_create),beam_create_sig,9,6,beam_frame_lens,3,5,beam_beam_create32,NULL,NULL,NULL,0},
};
#endif
