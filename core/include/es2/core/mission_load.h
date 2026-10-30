#ifndef ES2_CORE_MISSION_LOAD_H
#define ES2_CORE_MISSION_LOAD_H
#include "es2/core/weapons.h"
/* The simulation pieces of the mission load 461608 that run as own
 * code: L1 (pool construction), L2 (the load leaves and the pool walk they
 * use) and L3 (the initial damage of an authored mech). Contract in
 * docs/reverse-engineering/mission_load.md, section "pieces L1 to L3
 * and the snapshot noise model". Addresses are guest addresses; g->image is
 * the loaded image base (0x400000 in the original). Every game call goes
 * through g->call with the original's entry EAX/EDX/ECX and is reported to
 * g->observe first (reference VA, slot 0; a virtual call reports its slot),
 * so a callee that is itself a hook keeps its own mode. `entry` holds the
 * entry EAX/EDX/ECX. Each function returns the original's EDX:EAX and, when
 * `ecx` is not NULL, stores the original's ECX there (the adapters pass
 * NULL: ECX is dead at every caller, mission_load.md). */

/* pool_create 471ad8 (header, capacity, size): the pool block of
 * capacity x (size + 8) bytes from new[] 4774f0 (none for capacity 0),
 * cleared by memset 49681a, then the pool initializer 471a50 (computed
 * here): header words free = capacity, used = 0, size, the block twice
 * (+6, free list +0Ah), the used list +0Eh = 0, and the free list through
 * every node (next at +0, previous at +4). Only the low words of capacity
 * and size count. Returns EAX = header. */
uint64_t es2_pool_create_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t header,
    uint32_t capacity, uint32_t size, const uint32_t entry[3], uint32_t *ecx);
/* pool_nth 471cb0 (pool, index): the object at position index (low word)
 * of pool_prev's order (pool_prev 471c70 from 0), or 0. */
uint64_t es2_pool_nth_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t pool,
    uint32_t index, const uint32_t entry[3], uint32_t *ecx);
/* subject_resolve 425348 (index, scope): scope 0 the group record
 * [4a9e00] + 122 x index (low word, signed); 1/2/3 the object of the
 * MECH/FLYER/building pool at the remapped position (remap byte at
 * 4aa78e/4aa888/4aa946 + index, signed) through pool_nth; otherwise 0. */
uint64_t es2_subject_resolve_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t index,
    uint32_t scope, const uint32_t entry[3], uint32_t *ecx);
/* action_copy 411b90 (actor, ids, actions): ten words from ids to
 * actor+1BAh and ten from actions to actor+1CEh, interleaved. */
uint64_t es2_action_copy_registers(const es2_guest_memory *, uint32_t actor, uint32_t ids, uint32_t actions,
    uint32_t *ecx);
/* condition_init 415068 (conditions, first, second, third): the part
 * condition tables of a MEC actor (conditions = actor+206h) from the three
 * blocks of MEC condition words and the type's maxima. */
uint64_t es2_condition_init_registers(const es2_guest_memory *, uint32_t conditions, uint32_t first,
    uint32_t second, uint32_t third, uint32_t *ecx);
/* region_init 423358 (region, record, points): a script region record
 * (kind, first point, point count or radius) into the 10-byte runtime
 * region. */
uint64_t es2_region_init_registers(const es2_guest_memory *, uint32_t region, uint32_t record, uint32_t points,
    uint32_t *ecx);
/* timer_init 423104 (timer, first, words, duration): timer+0 = first,
 * timer_arm 467ac4 (timer+2Ch, duration), ten dwords from words to
 * timer+4. */
uint64_t es2_timer_init_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t timer,
    uint32_t first, uint32_t words, uint32_t duration, const uint32_t entry[3], uint32_t *ecx);
/* group_array 423d38 (groups, count): groups+4 = count (word), groups+0 =
 * the runtime's array construction 49667c(0, 7Ah, count, 1, 4240e4). */
uint64_t es2_group_array_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t groups,
    uint32_t count, const uint32_t entry[3], uint32_t *ecx);
/* initial_damage 4178e8 (actor, word): the authored damage word (low word)
 * selects the random_damage 417a04 severity; 20..39 also scales one part
 * maximum with multiply_q8 47e0a0, 0..19 also damages part 7 or 8 (and 13
 * or 14 when the type word +4Ah exceeds 2) by 32000 through the actor's
 * virtual +74h, picked by RNG stream A (rng_next 492ee0 at 4d2599h). */
uint64_t es2_initial_damage_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor,
    uint32_t word, const uint32_t entry[3], uint32_t *ecx);

/* (L8): the pool-only stage-2 callbacks of stage_dispatch(2), contract
 * in mission_load.md, section "L8 stage-2 pool callbacks". No
 * arguments. Each sets up the runtime exception record through 49630c with
 * the synthetic EBP f = g->scratch + ES2_STAGE_POOLS_FRAME (the bridge must
 * pass f as EBP for that call; g->scratch needs ES2_STAGE_POOLS_SCRATCH
 * bytes), allocates its pool header with new 47749c and builds it with
 * pool_create 471ad8, and its removal list (new(8), the slot array from
 * new[] 4774f0, count 0, capacity) into the image globals. Every game call
 * goes through g->call with the original's EAX/EDX/ECX. EAX is the record's
 * saved FS:0 head, which the caller writes back to FS:0 as the original's
 * epilogue does.
 * light_pools 4076e4: the light table (new(404h), its constructor 406e44,
 * construction count -5, the leaf 406ee4(table, 10, 2000) computed here,
 * table+0Ch = 10000) at 4a9608, the light pool (3 x 0Eh) at 4a95fe, the
 * list (25) at 4a9602. smoke_pools 4090cc: the smoke pool (20 x 2Ah) at
 * 4a9684, the list (25) at 4a9688, then the smoke-ball set-up 408fb8.
 * projectile_pools 40a060: the projectile pool (160 x 66h) at 4a96c2, the
 * list (80) at 4a96c6. */
#define ES2_STAGE_POOLS_SCRATCH 0x40u
#define ES2_STAGE_POOLS_FRAME 0x30u
uint64_t es2_light_pools_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);
uint64_t es2_smoke_pools_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);
uint64_t es2_projectile_pools_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);

/* (L4): the weapon fit of a mech during the actor load, contract in
 * mission_load.md, section "L4 weapon fit". Every game call goes
 * through g->call with the original's EAX/EDX/ECX; the runtime frame setup
 * 49630c needs the synthetic EBP f = g->scratch + ..._FRAME (the bridge
 * passes it as EBP), and the frame's locals lie at the original's offsets
 * below f (guest callees receive their addresses).
 * weapon_fit 4175dc (actor, items, secondary): the mount table of the
 * actor's type (420634); for the player (byte actor+0A3h) new(35h) and the
 * player weapon system 4104ec, otherwise new(14h) and the weapon factory
 * 40fff8, stored at actor+202h; then actor_animate(actor, 0) and, per
 * non-null weapon of the system, the mount offset, the record fix-ups and
 * the three part-table helpers 40cec0/40d240/40d128; the part lists
 * 40cb84/40d284 over the frame buffers; the 29 condition words; 40fb2c,
 * shield_capacity, 413ac8, 417d08. EAX = the record's saved FS:0 head,
 * which the caller writes back to FS:0 as the original's epilogue does.
 * g->scratch needs ES2_WEAPON_FIT_SCRATCH bytes. */
#define ES2_WEAPON_FIT_SCRATCH 0x140u
#define ES2_WEAPON_FIT_FRAME 0x130u
uint64_t es2_weapon_fit_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor,
    uint32_t items, uint32_t secondary, const uint32_t entry[3], uint32_t *ecx);
/* player_weapons 4104ec (system, actor, count, mounts, items, secondary):
 * the weapon factory with the last word 1, vtable 499238, the three fire
 * groups (49667c, then new[] of count words each), the group membership of
 * every weapon (equipment IDs 12h, 1Dh..20h in none, the others in group 0,
 * the first of them selected at system+1Dh), fire_group(system, 0),
 * system+16h = 1. ECX = the frame word [EBP-4] (the entry ECX, its words
 * replaced by the last weapon's index and group flag). */
uint64_t es2_player_weapons_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t system,
    uint32_t actor, uint32_t count, uint32_t mounts, uint32_t items, uint32_t secondary, const uint32_t entry[3],
    uint32_t *ecx);
/* weapon_factory 40fff8 (system, actor, count, mounts, items, secondary,
 * word): vtable 499338, the slot array (new[] of count dwords), and per
 * mount (stride 1Ah) the weapon of its item word (0 or negative: no
 * weapon, the slot 0; above 20h: the slot is not written) built by the
 * allocator 47a2c8 and the constructor of its class (the dispatch byte
 * table at 4100fd is read from the image), between the arena calls 474bbc
 * and 474be4. `word` (the seventh argument, 0 or 1) and the high word of
 * `ebx` (the caller's EBX) reach only argument slots that no callee reads.
 * The projectile record local [f-58h] is not written when the record's
 * selector is 21h; its initial value is the scratch content (stack residue
 * in the original). EAX = system; ECX = the record's saved FS:0 head, which
 * the caller writes back to FS:0. g->scratch needs ES2_WEAPON_FACTORY_SCRATCH
 * bytes. */
#define ES2_WEAPON_FACTORY_SCRATCH 0x90u
#define ES2_WEAPON_FACTORY_FRAME 0x80u
uint64_t es2_weapon_factory_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t system,
    uint32_t actor, uint32_t count, uint32_t mounts, uint32_t items, uint32_t secondary, uint32_t word, uint32_t ebx,
    const uint32_t entry[3], uint32_t *ecx);

/* (L5): the constructors of the actor load, contract in
 * mission_load.md, section "L5 constructors". Every game call goes
 * through g->call with the original's EAX/EDX/ECX; the runtime frame setup
 * 49630c needs the synthetic EBP f = g->scratch + ..._FRAME (the bridge
 * passes it as EBP). Computed here, not called: the type-index leaves 41ff00
 * and 422e80, the actor-list append 411dd4 and the energy leaf 413a90.
 * mech_ctor 415bb0 (actor, type): the MECH object in a pool node: shape
 * set-up 4033a4, the ACTOR fields, the actor list, the part tables 40d2cc,
 * the weapon groups (49667c), the type record [4A9D64h] + 21Eh x index, the
 * energy record, up to three animation tracks (402374), the flat objects of
 * the type (pool_alloc, object_init), the AI state (script_start), the part
 * and damage tables of the type's name (492bec, 40d160, 40cd88, 40d354),
 * the matrix and its inverse (euler_matrix, matrix_transpose, rotate_point).
 * `esi` is the caller's ESI: the high word of the dword pushed with the type
 * word, which 41ff00 does not read and POP ECX hands to 4033a4 as its
 * (dead) entry ECX. EAX = actor, EDX = the construction-count pointer, ECX =
 * the record's saved FS:0 head, which the caller writes back to FS:0 as the
 * original's epilogue does. g->scratch needs ES2_MECH_CTOR_SCRATCH bytes. */
#define ES2_MECH_CTOR_SCRATCH 0x70u
#define ES2_MECH_CTOR_FRAME 0x60u
uint64_t es2_mech_ctor_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t actor, uint32_t type,
    uint32_t esi, const uint32_t entry[3], uint32_t *ecx);
/* flyer_ctor 4215f4 (flyer, type): the FLYER object: shape set-up 403368,
 * the ACTOR fields, the actor list, the part tables 40d2cc, the type record
 * [4A9DF4h] + 70h x index, 40d354, the AI state, the matrix and its inverse.
 * No runtime frame; the vector f-0Ch that rotate_point reads lies in
 * g->scratch (ES2_FLYER_CTOR_SCRATCH bytes). EAX = flyer. */
#define ES2_FLYER_CTOR_SCRATCH 0x10u
#define ES2_FLYER_CTOR_FRAME 0x10u
uint64_t es2_flyer_ctor_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t flyer, uint32_t type,
    const uint32_t entry[3], uint32_t *ecx);
/* building_factory 405314 (record): the building-pool object of the
 * record's type word through the case byte table at 405366 (read from the
 * image): RADAR_BASE, GUN_BASE, BASE, VEHICLE, LC_BASE (pool_alloc, the
 * shape set-up 40332c or 403368, the class fields), or no object (case 0:
 * types 10, 53, 54 and above 3Dh); then up to two animation tracks (402374,
 * track_rate, track_seek), the part arrays (new[], 49667c) and the part
 * words from the record's damage word +32h. `edi` is the caller's EDI, which
 * case 0 uses as the object. EAX = the object, EDX = the record's saved FS:0
 * head, which the caller writes back to FS:0 as the original's epilogue
 * does. g->scratch needs ES2_BUILDING_FACTORY_SCRATCH bytes. */
#define ES2_BUILDING_FACTORY_SCRATCH 0xb8u
#define ES2_BUILDING_FACTORY_FRAME 0xa8u
uint64_t es2_building_factory_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t record,
    uint32_t edi, const uint32_t entry[3], uint32_t *ecx);
/* group_ctor 423b34 (group, record, trigger, orders, heading, point): the
 * runtime group from its 156-byte script record: class, heading, the
 * action words, the ten order pointers, the member array (new[]) of the
 * non-negative roster slots resolved by pool_nth in the class's pool, and
 * per member the class's join method (417aa8 MECH, 421ee8 FLYER, 405c3c
 * building), which places members still at the load's default angle and
 * height. */
uint64_t es2_group_ctor_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t group, uint32_t record,
    uint32_t trigger, uint32_t orders, uint32_t heading, uint32_t point, const uint32_t entry[3], uint32_t *ecx);
/* base_marks 405db0 (building): the terrain footprint of a base group's
 * layout record ([4A95ACh] + 1Ch x the group word +2), 47136c, unless the
 * record's mask word +8 is -1. */
uint64_t es2_base_marks_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t building,
    const uint32_t entry[3], uint32_t *ecx);

/* (L6): the actor load 4253d8, script pass 2, contract in
 * mission_load.md, section "L6 actor load". No arguments. It reopens
 * the script (492b90, 45ee00, the stream 47ad40 in its frame), skips pass 1's
 * lists, and per record builds the selected authored mechs and the MEC
 * actors (pool_alloc, mech_ctor, position, angles, weapon_fit, then
 * condition_init or the triggers, action_copy and initial_damage), the
 * selected flyers and buildings, the runtime orders, the groups
 * (group_ctor, base_marks) and objectives, then the terrain cell marks
 * 47129c, the building heights and the trigger subjects; the stream reads
 * (47af98 and the stream's virtual +18h), the weapon shape cache 40f998/
 * 40fa88, the terrain marks 470ed4, new[] 4774f0 and memcpy 49680e stay game
 * calls. Every game call goes through g->call with the original's EAX/EDX/
 * ECX; the runtime frame setup 49630c needs the synthetic EBP f = g->scratch
 * + ES2_ACTOR_LOAD_FRAME (the bridge passes it as EBP), and the frame's
 * locals and buffers lie at the original's offsets below f. EDX:EAX are the
 * stream close's (47ad78); ECX = the record's saved FS:0 head, which the
 * caller writes back to FS:0 as the original's epilogue does. g->scratch
 * needs ES2_ACTOR_LOAD_SCRATCH bytes. */
#define ES2_ACTOR_LOAD_SCRATCH 0xb40u
#define ES2_ACTOR_LOAD_FRAME 0xb30u
uint64_t es2_actor_load_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);

/* (L7): the world load 424308, script pass 1, and the four leaves
 * only it calls; contract in mission_load.md, section "L7 world
 * load". Every game call goes through g->call with the original's
 * EAX/EDX/ECX (a virtual call reports its slot to g->observe).
 * world_load 424308 (no arguments): the selection tables and the two local
 * type arrays cleared (memset 49681a), data\mission.var into the variables
 * 4A9E70h (words 10, 20, 21..42 cleared), data\player.mec (the player
 * ordinal 4AAA64h, N 4AAA5Eh, new[] of N x 126 bytes at 4AAA60h, the
 * records), the vehicle-8 byte 4D2571h, data\mission.str (453878, 4539a0,
 * the final LF of each string removed), data\script<ext>: the header
 * 4A9E4Eh, the world 42e010, the coordinates (49667c, the map rectangle
 * 4AA640h..4AA64Fh), angles, routes, regions (region_init), triggers,
 * timers (49667c, timer_init), the authored mechs, flyers, buildings (their
 * type words), the orders (skipped), the groups (group_array; group 0 kept
 * aside, the others select their members), the objectives (skipped), the
 * string indices; then the compaction into the remap tables, the MEC actors
 * appended, the type selects (mech_type_select, flyer_type_select, 405ebc),
 * the MECH and FLYER pools and lists (new, pool_create, new[]), base_pools,
 * actor_list_init, the three slot tables, mech_types, flyer_types,
 * zone_load, the result pairs and header word 4. The streams (47ad40,
 * 47af98, the read at slot +18h, 47ad78), the path builders 45ee00/492b90,
 * the fatal report 479c78, new/new[], 49667c, 405ebc and the world 42e010
 * stay game calls. The runtime frame setup 49630c needs the synthetic EBP
 * f = g->scratch + ES2_WORLD_LOAD_FRAME (the bridge passes it as EBP); the
 * frame's locals and buffers lie at the original's offsets below f. EDX:EAX
 * are the stream close's (47ad78); ECX = the record's saved FS:0 head
 * [f-54h], which the caller writes back to FS:0 as the original's epilogue
 * does. g->scratch needs ES2_WORLD_LOAD_SCRATCH bytes. */
#define ES2_WORLD_LOAD_SCRATCH 0xe70u
#define ES2_WORLD_LOAD_FRAME 0xe60u
uint64_t es2_world_load_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);
/* actor_list_init 411df8 (list, count): list+0 = new[](4 x sx(count)),
 * word list+4 = count. EAX/EDX = new[]'s, ECX = the size. */
uint64_t es2_actor_list_init_registers(const es2_guest_memory *, const es2_weapon_graph *, uint32_t list,
    uint32_t count, const uint32_t entry[3], uint32_t *ecx);
/* The slot tables of the type loads, sized by the running slot words that
 * mech_type_select and flyer_type_select advanced: gun_slot_table 40fecc
 * [4A98E4h] = new[](6 x sx(word 4A98E8h)) (also for 0); dmg_slot_table
 * 40d218 [4A9828h] = new[](12 x sx(word 498856h)) and col_slot_table 40cbec
 * [4A9824h] = new[](6 x sx(word 4987DCh)), both only for a nonzero word
 * (otherwise nothing, the entry registers). EAX/EDX = new[]'s, ECX = the
 * size. */
uint64_t es2_gun_slot_table_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);
uint64_t es2_dmg_slot_table_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);
uint64_t es2_col_slot_table_registers(const es2_guest_memory *, const es2_weapon_graph *, const uint32_t entry[3],
    uint32_t *ecx);
#endif
