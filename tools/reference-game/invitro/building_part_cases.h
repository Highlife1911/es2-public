/* Own synthetic state/callbacks for building V74; no original fixtures. */
static BYTE part_arena[4096],part_before[4096],part_after[4096];
static uint32_t part_actor,part_state,part_definition,part_flags,part_source,part_player,part_vtable[32],part_alt_vtable[32];
static unsigned part_mult_calls,part_case;
static int32_t part_old_bucket;
static hit_record *part_note(unsigned id,const uint32_t *a,unsigned n) {
    hit_record *r=hit_note(id,a,n);
    r->args[4]=get32((BYTE *)(uintptr_t)part_state);
    r->args[5]=get32((BYTE *)(uintptr_t)part_state+7);
    r->args[6]=get16((BYTE *)(uintptr_t)part_flags);
    r->args[7]=get32((BYTE *)(uintptr_t)part_actor+0x96);return r;
}
static void part_callback_changes(void) {
    if(part_case%7==0)put32((BYTE *)(uintptr_t)part_actor+0x201,part_actor+0x3c0);
    if(part_case%11==0)put32((BYTE *)(uintptr_t)part_actor+0x205,part_actor+0x780);
}
static uint64_t __cdecl part_divide(uint32_t a,uint32_t b) {
    uint32_t args[2]={a,b};hit_record *r=part_note(0,args,2);part_callback_changes();return hit_result(r);
}
static uint64_t __cdecl part_multiply(uint32_t a,uint32_t b) {
    uint32_t args[2]={a,b};hit_record *r=part_note(1,args,2);uint64_t result=hit_result(r);
    int32_t value;
    if(!part_mult_calls++)value=part_old_bucket=(int32_t)(part_case%7)-3;
    else value=part_old_bucket+(int32_t)(part_case%9)-2;
    if(part_case%5==0)put16((BYTE *)(uintptr_t)part_state,(uint16_t)hit_take());
    r->result=(result&UINT64_C(0xffffffff00000000))|(uint32_t)value;return r->result;
}
static uint64_t __cdecl part_random(uint32_t a) {
    hit_record *r=part_note(2,&a,1);uint64_t result=hit_result(r);
    static const uint32_t choices[]={0,0x199,0x19a,0xfff,0x80000199,0xffffffff};
    r->result=(result&UINT64_C(0xffffffff00000000))|choices[(part_case+hit_logged)%6];return r->result;
}
static uint64_t __cdecl part_v40(uint32_t a) {
    hit_record *r=part_note(3,&a,1);uint64_t result=hit_result(r);
    if(part_case%3==0)put16((BYTE *)(uintptr_t)part_definition+4,(uint16_t)(part_case%5-2));
    put32((BYTE *)(uintptr_t)part_player,(uint32_t)(uintptr_t)((part_case/8)%2?part_vtable:part_alt_vtable));
    r->result=(result&~UINT64_C(0xffff))|((part_case/4)%2?0x100:0x101);return r->result;
}
static uint64_t part_v60_impl(uint32_t a,uint32_t b,uint32_t c,uint32_t variant) {
    uint32_t args[4]={a,b,c,variant};hit_record *r=part_note(4,args,4);part_callback_changes();return hit_result(r);
}
static uint64_t __cdecl part_v60(uint32_t a,uint32_t b,uint32_t c){return part_v60_impl(a,b,c,0);}
static uint64_t __cdecl part_v60_alt(uint32_t a,uint32_t b,uint32_t c){return part_v60_impl(a,b,c,1);}
static uint64_t __cdecl part_notify(uint32_t a){hit_record *r=part_note(5,&a,1);return hit_result(r);}
static uint64_t __cdecl part_deactivate(uint32_t a) {
    hit_record *r=part_note(6,&a,1);put32((BYTE *)(uintptr_t)a+0x1b6,part_case%3?part_actor+0x900:0);return hit_result(r);
}
static uint64_t __cdecl part_unlink(uint32_t a){hit_record *r=part_note(7,&a,1);put32((BYTE *)(uintptr_t)a,hit_take());return hit_result(r);}
static const struct {uint32_t rva;void *stub;} part_stubs[]={
    {0x7e068,(void *)part_divide},{0x7e08d,(void *)part_multiply},{0x92ee0,(void *)part_random},
    {0x20a68,(void *)part_notify},{0x11bc8,(void *)part_deactivate},{0x23430,(void *)part_unlink}};
static void part_reset(void){hit_reset();part_mult_calls=0;}
static void part_build(unsigned n,uint32_t args[6],uint32_t regs[3]) {
    part_case=n;random_bytes(part_arena,sizeof(part_arena));
    for(unsigned i=0;i<256;++i)hit_script[i]=random32();
    part_actor=(uint32_t)(uintptr_t)part_arena;part_player=part_actor+0xa00;part_source=part_actor+0xc00;
    static const int indices[]={-2,-1,0,1,7};int index=indices[n%5],effective=index==-1?0:index;
    part_state=part_actor+0x500+11*effective;part_definition=part_actor+0x680+30*effective;part_flags=part_actor+0x380+2*effective;
    part_vtable[0x40/4]=(uint32_t)(uintptr_t)part_v40;
    part_vtable[0x60/4]=(uint32_t)(uintptr_t)part_v60;
    part_alt_vtable[0x60/4]=(uint32_t)(uintptr_t)part_v60_alt;
    put32(part_arena,(uint32_t)(uintptr_t)part_vtable);
    put32((BYTE *)(uintptr_t)part_player,(uint32_t)(uintptr_t)part_vtable);
    put32((BYTE *)(uintptr_t)part_source,(uint32_t)(uintptr_t)part_alt_vtable);
    put32((BYTE *)(uintptr_t)part_player+0x1a4,n%3?part_actor:part_source);
    put32(part_arena+0x1f2,part_actor+0x280);put16(part_arena+0x29e,(uint16_t)(n%17==0));
    put32(part_arena+0x294,part_actor+0x680);put32(part_arena+0x201,part_actor+0x380);put32(part_arena+0x205,part_actor+0x500);
    put16((BYTE *)(uintptr_t)part_flags,(uint16_t)(n%13!=0));
    put16((BYTE *)(uintptr_t)part_definition+4,(uint16_t)(n%5-2));
    uint16_t threshold=(uint16_t)random32(),old=(uint16_t)random32(),damage=(uint16_t)random32();
    if(n<1024){static const uint16_t edge[]={0,1,0xffff,0x8000,0x7fff,49,50,51};threshold=edge[n%8];old=edge[(n/8)%8];damage=edge[(n/64)%8];}
    if(n%6==0){threshold=100;old=60;damage=2;}
    put16((BYTE *)(uintptr_t)part_definition,threshold);put16((BYTE *)(uintptr_t)part_state,old);
    args[0]=part_actor;args[1]=(random32()&0xffff0000u)|(uint16_t)index;
    args[2]=(random32()&0xffff0000u)|damage;args[3]=n%4==0?0:n%4==1?part_player:part_source;args[4]=args[5]=0;
    for(unsigned i=0;i<3;++i)regs[i]=random32();put32(mapped+0xd24e6,part_player);
}
/* Real 423430: the already-active guard preserves entry EDX:EAX; its
 * inactive/all-disabled path writes AX only. An ordinary C call loses bits. */
static int part_unlink_probe(unsigned index) {
    for(unsigned active=0;active<2;++active) {
        uint32_t a[6],r[3];part_build(1028,a,r);
        put16((BYTE *)(uintptr_t)part_definition,100);put16((BYTE *)(uintptr_t)part_definition+4,0xffff);
        put16((BYTE *)(uintptr_t)part_state,0);put16((BYTE *)(uintptr_t)part_flags,1);a[2]=100;a[3]=0;
        BYTE *linked=part_arena+0x900;put16(linked+0xa,(uint16_t)active);
        for(unsigned i=0;i<10;++i)put16(linked+0xc+2*i,0xffff);
        memcpy(part_before,part_arena,sizeof(part_arena));part_reset();
        uint64_t original=actor_call(index,a,r);memcpy(part_after,part_arena,sizeof(part_arena));
        memcpy(part_arena,part_before,sizeof(part_arena));part_reset();
        es2_building_part_context g=building_part_context(mapped);
        uint64_t replacement=es2_building_part_registers(&harness_memory,&g,a[0],a[1],a[2],a[3],r[0]);
        if(original!=replacement || memcmp(part_arena,part_after,sizeof(part_arena))) {
            fprintf(stderr,"FAIL: building_part case %u real unlink scratch original=%016llx core=%016llx\n",1001024+active,(unsigned long long)original,(unsigned long long)replacement);return 1;
        }
    }
    puts("PASS: building_part real 423430 active guard and inactive empty records preserve caller scratch registers");return 0;
}
static int building_part_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index);unsigned cases=case_budget(limit,1001024);
    const char *override=getenv("ES2_INVITRO_DAMAGE_CASES");if(!limit&&override&&*override)cases=(unsigned)strtoul(override,NULL,10);
    BYTE saved[6][5];uint32_t saved_player=get32(mapped+0xd24e6);int status=0;
    for(unsigned i=0;i<6;++i)if(actor_patch(part_stubs[i].rva,part_stubs[i].stub,saved[i],TRUE))return 1;
    if(!limit)coverage_enable(1);hit_record expected[128];uint64_t totals[8]={0};
    for(unsigned n=0;n<cases && !status;++n) {
        current_case=n;current_case_valid=1;if(!limit&&n==4096)coverage_enable(0);
        uint32_t a[6],r[3];part_build(n,a,r);memcpy(part_before,part_arena,sizeof(part_arena));part_reset();
        uint64_t original=actor_call(index,a,r);unsigned calls=hit_logged;
        memcpy(expected,hit_log,sizeof(expected));memcpy(part_after,part_arena,sizeof(part_arena));
        memcpy(part_arena,part_before,sizeof(part_arena));part_reset();
        es2_building_part_context g=building_part_context(mapped);
        uint64_t replacement=es2_building_part_registers(&harness_memory,&g,a[0],a[1],a[2],a[3],r[0]);
        if(original!=replacement || calls!=hit_logged || memcmp(expected,hit_log,calls*sizeof(*expected)) || memcmp(part_arena,part_after,sizeof(part_arena))) {
            fprintf(stderr,"FAIL: %s case %u original=%016llx core=%016llx calls=%u/%u\n",d->name,n,(unsigned long long)original,(unsigned long long)replacement,calls,hit_logged);status=1;
        }
        for(unsigned j=0;j<calls;++j)++totals[expected[j].id];
    }
    if(!limit)coverage_enable(0);current_case_valid=0;
    if(actor_patch(part_stubs[5].rva,NULL,saved[5],FALSE))return 1;
    if(!limit && !status)status=part_unlink_probe(index);
    put32(mapped+0xd24e6,saved_player);
    for(unsigned i=0;i<5;++i)if(actor_patch(part_stubs[i].rva,NULL,saved[i],FALSE))return 1;
    if(status)return 1;
    printf("PASS: %-20s %u cases; full EDX:EAX, ordered lifecycle arguments/results/state, arena and graph rereads; RNG=%llu V40=%llu V60=%llu\n",d->name,cases,(unsigned long long)totals[2],(unsigned long long)totals[3],(unsigned long long)totals[4]);return 0;
}
