/* Synthetic damage contracts: no original byte arrays. Direct callees are
 * patched only in the private hash-verified mapping. V58's local output
 * pointer is normalized; its contents and all other full argument slots,
 * return pairs, writes, ordering and observer deltas are compared. */
static int damage_leaf_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    const es2_hook_extension *x=function_hook_extension(index);
    unsigned cases=case_budget(limit,1001024);
    BYTE data[64],before[64],after[64];
    static const uint32_t edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0x8000,0x7fff,0xffff,0x10000,1023,1024,1000,32767,32768,16384,4095};
    if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases;++n) {
        current_case=n;current_case_valid=1;
        if(!limit && n==4096)coverage_enable(0);
        random_bytes(data,sizeof(data));
        uint32_t a[6]={random32(),random32(),random32()},regs[3]={random32(),random32(),random32()};
        if(n<1024){a[0]=edges[n%16];a[1]=edges[(n/16)%16];a[2]=edges[(n/64)%16];}
        if(index==ES2_HOOK_INDEX_heading_point) {
            BYTE *p=data+1,*q=data+33;
            if(!limit && n%11==0)q=p; /* equal/alias points, compare intentionally skips aliases */
            put32(p,a[0]);put32(p+4,a[1]);put32(q,a[2]);put32(q+4,n<1024?0:random32());
            uint32_t dx=get32(p)-get32(q),dy=get32(p+4)-get32(q+4);
            if(!es2_angle_ratio_valid(dx,dy)) {put32(p,1);put32(q,0);put32(q+4,get32(p+4));}
            a[0]=(uint32_t)(uintptr_t)p;a[1]=(uint32_t)(uintptr_t)q;
        } else if(index==ES2_HOOK_INDEX_shield_damage) {
            put16(data+1,(uint16_t)a[0]);put16(data+3,(uint16_t)a[1]);
            a[0]=(uint32_t)(uintptr_t)(data+1);
            if(n<1024)a[1]=(n&1)?0x12340000u:0xffff0001u;
        }
        memcpy(before,data,sizeof(data));
        uint64_t got=actor_call(index,a,regs);
        memcpy(after,data,sizeof(data));memcpy(data,before,sizeof(data));
        uint64_t want=x->replacement_context(a,d->user,regs);
        if(got!=want || memcmp(data,after,sizeof(data))) {
            fprintf(stderr,"FAIL: %s case %u original=%016llx core=%016llx args=%08x,%08x,%08x\n",d->name,n,(unsigned long long)got,(unsigned long long)want,a[0],a[1],a[2]);return 1;
        }
        if(index!=ES2_HOOK_INDEX_shield_damage && memcmp(data,before,sizeof(data)))return fail("read-only leaf write");
    }
    if(!limit)coverage_enable(0);current_case_valid=0;
    printf("PASS: %-20s %u cases; full EDX:EAX, unaligned buffers, guards and signed boundaries\n",d->name,cases);fflush(stdout);return 0;
}

typedef struct hit_record { uint32_t id,args[8];uint64_t result; } hit_record;
static hit_record hit_log[128];static unsigned hit_logged,hit_next,hit_case;
static BYTE hit_arena[2048],hit_before[2048],hit_after[2048];
static int hit_forced,hit_composition;static uint32_t hit_forced_distance,hit_forced_remaining;
static uint32_t hit_script[256],hit_vtable[2][32],hit_actor,hit_point,hit_config[2],hit_flags[2];
static uint32_t hit_take(void) { return hit_script[hit_next++%256]; }
static hit_record *hit_note(unsigned id,const uint32_t *args,unsigned count) {
    if(hit_logged>=128){fail("hit log overflow");ExitProcess(1);}
    hit_record *r=&hit_log[hit_logged++];memset(r,0,sizeof(*r));r->id=id;
    for(unsigned i=0;i<count;++i)r->args[i]=args[i];return r;
}
static uint64_t hit_result(hit_record *r) {
    uint32_t eax=hit_take();r->result=((uint64_t)hit_take()<<32)|eax;return r->result;
}
static uint64_t __cdecl hit_heading(uint32_t p,uint32_t q) {
    uint32_t args[2]={p,q};hit_record *r=hit_note(0,args,2);
    for(unsigned i=0;i<2;++i){r->args[2+i]=get32((BYTE *)(uintptr_t)p+4*i);r->args[4+i]=get32((BYTE *)(uintptr_t)q+4*i);}
    uint64_t result=hit_result(r);
    static const uint16_t angles[]={0,1,0xffff,0x3fff,0x4000,0x4001,0x7fff,0x8000,0x8001,0xbfff,0xc000,0xc001};
    if(hit_case<1024)result=(result&~UINT64_C(0xffff))|(uint16_t)(get16((BYTE *)(uintptr_t)hit_actor+0x10)+angles[hit_case%12]);
    r->result=result;return result;
}
static uint64_t __cdecl hit_shield(uint32_t shields,uint32_t front,uint32_t damage) {
    uint32_t args[3]={shields,front,damage};hit_record *r=hit_note(1,args,3);
    r->args[3]=get32((BYTE *)(uintptr_t)shields);
    put16((BYTE *)(uintptr_t)shields+((uint16_t)front?0:2),(uint16_t)hit_take());
    uint64_t result=hit_result(r);
    static const uint16_t amounts[]={0,1,0xffff,0x7fff,0x8000,1024,1000,32760};
    uint16_t value=hit_case<1024?amounts[(hit_case/12)%8]:(uint16_t)(hit_take()%32768);
    if(hit_forced)value=(uint16_t)hit_forced_remaining;
    r->result=(result&~UINT64_C(0xffff))|value;return r->result;
}
static uint64_t __cdecl hit_random(uint32_t generator) {
    hit_record *r=hit_note(2,&generator,1);r->args[1]=get32((BYTE *)(uintptr_t)hit_actor+0x70);
    put32((BYTE *)(uintptr_t)hit_actor+0x70,r->args[1]+1u);
    uint64_t result=hit_result(r);
    static const uint32_t trials[]={0,0x801,0x802,0xfff,0x1000,0x80000801};
    if(hit_case<1024)result=(result&UINT64_C(0xffffffff00000000))|trials[(hit_case/3+hit_logged)%6];
    if(hit_forced)result&=UINT64_C(0xffffffff00000000);
    r->result=result;return result;
}
static uint64_t hit_lookup_impl(uint32_t actor,uint32_t part,uint32_t out,unsigned variant) {
    uint32_t args[4]={actor,part,0,variant};hit_record *r=hit_note(4,args,4);
    for(unsigned i=0;i<3;++i){
        uint32_t v=hit_take();
        if(hit_composition)v=get32((BYTE *)(uintptr_t)hit_point+4*i)+(part%2?2000u:v%100u);
        put32((BYTE *)(uintptr_t)out+4*i,v);r->args[4+i]=v;
    }
    /* Changes demonstrate that the body reloads the vtable but keeps the flag
     * pointer captured before the loop. Both alternative arrays are valid. */
    if(hit_case%7==0)put32((BYTE *)(uintptr_t)actor,(uint32_t)(uintptr_t)hit_vtable[1]);
    if(hit_case%11==0){put32((BYTE *)(uintptr_t)actor+0x201,hit_flags[1]);put32((BYTE *)(uintptr_t)actor+0x20e,hit_flags[1]);}
    return hit_result(r);
}
static uint64_t __cdecl hit_lookup(uint32_t a,uint32_t p,uint32_t out){return hit_lookup_impl(a,p,out,0);}
static uint64_t __cdecl hit_lookup_alt(uint32_t a,uint32_t p,uint32_t out){return hit_lookup_impl(a,p,out,1);}
static uint64_t __cdecl hit_distance(uint32_t p,uint32_t q) {
    uint32_t args[2]={0,q};hit_record *r=hit_note(3,args,2);
    for(unsigned i=0;i<3;++i){r->args[2+i]=get32((BYTE *)(uintptr_t)p+4*i);r->args[5+i]=get32((BYTE *)(uintptr_t)q+4*i);}
    uint64_t result=hit_result(r);uint32_t distance=hit_take();
    static const uint32_t distances[]={0,1,1199,1200,1201,0x7fffffffu,0x80000000u,0xffffffffu};
    if(hit_case<1024)distance=distances[(hit_case/8+hit_logged)%8];
    if(hit_forced)distance=hit_forced_distance;
    r->result=(result&UINT64_C(0xffffffff00000000))|distance;return r->result;
}
static uint64_t hit_part_impl(uint32_t actor,uint32_t part,uint32_t damage,uint32_t source,unsigned variant) {
    uint32_t args[5]={actor,part,damage,source,variant};hit_record *r=hit_note(5,args,5);
    put32((BYTE *)(uintptr_t)actor+0x74,damage);put32((BYTE *)(uintptr_t)actor+0x78,part);
    /* A count/config reread on the next building loop test is observable. */
    if(hit_case%5==0)put32((BYTE *)(uintptr_t)actor+0x1f2,hit_config[1]);
    if(part<28 && hit_case%13==0)put16((BYTE *)(uintptr_t)hit_flags[0]+2*(part+1),0);
    return hit_result(r);
}
static uint64_t __cdecl hit_part(uint32_t a,uint32_t p,uint32_t d,uint32_t s){return hit_part_impl(a,p,d,s,0);}
static uint64_t __cdecl hit_part_alt(uint32_t a,uint32_t p,uint32_t d,uint32_t s){return hit_part_impl(a,p,d,s,1);}
static const struct {uint32_t rva;void *stub;} hit_stubs[]={
    {0x92934,(void *)hit_heading},{0x13c68,(void *)hit_shield},
    {0x92ee0,(void *)hit_random},{0x9288c,(void *)hit_distance}};
static void hit_reset(void){hit_logged=hit_next=0;memset(hit_log,0,sizeof(hit_log));}
static void hit_build(unsigned n,uint32_t args[6],uint32_t regs[3]) {
    hit_case=n;random_bytes(hit_arena,sizeof(hit_arena));
    for(unsigned i=0;i<256;++i)hit_script[i]=random32();
    hit_actor=(uint32_t)(uintptr_t)hit_arena;hit_point=hit_actor+0x600;
    hit_config[0]=hit_actor+0x300;hit_config[1]=hit_actor+0x380;
    hit_flags[0]=hit_actor+0x400;hit_flags[1]=hit_actor+0x440;
    hit_vtable[0][0x58/4]=(uint32_t)(uintptr_t)hit_lookup;
    hit_vtable[0][0x74/4]=(uint32_t)(uintptr_t)hit_part;
    hit_vtable[1][0x58/4]=(uint32_t)(uintptr_t)hit_lookup_alt;
    hit_vtable[1][0x74/4]=(uint32_t)(uintptr_t)hit_part_alt;
    put32(hit_arena,(uint32_t)(uintptr_t)hit_vtable[0]);put32(hit_arena+0x1f2,hit_config[0]);
    put32(hit_arena+0x201,hit_flags[0]);put32(hit_arena+0x205,hit_actor+0x480);put32(hit_arena+0x20e,hit_flags[0]);
    put16(hit_arena+0x485,(uint16_t)((n/3)%5-1));hit_arena[0x99]=(BYTE)(n%3==0);
    for(unsigned k=0;k<2;++k) {
        BYTE *c=(BYTE *)(uintptr_t)hit_config[k];
        put16(c+0x1e,(uint16_t)(n%17==0));put16(c+4,n%4?0xffff:1);
        put32(c+0x38,n%19?1:0);
        put16(c+0x12,(uint16_t)(n%23==0?0xffff:n%29==0?0:k?3:1+n%29));
        for(unsigned i=0;i<29;++i)put16((BYTE *)(uintptr_t)hit_flags[k]+2*i,(uint16_t)(n%31==0?0:random32()%3));
    }
    args[0]=hit_actor;args[1]=random32();args[2]=hit_point;
    /* Full-width product wrap is covered while range stays out of the divide
     * fault domain. Negative divisors are included; zero/-1 faults get probes. */
    static const uint16_t ranges[]={1,2,1200,32767,0x8000,0xfffe};
    args[3]=(random32()&0xffff0000u)|ranges[n%6];args[4]=random32();args[5]=0;
    for(unsigned i=0;i<3;++i)regs[i]=random32();
}
static int hit_domain_probes(unsigned index) {
    static const struct {uint32_t range,distance,damage;int fault;} probes[]={
        {0,0,2,0},{0,0xffffffffu,2,1},{0xffffffffu,0xbfffffffu,2,1},
        {2,0x80000000u,1,0},{0x8000u,0x80000000u,32767,0}};
    for(unsigned i=0;i<sizeof(probes)/sizeof(probes[0]);++i) {
        uint32_t a[6],r[3];hit_build(1025,a,r);hit_reset();
        put16((BYTE *)(uintptr_t)hit_config[0]+0x12,1);
        for(unsigned j=0;j<29;++j)put16((BYTE *)(uintptr_t)hit_flags[0]+2*j,j?0:1);
        a[1]=probes[i].damage;a[3]=probes[i].range;
        hit_forced=1;hit_forced_distance=probes[i].distance;hit_forced_remaining=probes[i].damage;
        if(probe_divide(0x400000u+function_hook_descriptor(index)->rva,5,a,probes[i].fault)){hit_forced=0;return 1;}
        hit_forced=0;
    }
    puts("PASS: damage zero-range skip/fault, low-product signed division and quotient-overflow probes");return 0;
}
/* Real arithmetic/RNG composition: all 512 independent original/replaced
 * choices for the body and eight reachable leaf entries. Virtual methods
 * remain own logging stubs. Restore RNG and actor state before every mode. */
static int hit_modes(unsigned index) {
    const unsigned indices[]={index,ES2_HOOK_INDEX_heading_point,ES2_HOOK_INDEX_shield_damage,
        ES2_HOOK_INDEX_rng_next,ES2_HOOK_INDEX_dist_gate,ES2_HOOK_INDEX_dist3d_delta,
        ES2_HOOK_INDEX_dist3d_forward,ES2_HOOK_INDEX_multiply_q10,ES2_HOOK_INDEX_angle_ratio};
    BYTE initial_rng[114],before_rng[114],after_rng[114];
    memcpy(initial_rng,mapped+0xd2599,114);hit_record expected[128];unsigned verified=0,lookups=0,parts=0;hit_composition=1;
    for(unsigned n=0;n<8;++n) {
        uint32_t a[6],regs[3];hit_build(1024+n,a,regs);hit_reset();
        put32(hit_arena+0x26,1000);put32(hit_arena+0x2a,2000);put32(hit_arena+0x2e,3000);
        for(unsigned k=0;k<3;++k)put32((BYTE *)(uintptr_t)hit_point+4*k,1000*(k+1)+random32()%300);
        put16(hit_arena+0x222,(uint16_t)(n*50));put16(hit_arena+0x224,(uint16_t)(n*70));
        a[1]=1000;a[3]=1200;
        random_bytes(mapped+0xd2599,112);mapped[0xd2599+112]=(BYTE)(n%56);mapped[0xd2599+113]=(BYTE)((n+17)%56);
        memcpy(before_rng,mapped+0xd2599,114);memcpy(hit_before,hit_arena,sizeof(hit_arena));
        uint64_t result=actor_call(index,a,regs);unsigned count=hit_logged;
        for(unsigned j=0;j<count;++j){lookups+=hit_log[j].id==4;parts+=hit_log[j].id==5;}
        memcpy(expected,hit_log,sizeof(expected));memcpy(hit_after,hit_arena,sizeof(hit_arena));memcpy(after_rng,mapped+0xd2599,114);
        for(unsigned mode=0;mode<512;++mode) {
            es2_hook local[9]={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
            for(unsigned j=0;j<9;++j) {
                o.mode=(mode&(1u<<j))?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
                if(!es2_hook_install_ex(local+j,function_hook_descriptor(indices[j]),&o,function_hook_extension(indices[j])))return fail("damage mixed-mode install");
            }
            memcpy(hit_arena,hit_before,sizeof(hit_arena));memcpy(mapped+0xd2599,before_rng,114);hit_reset();
            uint64_t got=actor_call(index,a,regs);
            int bad=got!=result || hit_logged!=count || memcmp(hit_log,expected,count*sizeof(*expected)) ||
                memcmp(hit_arena,hit_after,sizeof(hit_arena)) || memcmp(mapped+0xd2599,after_rng,114);
            for(unsigned j=0;j<9;++j)if(!es2_hook_uninstall(local+j))return fail("damage mixed-mode uninstall");
            if(bad){fprintf(stderr,"FAIL: %s case %u mixed mode=%u original=%016llx hooked=%016llx\n",function_hook_descriptor(index)->name,1001024+n,mode,(unsigned long long)result,(unsigned long long)got);return 1;}
            ++verified;
        }
    }
    memcpy(mapped+0xd2599,initial_rng,114);hit_composition=0;
    if(!parts || lookups<=parts)return fail("damage composition did not cover both range outcomes");
    printf("PASS: %s %u mixed-mode cases: real heading/shield/RNG/distance and all eight leaf modes; exact RNG state and virtual log, V58=%u V74=%u\n",function_hook_descriptor(index)->name,verified,lookups,parts);return 0;
}
static int damage_hit_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    unsigned cases=case_budget(limit,1001024);
    const char *override=getenv("ES2_INVITRO_DAMAGE_CASES");if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    BYTE saved[4][5];for(unsigned i=0;i<4;++i)if(actor_patch(hit_stubs[i].rva,hit_stubs[i].stub,saved[i],TRUE))return 1;
    int building=index==ES2_HOOK_INDEX_building_hit,status=0;
    callee_counter *c=building?building_hit_counters:vehicle_hit_counters;
    unsigned counters=building?BUILDING_HIT_COUNTERS:VEHICLE_HIT_COUNTERS;
    uint64_t totals[6]={0};hit_record expected[128];
    if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases && !status;++n) {
        current_case=n;current_case_valid=1;if(!limit && n==4096)coverage_enable(0);
        uint32_t args[6],regs[3];hit_build(n,args,regs);
        memcpy(hit_before,hit_arena,sizeof(hit_arena));hit_reset();
        uint64_t got=actor_call(index,args,regs);unsigned count=hit_logged;
        memcpy(expected,hit_log,sizeof(expected));memcpy(hit_after,hit_arena,sizeof(hit_arena));
        memcpy(hit_arena,hit_before,sizeof(hit_arena));hit_reset();
        uint64_t previous[6];for(unsigned i=0;i<counters;++i)previous[i]=c[i].count;
        BYTE point[12];es2_damage_context g=damage_context(mapped,(uint32_t)(uintptr_t)point,building);
        uint64_t want=building?es2_building_hit_registers(&harness_memory,&g,args[0],args[1],args[2],args[3],args[4],regs[1]):
            es2_vehicle_hit_registers(&harness_memory,&g,args[0],args[1],args[2],args[3],args[4]);
        if(got!=want || count!=hit_logged || memcmp(expected,hit_log,count*sizeof(*expected)) || memcmp(hit_arena,hit_after,sizeof(hit_arena))) {
            unsigned diff=0;while(diff<count&&diff<hit_logged&&!memcmp(expected+diff,hit_log+diff,sizeof(*expected)))++diff;
            fprintf(stderr,"FAIL: %s case %u original=%016llx core=%016llx calls=%u/%u first_call_diff=%u\n",d->name,n,(unsigned long long)got,(unsigned long long)want,count,hit_logged,diff);status=1;
        }
        uint64_t observed[6]={0};for(unsigned i=0;i<count;++i)++observed[expected[i].id];
        for(unsigned i=0;i<counters;++i) {
            unsigned id=building?i+2:i;
            if(c[i].count-previous[i]!=observed[id])status=fail("hit counter delta");
            totals[i]+=observed[id];
        }
    }
    if(!limit)coverage_enable(0);current_case_valid=0;
    if(!limit && !status)status=hit_domain_probes(index);
    for(unsigned i=0;i<4;++i)if(actor_patch(hit_stubs[i].rva,NULL,saved[i],FALSE))return 1;
    if(status)return 1;
    if(!limit && hit_modes(index))return 1;
    printf("PASS: %-20s %u cases; full EDX:EAX, arena, ordered full-slot calls/results and counters; V58=%llu V74=%llu\n",d->name,cases,(unsigned long long)totals[counters-2],(unsigned long long)totals[counters-1]);fflush(stdout);return 0;
}
static int damage_round1_cases(unsigned index,unsigned limit) {
    return index<ES2_HOOK_INDEX_vehicle_hit?damage_leaf_cases(index,limit):damage_hit_cases(index,limit);
}
