/* Case dispatch of the first seven registry groups, whose cases grew over
 * several rounds into many headers (frozen; new groups never edit this file).
 * Every group G of hook_registry.h has one function
 *     static int G_cases(unsigned index,unsigned limit)
 * defined in the header that the generated hook_group_cases.h includes for
 * it (hook_groups.py --kind cases); main.c's run() calls it through a table
 * built from the registry. rng, geometry, timing and movement have no
 * function: their cases run in run()'s own loop (NULL table entries). The
 * branches below are the main.c dispatch chain, restricted to each
 * group and in the same order. */
#include "math_cases.h"
#include "chain_cases.h"
#include "contract_probes.h"
#include "padding_cases.h"
#include "animation_cases.h"
#include "advance_cases.h"
#include "actor_cases.h"
#include "terrain_cases.h"
#include "copy_cases.h"
#include "ground_cases.h"
#include "collide_cases.h"
#include "effects_cases.h"
#include "clip_cases.h"
#include "damage_cases.h"
#include "building_part_cases.h"
#include "weapons_cases.h"
#include "weapons_round2_cases.h"
#include "weapons_round3_cases.h"
#include "sweep_cases.h"
#include "sweep_leaf_cases.h"
#include "segment_cases.h"
#include "terrain_leaf_cases.h"
#include "camera_cases.h"
#include "vehicle_part_cases.h"
#include "emitter_cases.h"
#include "building_emitter_cases.h"
#include "track_callee_cases.h"
#define rng_cases NULL
#define geometry_cases NULL
#define timing_cases NULL
#define movement_cases NULL
static int math_cases(unsigned index,unsigned limit) {
    if(index>=ES2_HOOK_INDEX_track_sort&&index<=ES2_HOOK_INDEX_node_matrices)return track_callee_cases(index,limit);
    if(index==ES2_HOOK_INDEX_actor_snapshot||index==ES2_HOOK_INDEX_actor_rollback) return copy_cases(index,limit);
    if(index==ES2_HOOK_INDEX_ground_update) return ground_cases(index,limit);
    if(index==ES2_HOOK_INDEX_ground_collide) return collide_cases(index,limit);
    if(index==ES2_HOOK_INDEX_ground_effects) return effects_cases(index,limit);
    if(index==ES2_HOOK_INDEX_track_clip) return clip_cases(index,limit);
    if(index==ES2_HOOK_INDEX_terrain_height) return terrain_cases(index,limit);
    if(index>=ES2_HOOK_INDEX_track_target) return actor_cases(index,limit);
    if(index==ES2_HOOK_INDEX_track_advance) return advance_cases(index,limit);
    if(index>=ES2_HOOK_INDEX_manager_reset) return animation_cases(index,limit);
    if(index>=ES2_HOOK_INDEX_pose_parent) return padding_cases(index,limit);
    return math_round1_cases(index,limit);
}
static int damage_cases(unsigned index,unsigned limit) {
    if(index==ES2_HOOK_INDEX_building_part)return building_part_cases(index,limit);
    return damage_round1_cases(index,limit);
}
static int weapons_cases(unsigned index,unsigned limit) {
    if(index==ES2_HOOK_INDEX_terrain_face||index==ES2_HOOK_INDEX_camera_update)return camera_cases(index,limit);
    if(index==ES2_HOOK_INDEX_sweep)return sweep_cases(limit);
    if(index==ES2_HOOK_INDEX_side_scale||index==ES2_HOOK_INDEX_cell)return sweep_leaf_cases(index,limit);
    if(index==ES2_HOOK_INDEX_segment||index==ES2_HOOK_INDEX_sweep_terrain)return segment_cases(index,limit);
    if(index>=ES2_HOOK_INDEX_slope_test&&index<=ES2_HOOK_INDEX_terrain_line)return terrain_leaf_cases(index,limit);
    if(index>=ES2_HOOK_INDEX_emit && index<=ES2_HOOK_INDEX_impact)return weapons_round3_cases(index,limit);
    if(index==ES2_HOOK_INDEX_turret)return turret_cases(limit);
    if(index==ES2_HOOK_INDEX_target_emit)return target_emit_cases(limit);
    if(index==ES2_HOOK_INDEX_vehicle_part)return vehicle_part_cases(limit);
    if(index>=ES2_HOOK_INDEX_weapon_prepare)return weapons_round2_cases(index,limit);
    return weapons_round1_cases(index,limit);
}
