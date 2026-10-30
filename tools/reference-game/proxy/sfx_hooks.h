#ifndef ES2_SFX_HOOKS_H
#define ES2_SFX_HOOKS_H
/* Sound/clock isolation (docs/reverse-engineering/audio.md, section
 * "sound/clock isolation"): the six sound API calls of the simulation,
 * sfx_play 462830, sfx_play_at 4628e0, sfx_place 46299c, sfx_refresh
 * 46297c, sfx_stop 462ac4 and sfx_rate 463114, and the voice/lip timeline
 * of the cockpit radio, sfx_voice 463400, sfx_lip 4634b0 and sfx_lip_due
 * 4634a0 (the clock-reading sound code; its 467828 calls go through the
 * bridge at the original's places). Own code: the context test,
 * the row selection with the RNG stream B draw, the effect gate, the
 * volume, distance and pan arithmetic. Every game callee goes through the
 * weapons guest bridge and callee observer (weapons_graph) with the
 * original's registers: rng_bounded, dist3d_delta, angle_ratio and the
 * group's own sfx_place keep their modes, and the audible part (the SFX
 * manager 464618/464038/4646dc/464118/46466c with the SOS glue and driver
 * behind it) and the view helpers 48c444/48c57c run as original code, so
 * the game still plays every sound with all hooks replaced. Compare mode
 * refused (game calls).
 *
 * The originals return the ECX their last callee left (the SFX manager's,
 * or the entry ECX without a context); that ECX reaches callers of
 * 4628e0/46297c/463114 (radius_hit after meteor_update's sound, the
 * returns of 40a9c4 and 40b43c, fire_update). Each hook therefore has an
 * exit shim: it calls the framework's dispatch with the arguments copied
 * and, if the replacement ran, loads the ECX the core computed (the
 * control_record pattern); EAX and EDX pass through. No game instruction is embedded. */
#include "es2/core/sfx.h"
/* Set by the adapters, consumed by the shims; a nested hook sets and
 * consumes its own pair first. */
#define SFX_EXIT(name) __attribute__((used)) static uint32_t sfx_##name##_ecx,sfx_##name##_ecx_valid;
SFX_EXIT(play) SFX_EXIT(play_at) SFX_EXIT(place) SFX_EXIT(refresh) SFX_EXIT(stop) SFX_EXIT(rate)
SFX_EXIT(voice) SFX_EXIT(lip) SFX_EXIT(lip_due)
#undef SFX_EXIT
static uint64_t __cdecl sfx_play(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sfx_play);uint32_t ecx;
    uint64_t v=es2_sfx_play_registers(&math_native_memory,&g,a[0],r,&ecx);
    sfx_play_ecx=ecx;sfx_play_ecx_valid=1;return v;
}
static uint64_t __cdecl sfx_play_at(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sfx_play_at);uint32_t ecx;
    uint64_t v=es2_sfx_play_at_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
    sfx_play_at_ecx=ecx;sfx_play_at_ecx_valid=1;return v;
}
/* The scratch holds the view-space position (the original's EBP-18h). */
static uint64_t __cdecl sfx_place(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sfx_place);uint32_t ecx;
    uint64_t v=es2_sfx_place_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
    sfx_place_ecx=ecx;sfx_place_ecx_valid=1;return v;
}
static uint64_t __cdecl sfx_refresh(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sfx_refresh);uint32_t ecx;
    uint64_t v=es2_sfx_refresh_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
    sfx_refresh_ecx=ecx;sfx_refresh_ecx_valid=1;return v;
}
static uint64_t __cdecl sfx_stop(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sfx_stop);uint32_t ecx;
    uint64_t v=es2_sfx_stop_registers(&math_native_memory,&g,a[0],r,&ecx);
    sfx_stop_ecx=ecx;sfx_stop_ecx_valid=1;return v;
}
static uint64_t __cdecl sfx_rate(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sfx_rate);uint32_t ecx;
    uint64_t v=es2_sfx_rate_registers(&math_native_memory,&g,a[0],a[1],r,&ecx);
    sfx_rate_ecx=ecx;sfx_rate_ecx_valid=1;return v;
}
/* The voice/lip timeline (463400, 4634b0, 4634a0): the clock reads go
 * through the bridge to the wrapper 467828 (GetTickCount through the import
 * thunk), so the virtual clock sees the same calls. */
#define SFX_CHANNEL_ADAPTER(name) static uint64_t __cdecl sfx_##name(const uint32_t *a,void *u,const uint32_t *r) {     BYTE scratch[16]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sfx_##name);uint32_t ecx;     uint64_t v=es2_sfx_##name##_registers(&math_native_memory,&g,a[0],r,&ecx);     sfx_##name##_ecx=ecx;sfx_##name##_ecx_valid=1;return v; }
SFX_CHANNEL_ADAPTER(voice) SFX_CHANNEL_ADAPTER(lip) SFX_CHANNEL_ADAPTER(lip_due)
#undef SFX_CHANNEL_ADAPTER
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA. */
static callee_counter sfx_play_counters[]={
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_464618",ES2_COLLIDE_CALL,0x464618,0},
    {"call_464038",ES2_COLLIDE_CALL,0x464038,0},
};
static callee_counter sfx_play_at_counters[]={
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_46299c",ES2_COLLIDE_CALL,0x46299c,0},
    {"call_464038",ES2_COLLIDE_CALL,0x464038,0},
};
static callee_counter sfx_place_counters[]={
    {"call_48c444",ES2_COLLIDE_CALL,0x48c444,0},
    {"call_48c57c",ES2_COLLIDE_CALL,0x48c57c,0},
    {"call_47de72",ES2_COLLIDE_CALL,0x47de72,0},
    {"call_464618",ES2_COLLIDE_CALL,0x464618,0},
    {"call_47d32c",ES2_COLLIDE_CALL,0x47d32c,0},
    {"call_4646dc",ES2_COLLIDE_CALL,0x4646dc,0},
};
static callee_counter sfx_refresh_counters[]={
    {"call_46299c",ES2_COLLIDE_CALL,0x46299c,0},
};
static callee_counter sfx_stop_counters[]={
    {"call_464118",ES2_COLLIDE_CALL,0x464118,0},
};
static callee_counter sfx_rate_counters[]={
    {"call_46466c",ES2_COLLIDE_CALL,0x46466c,0},
};
static callee_counter sfx_voice_counters[]={
    {"call_467828",ES2_COLLIDE_CALL,0x467828,0},
    {"call_4634b0",ES2_COLLIDE_CALL,0x4634b0,0},
    {"call_464038",ES2_COLLIDE_CALL,0x464038,0},
};
static callee_counter sfx_lip_counters[]={
    {"call_467828",ES2_COLLIDE_CALL,0x467828,0},
};
static callee_counter sfx_lip_due_counters[]={
    {"call_4634b0",ES2_COLLIDE_CALL,0x4634b0,0},
};
static callee_counter *sfx_hook_counters(unsigned hook,unsigned *count) {
#define SFX_COUNTERS(name) if(hook==ES2_HOOK_INDEX_sfx_##name) { \
        *count=sizeof(sfx_##name##_counters)/sizeof(sfx_##name##_counters[0]);return sfx_##name##_counters; }
    SFX_COUNTERS(play) SFX_COUNTERS(play_at) SFX_COUNTERS(place) SFX_COUNTERS(refresh) SFX_COUNTERS(stop) SFX_COUNTERS(rate)
    SFX_COUNTERS(voice) SFX_COUNTERS(lip) SFX_COUNTERS(lip_due)
#undef SFX_COUNTERS
    *count=0;return NULL;
}
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8];
 * cmp dword [49f8ech],0 (462830 and 4628e0 begin alike) */
static const BYTE sfx_play_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x83,0x3d,0xec,0xf8,0x49,0x00,0x00};
static const BYTE sfx_play_lens[]={1,2,1,1};
/* push ebp; mov ebp,esp; add esp,-18h; cmp dword [49f8ech],0; push ebx;
 * push esi; push edi; mov ebx,[ebp+8] */
static const BYTE sfx_place_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xe8,0x83,0x3d,0xec,0xf8,0x49,0x00,0x00,0x53,0x56,0x57,0x8b,0x5d,0x08};
static const BYTE sfx_place_lens[]={1,2,3};
/* push ebp; mov ebp,esp; cmp dword [49f8ech],0; je +10h; mov eax,[ebp+0Ch] */
static const BYTE sfx_refresh_sig[]={0x55,0x8b,0xec,0x83,0x3d,0xec,0xf8,0x49,0x00,0x00,0x74,0x10,0x8b,0x45,0x0c};
static const BYTE sfx_refresh_lens[]={1,2,7};
/* push ebp; mov ebp,esp; mov eax,[49f8ech]; test eax,eax; je +14h */
static const BYTE sfx_stop_sig[]={0x55,0x8b,0xec,0xa1,0xec,0xf8,0x49,0x00,0x85,0xc0,0x74,0x14};
/* push ebp; mov ebp,esp; mov eax,[49f8ech]; test eax,eax; je +18h */
static const BYTE sfx_rate_sig[]={0x55,0x8b,0xec,0xa1,0xec,0xf8,0x49,0x00,0x85,0xc0,0x74,0x18};
static const BYTE sfx_context_lens[]={1,2,5};
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,[ebp+8];
 * mov eax,[ebx+2Fh] */
static const BYTE sfx_voice_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x5d,0x08,0x8b,0x43,0x2f};
/* push ebp; mov ebp,esp; push ebx; push esi; mov ebx,49f964h */
static const BYTE sfx_lip_sig[]={0x55,0x8b,0xec,0x53,0x56,0xbb,0x64,0xf9,0x49,0x00};
static const BYTE sfx_lip_lens[]={1,2,1,1,5};
/* push ebp; mov ebp,esp; mov eax,[ebp+8]; push eax */
static const BYTE sfx_lip_due_sig[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x50};
#define SFX_EXTENSION(name) \
static const es2_hook_extension sfx_sfx_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,sfx_##name,TRUE}; \
static uint32_t __cdecl sfx_##name##32(const uint32_t *a,void *u) { return (uint32_t)sfx_##name(a,u,weapons_no_registers); }
SFX_EXTENSION(play) SFX_EXTENSION(play_at) SFX_EXTENSION(place) SFX_EXTENSION(refresh) SFX_EXTENSION(stop) SFX_EXTENSION(rate)
SFX_EXTENSION(voice) SFX_EXTENSION(lip) SFX_EXTENSION(lip_due)
#undef SFX_EXTENSION
static es2_hook_descriptor sfx_hook_descriptors[]={
    {ES2_HOOK_ID(sfx_play),sfx_play_sig,15,5,sfx_play_lens,4,1,sfx_play32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_play_at),sfx_play_sig,15,5,sfx_play_lens,4,2,sfx_play_at32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_place),sfx_place_sig,19,6,sfx_place_lens,3,2,sfx_place32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_refresh),sfx_refresh_sig,15,10,sfx_refresh_lens,3,2,sfx_refresh32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_stop),sfx_stop_sig,12,8,sfx_context_lens,3,1,sfx_stop32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_rate),sfx_rate_sig,12,8,sfx_context_lens,3,2,sfx_rate32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_voice),sfx_voice_sig,11,5,sfx_play_lens,4,1,sfx_voice32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_lip),sfx_lip_sig,10,10,sfx_lip_lens,5,1,sfx_lip32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sfx_lip_due),sfx_lip_due_sig,7,6,sfx_place_lens,3,1,sfx_lip_due32,NULL,NULL,NULL,0},
};
/* The exit shims (shim_exit_call): a frame, the arguments copied, the
 * framework's dispatch as a call, then the core's ECX if the replacement
 * ran. EAX and EDX pass through. */
#define SFX_SHIM(name,rva,count) \
    static int sfx_##name##_entry(es2_machine *m,uint32_t bridge) {return shim_exit_call(m,bridge,count,&sfx_##name##_ecx,&sfx_##name##_ecx_valid);} \
    ES2_SHIM(sfx_##name##_shim,rva,sfx_##name##_entry,0);
SFX_SHIM(play,0x62830u,1) SFX_SHIM(play_at,0x628e0u,2) SFX_SHIM(place,0x6299cu,2) SFX_SHIM(refresh,0x6297cu,2)
SFX_SHIM(stop,0x62ac4u,1) SFX_SHIM(rate,0x63114u,2)
SFX_SHIM(voice,0x63400u,1) SFX_SHIM(lip,0x634b0u,1) SFX_SHIM(lip_due,0x634a0u,1)
#undef SFX_SHIM
#endif
