#ifndef ES2_ACTOR_PASS_SNAPSHOT_H
#define ES2_ACTOR_PASS_SNAPSHOT_H
/* The proxy's snapshot compare of contact_scan (actor_pass_hooks.h), bound
 * to its extension for the x86 framework; included by hook_install.h. */
/* Snapshot compare (docs/reference-game/compare_mode.md, section
 * "snapshot regions"): contact_scan accepts compare. Its writes outside the frame
 * are actor bytes (its own, contact_share's and sight_check's: +95h, +9Eh,
 * the contact tables +C2h, +132h, the timer +1E3h), all inside the blocks
 * of the three actor pools (buildings [4a95a0], MECHs [4a9b7a], FLYERs
 * [4a9db9]; header +0 free and +2 used count, +4 object size, +6 block of
 * (free + used) * (size + 8) bytes, 471a50/471ad8), and RNG stream A
 * 4d2599h (rng_bounded in sight_check). Snapshot regions: the three blocks
 * (absent when the header pointer or the block is 0) and the stream. The
 * other stores of the reach overwrite scratch with values independent of
 * its pre-state (terrain contact 6b505c..67h and byte 4a0c4c in 470798, the
 * BASE V24 buffer 4a95cc and its first-call byte 497424), so a correct
 * replacement phase leaves them as the original did. mech_contact (V48 of
 * the MECH, 412800: radio message 420a98, the message timer) and
 * trigger_fire (423430: trigger latch, variables, message port, clock) are
 * guards: a call whose original phase enters one is not compared. */
static BOOL actor_pass_readable(uint32_t address,size_t size) {
    MEMORY_BASIC_INFORMATION region;
    const void *probe=es2_guest_probe(address,(uint32_t)size);
    if(!address||!probe||!VirtualQuery(probe,&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return FALSE;
    return address+size>=address&&address+size<=(uint32_t)(uintptr_t)region.BaseAddress+region.RegionSize;
}
static BOOL __cdecl actor_pass_pool_block(void *user,BYTE *image,const uint32_t *args,BYTE **base,size_t *size) {
    (void)args;uint32_t header,block;uint16_t free_count,used,object;
    memcpy(&header,image+*(const uint32_t *)user,4);
    if(!header)return TRUE;
    if(!actor_pass_readable(header,10))return FALSE;
    const BYTE *h=es2_guest_span(header,10u);
    memcpy(&free_count,h,2);memcpy(&used,h+2,2);memcpy(&object,h+4,2);memcpy(&block,h+6,4);
    *size=block?((size_t)free_count+used)*((size_t)object+8u):0;*base=block?es2_guest_span(block,(uint32_t)*size):NULL;
    return TRUE;
}
/* The RVAs of the three pool header pointers. */
static const uint32_t actor_pass_pool_headers[3]={0xa95a0u,0xa9b7au,0xa9db9u};
static const es2_hook_snapshot actor_pass_contact_scan_regions[]={
    {0,0x40000,actor_pass_pool_block,(void *)&actor_pass_pool_headers[0]},
    {0,0x40000,actor_pass_pool_block,(void *)&actor_pass_pool_headers[1]},
    {0,0x40000,actor_pass_pool_block,(void *)&actor_pass_pool_headers[2]},
    {0xd2599,0x72,NULL,NULL},
};
static const uint32_t actor_pass_contact_scan_guards[]={0x12800,0x23430};
static const es2_hook_snapshot_set actor_pass_contact_scan_snapshot={actor_pass_contact_scan_regions,4,actor_pass_contact_scan_guards,2,1,1};
/* Bound at load, before any installation (hook.h: es2_hook_bind_snapshot). */
__attribute__((constructor)) static void actor_pass_bind_snapshot(void) {
    (void)es2_hook_bind_snapshot(&actor_pass_contact_scan_extension,&actor_pass_contact_scan_snapshot);
}
#endif
