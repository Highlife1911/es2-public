#ifndef ES2_PROJECTILE_HOOKS_H
#define ES2_PROJECTILE_HOOKS_H
/* Projectile and bullet steering: vector_angle 49290c, aim_angles
 * 492990, projectile_steer 40a254, bullet_steer 40aff0
 * (docs/reverse-engineering/weapons.md, "Projectile and bullet steering").
 * Included after weapons_hooks.h: the two steering hooks call every game
 * callee through its guest bridge with the original's registers and count
 * them (weapons_graph); their frame buffer stands in for the original's 28h
 * locals. vector_angle and aim_angles compute their helpers in core and get
 * entry shims that load the original's return ECX before the framework's
 * dispatch (the dispatch restores the entry ECX on return), so EAX, EDX and
 * ECX are exact for every caller. No game instruction is embedded; the
 * shims only compute register values from the arguments. */
#include "es2/core/projectile.h"
static uint64_t __cdecl projectile_vector_angle(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_vector_angle_registers(a[0],a[1],(BYTE *)u+0xa1c34);
}
static uint64_t __cdecl projectile_aim_angles(const uint32_t *a,void *u,const uint32_t *r) {
    (void)r;return es2_aim_angles_registers(&math_native_memory,a[0],a[1],a[2],(BYTE *)u+0xa1c34);
}
static uint64_t __cdecl projectile_projectile_steer(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[48]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_projectile_steer);
    return es2_projectile_steer_registers(&math_native_memory,&g,a[0],r);
}
static uint64_t __cdecl projectile_bullet_steer(const uint32_t *a,void *u,const uint32_t *r) {
    BYTE frame[48]={0};es2_weapon_graph g=weapons_graph(u,frame,ES2_HOOK_INDEX_bullet_steer);
    return es2_bullet_steer_registers(&math_native_memory,&g,a[0],r);
}
/* 49290c: ECX = 47d32c's divisor register for (x or 1, y): |y| when y is
 * nonzero and |y| > |x| (signed, wrapped), else |x|. */
__attribute__((used)) static void *projectile_vector_entry_bridge;
__attribute__((naked)) static void projectile_vector_entry(void) {
    __asm__ volatile(
        "pushl %eax\n\tpushl %edx\n\tmovl 12(%esp),%eax\n\tmovl 16(%esp),%edx\n\t"
        "movl %eax,%ecx\n\torl %edx,%ecx\n\tjnz 1f\n\tmovl $1,%eax\n\t1:\n\t"
        "movl %eax,%ecx\n\tsarl $31,%ecx\n\txorl %ecx,%eax\n\tsubl %ecx,%eax\n\t"
        "movl %edx,%ecx\n\tsarl $31,%ecx\n\txorl %ecx,%edx\n\tsubl %ecx,%edx\n\t"
        "movl %eax,%ecx\n\ttestl %edx,%edx\n\tjz 2f\n\tcmpl %eax,%edx\n\tjle 2f\n\tmovl %edx,%ecx\n\t2:\n\t"
        "popl %edx\n\tpopl %eax\n\tjmp *_projectile_vector_entry_bridge\n\t");
}
/* 492990: ECX = end.x - start.x (its POP ECX reloads that local). */
__attribute__((used)) static void *projectile_aim_entry_bridge;
__attribute__((naked)) static void projectile_aim_entry(void) {
    __asm__ volatile(
        "movl 8(%esp),%ecx\n\tmovl (%ecx),%ecx\n\tpushl %edx\n\tmovl 16(%esp),%edx\n\tsubl (%edx),%ecx\n\t"
        "popl %edx\n\tjmp *_projectile_aim_entry_bridge\n\t");
}
/* Called by vehicle_leaf_capture_entry (end of the shim chain) after the
 * framework installed its jump: redirect it to the shim and keep h->patch
 * consistent, so uninstall still validates the exact patch. */
/* The torso shim (torso_hooks.h) ends the chain. */
static BOOL torso_capture_entry(es2_hook *h);
static BOOL projectile_capture_entry(es2_hook *h) {
    void *target;void **bridge;
    switch(h->descriptor->rva) {
    case 0x9290cu:target=projectile_vector_entry;bridge=&projectile_vector_entry_bridge;break;
    case 0x92990u:target=projectile_aim_entry;bridge=&projectile_aim_entry_bridge;break;
    default:return torso_capture_entry(h);
    }
    DWORD old,ignored;
    if(memcmp(h->site,h->patch,h->descriptor->relocate_size))return FALSE;
    if(!VirtualProtect(h->site,h->descriptor->relocate_size,PAGE_EXECUTE_READWRITE,&old))return FALSE;
    *bridge=(void *)(uintptr_t)((uint32_t)(uintptr_t)(h->site+5)+math_native_read32(NULL,(uint32_t)(uintptr_t)(h->patch+1)));
    math_native_write32(NULL,(uint32_t)(uintptr_t)(h->patch+1),(uint32_t)(uintptr_t)target-(uint32_t)(uintptr_t)(h->site+5));
    memcpy(h->site,h->patch,h->descriptor->relocate_size);
    BOOL flushed=FlushInstructionCache(GetCurrentProcess(),h->site,h->descriptor->relocate_size);
    BOOL protected=VirtualProtect(h->site,h->descriptor->relocate_size,old,&ignored);
    return flushed && protected;
}
static const BYTE projectile_vector_angle_sig[]={0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x0c};
static const BYTE projectile_aim_angles_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xf8,0x53,0x56,0x57};
static const BYTE projectile_projectile_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x57};
static const BYTE projectile_bullet_steer_sig[]={0x55,0x8b,0xec,0x83,0xc4,0xd8,0x53,0x56,0x57};
static const BYTE projectile_frame_lens[]={1,2,3},projectile_saved_lens[]={1,2,1,1};
/* vector_angle writes nothing and reads only the correction table, so it
 * allows compare mode; the other three refuse it. */
static const es2_hook_extension projectile_vector_angle_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,math_ratio_region,1,projectile_vector_angle,FALSE};
#define PROJECTILE_EXT(name) static const es2_hook_extension projectile_##name##_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,projectile_##name,TRUE};
PROJECTILE_EXT(aim_angles) PROJECTILE_EXT(projectile_steer) PROJECTILE_EXT(bullet_steer)
#undef PROJECTILE_EXT
#define PROJECTILE_LEGACY(name) static uint32_t __cdecl projectile_##name##32(const uint32_t *a,void *u) {return (uint32_t)projectile_##name(a,u,weapons_no_registers);}
PROJECTILE_LEGACY(vector_angle) PROJECTILE_LEGACY(aim_angles) PROJECTILE_LEGACY(projectile_steer) PROJECTILE_LEGACY(bullet_steer)
#undef PROJECTILE_LEGACY
static es2_hook_descriptor projectile_hook_descriptors[]={
    {ES2_HOOK_ID(vector_angle),projectile_vector_angle_sig,8,5,projectile_saved_lens,4,2,projectile_vector_angle32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(aim_angles),projectile_aim_angles_sig,9,6,projectile_frame_lens,3,3,projectile_aim_angles32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(projectile_steer),projectile_projectile_steer_sig,9,6,projectile_frame_lens,3,1,projectile_projectile_steer32,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(bullet_steer),projectile_bullet_steer_sig,9,6,projectile_frame_lens,3,1,projectile_bullet_steer32,NULL,NULL,NULL,0},
};
#endif
