/* Own synthetic tick state and callees; never loads original game data. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static WCHAR config_path[MAX_PATH];
static void log_event(const char *event) { (void)event; }
#include "../rng_hooks.h"
#include "../live_coverage.h"

/* Exercise the complete tick writer, including the optional fields, with
 * deterministic time and an in-memory sink. */
#define ES2_VIRTUAL_TIME_H
static BOOL vt_enabled, vt_intra;
static DWORD vt_limiter_polls, vt_stall_advances, vt_late_polls, vt_intra_advances;
static void vt_frame_boundary(void) {}
#define ES2_INPUT_JOURNAL_H
enum { IJ_OFF, IJ_RECORD, IJ_REPLAY };
static unsigned ij_mode;
static DWORD ij_recorded, ij_injected, ij_dropped, ij_fallbacks, ij_focus_dropped;
static char captured[262144]; /* TICK_LOG_LINE_MAX; tick_log.h is included below */
static DWORD captured_size;
static BOOL WINAPI fixture_qpc(LARGE_INTEGER *value) { value->QuadPart = INT64_MIN; return TRUE; }
static BOOL WINAPI fixture_write(HANDLE file, const void *data, DWORD size, DWORD *written, OVERLAPPED *overlap) {
    (void)file; (void)overlap;
    if (size >= sizeof(captured)) return FALSE;
    memcpy(captured, data, size); captured[size] = 0; captured_size = size; *written = size; return TRUE;
}
#define QueryPerformanceCounter fixture_qpc
#define WriteFile fixture_write
#include "../tick_log.h"
#undef WriteFile
#undef QueryPerformanceCounter
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
#include "path_calls_fixture.h"

static int names_case(void) {
    static const char *const update[]={"call_402628","call_418f40","call_46e180","call_418f74","call_4027fc",
        "call_492ee0","call_462830","call_4792ac","call_4195c8"};
    static const char *const collide[]={"call_404ae4","call_46e498","call_492f24","call_434010","call_462830",
        "slot_20","slot_5c","slot_68","slot_70","slot_74","slot_7c","path_contact","path_slope_drift","path_slide_damage"};
    static const char *const effects[]={"call_47ebb8","call_48043c","call_47de72","call_4628e0","call_434144","slot_20"};
    CHECK(GROUND_UPDATE_COUNTERS==9 && GROUND_COLLIDE_COUNTERS==14 && GROUND_EFFECTS_COUNTERS==6);
    static char text[TICK_LOG_LINE_MAX];char expected[2048];
    for(unsigned group=0;group<3;++group) {
        callee_counter *c=group==2?ground_effects_counters:group?ground_collide_counters:ground_update_counters;
        const char *const *names=group==2?effects:group?collide:update;
        unsigned count=group==2?GROUND_EFFECTS_COUNTERS:group?GROUND_COLLIDE_COUNTERS:GROUND_UPDATE_COUNTERS;
        const char *hook=group==2?"ground_effects":group?"ground_collide":"ground_update";
        int n=0;
        for(unsigned i=0;i<count;++i) {
            CHECK(!strcmp(c[i].name,names[i])); c[i].count=i&1?UINT64_MAX:UINT64_C(4294967301);
            n+=snprintf(expected+n,sizeof(expected)-(size_t)n,",\"hk_%s_%s\":%s",hook,names[i],
                        i&1?"18446744073709551615":"4294967301");
        }
        CHECK(callee_tick_fields(text,sizeof(text),hook,c,count)==n && !strcmp(text,expected));
        memset(text,'Q',sizeof(text));
        CHECK(callee_tick_fields(text,(size_t)n,hook,c,count)==0 && text[n]=='Q' && text[n-1]==0);
        CHECK(callee_tick_fields(text,(size_t)n+1,hook,c,count)==n && text[n]==0 && text[n+1]=='Q');
    }
    rng_hooks_configured=TRUE;
    CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
    CHECK(!strstr(text,"hk_ground_"));
    for(unsigned mode=ES2_HOOK_REPLACED;mode<=ES2_HOOK_COMPARE;++mode) {
        rng_hooks[ES2_HOOK_INDEX_ground_update].options.mode=(es2_hook_mode)mode;
        CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
        CHECK(strstr(text,"hk_ground_update_call_402628") && !strstr(text,"hk_ground_collide_"));
        rng_hooks[ES2_HOOK_INDEX_ground_update].options.mode=ES2_HOOK_ORIGINAL;
        rng_hooks[ES2_HOOK_INDEX_ground_collide].options.mode=(es2_hook_mode)mode;
        CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
        CHECK(strstr(text,"hk_ground_collide_slot_70") && !strstr(text,"hk_ground_update_"));
        rng_hooks[ES2_HOOK_INDEX_ground_collide].options.mode=ES2_HOOK_ORIGINAL;
        rng_hooks[ES2_HOOK_INDEX_ground_effects].options.mode=(es2_hook_mode)mode;
        CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
        CHECK(strstr(text,"hk_ground_effects_slot_20") && !strstr(text,"hk_ground_update_") && !strstr(text,"hk_ground_collide_"));
        rng_hooks[ES2_HOOK_INDEX_ground_effects].options.mode=ES2_HOOK_ORIGINAL;
    }
    const unsigned hits[]={ES2_HOOK_INDEX_vehicle_hit,ES2_HOOK_INDEX_building_hit};
    for(unsigned j=0;j<2;++j) {
        callee_counter *c=j?building_hit_counters:vehicle_hit_counters;
        unsigned count=j?BUILDING_HIT_COUNTERS:VEHICLE_HIT_COUNTERS;
        CHECK(count==(j?4u:6u));
        CHECK(!strcmp(c[count-2].name,"slot_58")&&!strcmp(c[count-1].name,"slot_74"));
        rng_hooks[hits[j]].options.mode=ES2_HOOK_REPLACED;
        CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
        CHECK(strstr(text,j?"hk_building_hit_slot_58":"hk_vehicle_hit_slot_58"));
        CHECK(!strstr(text,j?"hk_vehicle_hit_":"hk_building_hit_"));
        rng_hooks[hits[j]].options.mode=ES2_HOOK_ORIGINAL;
        CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
        CHECK(!strstr(text,"hk_vehicle_hit_")&&!strstr(text,"hk_building_hit_"));
    }
    rng_hooks[ES2_HOOK_INDEX_radius_hit].options.mode=ES2_HOOK_REPLACED;
    CHECK(rng_hooks_tick_fields(text,sizeof(text))>0);
    CHECK(strstr(text,"hk_radius_hit_slot_5c")&&strstr(text,"hk_radius_hit_slot_70"));
    rng_hooks[ES2_HOOK_INDEX_radius_hit].options.mode=ES2_HOOK_ORIGINAL;
    CHECK(rng_hooks_tick_fields(text,sizeof(text))>0&&!strstr(text,"hk_radius_hit_"));
    for(unsigned hook=ES2_HOOK_INDEX_weapon_prepare;hook<FUNCTION_HOOK_COUNT;++hook) {
        /* cell, slope_test, diagonal, edge_test, terrain_face and light_level are leaves without
         * game calls; every other weapons hook counts its callees. */
        unsigned count;callee_counter *c=weapons_graph_counters(hook,&count);
        /* A group outside legacy_graph_counters lists its own counters in its
         * adapter header; a hook it gives none is a leaf. The frozen list
         * below covers the earlier groups. */
        if(hook_counter_functions[hook]!=legacy_graph_counters && !count) continue;
        if(hook==ES2_HOOK_INDEX_cell||hook==ES2_HOOK_INDEX_slope_test||hook==ES2_HOOK_INDEX_diagonal||hook==ES2_HOOK_INDEX_edge_test||hook==ES2_HOOK_INDEX_terrain_face) { CHECK(!count); continue; }
        if(hook==ES2_HOOK_INDEX_light_level) { CHECK(!count); continue; }
        if(hook==ES2_HOOK_INDEX_pool_alloc) { CHECK(!count); continue; }
        /* The track_advance subtree hooks (math group) count no callees: only
         * clip classes other than the two game classes reach game code. */
        if(hook==ES2_HOOK_INDEX_track_sort||hook==ES2_HOOK_INDEX_track_step||hook==ES2_HOOK_INDEX_track_list||hook==ES2_HOOK_INDEX_node_matrices) { CHECK(!count); continue; }
        /* weapon_helpers leaves without game calls. */
        if(hook==ES2_HOOK_INDEX_weapon_ready||hook==ES2_HOOK_INDEX_part_condition) { CHECK(!count); continue; }
        /* track_route, track_poses and pose_list run their callees in core. */
        if(hook==ES2_HOOK_INDEX_track_route||hook==ES2_HOOK_INDEX_track_poses||hook==ES2_HOOK_INDEX_pose_list) { CHECK(!count); continue; }
        /* part_ratio (40dbc0) is a pure leaf without game calls. */
        if(hook==ES2_HOOK_INDEX_part_ratio) { CHECK(!count); continue; }
        /* ai_helpers leaves without game calls. */
        if(hook==ES2_HOOK_INDEX_order_target||hook==ES2_HOOK_INDEX_route_point||hook==ES2_HOOK_INDEX_bounds_outside) { CHECK(!count); continue; }
        /* condition_table (4151a4) and condition_ratio (4152ec) are pure leaves. */
        if(hook==ES2_HOOK_INDEX_condition_table||hook==ES2_HOOK_INDEX_condition_ratio) { CHECK(!count); continue; }
        /* line_plane (47e610) computes its helpers in core. */
        if(hook==ES2_HOOK_INDEX_line_plane) { CHECK(!count); continue; }
        /* building leaf without game calls. */
        if(hook==ES2_HOOK_INDEX_group_death) { CHECK(!count); continue; }
        /* device_state (41aa10) and device_word (41aa44) are pure leaves. */
        if(hook==ES2_HOOK_INDEX_device_state||hook==ES2_HOOK_INDEX_device_word) { CHECK(!count); continue; }
        /* world leaves without game calls (poly_spans counts 4930c1). */
        if(hook==ES2_HOOK_INDEX_poly_clip||hook==ES2_HOOK_INDEX_view_rect||hook==ES2_HOOK_INDEX_cell_uv) { CHECK(!count); continue; }
        /* route leaves without game calls. */
        if(hook==ES2_HOOK_INDEX_route_closing||hook==ES2_HOOK_INDEX_route_advance) { CHECK(!count); continue; }
        /* vector_angle (49290c) and aim_angles (492990) compute their helpers in core. */
        if(hook==ES2_HOOK_INDEX_vector_angle||hook==ES2_HOOK_INDEX_aim_angles) { CHECK(!count); continue; }
        CHECK(count);
        char key[160];snprintf(key,sizeof(key),"hk_%s_%s",function_hook_descriptor(hook)->name,c[count-1].name);
        rng_hooks[hook].options.mode=ES2_HOOK_REPLACED;
        weapons_graph_observe((void *)(uintptr_t)hook,c[count-1].key,c[count-1].event==ES2_COLLIDE_SLOT?c[count-1].key:0);
        CHECK(c[count-1].count && rng_hooks_tick_fields(text,sizeof(text))>0 && strstr(text,key));
        rng_hooks[hook].options.mode=ES2_HOOK_ORIGINAL;
        CHECK(rng_hooks_tick_fields(text,sizeof(text))>0 && !strstr(text,key));
    }
    return 0;
}

static int tick_case(void) {
    BYTE *image = VirtualAlloc((void *)(uintptr_t)0x20000000, TICK_LOG_IMAGE_SIZE,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(image);
    BYTE *context = image + TICK_LOG_CTX_RVA, *actor = image + 0x1000;
    DWORD actor_address = (DWORD)(uintptr_t)actor;
    memset(actor, 0xff, TICK_LOG_ACTOR_SIZE);
    for (unsigned i = 0; i < 3; ++i) math_native_write32(NULL, actor_address + 0x26 + 4 * i, 0x80000000u);
    math_native_write16(NULL, actor_address + 0x28e, 0x8000u);
    math_native_write16(NULL, actor_address + 0x290, 0x8000u);
    memcpy(context + 0x2a, &actor_address, 4);
    memset(context + 0xbf, 0xff, 4); context[0xb6] = 255;
    memset(image + TICK_LOG_CLOCK_RVA, 0xff, 24);
    tick_log_image = image; tick_log_actor_raw = TRUE; tick_log_state = 1;
    tick_log_file = (HANDLE)(uintptr_t)1; tick_log_limit = UINT32_MAX; tick_log_sequence = UINT32_MAX - 1;
    vt_enabled = TRUE; vt_limiter_polls = vt_stall_advances = vt_late_polls = UINT32_MAX;
    ij_mode = IJ_RECORD; ij_recorded = ij_injected = ij_dropped = ij_fallbacks = ij_focus_dropped = UINT32_MAX;
    es2_tick_record(context);
    CHECK(captured_size == strlen(captured));
    CHECK(captured_size < TICK_LOG_LINE_MAX);
    CHECK(captured_size > 3 && !strcmp(captured + captured_size - 3, "}\r\n"));
    VirtualFree(image, 0, MEM_RELEASE);
    return 0;
}

int main(int argc, char **argv) {
    if(argc!=2) return 2;
    if(!strcmp(argv[1],"names")) {
        CHECK(!names_case()); puts("PASS: counter names, order, uint64 widths, bounds and per-hook modes"); return 0;
    }
    if(!strcmp(argv[1],"calls")) return calls_case();
    if(!strcmp(argv[1],"bound")) {
        rng_hooks_configured=TRUE;
        for(unsigned i=0;i<FUNCTION_HOOK_COUNT;++i) {
            rng_hooks[i].options.mode=ES2_HOOK_REPLACED;
            rng_hooks[i].calls=rng_hooks[i].bypassed=UINT64_MAX;
            /* Replaced dispatch never compares or snapshots. These two
             * counters cannot grow in an all-replaced run. */
            rng_hooks[i].mismatches=rng_hooks[i].skipped=0;
        }
        for(unsigned i=0;i<GROUND_UPDATE_COUNTERS;++i) ground_update_counters[i].count=UINT64_MAX;
        for(unsigned i=0;i<GROUND_COLLIDE_COUNTERS;++i) ground_collide_counters[i].count=UINT64_MAX;
        for(unsigned i=0;i<GROUND_EFFECTS_COUNTERS;++i) ground_effects_counters[i].count=UINT64_MAX;
        for(unsigned i=0;i<VEHICLE_HIT_COUNTERS;++i)vehicle_hit_counters[i].count=UINT64_MAX;
        for(unsigned i=0;i<BUILDING_HIT_COUNTERS;++i)building_hit_counters[i].count=UINT64_MAX;
        for(unsigned i=0;i<RADIUS_HIT_COUNTERS;++i)radius_hit_counters[i].count=UINT64_MAX;
        unsigned graph_fields=0;
        for(unsigned hook=0;hook<FUNCTION_HOOK_COUNT;++hook) {
            unsigned count;callee_counter *c=weapons_graph_counters(hook,&count);graph_fields+=count;
            for(unsigned i=0;i<count;++i)c[i].count=UINT64_MAX;
        }
        /* Optional intra-frame counter: present only with virtual_time_intra_frame. */
        vt_intra=TRUE; vt_intra_advances=UINT32_MAX;
        CHECK(!tick_case());
        CHECK(strstr(captured,"\"hk_ground_effects_slot_20\":18446744073709551615,"));
        CHECK(strstr(captured,"\"hk_projectile_flight_call_462830\":18446744073709551615"));
        unsigned fields=0;
        for(const char *p=captured;(p=strstr(p,"\"hk_"));p+=4) ++fields;
        CHECK(fields==2*FUNCTION_HOOK_COUNT+graph_fields+GROUND_UPDATE_COUNTERS+GROUND_COLLIDE_COUNTERS+GROUND_EFFECTS_COUNTERS+VEHICLE_HIT_COUNTERS+BUILDING_HIT_COUNTERS+RADIUS_HIT_COUNTERS);
        printf("PASS: maximum tick %lu bytes + NUL < %u; %u hook fields, %u hooks, all optional fields\n",
               (unsigned long)captured_size,TICK_LOG_LINE_MAX,fields,FUNCTION_HOOK_COUNT);
        return 0;
    }
    if(strcmp(argv[1],"original")) return 2;
    WCHAR temporary[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(L"TEST_TMPDIR", temporary, MAX_PATH);
    CHECK(length && length < MAX_PATH - 16);
    swprintf(config_path, MAX_PATH, L"%ls\\no-hooks.ini", temporary);
    CHECK(WritePrivateProfileStringW(L"trace", L"tick_log_actor_raw", L"1", config_path));
    CHECK(live_configuration(config_path)==0 && !live_enabled && !lc_handler);
    CHECK(install_rng_hooks());
    CHECK(!rng_hooks_configured);
    CHECK(!tick_case());
    CHECK(!strstr(captured, "\"hk_"));
    /* SHA-256 of the exact synthetic tick captured before the proxy change,
     * including CRLF, before stdout's text-mode newline translation. */
    HCRYPTPROV provider=0; HCRYPTHASH hash=0; BYTE digest[32]; DWORD size=sizeof(digest);
    CHECK(CryptAcquireContextW(&provider,NULL,NULL,PROV_RSA_AES,CRYPT_VERIFYCONTEXT));
    CHECK(CryptCreateHash(provider,CALG_SHA_256,0,0,&hash));
    CHECK(CryptHashData(hash,(const BYTE *)captured,captured_size,0));
    CHECK(CryptGetHashParam(hash,HP_HASHVAL,digest,&size,0) && size==32);
    char hex[65]; for(unsigned i=0;i<32;++i) snprintf(hex+2*i,3,"%02x",digest[i]);
    CryptDestroyHash(hash); CryptReleaseContext(provider,0);
    CHECK(captured_size==2505 && !strcmp(hex,"405eb398f723f39f37b683cf9e49fede35b7eddeb85d88f8b1ec416aa369a718"));
    printf("PASS: no-hooks tick byte-identical to pre-change baseline, %lu bytes, SHA-256 %s\n",(unsigned long)captured_size,hex);
    return 0;
}
