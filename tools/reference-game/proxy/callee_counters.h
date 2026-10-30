#ifndef ES2_CALLEE_COUNTERS_H
#define ES2_CALLEE_COUNTERS_H
/* Process-lifetime diagnostics, incremented only by replacement code.
 * The simulation thread is the sole writer, as for the hook counters. */
typedef struct callee_counter {
    const char *name;
    es2_collide_event event;
    uint32_t key; /* RVA for direct calls, byte offset for slots, 0 for paths */
    uint64_t count;
} callee_counter;
static callee_counter ground_update_counters[]={
    {"call_402628",ES2_COLLIDE_CALL,0x02628,0},
    {"call_418f40",ES2_COLLIDE_CALL,0x18f40,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x6e180,0},
    {"call_418f74",ES2_COLLIDE_CALL,0x18f74,0},
    {"call_4027fc",ES2_COLLIDE_CALL,0x027fc,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x92ee0,0},
    {"call_462830",ES2_COLLIDE_CALL,0x62830,0},
    {"call_4792ac",ES2_COLLIDE_CALL,0x792ac,0},
    {"call_4195c8",ES2_COLLIDE_CALL,0x195c8,0},
};
static callee_counter ground_collide_counters[]={
    {"call_404ae4",ES2_COLLIDE_CALL,0x04ae4,0},
    {"call_46e498",ES2_COLLIDE_CALL,0x6e498,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x92f24,0},
    {"call_434010",ES2_COLLIDE_CALL,0x34010,0},
    {"call_462830",ES2_COLLIDE_CALL,0x62830,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"slot_5c",ES2_COLLIDE_SLOT,0x5c,0},
    {"slot_68",ES2_COLLIDE_SLOT,0x68,0},
    {"slot_70",ES2_COLLIDE_SLOT,0x70,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
    {"slot_7c",ES2_COLLIDE_SLOT,0x7c,0},
    {"path_contact",ES2_COLLIDE_CONTACT,0,0},
    {"path_slope_drift",ES2_COLLIDE_SLOPE_DRIFT,0,0},
    {"path_slide_damage",ES2_COLLIDE_SLIDE_DAMAGE,0,0},
};
static callee_counter ground_effects_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x7ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x8043c,0},
    {"call_47de72",ES2_COLLIDE_CALL,0x7de72,0},
    {"call_4628e0",ES2_COLLIDE_CALL,0x628e0,0},
    {"call_434144",ES2_COLLIDE_CALL,0x34144,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
};
static callee_counter vehicle_hit_counters[]={
    {"call_492934",ES2_COLLIDE_CALL,0x92934,0},
    {"call_413c68",ES2_COLLIDE_CALL,0x13c68,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x92ee0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x9288c,0},
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
};
static callee_counter building_hit_counters[]={
    {"call_492ee0",ES2_COLLIDE_CALL,0x92ee0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x9288c,0},
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
};
#define VEHICLE_HIT_COUNTERS (sizeof(vehicle_hit_counters)/sizeof(vehicle_hit_counters[0]))
#define BUILDING_HIT_COUNTERS (sizeof(building_hit_counters)/sizeof(building_hit_counters[0]))
static void damage_observe(void *image,es2_damage_event event,uint32_t key,callee_counter *c,unsigned count) {
    if(event==ES2_DAMAGE_CALL)key-=(uint32_t)(uintptr_t)image;
    for(unsigned i=0;i<count;++i)if((unsigned)c[i].event==(unsigned)event && c[i].key==key){++c[i].count;return;}
}
static void vehicle_hit_observe(void *image,es2_damage_event event,uint32_t key) {
    damage_observe(image,event,key,vehicle_hit_counters,VEHICLE_HIT_COUNTERS);
}
static void building_hit_observe(void *image,es2_damage_event event,uint32_t key) {
    damage_observe(image,event,key,building_hit_counters,BUILDING_HIT_COUNTERS);
}
#define GROUND_UPDATE_COUNTERS (sizeof(ground_update_counters)/sizeof(ground_update_counters[0]))
#define GROUND_COLLIDE_COUNTERS (sizeof(ground_collide_counters)/sizeof(ground_collide_counters[0]))
#define GROUND_EFFECTS_COUNTERS (sizeof(ground_effects_counters)/sizeof(ground_effects_counters[0]))
static void ground_effects_observe(void *image,es2_effects_event event,uint32_t key) {
    if(event==ES2_EFFECTS_CALL) key-=(uint32_t)(uintptr_t)image;
    for(unsigned i=0;i<GROUND_EFFECTS_COUNTERS;++i)
        if((unsigned)ground_effects_counters[i].event==(unsigned)event && ground_effects_counters[i].key==key) {
            ++ground_effects_counters[i].count; return;
        }
}
static void ground_update_count(uint32_t rva) {
    for(unsigned i=0;i<GROUND_UPDATE_COUNTERS;++i)
        if(ground_update_counters[i].key==rva) { ++ground_update_counters[i].count; return; }
}
static void ground_collide_observe(void *image,es2_collide_event event,uint32_t key) {
    if(event==ES2_COLLIDE_CALL) key-=(uint32_t)(uintptr_t)image;
    for(unsigned i=0;i<GROUND_COLLIDE_COUNTERS;++i)
        if(ground_collide_counters[i].event==event && ground_collide_counters[i].key==key) {
            ++ground_collide_counters[i].count; return;
        }
}
static int callee_tick_fields(char *out,size_t capacity,const char *hook,const callee_counter *c,unsigned count) {
    int n=0;
    for(unsigned i=0;i<count;++i) {
        int added=snprintf(out+n,capacity-(size_t)n,",\"hk_%s_%s\":%llu",hook,c[i].name,(unsigned long long)c[i].count);
        if(added<=0 || (size_t)added>=capacity-(size_t)n) return 0;
        n+=added;
    }
    return n;
}
static callee_counter radius_hit_counters[]={
    {"slot_5c",ES2_COLLIDE_SLOT,0x5c,0},
    {"slot_70",ES2_COLLIDE_SLOT,0x70,0},
};
#define RADIUS_HIT_COUNTERS (sizeof(radius_hit_counters)/sizeof(radius_hit_counters[0]))
static void radius_hit_observe(void *unused,unsigned slot) {
    (void)unused;
    for(unsigned i=0;i<RADIUS_HIT_COUNTERS;++i)
        if(radius_hit_counters[i].key==slot) { ++radius_hit_counters[i].count; return; }
}
static callee_counter weapon_prepare_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47f4f4",ES2_COLLIDE_CALL,0x47f4f4,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_40f540",ES2_COLLIDE_CALL,0x40f540,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47f9a0",ES2_COLLIDE_CALL,0x47f9a0,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
};
static callee_counter bullet_flight_counters[]={
    {"call_40adc0",ES2_COLLIDE_CALL,0x40adc0,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_40aff0",ES2_COLLIDE_CALL,0x40aff0,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_40501c",ES2_COLLIDE_CALL,0x40501c,0},
    {"call_426b04",ES2_COLLIDE_CALL,0x426b04,0},
    {"call_426528",ES2_COLLIDE_CALL,0x426528,0},
    {"call_426a20",ES2_COLLIDE_CALL,0x426a20,0},
};
static callee_counter projectile_flight_counters[]={
    {"call_40a234",ES2_COLLIDE_CALL,0x40a234,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_40a488",ES2_COLLIDE_CALL,0x40a488,0},
    {"call_40a254",ES2_COLLIDE_CALL,0x40a254,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_426528",ES2_COLLIDE_CALL,0x426528,0},
    {"call_45b734",ES2_COLLIDE_CALL,0x45b734,0},
    {"call_47de72",ES2_COLLIDE_CALL,0x47de72,0},
    {"call_462830",ES2_COLLIDE_CALL,0x462830,0},
};
static callee_counter vehicle_part_counters[]={
    {"call_4240f4",ES2_COLLIDE_CALL,0x4240f4,0},
    {"call_40dbc0",ES2_COLLIDE_CALL,0x40dbc0,0},
    {"call_40da38",ES2_COLLIDE_CALL,0x40da38,0},
    {"call_420a68",ES2_COLLIDE_CALL,0x420a68,0},
    {"call_417bec",ES2_COLLIDE_CALL,0x417bec,0},
    {"call_478628",ES2_COLLIDE_CALL,0x478628,0},
    {"call_411bc8",ES2_COLLIDE_CALL,0x411bc8,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
    {"call_413e50",ES2_COLLIDE_CALL,0x413e50,0},
    {"call_40d9f8",ES2_COLLIDE_CALL,0x40d9f8,0},
    {"call_409904",ES2_COLLIDE_CALL,0x409904,0},
    {"slot_68",ES2_COLLIDE_SLOT,0x68,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
};
static callee_counter target_emit_counters[]={
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_40ffb0",ES2_COLLIDE_CALL,0x40ffb0,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_422108",ES2_COLLIDE_CALL,0x422108,0},
    {"call_49295c",ES2_COLLIDE_CALL,0x49295c,0},
    {"call_422098",ES2_COLLIDE_CALL,0x422098,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_40a9c4",ES2_COLLIDE_CALL,0x40a9c4,0},
    {"call_40b43c",ES2_COLLIDE_CALL,0x40b43c,0},
    {"call_4222fc",ES2_COLLIDE_CALL,0x4222fc,0},
    {"slot_38",ES2_COLLIDE_SLOT,0x38,0},
};
static callee_counter emit_counters[]={
    {"call_40e788",ES2_COLLIDE_CALL,0x40e788,0},
    {"call_40bf74",ES2_COLLIDE_CALL,0x40bf74,0},
    {"call_40b5a0",ES2_COLLIDE_CALL,0x40b5a0,0},
    {"call_40f540",ES2_COLLIDE_CALL,0x40f540,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_40b43c",ES2_COLLIDE_CALL,0x40b43c,0},
};
static callee_counter alt_emit_counters[]={
    {"call_40e788",ES2_COLLIDE_CALL,0x40e788,0},
    {"call_40bf74",ES2_COLLIDE_CALL,0x40bf74,0},
};
static callee_counter impact_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_40ffc8",ES2_COLLIDE_CALL,0x40ffc8,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_426528",ES2_COLLIDE_CALL,0x426528,0},
    {"call_4628e0",ES2_COLLIDE_CALL,0x4628e0,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_40b804",ES2_COLLIDE_CALL,0x40b804,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
};
static callee_counter turret_counters[]={
    {"call_403ca8",ES2_COLLIDE_CALL,0x403ca8,0},
    {"call_403914",ES2_COLLIDE_CALL,0x403914,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_411fa0",ES2_COLLIDE_CALL,0x411fa0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_40ffb0",ES2_COLLIDE_CALL,0x40ffb0,0},
    {"call_4929fc",ES2_COLLIDE_CALL,0x4929fc,0},
    {"call_403eec",ES2_COLLIDE_CALL,0x403eec,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_47f9a0",ES2_COLLIDE_CALL,0x47f9a0,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_40a9c4",ES2_COLLIDE_CALL,0x40a9c4,0},
    {"call_40b43c",ES2_COLLIDE_CALL,0x40b43c,0},
    {"slot_30",ES2_COLLIDE_SLOT,0x30,0},
    {"slot_38",ES2_COLLIDE_SLOT,0x38,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
};
static callee_counter sweep_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_426b04",ES2_COLLIDE_CALL,0x426b04,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
    {"call_427d98",ES2_COLLIDE_CALL,0x427d98,0},
    {"call_428048",ES2_COLLIDE_CALL,0x428048,0},
    {"call_427da0",ES2_COLLIDE_CALL,0x427da0,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
    {"call_423974",ES2_COLLIDE_CALL,0x423974,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_41f790",ES2_COLLIDE_CALL,0x41f790,0},
    {"slot_50",ES2_COLLIDE_SLOT,0x50,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_407f1c",ES2_COLLIDE_CALL,0x407f1c,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"slot_64",ES2_COLLIDE_SLOT,0x64,0},
};
static callee_counter side_scale_counters[]={
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
};
static callee_counter segment_counters[]={
    {"call_46c23c",ES2_COLLIDE_CALL,0x46c23c,0},
    {"call_46ff90",ES2_COLLIDE_CALL,0x46ff90,0},
    {"call_46fdb8",ES2_COLLIDE_CALL,0x46fdb8,0},
    {"call_470080",ES2_COLLIDE_CALL,0x470080,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_470468",ES2_COLLIDE_CALL,0x470468,0},
    {"call_470798",ES2_COLLIDE_CALL,0x470798,0},
};
static callee_counter normalize_counters[]={
    {"call_47de72",ES2_COLLIDE_CALL,0x47de72,0},
};
static callee_counter face_test_counters[]={
    {"call_46ff4c",ES2_COLLIDE_CALL,0x46ff4c,0},
};
static callee_counter face_cross_counters[]={
    {"call_46ff4c",ES2_COLLIDE_CALL,0x46ff4c,0},
};
static callee_counter contact_counters[]={
    {"call_47e610",ES2_COLLIDE_CALL,0x47e610,0},
};
static callee_counter terrain_line_counters[]={
    {"call_46e980",ES2_COLLIDE_CALL,0x46e980,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
static callee_counter sweep_terrain_counters[]={
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_46e980",ES2_COLLIDE_CALL,0x46e980,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
static callee_counter camera_update_counters[]={
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_46e498",ES2_COLLIDE_CALL,0x46e498,0},
    {"call_47de4c",ES2_COLLIDE_CALL,0x47de4c,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47fa20",ES2_COLLIDE_CALL,0x47fa20,0},
    {"call_47f9a0",ES2_COLLIDE_CALL,0x47f9a0,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
};
static callee_counter object_line_counters[]={
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
    {"call_404bc0",ES2_COLLIDE_CALL,0x404bc0,0},
};
static callee_counter obstacle_steer_counters[]={
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_404ca0",ES2_COLLIDE_CALL,0x404ca0,0},
    {"call_46e980",ES2_COLLIDE_CALL,0x46e980,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"slot_7c",ES2_COLLIDE_SLOT,0x7c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
};
static callee_counter terrain_pose_counters[]={
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_47d32c",ES2_COLLIDE_CALL,0x47d32c,0},
};
static callee_counter shield_geometry_counters[]={
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47de4c",ES2_COLLIDE_CALL,0x47de4c,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
};
static callee_counter debris_heading_counters[]={
    {"call_47dff0",ES2_COLLIDE_CALL,0x47dff0,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
};
static callee_counter trigger_scan_counters[]={
    {"call_4234b8",ES2_COLLIDE_CALL,0x4234b8,0},
    {"call_423508",ES2_COLLIDE_CALL,0x423508,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
};
static callee_counter trigger_regions_counters[]={
    {"call_4233a4",ES2_COLLIDE_CALL,0x4233a4,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
};
static callee_counter region_test_counters[]={
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
};
static callee_counter trigger_group_counters[]={
    {"call_4234b8",ES2_COLLIDE_CALL,0x4234b8,0},
};
static callee_counter trigger_fire_counters[]={
    {"call_433158",ES2_COLLIDE_CALL,0x433158,0},
};
static callee_counter explosion_update_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_4076a0",ES2_COLLIDE_CALL,0x4076a0,0},
};
static callee_counter fire_update_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_46297c",ES2_COLLIDE_CALL,0x46297c,0},
};
static callee_counter group_fire_counters[]={
    {"slot_48",ES2_COLLIDE_SLOT,0x48,0},
    {"slot_34",ES2_COLLIDE_SLOT,0x34,0},
    {"call_40ede8",ES2_COLLIDE_CALL,0x40ede8,0},
    {"slot_28",ES2_COLLIDE_SLOT,0x28,0},
};
static callee_counter sphere_hits_counters[]={
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_40c428",ES2_COLLIDE_CALL,0x40c428,0},
};
static callee_counter sphere_test_counters[]={
    {"call_47de4c",ES2_COLLIDE_CALL,0x47de4c,0},
};
static callee_counter track_seek_counters[]={
    {"call_4792ac",ES2_COLLIDE_CALL,0x4792ac,0},
};
static callee_counter turret_aim_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
    {"call_479344",ES2_COLLIDE_CALL,0x479344,0},
    {"call_402604",ES2_COLLIDE_CALL,0x402604,0},
};
static callee_counter slope_speed_counters[]={
    {"call_46e498",ES2_COLLIDE_CALL,0x46e498,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
};
static callee_counter random_damage_counters[]={
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_40dc58",ES2_COLLIDE_CALL,0x40dc58,0},
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"slot_74",ES2_COLLIDE_SLOT,0x74,0},
};
static callee_counter vehicle_control_counters[]={
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_41693c",ES2_COLLIDE_CALL,0x41693c,0},
    {"call_416274",ES2_COLLIDE_CALL,0x416274,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_40dbc0",ES2_COLLIDE_CALL,0x40dbc0,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
    {"call_4792ac",ES2_COLLIDE_CALL,0x4792ac,0},
    {"call_47967c",ES2_COLLIDE_CALL,0x47967c,0},
    {"call_4628e0",ES2_COLLIDE_CALL,0x4628e0,0},
    {"call_417a04",ES2_COLLIDE_CALL,0x417a04,0},
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
};
static callee_counter objective_eval_counters[]={
    {"call_41324c",ES2_COLLIDE_CALL,0x41324c,0},
    {"call_413920",ES2_COLLIDE_CALL,0x413920,0},
    {"call_413950",ES2_COLLIDE_CALL,0x413950,0},
    {"call_412d90",ES2_COLLIDE_CALL,0x412d90,0},
    {"call_412c58",ES2_COLLIDE_CALL,0x412c58,0},
    {"call_4137b4",ES2_COLLIDE_CALL,0x4137b4,0},
};
static callee_counter actor_safe_counters[]={
    {"call_411c58",ES2_COLLIDE_CALL,0x411c58,0},
    {"call_423918",ES2_COLLIDE_CALL,0x423918,0},
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
static callee_counter target_known_counters[]={
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
static callee_counter order_done_counters[]={
    {"call_412e58",ES2_COLLIDE_CALL,0x412e58,0},
    {"call_412e74",ES2_COLLIDE_CALL,0x412e74,0},
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
};
static callee_counter subject_done_counters[]={
    {"call_412dc4",ES2_COLLIDE_CALL,0x412dc4,0},
};
static callee_counter rivals_done_counters[]={
    {"call_412be4",ES2_COLLIDE_CALL,0x412be4,0},
};
static callee_counter outcome_poll_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_41379c",ES2_COLLIDE_CALL,0x41379c,0},
    {"call_413180",ES2_COLLIDE_CALL,0x413180,0},
};
static callee_counter status_eval_counters[]={
    {"call_4135e8",ES2_COLLIDE_CALL,0x4135e8,0},
};
static callee_counter bounds_check_counters[]={
    {"call_41373c",ES2_COLLIDE_CALL,0x41373c,0},
};
static callee_counter explosion_create_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_402188",ES2_COLLIDE_CALL,0x402188,0},
    {"call_471b30",ES2_COLLIDE_CALL,0x471b30,0},
    {"call_407604",ES2_COLLIDE_CALL,0x407604,0},
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"call_4628e0",ES2_COLLIDE_CALL,0x4628e0,0},
};
static callee_counter object_init_counters[]={
    {"call_49630c",ES2_COLLIDE_CALL,0x49630c,0},
    {"call_47749c",ES2_COLLIDE_CALL,0x47749c,0},
    {"call_490b64",ES2_COLLIDE_CALL,0x490b64,0},
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
static callee_counter light_create_counters[]={
    {"call_406f38",ES2_COLLIDE_CALL,0x406f38,0},
    {"call_407048",ES2_COLLIDE_CALL,0x407048,0},
};
static callee_counter sight_check_counters[]={
    {"call_492f24",ES2_COLLIDE_CALL,0x492f24,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_4280f4",ES2_COLLIDE_CALL,0x4280f4,0},
};
static callee_counter group_state_counters[]={
    {"slot_40",ES2_COLLIDE_SLOT,0x40,0},
};
static callee_counter target_bearing_counters[]={
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
static callee_counter part_position_counters[]={
    {"slot_20",ES2_COLLIDE_SLOT,0x20,0},
    {"call_4801d4",ES2_COLLIDE_CALL,0x4801d4,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
};
static callee_counter fire_selected_counters[]={
    {"slot_2c",ES2_COLLIDE_SLOT,0x2c,0},
    {"slot_30",ES2_COLLIDE_SLOT,0x30,0},
    {"slot_28",ES2_COLLIDE_SLOT,0x28,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
};
static callee_counter fire_line_counters[]={
    {"call_410dbc",ES2_COLLIDE_CALL,0x410dbc,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
};
static callee_counter aim_point_counters[]={
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_40e4dc",ES2_COLLIDE_CALL,0x40e4dc,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
};
static callee_counter leg_input_counters[]={
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
};
static callee_counter target_select_counters[]={
    {"call_411e80",ES2_COLLIDE_CALL,0x411e80,0},
    {"call_423918",ES2_COLLIDE_CALL,0x423918,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"slot_4c",ES2_COLLIDE_SLOT,0x4c,0},
};
static callee_counter target_valid_counters[]={
    {"call_411c58",ES2_COLLIDE_CALL,0x411c58,0},
    {"call_4238a0",ES2_COLLIDE_CALL,0x4238a0,0},
};
static callee_counter death_actions_counters[]={
    {"call_423f30",ES2_COLLIDE_CALL,0x423f30,0},
};
static callee_counter combat_rating_counters[]={
    {"call_41b4ec",ES2_COLLIDE_CALL,0x41b4ec,0},
    {"slot_54",ES2_COLLIDE_SLOT,0x54,0},
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
};
static callee_counter condition_record_counters[]={
    {"call_4151a4",ES2_COLLIDE_CALL,0x4151a4,0},
};
static callee_counter ready_tone_counters[]={
    {"call_467828",ES2_COLLIDE_CALL,0x467828,0},
    {"call_462830",ES2_COLLIDE_CALL,0x462830,0},
};
static callee_counter weapon_tally_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
};
/* Slot 0 of the weapon group is observed as a call of its body (41194a or
 * 411a1d), because a zero slot means a direct call to the observer. */
static callee_counter vehicle_update_counters[]={
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_41194a",ES2_COLLIDE_CALL,0x41194a,0},
    {"call_411a1d",ES2_COLLIDE_CALL,0x411a1d,0},
    {"call_413b38",ES2_COLLIDE_CALL,0x413b38,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_41edd8",ES2_COLLIDE_CALL,0x41edd8,0},
    {"call_41aa44",ES2_COLLIDE_CALL,0x41aa44,0},
    {"call_41aa10",ES2_COLLIDE_CALL,0x41aa10,0},
    {"call_40fbdc",ES2_COLLIDE_CALL,0x40fbdc,0},
    {"call_492ee0",ES2_COLLIDE_CALL,0x492ee0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_412608",ES2_COLLIDE_CALL,0x412608,0},
    {"slot_60",ES2_COLLIDE_SLOT,0x60,0},
    {"call_41b0bc",ES2_COLLIDE_CALL,0x41b0bc,0},
};
static callee_counter poly_spans_counters[]={
    {"call_4930c1",ES2_COLLIDE_CALL,0x4930c1,0},
};
/* flight_model 466b58 (movement_step also from the leaves 466ae0/466b20). */
static callee_counter flight_model_counters[]={
    {"call_477904",ES2_COLLIDE_CALL,0x477904,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
    {"call_47e0a0",ES2_COLLIDE_CALL,0x47e0a0,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_47dff0",ES2_COLLIDE_CALL,0x47dff0,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_47f9a0",ES2_COLLIDE_CALL,0x47f9a0,0},
};
static callee_counter flyer_move_counters[]={
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_47de72",ES2_COLLIDE_CALL,0x47de72,0},
    {"call_462830",ES2_COLLIDE_CALL,0x462830,0},
};
static callee_counter formation_steer_counters[]={
    {"slot_78",ES2_COLLIDE_SLOT,0x78,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_416a04",ES2_COLLIDE_CALL,0x416a04,0},
};
static callee_counter cockpit_valid_counters[]={
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
};
static callee_counter cockpit_arc_counters[]={
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
};
static callee_counter cockpit_next_counters[]={
    {"call_433174",ES2_COLLIDE_CALL,0x433174,0},
    {"call_433250",ES2_COLLIDE_CALL,0x433250,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"slot_3c",ES2_COLLIDE_SLOT,0x3c,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"slot_10",ES2_COLLIDE_SLOT,0x10,0},
    {"call_47d32c",ES2_COLLIDE_CALL,0x47d32c,0},
    {"call_47e0b0",ES2_COLLIDE_CALL,0x47e0b0,0},
    {"call_43d9b4",ES2_COLLIDE_CALL,0x43d9b4,0},
};
static callee_counter cockpit_nearest_counters[]={
    {"call_433174",ES2_COLLIDE_CALL,0x433174,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_43d9b4",ES2_COLLIDE_CALL,0x43d9b4,0},
};
static callee_counter cockpit_select_counters[]={
    {"call_43349c",ES2_COLLIDE_CALL,0x43349c,0},
    {"call_433174",ES2_COLLIDE_CALL,0x433174,0},
    {"call_433250",ES2_COLLIDE_CALL,0x433250,0},
    {"call_43d9b4",ES2_COLLIDE_CALL,0x43d9b4,0},
};
/* Route group. */
static callee_counter player_route_counters[]={
    {"call_423b0c",ES2_COLLIDE_CALL,0x423b0c,0},
    {"call_4928d0",ES2_COLLIDE_CALL,0x4928d0,0},
    {"call_423170",ES2_COLLIDE_CALL,0x423170,0},
    {"call_420a68",ES2_COLLIDE_CALL,0x420a68,0},
    {"call_42313c",ES2_COLLIDE_CALL,0x42313c,0},
    {"call_423918",ES2_COLLIDE_CALL,0x423918,0},
    {"call_41dbcc",ES2_COLLIDE_CALL,0x41dbcc,0},
    {"call_4238a0",ES2_COLLIDE_CALL,0x4238a0,0},
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_492934",ES2_COLLIDE_CALL,0x492934,0},
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
};
static callee_counter timer_scan_counters[]={
    {"call_4230a4",ES2_COLLIDE_CALL,0x4230a4,0},
};
static callee_counter mission_timer_counters[]={
    {"call_467aa8",ES2_COLLIDE_CALL,0x467aa8,0},
    {"call_423430",ES2_COLLIDE_CALL,0x423430,0},
    {"call_467ac4",ES2_COLLIDE_CALL,0x467ac4,0},
};
static callee_counter status_check_counters[]={
    {"call_467a48",ES2_COLLIDE_CALL,0x467a48,0},
    {"call_413780",ES2_COLLIDE_CALL,0x413780,0},
    {"call_41379c",ES2_COLLIDE_CALL,0x41379c,0},
    {"call_413280",ES2_COLLIDE_CALL,0x413280,0},
    {"event_post",ES2_COLLIDE_CALL,0x4136fb,0},
};
static callee_counter object_query_counters[]={
    {"call_49288c",ES2_COLLIDE_CALL,0x49288c,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_47df19",ES2_COLLIDE_CALL,0x47df19,0},
    {"call_480304",ES2_COLLIDE_CALL,0x480304,0},
    {"call_4273c8",ES2_COLLIDE_CALL,0x4273c8,0},
};
static callee_counter grid_sweep_counters[]={
    {"call_4927f0",ES2_COLLIDE_CALL,0x4927f0,0},
    {"call_47de72",ES2_COLLIDE_CALL,0x47de72,0},
    {"call_427238",ES2_COLLIDE_CALL,0x427238,0},
};
static callee_counter set_length_counters[]={
    {"call_49681a",ES2_COLLIDE_CALL,0x49681a,0},
};
/* Flyer control 41bb9c, 4221a8, 42215c, 4222fc. */
static callee_counter flyer_input_counters[]={
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_466b58",ES2_COLLIDE_CALL,0x466b58,0},
};
static callee_counter flyer_command_counters[]={
    {"call_466b58",ES2_COLLIDE_CALL,0x466b58,0},
};
static callee_counter flyer_bank_counters[]={
    {"call_4221a8",ES2_COLLIDE_CALL,0x4221a8,0},
};
static callee_counter flyer_steer_counters[]={
    {"call_47dff0",ES2_COLLIDE_CALL,0x47dff0,0},
    {"call_46e180",ES2_COLLIDE_CALL,0x46e180,0},
    {"call_42215c",ES2_COLLIDE_CALL,0x42215c,0},
    {"call_4221a8",ES2_COLLIDE_CALL,0x4221a8,0},
};
static callee_counter projectile_steer_counters[]={
    {"slot_58",ES2_COLLIDE_SLOT,0x58,0},
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
};
static callee_counter bullet_steer_counters[]={
    {"slot_24",ES2_COLLIDE_SLOT,0x24,0},
    {"call_47ebb8",ES2_COLLIDE_CALL,0x47ebb8,0},
    {"call_48043c",ES2_COLLIDE_CALL,0x48043c,0},
    {"call_492990",ES2_COLLIDE_CALL,0x492990,0},
    {"call_467adc",ES2_COLLIDE_CALL,0x467adc,0},
    {"call_467924",ES2_COLLIDE_CALL,0x467924,0},
};
/* Graph counters of the legacy groups (the list is generated from the
 * registry by scripts/legacy_graph_counters.py; a newer group keeps its
 * counters in its own adapter header, see below). */
static callee_counter *legacy_graph_counters(unsigned hook,unsigned *count) {
#define GRAPH_COUNTERS(name) if(hook==ES2_HOOK_INDEX_##name) { *count=sizeof(name##_counters)/sizeof(name##_counters[0]);return name##_counters; }
/* BEGIN generated: legacy graph counters (scripts/legacy_graph_counters.py) */
    GRAPH_COUNTERS(weapon_prepare)
    GRAPH_COUNTERS(bullet_flight)
    GRAPH_COUNTERS(projectile_flight)
    GRAPH_COUNTERS(vehicle_part)
    GRAPH_COUNTERS(target_emit)
    GRAPH_COUNTERS(emit)
    GRAPH_COUNTERS(alt_emit)
    GRAPH_COUNTERS(impact)
    GRAPH_COUNTERS(turret)
    GRAPH_COUNTERS(sweep)
    GRAPH_COUNTERS(side_scale)
    GRAPH_COUNTERS(segment)
    GRAPH_COUNTERS(sweep_terrain)
    GRAPH_COUNTERS(normalize)
    GRAPH_COUNTERS(face_test)
    GRAPH_COUNTERS(face_cross)
    GRAPH_COUNTERS(contact)
    GRAPH_COUNTERS(terrain_line)
    GRAPH_COUNTERS(camera_update)
    GRAPH_COUNTERS(object_line)
    GRAPH_COUNTERS(obstacle_steer)
    GRAPH_COUNTERS(terrain_pose)
    GRAPH_COUNTERS(shield_geometry)
    GRAPH_COUNTERS(debris_heading)
    GRAPH_COUNTERS(trigger_scan)
    GRAPH_COUNTERS(trigger_regions)
    GRAPH_COUNTERS(region_test)
    GRAPH_COUNTERS(trigger_group)
    GRAPH_COUNTERS(trigger_fire)
    GRAPH_COUNTERS(explosion_update)
    GRAPH_COUNTERS(fire_update)
    GRAPH_COUNTERS(group_fire)
    GRAPH_COUNTERS(sphere_hits)
    GRAPH_COUNTERS(sphere_test)
    GRAPH_COUNTERS(track_seek)
    GRAPH_COUNTERS(turret_aim)
    GRAPH_COUNTERS(slope_speed)
    GRAPH_COUNTERS(random_damage)
    GRAPH_COUNTERS(vehicle_control)
    GRAPH_COUNTERS(objective_eval)
    GRAPH_COUNTERS(actor_safe)
    GRAPH_COUNTERS(target_known)
    GRAPH_COUNTERS(order_done)
    GRAPH_COUNTERS(subject_done)
    GRAPH_COUNTERS(rivals_done)
    GRAPH_COUNTERS(outcome_poll)
    GRAPH_COUNTERS(status_eval)
    GRAPH_COUNTERS(bounds_check)
    GRAPH_COUNTERS(explosion_create)
    GRAPH_COUNTERS(object_init)
    GRAPH_COUNTERS(light_create)
    GRAPH_COUNTERS(sight_check)
    GRAPH_COUNTERS(group_state)
    GRAPH_COUNTERS(target_bearing)
    GRAPH_COUNTERS(part_position)
    GRAPH_COUNTERS(fire_selected)
    GRAPH_COUNTERS(fire_line)
    GRAPH_COUNTERS(aim_point)
    GRAPH_COUNTERS(leg_input)
    GRAPH_COUNTERS(target_select)
    GRAPH_COUNTERS(target_valid)
    GRAPH_COUNTERS(death_actions)
    GRAPH_COUNTERS(combat_rating)
    GRAPH_COUNTERS(condition_record)
    GRAPH_COUNTERS(ready_tone)
    GRAPH_COUNTERS(weapon_tally)
    GRAPH_COUNTERS(vehicle_update)
    GRAPH_COUNTERS(poly_spans)
    GRAPH_COUNTERS(flight_model)
    GRAPH_COUNTERS(flyer_move)
    GRAPH_COUNTERS(formation_steer)
    GRAPH_COUNTERS(cockpit_valid)
    GRAPH_COUNTERS(cockpit_arc)
    GRAPH_COUNTERS(cockpit_next)
    GRAPH_COUNTERS(cockpit_nearest)
    GRAPH_COUNTERS(cockpit_select)
    GRAPH_COUNTERS(player_route)
    GRAPH_COUNTERS(timer_scan)
    GRAPH_COUNTERS(mission_timer)
    GRAPH_COUNTERS(status_check)
    GRAPH_COUNTERS(object_query)
    GRAPH_COUNTERS(grid_sweep)
    GRAPH_COUNTERS(set_length)
    GRAPH_COUNTERS(flyer_input)
    GRAPH_COUNTERS(flyer_command)
    GRAPH_COUNTERS(flyer_bank)
    GRAPH_COUNTERS(flyer_steer)
    GRAPH_COUNTERS(projectile_steer)
    GRAPH_COUNTERS(bullet_steer)
/* END generated: legacy graph counters */
#undef GRAPH_COUNTERS
    *count=0;return NULL;
}
/* Every registry group G provides
 *     static callee_counter *G_hook_counters(unsigned hook,unsigned *count)
 * returning the callee counter array of one of its hooks and its length, or
 * NULL and 0 for a hook without game calls. A newer group defines it in its
 * own adapter header next to its own counter arrays (callee_counter and
 * ES2_COLLIDE_* are declared here, before every group header); the legacy
 * groups map to the generated list above. */
/* BEGIN generated: legacy group definitions (scripts/legacy_graph_counters.py) */
#define rng_hook_counters legacy_graph_counters
#define geometry_hook_counters legacy_graph_counters
#define timing_hook_counters legacy_graph_counters
#define movement_hook_counters legacy_graph_counters
#define math_hook_counters legacy_graph_counters
#define damage_hook_counters legacy_graph_counters
#define weapons_hook_counters legacy_graph_counters
#define ai_hook_counters legacy_graph_counters
#define node_hook_counters legacy_graph_counters
#define mission_hook_counters legacy_graph_counters
#define logic_hook_counters legacy_graph_counters
#define weapon_helpers_hook_counters legacy_graph_counters
#define track_pose_hook_counters legacy_graph_counters
#define vehicle_control_hook_counters legacy_graph_counters
#define mission_objectives_hook_counters legacy_graph_counters
#define effect_hook_counters legacy_graph_counters
#define vehicle_hook_counters legacy_graph_counters
#define input_hook_counters legacy_graph_counters
#define line_plane_hook_counters legacy_graph_counters
#define building_hook_counters legacy_graph_counters
#define vehicle_update_hook_counters legacy_graph_counters
#define world_hook_counters legacy_graph_counters
#define flyer_hook_counters legacy_graph_counters
#define ai_motion_hook_counters legacy_graph_counters
#define cockpit_target_hook_counters legacy_graph_counters
#define route_hook_counters legacy_graph_counters
#define object_query_hook_counters legacy_graph_counters
#define flyer_control_hook_counters legacy_graph_counters
#define projectile_hook_counters legacy_graph_counters
/* END generated: legacy group definitions */
#define ES2_HOOK(name,rva,descriptor,extension,group) static callee_counter *group##_hook_counters(unsigned hook,unsigned *count);
#include "hook_registry.h"
#undef ES2_HOOK
static callee_counter *(*const hook_counter_functions[])(unsigned,unsigned *)={
#define ES2_HOOK(name,rva,descriptor,extension,group) group##_hook_counters,
#include "hook_registry.h"
#undef ES2_HOOK
};
static callee_counter *weapons_graph_counters(unsigned hook,unsigned *count) {
    if(hook<sizeof(hook_counter_functions)/sizeof(hook_counter_functions[0]))return hook_counter_functions[hook](hook,count);
    *count=0;return NULL;
}
/* A graph's observe context points at its hook's index here. */
static const unsigned weapons_graph_hooks[]={
#define ES2_HOOK(name,rva,descriptor,extension,group) ES2_HOOK_INDEX_##name,
#include "hook_registry.h"
#undef ES2_HOOK
};
static inline void *weapons_graph_context(unsigned hook) {
    return hook<sizeof(weapons_graph_hooks)/sizeof(weapons_graph_hooks[0])?(void *)&weapons_graph_hooks[hook]:NULL;
}
static void weapons_graph_observe(void *context,uint32_t function,unsigned slot) {
    unsigned count;callee_counter *c=weapons_graph_counters(context?*(const unsigned *)context:~0u,&count);
    for(unsigned i=0;i<count;++i)if(c[i].event==(slot?ES2_COLLIDE_SLOT:ES2_COLLIDE_CALL) && c[i].key==(slot?slot:function)) {++c[i].count;return;}
}
#endif
