/* rotation_helpers group: rotation_compose 47f058 and rotation_place 47f4f4
 * (flyer.md, "the rotation helpers `0x47f058` and `0x47f4f4`"). Direct cases
 * run the original body with chosen entry EAX/EDX/ECX and the core on generated
 * states: edge and random words, every mode byte class (< 1 including 80h..FFh,
 * = 1, > 1) for both operands, random byte 19 and translation dwords, and out,
 * a and b overlapping each other at any byte offset (also over the mode bytes
 * and the translation). EAX, EDX, ECX (the adapter's exit value) and the arena
 * are compared. Then installed 31/a7 poison probes through the exit shims,
 * real-caller passes from a 31/a7 stack with the hook original, replaced or
 * compare and the caller's body original or its hook replaced: rotation_compose
 * under debris_piece 408588 (the debris_burst stubs for every callee except
 * 47f058 and the real matrix_angles 47f9a0, which receives 47f058's ECX; the
 * stub log holds the entry registers of every later call), rotation_place under
 * weapon_prepare 40e788 (synthetic weapon lookup V20 and mount offset 40f540,
 * every other callee real; the pair, 4a9834..4a9865 with the pose byte 19 and
 * the arena compared), and the installed compare cases of compare_probes.h. */
enum { RH_SIZE=0x200, RH_A=0x40, RH_B=0x80, RH_OUT=0x100 };
#define RH_BASE ((uint32_t)(uintptr_t)rh_arena)
#define RH_ADDR(o) (RH_BASE+(uint32_t)(o))
static BYTE rh_arena[RH_SIZE],rh_before[RH_SIZE],rh_after[RH_SIZE];
static unsigned rh_paths[2][6];
/* Calls a target with three slots and chosen EAX/EDX/ECX; returns all three. */
__attribute__((naked)) static void rh_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\t"
        "call *8(%ebp)\n\tmovl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The same from 3840 bytes of patterned stack; EAX/EDX/ECX = pattern,
 * pattern + 1, pattern + 2. */
__attribute__((naked)) static void rh_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tleal 1(%eax),%edx\n\tleal 2(%eax),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint16_t rh_word(void){
    static const uint16_t edges[]={0,1,0xffff,0x4000,0xc000,0x7fff,0x8000,0x8001,0x3fff,0x2000,0xe000,0x1fff,0x2d41,0xd2bf};
    return random32()%2?edges[random32()%14]:(uint16_t)random32();
}
static const uint8_t rh_modes[]={0,1,2,3,0x7f,0x80,0xff,0xfe,0x81};
static uint8_t rh_mode(void){return random32()%4?rh_modes[random32()%9]:(uint8_t)random32();}
/* Nine words, the mode byte and the random rest (byte 19, translation). */
static void rh_matrix(uint32_t m,uint8_t mode){
    for(unsigned j=0;j<9;++j)put16((BYTE *)(uintptr_t)(m+2*j),rh_word());
    ((BYTE *)(uintptr_t)m)[18]=mode;
}
/* path index: 0 a < 1, 1 b < 1, 2 both planar, 3 b planar, 4 a planar, 5 general. */
static unsigned rh_path(int8_t ma,int8_t mb){return ma<1?0:mb<1?1:ma==1&&mb==1?2:mb==1?3:ma==1?4:5;}
/* One state: a = the three slots, r = entry EAX/EDX/ECX; apart keeps a, b
 * and out disjoint (the callers' layout, required by compare mode). */
static void rh_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int apart){
    random_bytes(rh_arena,RH_SIZE);memset(a,0,24);(void)index;
    for(unsigned i=0;i<3;++i)r[i]=random32();
    uint32_t A=RH_ADDR(RH_A)+random32()%4u,B=RH_ADDR(RH_B)+random32()%4u,O=RH_ADDR(RH_OUT)+random32()%4u;
    const unsigned overlap=apart?99u:random32()%16u;
    switch(overlap){
    case 4:B=A;break;                                   /* the same matrix twice */
    case 5:B=A-0x12u+random32()%0x25u;break;           /* b over a */
    case 6:B=A;break;
    default:break;
    }
    const uint8_t ma=n<81?rh_modes[n%9]:rh_mode(),mb=n<81?rh_modes[n/9]:rh_mode();
    rh_matrix(A,ma);rh_matrix(B,mb);
    switch(overlap){
    case 0:O=A;break;
    case 1:O=B;break;
    case 2:O=A-0x13u+random32()%0x27u;break;           /* over a's words and mode byte */
    case 3:O=B-0x1fu+random32()%0x3fu;break;           /* over b, also its translation */
    case 6:O=A;break;
    case 7:O=B-2u-random32()%12u;break;                /* 47f4f4's translation copy over b's mode byte */
    case 8:O=A-2u-random32()%12u;break;                /* ... and over a's */
    default:break;
    }
    a[0]=A;a[1]=B;a[2]=O;
}
static int rh_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3]){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2]);
    for(unsigned j=0;j<RH_SIZE;++j)if(rh_after[j]!=rh_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,rh_after[j],rh_arena[j]);break;}
    return 1;
}
static uint32_t rh_exit_ecx(unsigned index){
    return index==ES2_HOOK_INDEX_rotation_compose?rotation_helpers_compose_ecx:rotation_helpers_place_ecx;
}
static int rh_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    memcpy(rh_before,rh_arena,RH_SIZE);
    uint32_t want[3],got[3];rh_machine(mapped+d->rva,a,r,want);memcpy(rh_after,rh_arena,RH_SIZE);
    memcpy(rh_arena,rh_before,RH_SIZE);
    uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=rh_exit_ecx(index);
    if(memcmp(want,got,sizeof(want))||memcmp(rh_after,rh_arena,RH_SIZE))return rh_report("direct",n,want,got);
    ++rh_paths[index==ES2_HOOK_INDEX_rotation_place][rh_path((int8_t)rh_before[a[0]-RH_BASE+18],(int8_t)rh_before[a[1]-RH_BASE+18])];
    return 0;
}
/* Installed probes: the original from a 31/a7 stack against the installed
 * hook (through its exit shim); EAX, EDX, ECX, the arena, x87. */
static int rh_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u;rh_build(index,n*5u+fast+2u*poison,a,r,0);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(rh_before,rh_arena,RH_SIZE);
        rh_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);memcpy(rh_after,rh_arena,RH_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("rotation_helpers poison install");
        memcpy(rh_arena,rh_before,RH_SIZE);
        rh_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(memcmp(want,got,sizeof(want))||memcmp(rh_after,rh_arena,RH_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=rh_report(fast?"installed fast":"installed preserving",n,want,got);
        if(!es2_hook_uninstall(&h))return fail("rotation_helpers poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX (exit shim), arena\n",d->name,total);
    return status;
}
/* Installs the hook under test in mode (0 original, 1 replaced, 2 compare)
 * and, with caller set, the caller's hook replaced. */
static int rh_install(es2_hook hk[2],unsigned index,unsigned caller,unsigned mode,int with_caller){
    memset(hk,0,2*sizeof(*hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
    o.mode=mode==0?ES2_HOOK_ORIGINAL:mode==1?ES2_HOOK_REPLACED:ES2_HOOK_COMPARE;
    if(!weapons_install_hook(hk,function_hook_descriptor(index),&o,function_hook_extension(index)))return fail("rotation_helpers real install");
    o.mode=ES2_HOOK_REPLACED;
    if(with_caller&&!weapons_install_hook(hk+1,function_hook_descriptor(caller),&o,function_hook_extension(caller)))
        return fail("rotation_helpers caller install");
    return 0;
}
static int rh_uninstall(es2_hook hk[2]){
    for(unsigned j=0;j<2;++j)if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("rotation_helpers real uninstall");
    return 0;
}
/* rotation_compose under debris_piece 408588: the debris_burst stubs for
 * 49630c, pool_alloc, debris_init, euler_matrix (which fills the rotation
 * a with random bytes, so every mode pair occurs), debris_spin and
 * debris_spin_any; 47f058 and matrix_angles run as code. */
static int rh_debris_real(void){
    const unsigned index=ES2_HOOK_INDEX_rotation_compose,caller=ES2_HOOK_INDEX_debris_piece;
    const unsigned which=db_which(caller)&~(1u<<8|1u<<9);
    BYTE saved[DB_IDS][5],old_globals[DB_GLOBALS];int active[DB_IDS];db_globals(old_globals,0);
    if(db_patch(saved,active,which,1))return 1;
    int status=0;unsigned total=0;uint64_t entered=0;static db_record expected[DB_MAX];
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],want[2],got[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,head,head_after;db_build(caller,n,a,r,0);
        BYTE before[DB_GLOBALS],after[DB_GLOBALS],actual[DB_GLOBALS];
        memcpy(db_before,db_arena,DB_SIZE);db_globals(before,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head));
        db_reset();sg_poison_call(mapped+0x8588,a,pattern,want);
        const unsigned calls=db_calls;memcpy(expected,db_log,calls*sizeof(*expected));
        memcpy(db_after,db_arena,DB_SIZE);db_globals(after,0);
        es2_hook hk[2];if(rh_install(hk,index,caller,mode%3,mode>=3))return 1;
        memcpy(db_arena,db_before,DB_SIZE);db_globals(before,1);db_reset();
        sg_poison_call(mapped+0x8588,a,pattern,got);db_globals(actual,0);
        __asm__ volatile("movl %%fs:0,%0":"=r"(head_after));
        if(head_after!=head||memcmp(want,got,sizeof(want))||calls!=db_calls||memcmp(expected,db_log,calls*sizeof(*expected))||
           memcmp(db_after,db_arena,DB_SIZE)||db_globals_differ(after,actual)||hk[0].mismatches||hk[0].skipped){
            fprintf(stderr,"FAIL: rotation_compose real case %u mode %u pair %08x:%08x/%08x:%08x calls %u/%u mismatches %llu skipped %llu\n",
                n,mode,want[1],want[0],got[1],got[0],calls,db_calls,(unsigned long long)hk[0].mismatches,(unsigned long long)hk[0].skipped);
            for(unsigned j=0;j<calls&&j<db_calls;++j)if(memcmp(expected+j,db_log+j,sizeof(*expected))){
                const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(db_log+j);
                for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
                break;
            }
            status=1;
        }
        if(mode%3)entered+=hk[0].calls;
        if(rh_uninstall(hk))return 1;
        db_globals(before,1);++total;
    }
    if(db_patch(saved,active,which,0))return 1;
    db_globals(old_globals,1);
    if(!status&&!entered){fprintf(stderr,"FAIL: rotation_compose real pass entered 0\n");status=1;}
    if(!status)printf("PASS: rotation_compose %u real-caller cases under 408588 (body original or debris_piece replaced; %llu replaced/compare entries), real matrix_angles, 31/a7 stack, pair, ordered stub calls with entry registers, arena, globals, FS:0\n",
        total,(unsigned long long)entered);
    return status;
}
/* rotation_place under weapon_prepare 40e788: weapon W, owner O, its mount
 * table M with the lookup receiver (V20 at vtable +20h) and the record
 * table at [M+16h]; the record index word of the lookup result picks a
 * record, -1 or a null result the static record 6c56a8. Both W+5Bh and
 * W+5Fh nonzero (one case in eight) skip 47f4f4. In one case in four the
 * global 49ef34 is set and O is the player [4d24e6] = [4d2684] (the record
 * goes to a local copy with Z - 1500). */
static BYTE rh_prep[1024];
static uint32_t rh_prep_table[16],rh_prep_result;
static uint32_t __cdecl rh_prep_lookup(uint32_t receiver,uint32_t selector){(void)receiver;(void)selector;return rh_prep_result;}
static uint32_t __cdecl rh_prep_offset(uint32_t weapon){(void)weapon;return (uint32_t)(uintptr_t)(rh_prep+0x380);}
static const struct { uint32_t rva,size; } rh_prep_ranges[]={{0xa9834,50},{0x9ef34,4},{0xd24e6,4},{0xd2684,4}};
enum { RH_PREP_GLOBALS=62 };
static void rh_prep_globals(BYTE *out,int restore){
    for(unsigned i=0;i<4;++i){
        if(restore)memcpy(mapped+rh_prep_ranges[i].rva,out,rh_prep_ranges[i].size);else memcpy(out,mapped+rh_prep_ranges[i].rva,rh_prep_ranges[i].size);
        out+=rh_prep_ranges[i].size;
    }
}
static void rh_prep_build(unsigned n,uint32_t a[6]){
    const uint32_t b=(uint32_t)(uintptr_t)rh_prep,w=b+0x20,owner=b+0x100;
    random_bytes(rh_prep,sizeof(rh_prep));memset(a,0,24);
    put32(rh_prep+0x38,b+0x180);                       /* W+18h: the selector word */
    put32(rh_prep+0x3c,b+0x200);                       /* W+1Ch: C (point +40h, Q10 word +4Ch) */
    put16(rh_prep+0x44,n%4==0?0:rh_word());            /* W+24h, W+28h: local angles */
    put16(rh_prep+0x48,n%5==0?0:rh_word());
    const unsigned k=n%8;                              /* W+5Bh, W+5Fh */
    put32(rh_prep+0x7b,k==0||k==1||k==4?0:random32()|1u);put32(rh_prep+0x7f,k==0||k==2||k==5?0:random32()|1u);
    if(k==3||k==6)put32(rh_prep+0x7b,0);
    put32(rh_prep+0x134,b+0x280);                      /* O+34h: M */
    put32(rh_prep+0x284,b+0x2c0);                      /* M+4: the receiver */
    put32(rh_prep+0x2c0,(uint32_t)(uintptr_t)rh_prep_table);rh_prep_table[8]=(uint32_t)(uintptr_t)rh_prep_lookup;
    put32(rh_prep+0x296,b+0x340);                      /* M+16h: the records */
    rh_prep_result=n%16==0?0:b+0x300;
    put16(rh_prep+0x304,n%16==1?0xffff:(uint16_t)(random32()%2));
    rh_matrix(b+0x340,rh_mode());rh_matrix(b+0x360,rh_mode());
    put16(rh_prep+0x132,(uint16_t)(n%3?0:1u+random32()%0xffffu));   /* O+32h: owner matrix valid */
    rh_matrix(owner+0x12,rh_mode());
    const int player=n%4==3;
    put32(mapped+0x9ef34,player);put32(mapped+0xd24e6,player?owner:b);put32(mapped+0xd2684,owner);
    a[0]=w;a[1]=owner;a[2]=random32();
}
static int rh_prepare_real(void){
    const unsigned index=ES2_HOOK_INDEX_rotation_place,caller=ES2_HOOK_INDEX_weapon_prepare;
    BYTE saved[5],old_globals[RH_PREP_GLOBALS],state[sizeof(rh_prep)],expected_state[sizeof(rh_prep)];
    rh_prep_globals(old_globals,0);
    if(actor_patch(0xf540,(void *)rh_prep_offset,saved,TRUE))return 1;
    int status=0;unsigned total=0,paths[6]={0};uint64_t entered=0;
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<1024&&!status;++n){
        uint32_t a[6],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        BYTE before[RH_PREP_GLOBALS],after[RH_PREP_GLOBALS],actual[RH_PREP_GLOBALS];
        rh_prep_build(n,a);memcpy(state,rh_prep,sizeof(state));rh_prep_globals(before,0);
        rh_poison_call(mapped+0xe788,a,pattern,want);
        memcpy(expected_state,rh_prep,sizeof(rh_prep));rh_prep_globals(after,0);
        es2_hook hk[2];if(rh_install(hk,index,caller,mode%3,mode>=3))return 1;
        memcpy(rh_prep,state,sizeof(state));rh_prep_globals(before,1);
        rh_poison_call(mapped+0xe788,a,pattern,got);rh_prep_globals(actual,0);
        /* The caller's hook returns EDX:EAX exactly; ECX only with the body original. */
        if(mode>=3)got[2]=want[2];
        if(memcmp(want,got,sizeof(want))||memcmp(expected_state,rh_prep,sizeof(rh_prep))||memcmp(after,actual,sizeof(after))||
           hk[0].mismatches||hk[0].skipped){
            unsigned j=0;while(j<RH_PREP_GLOBALS&&after[j]==actual[j])++j;
            unsigned s=0;while(s<sizeof(rh_prep)&&expected_state[s]==rh_prep[s])++s;
            fprintf(stderr,"FAIL: rotation_place real case %u mode %u eax %08x/%08x edx %08x/%08x ecx %08x/%08x global +%u state +%x mismatches %llu skipped %llu\n",
                n,mode,want[0],got[0],want[1],got[1],want[2],got[2],j,s,(unsigned long long)hk[0].mismatches,(unsigned long long)hk[0].skipped);
            status=1;
        }
        if(mode%3)entered+=hk[0].calls;
        if(rh_uninstall(hk))return 1;
        rh_prep_globals(before,1);++total;
        if(!mode&&n%8!=7){
            /* The 47f4f4 path: the local Euler mode (first angle W+24h when
             * W+5Bh is 0, third W+28h when W+5Fh is 0) and the record's mode. */
            const unsigned k=n%8;const int first=k==0||k==1||k==3||k==4||k==6,third=k==0||k==2||k==5;
            const int8_t ma=first&&get16(state+0x44)?3:third&&get16(state+0x48)?1:0;
            const int16_t pick=(int16_t)get16(state+0x304);
            const int8_t mb=(int8_t)(!rh_prep_result||pick<=-1?mapped[0x2c56a8+18]:state[0x340+32*pick+18]);
            ++paths[rh_path(ma,mb)];
        }
    }
    if(actor_patch(0xf540,NULL,saved,FALSE))return 1;
    rh_prep_globals(old_globals,1);
    if(!status&&!entered){fprintf(stderr,"FAIL: rotation_place real pass entered 0\n");status=1;}
    for(unsigned i=0;i<6&&!status;++i)if(!paths[i]){fprintf(stderr,"FAIL: rotation_place real pass path %u not reached\n",i);status=1;}
    if(!status)printf("PASS: rotation_place %u real-caller cases under 40e788 (body original or weapon_prepare replaced; %llu replaced/compare entries; paths %u/%u/%u/%u/%u/%u), real Euler/compose/point/angles/Q10, 31/a7 stack, pair, 4a9834.., arena\n",
        total,(unsigned long long)entered,paths[0],paths[1],paths[2],paths[3],paths[4],paths[5]);
    return status;
}
/* Compare cases (compare_probes.h): both hooks accept compare; a, b and
 * out apart as at the three call sites. */
static void rh_compare_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){rh_build(index,n,a,r,1);}
static void rh_compare_state(BYTE *s,int restore){if(restore)memcpy(rh_arena,s,RH_SIZE);else memcpy(s,rh_arena,RH_SIZE);}
static int rotation_helpers_cases(unsigned index,unsigned limit){
    if(limit&&compare_probe_installed(index))return compare_probe_run(index,limit,rh_compare_build,rh_compare_state,RH_SIZE);
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_ROTATION_HELPERS_CASES");
    if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    const unsigned which=index==ES2_HOOK_INDEX_rotation_place;
    int status=0;if(!limit)coverage_enable(1);memset(rh_paths[which],0,sizeof(rh_paths[which]));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];rh_build(index,n,a,r,0);status=rh_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)for(unsigned i=0;i<6;++i)if(!rh_paths[which][i]){fprintf(stderr,"FAIL: %s mode path %u not reached\n",current_function,i);status=1;break;}
    if(!limit&&!status)status=rh_installed_probes(index,64);
    if(!limit&&!status)status=index==ES2_HOOK_INDEX_rotation_compose?rh_debris_real():rh_prepare_real();
    if(!limit&&!status)status=compare_probe_run(index,0,rh_compare_build,rh_compare_state,RH_SIZE);
    if(!status)printf("PASS: %-20s %u cases (mode paths a<1/b<1/planar/b planar/a planar/general: %u/%u/%u/%u/%u/%u); EAX/EDX/ECX, arena\n",
        function_hook_descriptor(index)->name,cases,rh_paths[which][0],rh_paths[which][1],rh_paths[which][2],rh_paths[which][3],
        rh_paths[which][4],rh_paths[which][5]);
    return status;
}
