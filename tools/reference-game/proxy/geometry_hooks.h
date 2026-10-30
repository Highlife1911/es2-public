/* Static instruction semantics: docs/reference-game/hooks.md, "Distance and timing".
 * Read-only point arguments; no global reads/writes or temporary effects. */
#ifndef ES2_GEOMETRY_HOOKS_H
#define ES2_GEOMETRY_HOOKS_H
#include "es2/hook/descriptor.h"
#include "hook_registry_ids.h"
#include "es2/core/geometry.h"
#include "guest_native.h"

static uint32_t __cdecl geometry_points2(const uint32_t *a, void *user) {
    (void)user;
    return es2_distance2_points(es2_guest_span(a[0], 8u), es2_guest_span(a[1], 8u));
}
static uint32_t __cdecl geometry_vector2(const uint32_t *a, void *user) {
    (void)user;
    return es2_distance2_vector(es2_guest_span(a[0], 8u));
}
static uint32_t __cdecl geometry_delta2(const uint32_t *a, void *user) {
    (void)user;
    return es2_distance2(a[0], a[1]);
}
static uint32_t __cdecl geometry_gate(const uint32_t *a, void *user) {
    (void)user;
    return es2_distance3_points(es2_guest_span(a[0], 12u), es2_guest_span(a[1], 12u));
}

/* 49288c propagates 47de72's Z-delta sign through both wrappers. */
static uint64_t __cdecl geometry_gate_registers(const uint32_t *a,void *u,const uint32_t *regs) {
    (void)regs;const BYTE *p=es2_guest_span(a[0],12u),*q=es2_guest_span(a[1],12u);
    uint32_t z=0,w=0;for(unsigned i=0;i<4;++i){z|=(uint32_t)p[8+i]<<(8*i);w|=(uint32_t)q[8+i]<<(8*i);}
    return ((uint64_t)(((z-w)&0x80000000u)?UINT32_MAX:0)<<32)|geometry_gate(a,u);
}
static const es2_hook_extension geometry_gate_extension={ES2_ABI_CDECL,ES2_RESULT_PAIR,NULL,NULL,NULL,0,geometry_gate_registers,FALSE};

/* Exact validation prefixes, independently checked in the reference EXE. */
static const BYTE geometry_points2_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xf8};
static const BYTE geometry_gate_sig[] = {0x55,0x8b,0xec,0x83,0xc4,0xf4};
static const BYTE geometry_vector2_sig[] = {0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08};
static const BYTE geometry_delta2_sig[] = {0x55,0x8b,0xec,0x8b,0x45,0x0c};
static const BYTE geometry_octagonal_sig[] = {0xc8,0x00,0x00,0x00,0x8b,0x45,0x08};
static const BYTE geometry_standard_lengths[] = {1,2,3};
static const BYTE geometry_vector_lengths[] = {1,2,1,3};
static const BYTE geometry_enter_lengths[] = {4,3};
static const es2_hook_descriptor geometry_hook_descriptors[] = {
    {ES2_HOOK_ID(dist2d_point),geometry_points2_sig,sizeof(geometry_points2_sig),6,
     geometry_standard_lengths,3,2,geometry_points2,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dist2d_vector),geometry_vector2_sig,sizeof(geometry_vector2_sig),7,
     geometry_vector_lengths,4,1,geometry_vector2,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dist2d_delta),geometry_delta2_sig,sizeof(geometry_delta2_sig),6,
     geometry_standard_lengths,3,2,geometry_delta2,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dist_octagonal),geometry_octagonal_sig,sizeof(geometry_octagonal_sig),7,
     geometry_enter_lengths,2,2,geometry_delta2,NULL,NULL,NULL,0},
    {ES2_HOOK_ID(dist_gate),geometry_gate_sig,sizeof(geometry_gate_sig),6,
     geometry_standard_lengths,3,2,geometry_gate,NULL,NULL,NULL,0},
};
#endif
