/* camera_view group: camera_attached 401078, camera_orbit_set 40109c,
 * camera_attach_set 40111c, camera_turn_step 401c20, camera_speed_step 401c4c,
 * outside_camera 401c74, view_apply 45de30, view_subject_set 45df34,
 * view_offsets 45edb0 and camera_current 45edf8
 * (docs/reverse-engineering/camera.md, "The hooks (group camera_view)").
 *
 * Direct cases run the original body, the adapter and the core on the same
 * generated state (an arena of 8 KiB with random bytes: eight camera
 * objects, the player and its group record, subjects with an arena vtable,
 * the display record, the runtime counter, offset records; in the mapped
 * image the view block 4D24BCh..4D253Fh, the subject, the group cameras and
 * the static camera 4D2684h..4D27A1h, the guard byte 49F1ECh, the counter
 * pointer 6CBEE4h and the display pointer 4CF99Ch) against logging stubs for
 * every game callee: multiply_q8 47e0a0, approach_word 467adc, the steps
 * 401c20/401c4c, view_subject_set 45df34, the label step 45e208, the runtime
 * record 49630c (a record stub: links a record at EBP + [EAX+4] with the
 * real FS:0 head, as beam_cases.h does), the graphics camera constructor
 * 48bd84, view_offsets 45edb0, the setters 40111c/40109c and the subject's
 * virtual slot +30h (an arena vtable; the stub writes both 6-byte offset
 * records). The stubs log the call (ordered arguments, the slot count the
 * caller pushed, entry EAX/EDX/ECX, a hash of the arena and one of the
 * mapped globals at the call; values on the stack are masked), return
 * registers from a per-case stream and, in some cases, change what the
 * original reads again after the call (the camera pointer B+2Eh after a
 * subject change, the countdown B+64h after 45e208, the player pointer
 * 4D24E6h after view_offsets, the current camera 4D24EAh between the
 * setters, the static camera's bytes in the constructor, the stepped word).
 * Compared: EDX:EAX of the original, the adapter and the core, ECX of the
 * original and the core (camera_orbit_set: not when it leaves its frame
 * address), the logged calls of all three, the arena and the globals after
 * each run, and the callee counter deltas of the adapter. Camera objects
 * alias each other, the offset records alias the camera, the current camera
 * can be a group camera or the static camera. Then installed 31/a7 poison
 * probes and a real-callee pass for the hooks with hooked callees
 * (camera_turn_step and camera_speed_step over the real multiply_q8 and
 * approach_word; outside_camera over the real steps; view_offsets over the
 * real setters; view_subject_set over the real view_offsets and setters;
 * view_apply over the real view_subject_set chain), the nested hooks in
 * rotating original/replaced/compare modes and the hook original or
 * replaced. Prefix cmv_/CMV_. */
typedef struct cmv_record { uint32_t id,args[4],regs[3],hash,globals,count; } cmv_record;
enum { CMV_SIZE=0x2000, CMV_CAM=0x100, CMV_CAMS=8, CMV_P=0x400, CMV_G=0x480, CMV_SUBJ=0x500, CMV_VT=0x700, CMV_D=0x780,
    CMV_N=0x7c0, CMV_OFF=0x800, CMV_WORD=0x880, CMV_MAX=64, CMV_IDS=12, CMV_NESTED=4,
    CMV_GLOBALS=0x84+286+1+4+4 };
#define CMV_BASE ((uint32_t)(uintptr_t)cmv_arena)
static BYTE cmv_arena[CMV_SIZE],cmv_before[CMV_SIZE],cmv_after[CMV_SIZE];
static cmv_record cmv_log[CMV_MAX];
static unsigned cmv_calls,cmv_poke_at;
static uint32_t cmv_seed,cmv_stream,cmv_poke_kind,cmv_poke_value;
static int cmv_real,cmv_ecx_frame;
__attribute__((used)) static uint32_t cmv_ecx;
/* Stub ids: 0 multiply_q8 47e0a0, 1 approach_word 467adc, 2 401c20, 3
 * 401c4c, 4 45df34, 5 45e208, 6 49630c (record), 7 48bd84, 8 45edb0, 9
 * 40111c, 10 40109c, 11 the subject's slot +30h (arena vtable). */
static const uint32_t cmv_addresses[CMV_IDS]={0x47e0a0,0x467adc,0x401c20,0x401c4c,0x45df34,0x45e208,0x49630c,0x48bd84,
    0x45edb0,0x40111c,0x40109c,0};
static const unsigned cmv_argc[CMV_IDS]={2,3,2,2,1,0,0,1,1,3,3,3};
static uint32_t cmv_next(void){
    uint32_t z=cmv_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static int cmv_stack(uint32_t value){
    const NT_TIB *t=(const NT_TIB *)NtCurrentTeb();uint32_t low=(uint32_t)(uintptr_t)t->StackLimit,top=(uint32_t)(uintptr_t)t->StackBase;
    return value>=low-0x100000u&&value<top;
}
static int cmv_in_arena(uint32_t p,uint32_t n){return p-CMV_BASE<CMV_SIZE&&CMV_SIZE-(p-CMV_BASE)>=n;}
/* The globals of the mapped image: the view block, the subject with the
 * group cameras and the static camera, the guard byte, the counter pointer
 * and the display pointer. */
static const uint32_t cmv_global_rva[5]={0xd24bc,0xd2684,0x9f1ec,0x2cbee4,0xcf99c},cmv_global_size[5]={0x84,0x11e,1,4,4};
static void cmv_globals(BYTE *out,int restore){
    for(unsigned i=0,at=0;i<5;at+=cmv_global_size[i++]){
        if(restore)memcpy(mapped+cmv_global_rva[i],out+at,cmv_global_size[i]);else memcpy(out+at,mapped+cmv_global_rva[i],cmv_global_size[i]);
    }
}
static uint32_t cmv_hash_bytes(uint32_t h,const BYTE *p,unsigned n){for(unsigned i=0;i<n;++i)h=(h^p[i])*0x01000193u;return h;}
static uint32_t cmv_globals_hash(void){
    uint32_t h=0x811c9dc5u;
    for(unsigned i=0;i<5;++i)h=cmv_hash_bytes(h,mapped+cmv_global_rva[i],cmv_global_size[i]);
    return h;
}
static uint32_t cmv_camera(unsigned i){return CMV_BASE+CMV_CAM+0x60u*i;}
/* Pokes after a stub call (kind, applied to the call cmv_poke_at). */
enum { CMV_POKE_NONE, CMV_POKE_CAMERA, CMV_POKE_COUNTDOWN, CMV_POKE_PLAYER, CMV_POKE_CURRENT, CMV_POKE_WORD };
__attribute__((used,noinline)) static uint64_t __cdecl cmv_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(cmv_calls==CMV_MAX){fail("camera_view synthetic call overflow");ExitProcess(1);}
    cmv_record *v=cmv_log+cmv_calls++;memset(v,0,sizeof(*v));v->id=id;
    for(unsigned k=0;k<cmv_argc[id];++k)v->args[k]=cmv_stack(a[k])?0:a[k];
    for(unsigned j=0;j<3;++j)v->regs[j]=cmv_stack(regs[j])?0:regs[j];
    v->count=weapons_bridge_slots(a,cmv_argc[id]);
    v->hash=cmv_hash_bytes(0x811c9dc5u,cmv_arena,CMV_SIZE);v->globals=cmv_globals_hash();
    uint32_t eax=cmv_next(),edx=cmv_next();cmv_ecx=cmv_next();
    switch(id){
    case 0: if(cmv_real){const int64_t p=(int64_t)(int32_t)a[0]*(int32_t)a[1];eax=(uint32_t)(p>>8);edx=(uint32_t)((uint64_t)p>>32);} break;
    case 1: if(cmv_in_arena(a[0],2)&&cmv_next()%2)put16((BYTE *)(uintptr_t)a[0],(uint16_t)cmv_next()); break;
    case 6: __asm__ volatile("movl %%fs:0,%0":"=r"(eax)); break;   /* the record stub returns the real FS:0 head */
    case 7: if(!cmv_real&&cmv_next()%2)for(unsigned j=0;j<0x4e;++j)mapped[0xd2754+j]=(BYTE)cmv_next(); break;
    case 11: /* both 6-byte offset records */
        for(unsigned j=0;j<6;++j){((BYTE *)(uintptr_t)a[1])[j]=(BYTE)cmv_next();((BYTE *)(uintptr_t)a[2])[j]=(BYTE)cmv_next();}
        break;
    default: break;
    }
    if(!cmv_real&&cmv_poke_kind&&cmv_poke_at==cmv_calls-1u){
        switch(cmv_poke_kind){
        case CMV_POKE_CAMERA: put32(mapped+0xd24ea,cmv_camera(cmv_poke_value%CMV_CAMS));break;
        case CMV_POKE_COUNTDOWN: put16(mapped+0xd2520,(uint16_t)cmv_poke_value);break;
        case CMV_POKE_PLAYER: put32(mapped+0xd24e6,cmv_poke_value%2?CMV_BASE+CMV_P:CMV_BASE+CMV_SUBJ);break;
        case CMV_POKE_CURRENT: put32(mapped+0xd24ea,cmv_poke_value%3==0?0x4d2754u:cmv_camera(cmv_poke_value%CMV_CAMS));break;
        default: break;
        }
    }
    return (uint64_t)edx<<32|eax;
}
#define CMV_STUB(n) __attribute__((naked)) static void cmv_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _cmv_stub_impl\n\taddl $24,%esp\n\tmovl _cmv_ecx,%ecx\n\tret\n\t");}
CMV_STUB(0) CMV_STUB(1) CMV_STUB(2) CMV_STUB(3) CMV_STUB(4) CMV_STUB(5) CMV_STUB(7) CMV_STUB(8) CMV_STUB(9) CMV_STUB(10) CMV_STUB(11)
#undef CMV_STUB
/* 49630c: logs like the others, then links a record at EBP + [EAX+4]
 * (EAX = the frame descriptor at entry) with the FS:0 head at record+0 and
 * a zero word at record+16, and makes it the FS:0 head; EDX comes back
 * unchanged. */
__attribute__((naked)) static void cmv_stub_6(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "pushl $6\n\tcalll _cmv_stub_impl\n\taddl $12,%esp\n\t"
    "popl %ecx\n\tmovl 4(%ecx),%ecx\n\taddl %ebp,%ecx\n\tmovl %eax,(%ecx)\n\tmovw $0,16(%ecx)\n\tmovl %ecx,%fs:0\n\t"
    "popl %edx\n\tpopl %ecx\n\tret\n\t");}
static void *const cmv_stubs[CMV_IDS]={cmv_stub_0,cmv_stub_1,cmv_stub_2,cmv_stub_3,cmv_stub_4,cmv_stub_5,cmv_stub_6,cmv_stub_7,
    cmv_stub_8,cmv_stub_9,cmv_stub_10,cmv_stub_11};
static void cmv_reset(void){cmv_calls=0;cmv_stream=cmv_seed;}
static int cmv_patch(BYTE saved[CMV_IDS][5],int on,const int *keep){
    for(unsigned i=0;i<CMV_IDS;++i){
        if(!cmv_addresses[i]||keep[i])continue;
        if(actor_patch(cmv_addresses[i]-0x400000u,on?cmv_stubs[i]:NULL,saved[i],on?TRUE:FALSE))return 1;
    }
    return 0;
}
/* keep[i]: callee i stays unpatched (the function under test; with real,
 * the real callees of its real-callee pass). */
static unsigned cmv_nested(unsigned index,unsigned out[CMV_NESTED]);
static void cmv_keep(unsigned index,int keep[CMV_IDS],int real){
    memset(keep,0,CMV_IDS*sizeof(int));
    const uint32_t va=function_hook_descriptor(index)->rva+0x400000u;
    for(unsigned i=0;i<CMV_IDS;++i)if(cmv_addresses[i]==va)keep[i]=1;
    if(real){
        unsigned nested[CMV_NESTED];const unsigned count=cmv_nested(index,nested);
        for(unsigned j=0;j<count;++j){const uint32_t n=function_hook_descriptor(nested[j])->rva+0x400000u;
            for(unsigned i=0;i<CMV_IDS;++i)if(cmv_addresses[i]==n)keep[i]=1;}
    }
}
static unsigned cmv_nested(unsigned index,unsigned out[CMV_NESTED]){
    if(index==ES2_HOOK_INDEX_camera_turn_step||index==ES2_HOOK_INDEX_camera_speed_step){
        out[0]=ES2_HOOK_INDEX_multiply_q8;out[1]=ES2_HOOK_INDEX_approach_word;return 2;}
    if(index==ES2_HOOK_INDEX_outside_camera){out[0]=ES2_HOOK_INDEX_camera_turn_step;out[1]=ES2_HOOK_INDEX_camera_speed_step;
        out[2]=ES2_HOOK_INDEX_multiply_q8;out[3]=ES2_HOOK_INDEX_approach_word;return 4;}
    if(index==ES2_HOOK_INDEX_view_offsets){out[0]=ES2_HOOK_INDEX_camera_attach_set;out[1]=ES2_HOOK_INDEX_camera_orbit_set;return 2;}
    if(index==ES2_HOOK_INDEX_view_subject_set){out[0]=ES2_HOOK_INDEX_view_offsets;out[1]=ES2_HOOK_INDEX_camera_attach_set;
        out[2]=ES2_HOOK_INDEX_camera_orbit_set;return 3;}
    if(index==ES2_HOOK_INDEX_view_apply){out[0]=ES2_HOOK_INDEX_view_subject_set;out[1]=ES2_HOOK_INDEX_view_offsets;
        out[2]=ES2_HOOK_INDEX_camera_attach_set;out[3]=ES2_HOOK_INDEX_camera_orbit_set;return 4;}
    return 0;
}
static BYTE *cmv_at(uint32_t offset){return cmv_arena+offset;}
static uint16_t cmv_word(const uint16_t *values,unsigned count){return random32()%4?values[random32()%count]:(uint16_t)random32();}
static uint32_t cmv_dword_word(const uint16_t *values,unsigned count){return (random32()&0xffff0000u)|cmv_word(values,count);}
/* A camera object: the lock dword mostly 0, the mode word from the edges. */
static void cmv_camera_init(uint32_t camera,int lock_rare){
    static const uint16_t modes[]={0,1,2,3,4,5,0xffff,0x8000,0x7fff,0x100};
    BYTE *C=(BYTE *)(uintptr_t)camera;
    put32(C+0x4a,lock_rare?(random32()%8==0?random32()|1u:0):(random32()%2?random32()|1u:0));
    put16(C+0x36,cmv_word(modes,10));
}
/* The common part of the view state: cameras, the player and its group
 * record, the subjects' vtables, the display record and the runtime
 * counter, the view block in the image. */
static void cmv_view_state(int real){
    const uint32_t b=CMV_BASE;
    for(unsigned i=0;i<CMV_CAMS;++i)cmv_camera_init(cmv_camera(i),1);
    put32(cmv_at(CMV_P+0x45),b+CMV_G);
    for(unsigned i=0;i<8;++i)put32(cmv_at(CMV_SUBJ+0x20u*i),b+CMV_VT);
    put32(cmv_at(CMV_P),b+CMV_VT);
    put32(cmv_at(CMV_SUBJ+0x45),b+CMV_G);           /* the first subject can stand in as the player */
    put16(cmv_at(CMV_G+0x10),(uint16_t)(random32()%(CMV_CAMS+1)));
    put32(cmv_at(CMV_VT+0x30),(uint32_t)(uintptr_t)cmv_stub_11);
    random_bytes(mapped+0xd24bc,0x84);random_bytes(mapped+0xd2684,0x11e);
    put32(mapped+0xd24e6,b+CMV_P);put32(mapped+0xd24ea,cmv_camera(random32()%CMV_CAMS));
    put32(mapped+0x2cbee4,b+CMV_N);put32(mapped+0xcf99c,b+CMV_D+random32()%4u);
    for(unsigned i=0;i<CMV_CAMS;++i)put32(mapped+0xd2688+4u*i,cmv_camera(i));
    (void)real;
}
static uint32_t cmv_subject(void){
    const unsigned k=random32()%10;
    if(k<2)return CMV_BASE+CMV_P;
    return CMV_BASE+CMV_SUBJ+0x20u*(random32()%8);
}
/* One state for hook index (case n). real: valid inputs for the real
 * callees and no pokes. */
static void cmv_build(unsigned index,unsigned n,int real,uint32_t a[6],uint32_t r[3]){
    static const uint16_t words[]={0,1,2,0xffff,0x8000,0x7fff,0x7f,0x80,0x100,0xff00,0x1234,0xfffe};
    static const uint16_t views[]={0,1,2,3,4,5,0xffff,0x8000};
    const uint32_t b=CMV_BASE;
    cmv_seed=random32();random_bytes(cmv_arena,CMV_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    cmv_real=real;cmv_poke_kind=CMV_POKE_NONE;cmv_poke_at=0;cmv_poke_value=random32();cmv_ecx_frame=0;
    cmv_view_state(real);
    const uint32_t C=cmv_camera(random32()%CMV_CAMS);
    if(index==ES2_HOOK_INDEX_camera_attached){
        cmv_camera_init(C,0);if(random32()%2)put16((BYTE *)(uintptr_t)C+0x36,2);
        const uint32_t O=cmv_subject();put32((BYTE *)(uintptr_t)C+0x32,random32()%2?O:cmv_subject());
        a[0]=C;a[1]=random32()%8?O:random32();
    } else if(index==ES2_HOOK_INDEX_camera_orbit_set||index==ES2_HOOK_INDEX_camera_attach_set){
        cmv_camera_init(C,0);a[0]=C;a[1]=cmv_subject();
        a[2]=random32()%4==0?C+0x1cu+random32()%0x30u:b+CMV_OFF+random32()%8u;   /* the offset over the camera's own fields */
        cmv_ecx_frame=index==ES2_HOOK_INDEX_camera_orbit_set&&!get32((BYTE *)(uintptr_t)C+0x4a);
    } else if(index==ES2_HOOK_INDEX_camera_turn_step||index==ES2_HOOK_INDEX_camera_speed_step){
        a[0]=b+CMV_WORD+random32()%8u;a[1]=n<12?(random32()&0xffff0000u)|words[n]:cmv_dword_word(words,12);
        put16((BYTE *)(uintptr_t)a[0],cmv_word(words,12));
    } else if(index==ES2_HOOK_INDEX_outside_camera){
        cmv_camera_init(C,1);a[0]=C;
        if(n<10){static const uint16_t edge[]={0,1,2,3,4,5,0xffff,0x8000,0x7fff,0x100};put16((BYTE *)(uintptr_t)C+0x36,edge[n]);}
        a[1]=cmv_dword_word(words,12);a[2]=cmv_dword_word(words,12);a[3]=random32()%2?random32()&0xffff0000u:random32();
    } else if(index==ES2_HOOK_INDEX_view_apply){
        BYTE *B=mapped+0xd24bc;
        cmv_at(CMV_D)[0x1c+(get32(mapped+0xcf99c)-b-CMV_D)]=(BYTE)(random32()%3?random32()|1u:0);
        put16(B+0x5e,cmv_word(views,8));put16(B+0x32,random32()%4==0?get16(B+0x5e):cmv_word(views,8));
        if(n<8)put16(B+0x5e,views[n]);
        put16(B+0x78,(uint16_t)(random32()%2?0:random32()|1u));
        put32(B+0x60,cmv_subject());
        if(random32()%2)put32(mapped+0xd2684,get32(B+0x2a));
        put16(B+0x5c,cmv_word(words,12));put16(B+0x34,random32()%3==0?get16(B+0x5c):cmv_word(words,12));
        static const uint16_t counts[]={0,0,1,2,3,0xffff};put16(B+0x64,counts[random32()%6]);
        if(!real&&random32()%3==0){cmv_poke_at=0;cmv_poke_kind=random32()%2?CMV_POKE_CAMERA:CMV_POKE_COUNTDOWN;cmv_poke_value=random32()%4;}
    } else if(index==ES2_HOOK_INDEX_view_subject_set){
        mapped[0x9f1ec]=(BYTE)(random32()%4==0?0:random32()|1u);
        static const uint16_t counts[]={0,1,2,3,4,5,6,7,8,0xffff,0x8000};
        put16(cmv_at(CMV_G+0x10),counts[n<11?n:random32()%11]);
        const uint32_t S=cmv_subject();a[0]=S;
        for(unsigned i=0;i<CMV_CAMS;++i){
            const unsigned k=random32()%6;
            put32((BYTE *)(uintptr_t)cmv_camera(i)+0x32,k==0?S:k==1?0:cmv_subject()+1u);
        }
        const unsigned c=random32()%6;
        if(c==0)put32(mapped+0xd24ea,0x4d2754u);                                     /* the static camera is current */
        if(c==1)put32(mapped+0xd24ea,get32(mapped+0xd2688+4u*(random32()%CMV_CAMS)));  /* a group camera is current */
        put16(mapped+0xd2518,cmv_word(words,12));
        if(!real&&random32()%3==0){cmv_poke_kind=CMV_POKE_PLAYER;cmv_poke_at=1u+random32()%2u;}
    } else if(index==ES2_HOOK_INDEX_view_offsets){
        a[0]=cmv_subject();
        if(!real&&random32()%3==0){cmv_poke_kind=CMV_POKE_CURRENT;cmv_poke_at=random32()%2;}
    } else {
        /* camera_current */
        put32(mapped+0xd24ea,random32());
    }
    put32(cmv_at(CMV_VT+0x30),(uint32_t)(uintptr_t)cmv_stub_11);
}
/* The original with the four argument slots and EAX/EDX/ECX = r; out
 * receives EAX, EDX and ECX after the call. */
__attribute__((naked,noinline)) static void cmv_invoke(void *target,const uint32_t *a,const uint32_t *r,uint32_t out[3]){
    __asm__ volatile("pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "movl 12(%ebp),%esi\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
static uint64_t cmv_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    const es2_guest_memory *m=&math_native_memory;BYTE scratch[ES2_VIEW_SUBJECT_FRAME]={0};
    es2_weapon_graph g=weapons_graph(mapped,scratch,index);
    if(index==ES2_HOOK_INDEX_camera_attached)return es2_camera_attached_registers(m,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_camera_orbit_set)return es2_camera_orbit_set_registers(m,a[0],a[1],a[2],r,ecx);
    if(index==ES2_HOOK_INDEX_camera_attach_set)return es2_camera_attach_set_registers(m,a[0],a[1],a[2],r,ecx);
    if(index==ES2_HOOK_INDEX_camera_turn_step)return es2_camera_turn_step_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_camera_speed_step)return es2_camera_speed_step_registers(m,&g,a[0],a[1],r,ecx);
    if(index==ES2_HOOK_INDEX_outside_camera)return es2_outside_camera_registers(m,&g,a[0],a[1],a[2],a[3],r,ecx);
    if(index==ES2_HOOK_INDEX_view_apply)return es2_view_apply_registers(m,&g,r,ecx);
    if(index==ES2_HOOK_INDEX_view_subject_set){
        weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_VIEW_SUBJECT_FRAME)};
        g.call=weapons_impact_guest_call;g.context=&c;
        const uint64_t result=es2_view_subject_set_registers(m,&g,a[0],r,ecx);
        const uint32_t head=(uint32_t)(result>>32);__asm__ volatile("movl %0,%%fs:0"::"r"(head):"memory");
        return result;
    }
    if(index==ES2_HOOK_INDEX_view_offsets)return es2_view_offsets_registers(m,&g,a[0],r,ecx);
    return es2_camera_current_registers(m,(uint32_t)(uintptr_t)mapped,r,ecx);
}
static int cmv_report(const char *what,unsigned n,const uint64_t pair[3],unsigned calls,const cmv_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],(unsigned long long)pair[2],calls,cmv_calls);
    for(unsigned j=0;j<calls&&j<cmv_calls&&expected;++j)if(memcmp(expected+j,cmv_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(cmv_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u word%u %08x/%08x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CMV_SIZE;++j)if(cmv_after[j]!=cmv_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,cmv_after[j],cmv_arena[j]);break;}
    return 1;
}
static int cmv_globals_differ(const BYTE *want,const BYTE *got){
    for(unsigned j=0;j<CMV_GLOBALS;++j)if(want[j]!=got[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,want[j],got[j]);return 1;}
    return 0;
}
static uint64_t cmv_expected_count(const cmv_record *expected,unsigned calls,const callee_counter *c){
    uint64_t count=0;
    for(unsigned j=0;j<calls;++j){
        if(c->event==ES2_COLLIDE_SLOT){if(expected[j].id==11)++count;}
        else if(cmv_addresses[expected[j].id]==c->key)++count;
    }
    return count;
}
static int cmv_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CMV_GLOBALS],after[CMV_GLOBALS],actual[CMV_GLOBALS];static cmv_record expected[CMV_MAX],adapter[CMV_MAX];
    static BYTE adapter_arena[CMV_SIZE];
    uint32_t want[3],core_ecx=0;uint64_t pair[3]={0,0,0};
    memcpy(cmv_before,cmv_arena,CMV_SIZE);cmv_globals(before,0);
    cmv_reset();cmv_invoke(mapped+d->rva,a,r,want);
    const unsigned calls=cmv_calls;memcpy(expected,cmv_log,calls*sizeof(*expected));
    memcpy(cmv_after,cmv_arena,CMV_SIZE);cmv_globals(after,0);
    /* the adapter */
    memcpy(cmv_arena,cmv_before,CMV_SIZE);cmv_globals(before,1);cmv_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[4]={0};
    for(unsigned i=0;i<count&&i<4;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    const unsigned adapter_calls=cmv_calls;memcpy(adapter,cmv_log,adapter_calls*sizeof(*adapter));
    memcpy(adapter_arena,cmv_arena,CMV_SIZE);cmv_globals(actual,0);
    for(unsigned i=0;i<count&&i<4;++i)
        if(c[i].count-counts[i]!=cmv_expected_count(expected,calls,c+i))return fail("camera_view callee counter delta");
    pair[0]=(uint64_t)want[1]<<32|want[0];
    int bad=pair[0]!=pair[1]||calls!=adapter_calls||memcmp(expected,adapter,calls*sizeof(*expected))||
        memcmp(cmv_after,adapter_arena,CMV_SIZE)||memcmp(after,actual,CMV_GLOBALS);
    if(bad)cmv_globals_differ(after,actual);
    /* the core */
    memcpy(cmv_arena,cmv_before,CMV_SIZE);cmv_globals(before,1);cmv_reset();
    pair[2]=cmv_core(index,a,r,&core_ecx);cmv_globals(actual,0);
    const int ecx_known=!rng_hooks[index].installed&&!cmv_ecx_frame;
    bad=bad||pair[2]!=pair[0]||calls!=cmv_calls||memcmp(expected,cmv_log,calls*sizeof(*expected))||
        memcmp(cmv_after,cmv_arena,CMV_SIZE)||memcmp(after,actual,CMV_GLOBALS)||(ecx_known&&want[2]!=core_ecx);
    if(bad){
        if(ecx_known)fprintf(stderr," ecx %08x/%08x\n",want[2],core_ecx);
        cmv_globals_differ(after,actual);
        return cmv_report("direct",n,pair,calls,expected);
    }
    cmv_globals(before,1);
    return 0;
}
static int cmv_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;cmv_build(index,n,0,a,r);
        BYTE before[CMV_GLOBALS],after[CMV_GLOBALS],actual[CMV_GLOBALS],expected_fpu[108],actual_fpu[108];static cmv_record expected[CMV_MAX];
        memcpy(cmv_before,cmv_arena,CMV_SIZE);cmv_globals(before,0);
        cmv_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=cmv_calls;memcpy(expected,cmv_log,calls*sizeof(*expected));memcpy(cmv_after,cmv_arena,CMV_SIZE);cmv_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("camera_view poison install");
        memcpy(cmv_arena,cmv_before,CMV_SIZE);cmv_globals(before,1);cmv_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);es2_hook_fpu_save(actual_fpu);cmv_globals(actual,0);
        uint64_t pair[3]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0],0};
        if(pair[0]!=pair[1]||calls!=cmv_calls||memcmp(expected,cmv_log,calls*sizeof(*expected))||memcmp(cmv_after,cmv_arena,CMV_SIZE)||
           memcmp(after,actual,CMV_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cmv_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("camera_view poison uninstall");cmv_globals(before,1);++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, EDX:EAX/trace/arena/globals\n",d->name,total);
    return status;
}
static int cmv_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0;
    uint64_t entered[CMV_NESTED]={0},hooked=0,replaced=0;
    unsigned nested[CMV_NESTED];const unsigned count=cmv_nested(index,nested);
    for(unsigned mode=0;mode<6&&!status;++mode)for(unsigned n=0;n<512&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u;cmv_build(index,n,1,a,r);
        BYTE before[CMV_GLOBALS],after[CMV_GLOBALS],actual[CMV_GLOBALS];
        memcpy(cmv_before,cmv_arena,CMV_SIZE);cmv_globals(before,0);
        cmv_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);
        memcpy(cmv_after,cmv_arena,CMV_SIZE);cmv_globals(after,0);
        es2_hook hk[CMV_NESTED+1];memset(hk,0,sizeof(hk));es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        for(unsigned j=0;j<count;++j){o.mode=(es2_hook_mode)((mode+j)%3);
            const es2_hook_extension *e=function_hook_extension(nested[j]);if((!e||e->compare_unsupported)&&o.mode==ES2_HOOK_COMPARE)o.mode=ES2_HOOK_REPLACED;
            if(!weapons_install_hook(hk+j,function_hook_descriptor(nested[j]),&o,e))return fail("camera_view nested install");}
        o.mode=mode<3?ES2_HOOK_ORIGINAL:ES2_HOOK_REPLACED;
        if(!weapons_install_hook(hk+CMV_NESTED,d,&o,function_hook_extension(index)))return fail("camera_view real install");
        memcpy(cmv_arena,cmv_before,CMV_SIZE);cmv_globals(before,1);cmv_reset();
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);cmv_globals(actual,0);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(cmv_after,cmv_arena,CMV_SIZE)||memcmp(after,actual,CMV_GLOBALS)){
            uint64_t pair[3]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0],0};
            fprintf(stderr,"real mode=%u\n",mode);cmv_globals_differ(after,actual);cmv_calls=0;status=cmv_report("real",n,pair,0,NULL);
        }
        for(unsigned j=0;j<=CMV_NESTED;++j){
            if(j<count)entered[j]+=hk[j].calls;
            if(hk[j].mismatches)status=fail("camera_view nested compare mismatch");
            if((j<count||j==CMV_NESTED)&&!es2_hook_uninstall(hk+j))return fail("camera_view real uninstall");
        }
        hooked+=hk[CMV_NESTED].calls;replaced+=mode>=3;
        cmv_globals(before,1);++total;
    }
    if(!status&&hooked!=replaced){fprintf(stderr,"FAIL: %s real pass entered %llu of %llu\n",d->name,(unsigned long long)hooked,(unsigned long long)replaced);status=1;}
    for(unsigned j=0;j<count&&!status;++j)if(!entered[j]){fprintf(stderr,"FAIL: %s real callee %s never entered\n",d->name,function_hook_descriptor(nested[j])->name);status=1;}
    if(!status){printf("PASS: %s %u real-callee cases, %u nested hook(s) in rotating modes, hook original/replaced, 31/a7 stack, EDX:EAX, arena and globals\n",d->name,total,count);
        for(unsigned j=0;j<count;++j)printf("REAL %s nested %s entered %llu\n",d->name,function_hook_descriptor(nested[j])->name,(unsigned long long)entered[j]);}
    return status;
}
static int camera_view_cases(unsigned index,unsigned limit){
    BYTE saved[CMV_IDS][5],old_globals[CMV_GLOBALS];int keep[CMV_IDS];
    cmv_globals(old_globals,0);cmv_keep(index,keep,0);
    if(cmv_patch(saved,1,keep))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_CAMERA_VIEW_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];cmv_build(index,n,0,a,r);status=cmv_compare(index,n,a,r);cmv_globals(old_globals,1);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=cmv_installed_probes(index,64);
    if(cmv_patch(saved,0,keep))return 1;
    current_case_valid=0;
    unsigned nested[CMV_NESTED];
    if(!limit&&!status&&cmv_nested(index,nested)){
        BYTE real_saved[CMV_IDS][5];int real_keep[CMV_IDS];cmv_keep(index,real_keep,1);
        if(cmv_patch(real_saved,1,real_keep))return 1;
        status=cmv_real_pass(index);
        if(cmv_patch(real_saved,0,real_keep))return 1;
    }
    cmv_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; EDX:EAX of the original, adapter and core, ECX (core), callee calls with entry registers and arena/globals hashes, arena/globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
