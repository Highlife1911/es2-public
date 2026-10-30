/* terrain_face 46e498 and camera_update 4011a0 (docs/reverse-engineering/camera.md).
 * terrain_face: generated grids and points, read-only. camera_update: the
 * eleven game callees and the target's virtual +24 are logging stubs that
 * record their entry registers and input payloads by content; then
 * installed poison cases and a real-callee pass under mixed nested modes. */
typedef struct cm_record { uint32_t id,args[3],regs[3],input[28],eax,edx; } cm_record;
enum { CM_SIZE=0x1000, CM_MAP=0x40, CM_CELLS=0x200, CM_CAM=0x640, CM_TGT=0x6c0, CM_PLY=0x740, CM_VT=0x780,
    CM_MOUNT=0x7c0, CM_NORMAL=0x7f0, CM_POINT=0x800, CM_MAX=32, CM_IDS=12,
    CM_BLOCK=0xa7bfc, CM_BLOCK_SIZE=0x450, CM_GLOBALS=CM_BLOCK_SIZE+8 };
#define CM_BASE ((uint32_t)(uintptr_t)cm_arena)
static BYTE cm_arena[CM_SIZE],cm_before[CM_SIZE],cm_after[CM_SIZE];
static cm_record cm_log[CM_MAX];
static unsigned cm_calls;
static uint32_t cm_seed,cm_stream;
__attribute__((used)) static uint32_t cm_ecx;
/* 467924, 47ebb8, 48043c, 46e498, 47de4c, 46e180, 47e0b0, 480304, 4801d4,
 * 47fa20, 47f9a0; id 11 is the virtual +24 (never patched). */
static const uint32_t cm_addresses[CM_IDS]={0x467924,0x47ebb8,0x48043c,0x46e498,0x47de4c,0x46e180,0x47e0b0,0x480304,0x4801d4,0x47fa20,0x47f9a0,0};
static const unsigned cm_counts[CM_IDS]={1,2,3,2,2,2,2,3,3,3,2,1};
static uint32_t cm_next(void){
    uint32_t z=cm_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *cm_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
/* Fixed window (the worker's 16 MiB stack reservation), as in
 * terrain_leaf_cases.h: a window around the current frame would move
 * between the two runs. */
static int cm_stack_address(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Step words: mostly positive; zero and negative newest entries are
 * allowed because the generated ring keeps the backward walk finite. */
static uint32_t cm_step(uint32_t pick){
    switch(pick%16){case 0:return 0;case 1:return (uint16_t)(0u-(1u+(pick>>8)%200u));case 2:return 0x1f40;case 3:return 0x7fff;
    default:return 100u+(pick>>8)%4000u;}
}
/* Matrix payload: nine words, the mode byte, three translations (byte 19 is padding). */
static unsigned cm_matrix(uint32_t *out,uint32_t m,int translation){
    const BYTE *p=cm_host(m);unsigned n=0;
    for(unsigned j=0;j<9;++j)out[n++]=get16(p+2*j);
    out[n++]=p[18];
    if(translation)for(unsigned j=0;j<3;++j)out[n++]=get32(p+20+4*j);
    return n;
}
static void cm_fill(uint32_t a,unsigned n){for(unsigned j=0;j<n;++j)cm_host(a)[j]=(BYTE)cm_next();}
__attribute__((used,noinline)) static uint64_t __cdecl cm_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(cm_calls==CM_MAX){fail("camera synthetic call overflow");ExitProcess(1);}
    cm_record *v=cm_log+cm_calls++;memset(v,0,sizeof(*v));v->id=id;memcpy(v->args,a,4*cm_counts[id]);memcpy(v->regs,regs,12);
    /* Frame addresses differ by construction; their payloads are compared. */
    for(unsigned j=0;j<3;++j)if(cm_stack_address(v->regs[j]))v->regs[j]=0;
    for(unsigned j=0;j<cm_counts[id];++j)if(cm_stack_address(v->args[j]))v->args[j]=0;
    uint32_t eax=cm_next(),edx=cm_next(),pick=cm_next();cm_ecx=cm_next();
    switch(id){
    case 0:eax=(eax&0xffff0000u)|cm_step(pick);break;
    case 1:for(unsigned j=0;j<3;++j)v->input[j]=get16(cm_host(a[0])+2*j);cm_fill(a[1],19);break;
    case 2:for(unsigned j=0;j<3;++j)v->input[j]=get32(cm_host(a[0])+4*j);cm_matrix(v->input+3,a[1],1);cm_fill(a[2],12);break;
    case 3:
        v->input[0]=get32(cm_host(a[1]));v->input[1]=get32(cm_host(a[1])+4);
        if(pick%4){eax=CM_BASE+CM_NORMAL;cm_fill(eax,6);} else eax=0;
        break;
    case 5:for(unsigned j=0;j<3;++j)v->input[j]=get32(cm_host(a[1])+4*j);break;
    case 7:for(unsigned j=0;j<3;++j)v->input[j]=get32(cm_host(a[0])+4*j);cm_matrix(v->input+3,a[1],0);cm_fill(a[2],12);break;
    case 8:for(unsigned j=0;j<3;++j)v->input[j]=get16(cm_host(a[0])+2*j);cm_matrix(v->input+3,a[1],1);cm_fill(a[2],12);break;
    case 9:{unsigned n=cm_matrix(v->input,a[0],1);cm_matrix(v->input+n,a[1],1);cm_fill(a[2],19);cm_fill(a[2]+20,12);break;}
    case 10:cm_matrix(v->input,a[0],0);cm_fill(a[1],6);break;
    case 11:if(pick%3)eax=CM_BASE+CM_MOUNT;else eax=0;break;
    }
    v->eax=eax;v->edx=edx;return (uint64_t)edx<<32|eax;
}
#define CM_STUB(n) __attribute__((naked)) static void cm_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _cm_stub_impl\n\taddl $24,%esp\n\tmovl _cm_ecx,%ecx\n\tret\n\t");}
CM_STUB(0) CM_STUB(1) CM_STUB(2) CM_STUB(3) CM_STUB(4) CM_STUB(5) CM_STUB(6) CM_STUB(7) CM_STUB(8) CM_STUB(9) CM_STUB(10) CM_STUB(11)
#undef CM_STUB
static void *cm_stubs[CM_IDS]={cm_stub_0,cm_stub_1,cm_stub_2,cm_stub_3,cm_stub_4,cm_stub_5,cm_stub_6,cm_stub_7,cm_stub_8,cm_stub_9,cm_stub_10,cm_stub_11};
/* Real-callee pass: the virtual +24 returns the mount matrix or null. */
static uint32_t cm_mount_choice;
static uint32_t __cdecl cm_real_virtual(uint32_t target){(void)target;return cm_mount_choice;}
/* Rings, step table and ring index are one block 4a7bfc..4a804b; then the
 * followed-object pointer 4d24e6 and the terrain pointer 4a0be0. */
static void cm_globals(BYTE *out,int restore){
    static const unsigned offsets[]={CM_BLOCK,0xd24e6,0xa0be0};
    static const unsigned sizes[]={CM_BLOCK_SIZE,4,4};
    for(unsigned i=0;i<3;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void cm_reset(void){cm_calls=0;cm_stream=cm_seed;}
/* Grid of at most 64 cells; column/row exponents may carry multiples of 32
 * (shift counts use the low five bits) so the index limit stays valid. */
static void cm_grid(int real){
    BYTE *map=cm_arena+CM_MAP;
    uint32_t w=random32()%4,h=random32()%4,s=real?6+random32()%6:random32()%3?random32()%13:random32()%32;
    if(w+h>6)h=6-w;
    put32(map+0x100,!real&&random32()%5==0?w|(random32()&0xffffffe0u):w);
    put32(map+0x104,!real&&random32()%5==0?h|(random32()&0xffffffe0u):h);
    put32(map+0x108,!real&&random32()%7==0?s|(random32()&0xffffffe0u):s);
    put32(map+0xec,CM_BASE+CM_CELLS);put32(map+0x110,random32()%4096);put32(map+0x118,random32()%64);
    for(unsigned c=0;c<(1u<<(w+h));++c){BYTE *cell=cm_arena+CM_CELLS+16*c;random_bytes(cell,16);
        for(unsigned j=1;j<13;j+=2)if(real)put16(cell+j,(uint16_t)(random32()%0x8001u-0x4000u));
        cell[15]=(BYTE)((random32()&0xfcu)|random32()%4);}
}
static uint32_t cm_coordinate(uint32_t cells,uint32_t s){
    s&=31u;uint32_t cell=1u<<s,index=(uint32_t)(random32()%(cells+4u))-2u;
    switch(random32()%6){case 0:return index<<s;case 1:return (index<<s)+cell-1u;case 2:return random32();default:return (index<<s)+(s?random32()%cell:0);}
}
static void cm_point(uint32_t at){
    BYTE *map=cm_arena+CM_MAP;uint32_t s=get32(map+0x108);
    put32(cm_arena+at,cm_coordinate(1u<<(get32(map+0x100)&31u),s));put32(cm_arena+at+4,cm_coordinate(1u<<(get32(map+0x104)&31u),s));
    put32(cm_arena+at+8,random32()%4?random32()%8192:random32());
}
/* Angle words near the +-3000h pitch clamp, or anything. */
static uint16_t cm_pitch(void){
    switch(random32()%4){case 0:return (uint16_t)(0x2f00u+random32()%0x200u);case 1:return (uint16_t)(0xcf00u+random32()%0x200u);
    case 2:return (uint16_t)(random32()%0x80u-0x40u);default:return (uint16_t)random32();}
}
static uint16_t cm_delta(void){return random32()%3?(uint16_t)(random32()%0x200u-0x100u):(uint16_t)random32();}
/* Arguments and globals for one camera case; real != 0 builds sane matrices
 * for the real callees and keeps positions on the grid. */
static void cm_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3],int real){
    uint32_t b=CM_BASE;cm_seed=random32();
    memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    random_bytes(cm_arena+CM_MAP,0x120);cm_grid(real);
    if(index==ES2_HOOK_INDEX_terrain_face){cm_point(CM_POINT);a[0]=b+CM_MAP;a[1]=b+CM_POINT;return;}
    random_bytes(cm_arena+CM_CAM-0x40,CM_POINT-CM_CAM+0x40);
    BYTE *c=cm_arena+CM_CAM,*t=cm_arena+CM_TGT;
    static const uint16_t modes[]={0,1,2,3,0,1,2,3,0,1,2,3,4,0xffff,0x8000,7};
    put16(c+0x36,modes[n<64?n%16:random32()%16]);
    put16(c+0x10,cm_pitch());put16(c+0x26,cm_pitch());put16(c+0x2c,cm_delta());put16(c+0x2e,cm_delta());put16(c+0x30,cm_delta());
    switch(random32()%4){
    case 0:put16(c+0x22,(uint16_t)(2400+random32()%200));put16(c+0x24,(uint16_t)(random32()%200-100));break;
    case 1:put16(c+0x22,(uint16_t)(29900+random32()%200));put16(c+0x24,(uint16_t)(random32()%200-100));break;
    case 2:put16(c+0x22,(uint16_t)(2500+random32()%27500));put16(c+0x24,(uint16_t)(random32()%400-200));break;
    }
    uint32_t target=b+CM_TGT;
    /* Target overlapping the camera: T+32 is C+2c, the flag the camera clears. */
    if(!real&&get16(c+0x36)==1&&random32()%6==0)target=b+CM_CAM-6;
    put32(c+0x32,target);
    put32(t,b+CM_VT);put32(cm_arena+CM_VT+0x24,real?(uint32_t)(uintptr_t)cm_real_virtual:(uint32_t)(uintptr_t)cm_stub_11);
    if(target==b+CM_TGT)put16(t+0x32,random32()%2?0:(uint16_t)(random32()|1u));
    if(real){
        for(unsigned j=0;j<3;++j)put16(t+0xc+2*j,(uint16_t)random32());
        ((void (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x7ebb8))(b+CM_TGT+0xc,b+CM_TGT+0x12);
        BYTE angles[6];random_bytes(angles,6);
        ((void (__cdecl *)(uint32_t,uint32_t))(void *)(mapped+0x7ebb8))((uint32_t)(uintptr_t)angles,b+CM_MOUNT);
        BYTE *map=cm_arena+CM_MAP;uint32_t s=get32(map+0x108)&31u,w=1u<<get32(map+0x100),h=1u<<get32(map+0x104);
        for(unsigned j=0;j<2;++j){put32(t+0x26+4*j,(random32()%((j?h:w)<<s)));put32(c+4+4*j,random32()%((j?h:w)<<s));}
        for(unsigned j=0;j<3;++j){put32(cm_arena+CM_MOUNT+20+4*j,random32()%2048-1024);put16(c+0x38+2*j,(uint16_t)(random32()%2048-1024));
            put16(c+2*j+0x3e,(uint16_t)(random32()%2048-1024));}
        put32(c+0xc,random32()%20000);put32(t+0x2e,random32()%20000);
        cm_mount_choice=random32()%3?b+CM_MOUNT:0;
    }
    put32(mapped+0xd24e6,random32()%3?b+CM_PLY:target);put32(mapped+0xa0be0,b+CM_MAP);
    random_bytes(mapped+CM_BLOCK,CM_BLOCK_SIZE-4);
    uint32_t slot=n<50?n:random32()%8?random32()%50:random32()%2?0:49;
    put32(mapped+CM_BLOCK+CM_BLOCK_SIZE-4,slot);
    for(unsigned j=0;j<50;++j){uint32_t pick=random32();
        put32(mapped+0xa7f80+4*j,pick%10==0?4000u+(pick>>8)%0x7000u:pick%37==0?0x1f40u:50u+(pick>>8)%4000u);}
    a[0]=b+CM_CAM;
}
static int cm_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const cm_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,cm_calls);
    for(unsigned j=0;j<calls&&j<cm_calls&&expected;++j)if(memcmp(expected+j,cm_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(cm_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CM_SIZE;++j)if(cm_after[j]!=cm_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,cm_after[j],cm_arena[j]);break;}
    return 1;
}
static unsigned cm_expected_hits(const callee_counter *c){
    unsigned hits=0;
    for(unsigned j=0;j<cm_calls;++j){unsigned id=cm_log[j].id;
        if(c->event==ES2_COLLIDE_SLOT?id==11:id<11&&cm_addresses[id]==c->key)++hits;}
    return hits;
}
static int cm_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    static BYTE before[CM_GLOBALS],after[CM_GLOBALS],actual[CM_GLOBALS];static cm_record expected[CM_MAX];
    memcpy(cm_before,cm_arena,CM_SIZE);cm_globals(before,0);
    cm_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=cm_calls;memcpy(expected,cm_log,calls*sizeof(*expected));
    memcpy(cm_after,cm_arena,CM_SIZE);cm_globals(after,0);
    memcpy(cm_arena,cm_before,CM_SIZE);cm_globals(before,1);cm_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);cm_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=cm_expected_hits(c+i))return fail("camera callee counter delta");
    if(pair[0]!=pair[1]||calls!=cm_calls||memcmp(expected,cm_log,calls*sizeof(*expected))||memcmp(cm_after,cm_arena,CM_SIZE)||memcmp(after,actual,CM_GLOBALS)){
        for(unsigned j=0;j<CM_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
        return cm_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int cm_patch(BYTE saved[CM_IDS][5],int on){
    for(unsigned i=0;i<CM_IDS-1;++i)if(actor_patch(cm_addresses[i]-0x400000u,on?cm_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    return 0;
}
static int cm_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    static BYTE before[CM_GLOBALS],after[CM_GLOBALS],actual[CM_GLOBALS];static cm_record expected[CM_MAX];
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;cm_build(index,n,a,r,0);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(cm_before,cm_arena,CM_SIZE);cm_globals(before,0);
        cm_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=cm_calls;memcpy(expected,cm_log,calls*sizeof(*expected));memcpy(cm_after,cm_arena,CM_SIZE);cm_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("camera poison install");
        memcpy(cm_arena,cm_before,CM_SIZE);cm_globals(before,1);cm_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);cm_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=cm_calls||memcmp(expected,cm_log,calls*sizeof(*expected))||memcmp(cm_after,cm_arena,CM_SIZE)||
           memcmp(after,actual,CM_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cm_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("camera poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* Real callees on generated terrain and real target/mount matrices, nested
 * hooks in rotating modes; full pair and all memory are compared. */
static int cm_real(unsigned index){
    static const unsigned nested[]={ES2_HOOK_INDEX_movement_step,ES2_HOOK_INDEX_euler_matrix,ES2_HOOK_INDEX_transform_point,
        ES2_HOOK_INDEX_terrain_face,ES2_HOOK_INDEX_dist_octagonal,ES2_HOOK_INDEX_terrain_height,ES2_HOOK_INDEX_multiply_q10,
        ES2_HOOK_INDEX_rotate_point,ES2_HOOK_INDEX_matrix_compose,ES2_HOOK_INDEX_matrix_angles};
    enum { NESTED=sizeof(nested)/sizeof(nested[0]) };
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,modes[5]={0};
    static BYTE before[CM_GLOBALS],after[CM_GLOBALS],actual[CM_GLOBALS];
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;cm_build(index,n,a,r,1);
        memcpy(cm_before,cm_arena,CM_SIZE);cm_globals(before,0);
        es2_hook hk[NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<NESTED;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if(e&&e->compare_unsupported&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("camera nested install");}
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(cm_after,cm_arena,CM_SIZE);cm_globals(after,0);
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+NESTED,d,&o,function_hook_extension(index)))return fail("camera caller install");
        memcpy(cm_arena,cm_before,CM_SIZE);cm_globals(before,1);
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);cm_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(cm_after,cm_arena,CM_SIZE)||memcmp(after,actual,CM_GLOBALS)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            for(unsigned j=0;j<CM_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
            fprintf(stderr,"real mode=%u\n",mode);cm_calls=0;status=cm_report("real",n,pair,0,NULL);
        }
        uint16_t m=get16(cm_before+CM_CAM+0x36);++modes[m<4?m:4];
        for(unsigned j=0;j<=NESTED;++j){if(hk[j].mismatches)status=fail("camera nested compare mismatch");
            if(hk[j].descriptor&&!es2_hook_uninstall(hk+j))return fail("camera nested uninstall");}
        cm_globals(before,1);++total;
    }
    if(!status)printf("PASS: %s %u real-callee cases (modes 0-3/other %u/%u/%u/%u/%u), mixed nested modes, 31/a7 stack, pair and memory\n",
        d->name,total,modes[0],modes[1],modes[2],modes[3],modes[4]);
    return status;
}
static int camera_cases(unsigned index,unsigned limit){
    static BYTE old_globals[CM_GLOBALS];BYTE saved[CM_IDS][5];int camera=index==ES2_HOOK_INDEX_camera_update;
    cm_globals(old_globals,0);if(camera&&cm_patch(saved,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_CAMERA_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];cm_build(index,n,a,r,0);status=cm_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status){coverage_enable(1);status=cm_installed_probes(index,64);coverage_enable(0);}
    if(camera&&cm_patch(saved,0))return 1;
    current_case_valid=0;
    if(!limit&&!status&&camera)status=cm_real(index);
    cm_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, ordered calls with entry registers, frame payloads by content, arena/rings/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
