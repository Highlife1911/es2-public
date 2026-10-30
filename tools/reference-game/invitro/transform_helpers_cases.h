/* transform_helpers group: rotate_words 47ff8c and matrix_point 4801d4
 * (player_movement.md, "enter-prologue helpers 47ff8c and 4801d4"). Direct
 * cases run the original body with chosen entry EAX/EDX/ECX and the core on
 * generated states: edge and random words, every mode byte class (< 1 including
 * 80h..FFh, = 1, > 1), random translation dwords, and out overlapping v or the
 * matrix (also the mode byte and the translation) at any byte offset. EAX, EDX,
 * ECX (the adapter's exit value) and the arena are compared. Then installed
 * 31/a7 poison probes through the exit shims, and real-caller passes from a
 * 31/a7 stack: matrix_point under the wrapper 48db93 (which returns 4801d4's
 * EAX/EDX/ECX unchanged), rotate_words under slope_speed 41693c (the hook not
 * installed, so its original body runs) over the real 47ebb8 and 47dbca with a
 * stub for the terrain face lookup 46e498; its early return hands 47ff8c's
 * EAX/EDX/ECX back. */
enum { TF_SIZE=0x200, TF_V=0x40, TF_M=0x80, TF_OUT=0x100, TF_ACTOR=0x140, TF_FACE=0x1c0, TF_SPEED=0x1e0 };
#define TF_BASE ((uint32_t)(uintptr_t)tf_arena)
#define TF_ADDR(o) (TF_BASE+(uint32_t)(o))
static BYTE tf_arena[TF_SIZE],tf_before[TF_SIZE],tf_after[TF_SIZE];
static unsigned tf_paths[3];
/* Calls a target with three slots and chosen EAX/EDX/ECX; returns all three. */
__attribute__((naked)) static void tf_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\t"
        "call *8(%ebp)\n\tmovl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* The same from a stack filled with a pattern; EAX/EDX/ECX = pattern,
 * pattern + 1, pattern + 2 (distinct, so passed-through entry registers
 * are told apart). */
__attribute__((naked)) static void tf_poison_call(void *target,const uint32_t *a,uint32_t pattern,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 16(%ebp),%eax\n\tmovl $1024,%ecx\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%eax\n\tleal 1(%eax),%edx\n\tleal 2(%eax),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint16_t tf_word(void){
    static const uint16_t edges[]={0,1,0xffff,0x4000,0xc000,0x7fff,0x8000,0x8001,0x3fff,0x2000,0xe000,0x2d41,0xd2bf};
    return random32()%2?edges[random32()%13]:(uint16_t)random32();
}
static uint32_t tf_dword(void){
    static const uint32_t edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0x10000,0xffff0000u};
    switch(random32()%3){
    case 0:return edges[random32()%7];
    case 1:return random32()%0x200001u-0x100000u;
    default:return random32();
    }
}
static uint8_t tf_mode(void){
    static const uint8_t modes[]={0,1,2,3,0x7f,0x80,0xff,0xfe,0x81};
    return random32()%4?modes[random32()%9]:(uint8_t)random32();
}
/* One state for hook `index`: a = the three slots, r = entry EAX/EDX/ECX. */
static void tf_build(unsigned index,uint32_t a[6],uint32_t r[3]){
    random_bytes(tf_arena,TF_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    uint32_t v=TF_ADDR(TF_V)+random32()%4u,M=TF_ADDR(TF_M)+random32()%4u,out=TF_ADDR(TF_OUT)+random32()%4u;
    const unsigned overlap=random32()%16;
    if(index==ES2_HOOK_INDEX_rotate_words){
        if(overlap==9)M=v-2u+random32()%7u;   /* the matrix over the vector */
        for(unsigned j=0;j<2;++j)put16((BYTE *)(uintptr_t)(v+2*j),tf_word());
        for(unsigned j=0;j<4;++j)put16((BYTE *)(uintptr_t)(M+2*j),tf_word());
        switch(overlap){
        case 0:out=v;break;
        case 1:out=v+2u;break;
        case 2:out=v-2u;break;
        case 3:out=v-3u+random32()%7u;break;
        case 4:out=M;break;
        case 5:out=M-3u+random32()%11u;break;
        default:break;
        }
    } else {
        if(overlap==9)M=v-0x20u+random32()%0x26u;   /* the matrix over the vector */
        for(unsigned j=0;j<3;++j)put16((BYTE *)(uintptr_t)(v+2*j),tf_word());
        for(unsigned j=0;j<9;++j)put16((BYTE *)(uintptr_t)(M+2*j),tf_word());
        ((BYTE *)(uintptr_t)M)[0x12]=tf_mode();
        for(unsigned j=0;j<3;++j)put32((BYTE *)(uintptr_t)(M+0x14+4*j),tf_dword());
        switch(overlap){
        case 0:out=v;break;
        case 1:out=v+2u;break;
        case 2:out=v-4u;break;
        case 3:out=v-0xbu+random32()%0x11u;break;
        case 4:out=M;break;
        case 5:out=M-0xbu+random32()%0x2bu;break;     /* also over the mode byte and the translation */
        case 6:out=M+0x12u-random32()%12u;break;
        default:break;
        }
    }
    a[0]=v;a[1]=M;a[2]=out;
}
static int tf_report(const char *what,unsigned n,const uint32_t want[3],const uint32_t got[3]){
    fprintf(stderr,"FAIL: %s case %u (%s) eax %08x/%08x edx %08x/%08x ecx %08x/%08x\n",current_function,n,what,
        want[0],got[0],want[1],got[1],want[2],got[2]);
    for(unsigned j=0;j<TF_SIZE;++j)if(tf_after[j]!=tf_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,tf_after[j],tf_arena[j]);break;}
    return 1;
}
static uint32_t tf_exit_ecx(unsigned index){
    return index==ES2_HOOK_INDEX_rotate_words?transform_helpers_rotate_ecx:transform_helpers_point_ecx;
}
static int tf_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    memcpy(tf_before,tf_arena,TF_SIZE);
    uint32_t want[3],got[3];tf_machine(mapped+d->rva,a,r,want);memcpy(tf_after,tf_arena,TF_SIZE);
    memcpy(tf_arena,tf_before,TF_SIZE);
    uint64_t pair=x->replacement_context(a,d->user,r);got[0]=(uint32_t)pair;got[1]=(uint32_t)(pair>>32);got[2]=tf_exit_ecx(index);
    if(memcmp(want,got,sizeof(want))||memcmp(tf_after,tf_arena,TF_SIZE))return tf_report("direct",n,want,got);
    if(index==ES2_HOOK_INDEX_matrix_point){int8_t mode=(int8_t)tf_before[a[1]-TF_BASE+0x12];++tf_paths[mode<1?0:mode==1?1:2];}
    return 0;
}
/* Installed probes: the original from a 31/a7 stack against the installed
 * hook (through its exit shim); EAX, EDX, ECX, the arena, x87. */
static int tf_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=poison?0xa7a7a7a7u:0x31313131u;tf_build(index,a,r);
        BYTE expected_fpu[108],actual_fpu[108];
        memcpy(tf_before,tf_arena,TF_SIZE);
        tf_poison_call(mapped+d->rva,a,pattern,want);es2_hook_fpu_save(expected_fpu);memcpy(tf_after,tf_arena,TF_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("transform_helpers poison install");
        memcpy(tf_arena,tf_before,TF_SIZE);
        tf_poison_call(mapped+d->rva,a,pattern,got);es2_hook_fpu_save(actual_fpu);
        if(memcmp(want,got,sizeof(want))||memcmp(tf_after,tf_arena,TF_SIZE)||
           memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=tf_report(fast?"installed fast":"installed preserving",n,want,got);
        if(!es2_hook_uninstall(&h))return fail("transform_helpers poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EAX/EDX/ECX (exit shim), arena\n",d->name,total);
    return status;
}
/* 46e498 stub for the slope_speed pass: returns 0 (the early return with
 * 47ff8c's registers) or a face record in the arena. */
static uint32_t tf_face;
__attribute__((used,noinline)) static uint32_t __cdecl tf_face_stub(uint32_t map,uint32_t position){(void)map;(void)position;return tf_face;}
/* slope_speed's state: actor A (A+0Ch angles, A+12h matrix, A+32h the
 * matrix-valid word), the speed word and the face; slots (A, speed). */
static void tf_slope_build(uint32_t a[6],unsigned n){
    random_bytes(tf_arena,TF_SIZE);memset(a,0,24);
    BYTE *A=tf_arena+TF_ACTOR;
    for(unsigned j=0;j<12;++j)put16(A+0x12+2*j,tf_word());
    for(unsigned j=0;j<3;++j)put16(A+0xc+2*j,(uint16_t)random32());
    put16(A+0x32,(uint16_t)(n%4==0?0:1u+random32()%0xffffu));
    put16(tf_arena+TF_SPEED,n%8==0?0:tf_word());
    for(unsigned j=0;j<2;++j)put16(tf_arena+TF_FACE+2*j,tf_word());
    tf_face=n%3==0?0:TF_ADDR(TF_FACE);
    a[0]=TF_ADDR(TF_ACTOR);a[1]=TF_ADDR(TF_SPEED);
}
/* matrix_point under 48db93 (slots v, M, out passed on) and rotate_words
 * under slope_speed 41693c; the hook original or replaced. */
static int tf_real(unsigned index){
    const int point=index==ES2_HOOK_INDEX_matrix_point;const uint32_t caller=point?0x8db93u:0x1693cu;
    BYTE saved_stub[5];if(!point&&actor_patch(0x6e498,(void *)tf_face_stub,saved_stub,TRUE))return 1;
    int status=0;unsigned total=0,early=0;uint64_t entered=0;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<2048&&!status;++n){
        uint32_t a[6],r[3],want[3],got[3],pattern=n&1?0xa7a7a7a7u:0x31313131u;
        if(point)tf_build(index,a,r);else tf_slope_build(a,n);
        memcpy(tf_before,tf_arena,TF_SIZE);
        tf_poison_call(mapped+caller,a,pattern,want);memcpy(tf_after,tf_arena,TF_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,function_hook_descriptor(index),&o,function_hook_extension(index)))return fail("transform_helpers real install");
        memcpy(tf_arena,tf_before,TF_SIZE);
        tf_poison_call(mapped+caller,a,pattern,got);
        if(memcmp(want,got,sizeof(want))||memcmp(tf_after,tf_arena,TF_SIZE)){fprintf(stderr,"real mode=%u\n",mode);status=tf_report("real",n,want,got);}
        if(mode)entered+=h.calls;
        if(!point&&!tf_face)++early;
        if(!es2_hook_uninstall(&h))return fail("transform_helpers real uninstall");
        ++total;
    }
    if(!point&&actor_patch(0x6e498,NULL,saved_stub,FALSE))return 1;
    if(!status&&!entered){fprintf(stderr,"FAIL: %s real pass entered 0\n",current_function);status=1;}
    if(!status&&!point&&(!early||early==total)){fprintf(stderr,"FAIL: %s real pass early returns %u of %u\n",current_function,early,total);status=1;}
    if(!status)printf("PASS: %s %u real-caller cases under %06x (%llu replaced entries%s%u), 31/a7 stack, EAX/EDX/ECX/arena\n",
        function_hook_descriptor(index)->name,total,0x400000u+caller,(unsigned long long)entered,
        point?", wrapper returns 4801d4's registers: ":", early returns with 47ff8c's registers: ",point?total:early);
    return status;
}
static int transform_helpers_cases(unsigned index,unsigned limit){
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_TRANSFORM_HELPERS_CASES");
    if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);memset(tf_paths,0,sizeof(tf_paths));
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];tf_build(index,a,r);status=tf_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=tf_installed_probes(index,64);
    if(!limit&&!status)status=tf_real(index);
    if(!status&&index==ES2_HOOK_INDEX_matrix_point)printf("PASS: %-20s %u cases (mode paths <1/=1/>1: %u/%u/%u); EAX/EDX/ECX, arena\n",
        function_hook_descriptor(index)->name,cases,tf_paths[0],tf_paths[1],tf_paths[2]);
    else if(!status)printf("PASS: %-20s %u cases; EAX/EDX/ECX, arena\n",function_hook_descriptor(index)->name,cases);
    return status;
}
