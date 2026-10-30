#ifndef ES2_CONFIG_HOOKS_H
#define ES2_CONFIG_HOOKS_H
/* Group config: DBSIM's configuration readers and the tape header
 * records as own code (docs/reverse-engineering/file_io.md, section
 * "configuration and tape files as own code"): prefs_video 45e510,
 * prefs_load 459770, prefs_store 459b94, keyjoy_load 45b7a8,
 * sound_config 46379c, tape_save 45cca4 and tape_restore 45cd5c
 * (core/src/config.c).
 *
 * Every game, runtime and KERNEL32 call goes through the weapons guest
 * bridge with the original's registers: the CW3220 stream functions and
 * the string imports through DBSIM's thunks (so the CW3220 import shim,
 * when armed, serves them in its own modes: its file I/O stays below these
 * hooks), GetPrivateProfileStringA through its thunk 496910 (stdcall; the
 * bridge restores ESP from its frame), the path builder 45ee00, the read
 * and write stream classes and the prefs callbacks as original game code.
 * The original's frame lives in a zeroed scratch block at the original's
 * EBP offsets; prefs_load and prefs_store call the runtime frame setup
 * 49630c with that synthetic EBP (weapons_impact_guest_call) and the
 * adapter restores FS:0 to the record's saved head (the returned EAX) as
 * the original's epilogue does.
 *
 * Not reproduced (documented in file_io.md): the stack below the
 * caller's ESP (the original's buffers stay there as residue), and the
 * bytes the original takes from that residue: a prefs file that cannot
 * be opened or is shorter than 54 bytes (prefs_store writes its stale
 * buffer), a short tape read; both count hk_<hook>_residue. An index
 * outside the frame (prefs_store) and a negative tape length are not
 * modeled (hk_<hook>_unmodeled). Exit ECX is not returned (no caller
 * reads it before a write). All seven refuse compare mode: they write
 * files or game state through calls a second phase would repeat. No
 * entry shim, no game instruction embedded. */
#include "es2/core/config.h"
static uint64_t config_frame_call(uint64_t (*body)(const es2_guest_memory *,const es2_weapon_graph *,uint32_t,uint32_t,
    const uint32_t *,uint32_t *),const uint32_t *a,void *u,const uint32_t *r,unsigned hook) {
    BYTE scratch[ES2_CONFIG_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,hook);
    weapons_impact_context c={(uint32_t)(uintptr_t)u,(uint32_t)(uintptr_t)(scratch+ES2_CONFIG_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t result=body(&math_native_memory,&g,a[0],a[1],r,NULL);
    uint32_t head=(uint32_t)result;__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");return result;
}
static uint64_t config_load_body(const es2_guest_memory *m,const es2_weapon_graph *g,uint32_t flag,uint32_t unused,
    const uint32_t *r,uint32_t *ecx) {
    (void)unused;return es2_prefs_load_registers(m,g,flag,r,ecx);
}
static uint64_t __cdecl config_prefs_load(const uint32_t *a,void *u,const uint32_t *r) {
    const uint32_t b[2]={a[0],0};return config_frame_call(config_load_body,b,u,r,ES2_HOOK_INDEX_prefs_load);
}
static uint64_t __cdecl config_prefs_store(const uint32_t *a,void *u,const uint32_t *r) {
    return config_frame_call(es2_prefs_store_registers,a,u,r,ES2_HOOK_INDEX_prefs_store);
}
static uint64_t __cdecl config_prefs_video(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_CONFIG_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_prefs_video);
    return es2_prefs_video_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl config_keyjoy_load(const uint32_t *a,void *u,const uint32_t *r) {
    (void)a;BYTE scratch[ES2_CONFIG_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_keyjoy_load);
    return es2_keyjoy_load_registers(&math_native_memory,&g,r,NULL);
}
static uint64_t __cdecl config_sound_config(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_CONFIG_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_sound_config);
    return es2_sound_config_registers(&math_native_memory,&g,a[0],r,NULL);
}
static uint64_t __cdecl config_tape_save(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_CONFIG_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_tape_save);
    return es2_tape_save_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
static uint64_t __cdecl config_tape_restore(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE scratch[ES2_CONFIG_SCRATCH]={0};es2_weapon_graph g=weapons_graph(u,scratch,ES2_HOOK_INDEX_tape_restore);
    return es2_tape_restore_registers(&math_native_memory,&g,a[0],a[1],r,NULL);
}
/* Callee counters (hk_<hook>_<suffix>), incremented only while the hook
 * is non-original. Keys: the callee VA; callback = an indirect call of a
 * prefs callback (key ES2_CONFIG_CALLBACK); residue and unmodeled: see
 * above (expected 0 in the suites). */
#define CONFIG_CALL(va) {"call_" #va,ES2_COLLIDE_CALL,0x##va,0}
#define CONFIG_RESIDUE {"residue",ES2_COLLIDE_CALL,ES2_CONFIG_RESIDUE,0}
#define CONFIG_UNMODELED {"unmodeled",ES2_COLLIDE_CALL,ES2_CONFIG_UNMODELED,0}
static callee_counter config_prefs_video_counters[]={
    CONFIG_CALL(45ee00),CONFIG_CALL(496802),CONFIG_CALL(49684a),CONFIG_CALL(496838)};
static callee_counter config_prefs_load_counters[]={
    CONFIG_CALL(49630c),CONFIG_CALL(47ad40),CONFIG_CALL(49681a),CONFIG_CALL(47af98),
    {"callback",ES2_COLLIDE_CALL,ES2_CONFIG_CALLBACK,0},CONFIG_CALL(47ad78)};
static callee_counter config_prefs_store_counters[]={
    CONFIG_CALL(49630c),CONFIG_CALL(47ad40),CONFIG_CALL(47af98),CONFIG_CALL(47b064),CONFIG_CALL(47b2b8),
    CONFIG_CALL(47b09c),CONFIG_CALL(47ad78),CONFIG_RESIDUE,CONFIG_UNMODELED};
static callee_counter config_keyjoy_load_counters[]={CONFIG_CALL(45ee00),CONFIG_CALL(496910),CONFIG_CALL(496868)};
static callee_counter config_sound_config_counters[]={
    CONFIG_CALL(45ee00),CONFIG_CALL(496910),CONFIG_CALL(496868),CONFIG_CALL(496880)};
static callee_counter config_tape_save_counters[]={
    CONFIG_CALL(496802),CONFIG_CALL(49682c),CONFIG_CALL(4967de),CONFIG_CALL(496772),CONFIG_CALL(496832),
    CONFIG_CALL(49684a),CONFIG_CALL(496838),CONFIG_RESIDUE,CONFIG_UNMODELED};
static callee_counter config_tape_restore_counters[]={
    CONFIG_CALL(496802),CONFIG_CALL(49684a),CONFIG_CALL(496832),CONFIG_CALL(496838),CONFIG_RESIDUE,CONFIG_UNMODELED};
#undef CONFIG_CALL
#undef CONFIG_RESIDUE
#undef CONFIG_UNMODELED
static callee_counter *config_hook_counters(unsigned hook,unsigned *count) {
#define CONFIG_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name){*count=sizeof(config_##name##_counters)/sizeof(config_##name##_counters[0]);return config_##name##_counters;}
    CONFIG_COUNTERS(prefs_video) CONFIG_COUNTERS(prefs_load) CONFIG_COUNTERS(prefs_store) CONFIG_COUNTERS(keyjoy_load)
    CONFIG_COUNTERS(sound_config) CONFIG_COUNTERS(tape_save) CONFIG_COUNTERS(tape_restore)
#undef CONFIG_COUNTERS
    *count=0;return NULL;
}
/* 45e510: push ebp; mov ebp,esp; add esp,-8; cmp byte [49f254h],0 */
static const BYTE config_prefs_video_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x80,0x3d,0x54,0xf2,0x49,0x00,0x00};
/* 459770: push ebp; mov ebp,esp; add esp,-38h; mov eax,49e92ch; push ebx */
static const BYTE config_prefs_load_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xc8,0xb8,0x2c,0xe9,0x49,0x00,0x53};
/* 459b94: push ebp; mov ebp,esp; add esp,-70h; push ebx; push esi; mov ebx,[ebp+0Ch] */
static const BYTE config_prefs_store_sig[]={0x55,0x8b,0xec,0x83,0xc4,0x90,0x53,0x56,0x8b,0x5d,0x0c};
/* 45b7a8: push ebx; push esi; add esp,-28h; mov ebx,49ebe0h */
static const BYTE config_keyjoy_load_sig[]={0x53,0x56,0x83,0xc4,0xd8,0xbb,0xe0,0xeb,0x49,0x00};
/* 46379c: push ebp; mov ebp,esp; add esp,-28h; xor eax,eax; xor edx,edx; xor ecx,ecx */
static const BYTE config_sound_config_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x33,0xc0,0x33,0xd2,0x33,0xc9};
/* 45cca4: push ebp; mov ebp,esp; add esp,-1F8h; push ebx; push esi; push edi; push 49efa8h */
static const BYTE config_tape_save_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x08,0xfe,0xff,0xff,0x53,0x56,0x57,0x68,0xa8,0xef,0x49,0x00};
/* 45cd5c: push ebp; mov ebp,esp; add esp,-1F8h; push ebx; push esi; push edi; mov eax,[ebp+0Ch] */
static const BYTE config_tape_restore_sig[]={0x55,0x8b,0xec,0x81,0xc4,0x08,0xfe,0xff,0xff,0x53,0x56,0x57,0x8b,0x45,0x0c};
static const BYTE config_frame_lens[]={1,2,3};
static const BYTE config_keyjoy_lens[]={1,1,3};
static const BYTE config_tape_lens[]={1,2,6};
#define CONFIG_EXT(name) \
    static const es2_hook_extension config_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,config_##name,TRUE}; \
    static uint32_t __cdecl config_##name##32(const uint32_t *a,void *u) {return (uint32_t)config_##name(a,u,weapons_no_registers);}
CONFIG_EXT(prefs_video)
CONFIG_EXT(prefs_load)
CONFIG_EXT(prefs_store)
CONFIG_EXT(keyjoy_load)
CONFIG_EXT(sound_config)
CONFIG_EXT(tape_save)
CONFIG_EXT(tape_restore)
#undef CONFIG_EXT
static es2_hook_descriptor config_hook_descriptors[]={
    {ES2_HOOK_ID(prefs_video),config_prefs_video_sig,13,6,config_frame_lens,3,1,config_prefs_video32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(prefs_load),config_prefs_load_sig,12,6,config_frame_lens,3,1,config_prefs_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(prefs_store),config_prefs_store_sig,11,6,config_frame_lens,3,2,config_prefs_store32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(keyjoy_load),config_keyjoy_load_sig,10,5,config_keyjoy_lens,3,0,config_keyjoy_load32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(sound_config),config_sound_config_sig,12,6,config_frame_lens,3,1,config_sound_config32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(tape_save),config_tape_save_sig,17,9,config_tape_lens,3,2,config_tape_save32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(tape_restore),config_tape_restore_sig,15,9,config_tape_lens,3,2,config_tape_restore32,NULL,NULL,NULL,0},
};
#endif
