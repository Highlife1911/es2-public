/* Config group: prefs_video 45e510, prefs_load 459770, prefs_store 459b94,
 * keyjoy_load 45b7a8, sound_config 46379c, tape_save 45cca4 and tape_restore
 * 45cd5c (docs/reverse-engineering/file_io.md, "configuration and tape files as
 * own code").
 *
 * Direct cases run the original body (through an own caller that fills
 * the 4 KiB below its frame with zeros, hands over EAX/EDX/ECX and returns
 * all three), the adapter and the core (for ECX) against logging stubs for
 * every callee except the runtime frame setup 49630c, which runs as
 * original code: the path builder 45ee00, the read stream 47ad40/47af98/
 * 47ad78, the write stream 47b064/47b2b8/47b09c and, through their
 * thunks, _memset, _fopen, _fread, _fwrite, _fclose, _fseek, _ftell,
 * _rewind, GetPrivateProfileStringA (stdcall), _stricmp and _atol; the
 * prefs callbacks are four logging stubs in the table 4d1fdc. Logged per
 * call: the slots the bridge pushed, the arguments and entry EAX/EDX/ECX
 * (frame addresses relative to the first frame address of the run, FS:0's
 * head as a tag, everything else raw; no stub result lies in the stack
 * range), the stub's EAX, and by content the bytes every write receives
 * (FNV hash and the first 32 bytes) and the strings _stricmp and _atol
 * read. The stubs deliver random bytes, short reads, failed opens and
 * stream errors; the zero fill makes the original's residue (a prefs
 * file that does not open or is short, a short tape read) equal to the
 * adapter's zeroed scratch. Compared: the pair, ECX (core against the
 * original), the call log, the arena, the global windows (prefs blocks,
 * callbacks, display globals, keyjoy flags, done byte, the construction
 * counter), FS:0, and the callee-counter deltas including residue and
 * unmodeled. Then installed 31/a7 poison probes (stubs without short
 * reads or failures), and a model pass: the stubs become a small file
 * system, GetPrivateProfileStringA reads it through formats/cfg's INI
 * reader, _stricmp and _atol run in the privately mapped CW3220.DLL; the
 * original and the installed hook run on the CD's PREFS.CFG, SOUND.CFG,
 * KEYJOY.CFG and DEMO tapes and on variants, the files and state are
 * compared, and the original's results are checked against formats/cfg
 * (es2_prefs_load, es2_prefs_overlay, es2_prefs_video_read,
 * es2_cfg_sound_parse, es2_cfg_keyjoy_parse, es2_tape_header_parse).
 * Identifiers: cfx_/CFX_. */
#include "es2/formats/cfg.h"
static int rtl_map_cw3220(const char *root);
static uint32_t rtl_cw_export(const char *name);
static BYTE *rtl_cw;
typedef struct cfx_record { uint32_t id,n,args[6],regs[3],eax,length,hash; BYTE data[32]; } cfx_record;
enum { CFX_MAX=96, CFX_IDS=23, CFX_SIZE=0x800, CFX_OBJ=0x000, CFX_LIST=0x100, CFX_PATH=0x300, CFX_NAMES=0x400,
    CFX_FILES=12, CFX_FILE_SIZE=0x10000 };
enum { CF_FRAME, CF_PATH, CF_RCTOR, CF_READ, CF_RDTOR, CF_WCTOR, CF_WRITE, CF_WDTOR, CF_MEMSET, CF_FOPEN, CF_FREAD,
    CF_FWRITE, CF_FCLOSE, CF_FSEEK, CF_FTELL, CF_REWIND, CF_PROFILE, CF_STRICMP, CF_ATOL, CF_CALLBACK };
#define CFX_BASE ((uint32_t)(uintptr_t)cfx_arena)
#define CFX_FILE_HANDLE 0x9f000000u   /* FILE* values: never in the stack range */
static BYTE cfx_arena[CFX_SIZE],cfx_before[CFX_SIZE],cfx_after[CFX_SIZE];
static cfx_record cfx_log[CFX_MAX];
static unsigned cfx_calls,cfx_mode,cfx_hook,cfx_residue;   /* mode: 0 random, 1 full (no short/failed I/O), 2 model */
static uint32_t cfx_seed,cfx_stream,cfx_anchor,cfx_head,cfx_counter,cfx_tape_length;
static int cfx_anchored,cfx_length_pending;
__attribute__((used)) static uint32_t cfx_ecx;
static const uint32_t cfx_addresses[CFX_IDS]={0,0x45ee00,0x47ad40,0x47af98,0x47ad78,0x47b064,0x47b2b8,0x47b09c,0x49681a,
    0x496802,0x49684a,0x496832,0x496838,0x49682c,0x4967de,0x496772,0x496910,0x496868,0x496880,0,0,0,0};
static const unsigned cfx_counts[CFX_IDS]={0,1,2,3,2,2,3,2,3,2,4,4,1,3,1,1,6,2,1,1,1,1,1};
/* The model's files (mode 2): name, bytes, size, position, existence. */
typedef struct cfx_file { char name[32]; BYTE *data; uint32_t size,pos; int exists; } cfx_file;
static cfx_file cfx_files[CFX_FILES];
static int cfx_write_fails;
static uint32_t cfx_next(void){
    uint32_t z=cfx_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *cfx_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static uint32_t cfx_fs0(void){uint32_t v;__asm__ volatile("movl %%fs:0,%0":"=r"(v));return v;}
static void cfx_set_fs0(uint32_t v){__asm__ volatile("movl %0,%%fs:0"::"r"(v):"memory");}
static int cfx_stack(uint32_t value){
    uint32_t top=(uint32_t)(uintptr_t)((const NT_TIB *)NtCurrentTeb())->StackBase;return top-value-1u<0x1000000u;
}
/* Frame addresses relative to the first one of the run; FS:0's head at
 * entry as a tag; other values raw. */
static uint32_t cfx_mask(uint32_t v){
    if(v==cfx_head)return 0x5e000000u;
    if(!cfx_stack(v))return v;
    if(!cfx_anchored){cfx_anchored=1;cfx_anchor=v;}
    return 0x5f000000u|((v-cfx_anchor)&0xffffffu);
}
static void cfx_content(cfx_record *v,uint32_t p,uint32_t n){
    uint32_t h=0x811c9dc5u;const BYTE *b=cfx_host(p);
    v->length=n;for(uint32_t i=0;i<n;++i){h=(h^b[i])*0x01000193u;if(i<32)v->data[i]=b[i];}
    v->hash=h;
}
static void cfx_string(cfx_record *v,uint32_t p){uint32_t n=0;while(n<64&&cfx_host(p)[n])++n;cfx_content(v,p,n);}
/* ---- model files ---------------------------------------------------- */
static cfx_file *cfx_find(const char *name){
    for(unsigned i=0;i<CFX_FILES;++i)if(cfx_files[i].data&&!_stricmp(cfx_files[i].name,name))return &cfx_files[i];
    return NULL;
}
static cfx_file *cfx_slot(const char *name){
    cfx_file *f=cfx_find(name);if(f)return f;
    for(unsigned i=0;i<CFX_FILES;++i)if(cfx_files[i].data&&!cfx_files[i].name[0]){snprintf(cfx_files[i].name,32,"%s",name);return &cfx_files[i];}
    return NULL;
}
static void cfx_file_set(const char *name,const BYTE *bytes,uint32_t size,int exists){
    cfx_file *f=cfx_slot(name);if(!f)return;
    if(size>CFX_FILE_SIZE)size=CFX_FILE_SIZE;
    f->exists=exists;f->size=exists?size:0;f->pos=0;if(exists&&size)memcpy(f->data,bytes,size);
}
static uint32_t cfx_handle(cfx_file *f){return CFX_FILE_HANDLE+0x10u*(uint32_t)(f-cfx_files);}
static cfx_file *cfx_from_handle(uint32_t h){
    const uint32_t i=(h-CFX_FILE_HANDLE)/0x10u;return h>=CFX_FILE_HANDLE&&i<CFX_FILES?&cfx_files[i]:NULL;
}
static void cfx_files_clear(void){
    for(unsigned i=0;i<CFX_FILES;++i){cfx_files[i].name[0]=0;cfx_files[i].size=cfx_files[i].pos=0;cfx_files[i].exists=0;}
}
/* GetPrivateProfileStringA over the model: formats/cfg's INI subset. */
static uint32_t cfx_profile(const uint32_t *a){
    cfx_file *f=cfx_find((const char *)cfx_host(a[5]));char *out=(char *)cfx_host(a[3]);int found=0;
    if(!a[4])return 0;
    if(!f||!f->exists||es2_cfg_ini_get((es2_span){f->data,f->size},(const char *)cfx_host(a[0]),(const char *)cfx_host(a[1]),
        out,a[4],&found,NULL)!=ES2_OK||!found)snprintf(out,a[4],"%s",(const char *)cfx_host(a[2]));
    return (uint32_t)strlen(out);
}
/* ---- random answers ------------------------------------------------- */
static const char *const cfx_values[]={"Reverse","reverse","REVERSE"," Reverse","Reverse ","Reversed","Default","",
    "DirectSound","directsound","DIRECTSOUND","Direct","MME","5","64","65","0","1","-3","11","22","011"," 11","11x",
    "Mono","mono","MONO","Stereo","Mon","xyzzy-a-long-value-of-thirty-nine-chars"};
#define CFX_VALUES (sizeof(cfx_values)/sizeof(cfx_values[0]))
static uint32_t cfx_answer(unsigned id,const uint32_t *a,cfx_record *v,uint32_t eax,uint32_t pick){
    const int full=cfx_mode!=0,model=cfx_mode==2;
    switch(id){
    case CF_PATH: return model?a[0]:CFX_BASE+CFX_PATH+0x10u*(pick%4);
    case CF_RCTOR: case CF_WCTOR: {
        uint32_t status=full||pick%6?0u:1u+(pick>>8)%3u;
        if(model){
            cfx_file *f=cfx_find((const char *)cfx_host(a[1]));
            if(id==CF_RCTOR)status=f&&f->exists?0u:1u;
            else if(cfx_write_fails)status=1;
            else {f=cfx_slot((const char *)cfx_host(a[1]));if(f){f->exists=1;f->size=0;}else status=1;}
            put16(cfx_host(a[0]+8),(uint16_t)(f?f-cfx_files:0xffff));
        } else put16(cfx_host(a[0]+8),(uint16_t)(pick>>12));
        put32(cfx_host(a[0]),id==CF_RCTOR?0x4a1738u:0x4a1760u);put32(cfx_host(a[0]+4),status);cfx_host(a[0])[10]=(BYTE)(status==0);
        if(id==CF_RCTOR&&status&&cfx_hook==ES2_HOOK_INDEX_prefs_store)++cfx_residue;
        return a[0];
    }
    case CF_READ: {
        uint32_t n=a[1],k=n;
        if(model){cfx_file *f=&cfx_files[get16(cfx_host(a[0]+8))%CFX_FILES];k=f->size<n?f->size:n;memcpy(cfx_host(a[2]),f->data,k);}
        else {if(!full&&pick%5==0)k=(pick>>8)%n;for(uint32_t i=0;i<k;++i)cfx_host(a[2])[i]=(BYTE)cfx_next();}
        if(k!=n){put32(cfx_host(a[0]+4),2);if(cfx_hook==ES2_HOOK_INDEX_prefs_store)++cfx_residue;return eax&0xffffff00u;}
        return (eax&0xffffff00u)|1u;
    }
    case CF_WRITE:
        cfx_content(v,a[2],a[1]);
        if(model){cfx_file *f=&cfx_files[get16(cfx_host(a[0]+8))%CFX_FILES];memcpy(f->data,cfx_host(a[2]),a[1]);f->size=a[1];}
        return eax;
    case CF_MEMSET: memset(cfx_host(a[0]),(int)a[1],a[2]);return a[0];
    case CF_FOPEN: {
        if(model){
            const char *mode=(const char *)cfx_host(a[1]);cfx_file *f=cfx_find((const char *)cfx_host(a[0]));
            if(mode[0]=='w'){if(cfx_write_fails)return 0;f=cfx_slot((const char *)cfx_host(a[0]));if(!f)return 0;f->exists=1;f->size=0;}
            else if(!f||!f->exists)return 0;
            f->pos=0;return cfx_handle(f);
        }
        return !full&&pick%8==0?0:CFX_FILE_HANDLE|(pick&0xfff0u);
    }
    case CF_FREAD: {
        const uint32_t total=a[1]*a[2];uint32_t got=total;BYTE *p=cfx_host(a[0]);
        if(model){cfx_file *f=cfx_from_handle(a[3]);got=f?f->size-f->pos:0;if(got>total)got=total;if(got)memcpy(p,f->data+f->pos,got);if(f)f->pos+=got;}
        else {
            if(!full&&pick%7==0)got=(pick>>8)%(total+1u);
            for(uint32_t i=0;i<got;++i)p[i]=(BYTE)cfx_next();
            if(cfx_length_pending&&a[1]==1&&a[2]==4){BYTE l[4];put32(l,cfx_tape_length);memcpy(p,l,got);}
            if(cfx_hook==ES2_HOOK_INDEX_prefs_video&&got>4&&pick%2)p[4]=(BYTE)(pick>>16&1?1:2);
        }
        cfx_length_pending=0;
        if(got!=total&&(cfx_hook==ES2_HOOK_INDEX_tape_save||cfx_hook==ES2_HOOK_INDEX_tape_restore))++cfx_residue;
        return a[1]?got/a[1]:0;
    }
    case CF_FWRITE: {
        const uint32_t total=a[1]*a[2];cfx_content(v,a[0],total);
        if(model){cfx_file *f=cfx_from_handle(a[3]);if(f&&f->pos+total<=CFX_FILE_SIZE){memcpy(f->data+f->pos,cfx_host(a[0]),total);f->pos+=total;if(f->pos>f->size)f->size=f->pos;}}
        return a[2];
    }
    case CF_FCLOSE: return a[0]?0:0xffffffffu;
    case CF_FSEEK: if(model){cfx_file *f=cfx_from_handle(a[0]);if(f)f->pos=f->size;}return 0;
    case CF_FTELL: if(model){cfx_file *f=cfx_from_handle(a[0]);return f?f->pos:0xffffffffu;}return cfx_tape_length;
    case CF_REWIND: if(model){cfx_file *f=cfx_from_handle(a[0]);if(f)f->pos=0;}return eax;
    case CF_PROFILE: {
        if(model)return cfx_profile(a);
        const char *s=cfx_values[pick%CFX_VALUES];char *out=(char *)cfx_host(a[3]);
        size_t n=strlen(s);if(n>a[4]-1u)n=a[4]-1u;memcpy(out,s,n);out[n]=0;return (uint32_t)n;
    }
    case CF_STRICMP:
        cfx_string(v,a[0]);
        if(model)return (uint32_t)((int (__cdecl *)(const char *,const char *))(uintptr_t)rtl_cw_export("_stricmp"))(
            (const char *)cfx_host(a[0]),(const char *)cfx_host(a[1]));
        return pick%3==0?0:eax;
    case CF_ATOL: {
        cfx_string(v,a[0]);
        if(model)return (uint32_t)((long (__cdecl *)(const char *))(uintptr_t)rtl_cw_export("_atol"))((const char *)cfx_host(a[0]));
        static const uint32_t values[]={0,1,5,11,63,64,65,0xffffffffu,0x80000000u,0x7fffffffu,0x40,0x41};
        return pick%4?values[(pick>>4)%12]:eax;
    }
    default: /* a prefs callback: may change the block bytes the original rereads */
        if(!model&&pick%7==0)mapped[0xd1f38+(pick>>8)%0x36]=(BYTE)cfx_next();
        return eax;
    }
}
__attribute__((used,noinline)) static uint64_t __cdecl cfx_stub_impl(unsigned id,const uint32_t *regs,const uint32_t *a){
    if(cfx_calls==CFX_MAX){fail("config synthetic call overflow");ExitProcess(1);}
    cfx_record *v=cfx_log+cfx_calls++;memset(v,0,sizeof(*v));v->id=id;
    const unsigned n=weapons_bridge_slots(a,cfx_counts[id]);v->n=n;
    for(unsigned j=0;j<n&&j<6;++j)v->args[j]=cfx_mask(a[j]);
    for(unsigned j=0;j<3;++j)v->regs[j]=cfx_mask(regs[j]);
    uint32_t eax=cfx_next()|0x80000000u,edx=cfx_next()|0x80000000u,pick=cfx_next();cfx_ecx=cfx_next()|0x80000000u;
    eax=cfx_answer(id,a,v,eax,pick);
    v->eax=cfx_mask(eax);return (uint64_t)edx<<32|eax;
}
#define CFX_STUB(n,ret) __attribute__((naked)) static void cfx_stub_##n(void){__asm__ volatile( \
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t" \
    "pushl $" #n "\n\tcalll _cfx_stub_impl\n\taddl $24,%esp\n\tmovl _cfx_ecx,%ecx\n\t" ret "\n\t");}
CFX_STUB(1,"ret") CFX_STUB(2,"ret") CFX_STUB(3,"ret") CFX_STUB(4,"ret") CFX_STUB(5,"ret") CFX_STUB(6,"ret")
CFX_STUB(7,"ret") CFX_STUB(8,"ret") CFX_STUB(9,"ret") CFX_STUB(10,"ret") CFX_STUB(11,"ret") CFX_STUB(12,"ret")
CFX_STUB(13,"ret") CFX_STUB(14,"ret") CFX_STUB(15,"ret") CFX_STUB(16,"ret $24") CFX_STUB(17,"ret") CFX_STUB(18,"ret")
CFX_STUB(19,"ret") CFX_STUB(20,"ret") CFX_STUB(21,"ret") CFX_STUB(22,"ret")
#undef CFX_STUB
static void *cfx_stubs[CFX_IDS]={NULL,cfx_stub_1,cfx_stub_2,cfx_stub_3,cfx_stub_4,cfx_stub_5,cfx_stub_6,cfx_stub_7,
    cfx_stub_8,cfx_stub_9,cfx_stub_10,cfx_stub_11,cfx_stub_12,cfx_stub_13,cfx_stub_14,cfx_stub_15,cfx_stub_16,
    cfx_stub_17,cfx_stub_18,cfx_stub_19,cfx_stub_20,cfx_stub_21,cfx_stub_22};
/* Six argument slots, entry EAX/EDX/ECX; the 4 KiB below the frame are
 * filled with `fill` first; EAX/EDX/ECX come back in out. */
__attribute__((naked,noinline)) static void cfx_machine(void *target,const uint32_t *a,const uint32_t *registers,uint32_t *out,
    uint32_t fill){
    __asm__ volatile(
        "pushl %ebp\n\tmovl %esp,%ebp\n\tpushl %ebx\n\tpushl %esi\n\tpushl %edi\n\t"
        "subl $4096,%esp\n\tmovl %esp,%edi\n\tmovl 24(%ebp),%eax\n\tmovl $1024,%ecx\n\tcld\n\trep stosl\n\taddl $3840,%esp\n\t"
        "movl 12(%ebp),%esi\n\tpushl 20(%esi)\n\tpushl 16(%esi)\n\tpushl 12(%esi)\n\tpushl 8(%esi)\n\tpushl 4(%esi)\n\tpushl (%esi)\n\t"
        "movl 16(%ebp),%edi\n\tmovl (%edi),%eax\n\tmovl 4(%edi),%edx\n\tmovl 8(%edi),%ecx\n\tcall *8(%ebp)\n\t"
        "movl 20(%ebp),%edi\n\tmovl %eax,(%edi)\n\tmovl %edx,4(%edi)\n\tmovl %ecx,8(%edi)\n\t"
        "leal -12(%ebp),%esp\n\tpopl %edi\n\tpopl %esi\n\tpopl %ebx\n\tpopl %ebp\n\tret\n\t");
}
/* Global windows: the prefs blocks, the callback table and its flag byte
 * 4d20b4; the display globals with 4d255e; the keyjoy flags; the done
 * byte 49f254; the prefs path 49e81c; the counter pointer 6cbee4. */
static const uint32_t cfx_window_rva[6]={0xd1f30,0xd2530,0x9ea88,0x9f250,0x9e81c,0x2cbee4};
static const unsigned cfx_window_len[6]={0x190,0x50,0x20,8,0x20,4};
#define CFX_GLOBALS (0x190+0x50+0x20+8+0x20+4+4)
static void cfx_globals(BYTE *out,int restore){
    for(unsigned i=0;i<6;++i){if(restore)memcpy(mapped+cfx_window_rva[i],out,cfx_window_len[i]);else memcpy(out,mapped+cfx_window_rva[i],cfx_window_len[i]);out+=cfx_window_len[i];}
    if(restore)cfx_counter=get32(out);else put32(out,cfx_counter);
}
static void cfx_reset(void){cfx_calls=0;cfx_stream=cfx_seed;cfx_anchored=0;cfx_residue=0;cfx_head=cfx_fs0();cfx_length_pending=0;}
/* The callback stubs as table entries. */
static uint32_t cfx_callback(unsigned k){return (uint32_t)(uintptr_t)cfx_stubs[CF_CALLBACK+k];}
static const char *const cfx_paths[7]={"data\\script.dat","data\\player.mec","data\\mission.var","data\\prefs.cfg",
    "data\\restore.dat","data\\object.str","data\\keyjoy.cfg"};
static void cfx_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    const uint32_t b=CFX_BASE;
    cfx_seed=random32();random_bytes(cfx_arena,CFX_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32()|0x80000000u;
    for(unsigned i=0;i<4;++i)snprintf((char *)cfx_arena+CFX_PATH+0x10*i,16,"path%u",i);
    put32(mapped+0x2cbee4,(uint32_t)(uintptr_t)&cfx_counter);cfx_counter=random32();
    random_bytes(mapped+0xd1f38,0x36*3);
    for(unsigned i=0;i<0x36;++i)put32(mapped+0xd1fdc+4*i,random32()%5==0?cfx_callback(random32()%4):0);
    random_bytes(mapped+0xd2530,0x50);random_bytes(mapped+0x9ea88,0x20);
    mapped[0x9f254]=(BYTE)(random32()%4==0?1+random32()%255:0);
    snprintf((char *)mapped+0x9e81c,0x20,"%s",random32()%2?"data\\prefs.cfg":"tapes\\prefs.cfg");
    switch(index){
    case ES2_HOOK_INDEX_prefs_video: {
        static const uint32_t modes[]={0,1,2,3,4,0xff,0x101};
        a[0]=n<7?modes[n]:random32()%3?modes[random32()%7]:random32();
        if(n<14)mapped[0x9f254]=n<7?1:0;
        break;
    }
    case ES2_HOOK_INDEX_prefs_load: {static const uint32_t flags[]={0,1,0x100,0xffffff00u};a[0]=random32()%3?flags[random32()%4]:random32();break;}
    case ES2_HOOK_INDEX_prefs_store: {
        const unsigned count=random32()%8==0?random32()%40:random32()%30;
        a[0]=(random32()%4==0?random32()&0xffff0000u:0)|(random32()%16==0?(uint16_t)(0x10000u-1u-random32()%3):count);
        a[1]=b+CFX_LIST;
        for(unsigned i=0;i<64;++i){
            const unsigned k=random32()%32;
            const uint16_t index16=k<26?(uint16_t)(random32()%0x36):k<30?(uint16_t)(0x36+random32()%0x3a):(uint16_t)(0x10000u-1u-random32()%0x100u);
            put16(cfx_arena+CFX_LIST+2*i,index16);
        }
        break;
    }
    case ES2_HOOK_INDEX_sound_config: a[0]=b+CFX_OBJ;break;
    case ES2_HOOK_INDEX_tape_save: case ES2_HOOK_INDEX_tape_restore: {
        static const uint32_t lengths[]={0,1,4,499,500,501,999,1000,1001,1300};
        cfx_tape_length=random32()%3?lengths[random32()%10]:random32()%1400;
        a[0]=CFX_FILE_HANDLE|0x00800000u|(random32()&0xfff0u);
        a[1]=index==ES2_HOOK_INDEX_tape_restore&&random32()%8==0?0:b+CFX_PATH;
        cfx_length_pending=index==ES2_HOOK_INDEX_tape_restore;
        break;
    }
    default: break;
    }
}
static int cfx_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const cfx_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,cfx_calls);
    for(unsigned j=0;j<calls&&j<cfx_calls&&expected;++j)if(memcmp(expected+j,cfx_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(cfx_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u id%u word%u %08x/%08x\n",j,expected[j].id,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<CFX_SIZE;++j)if(cfx_after[j]!=cfx_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,cfx_after[j],cfx_arena[j]);break;}
    return 1;
}
/* Expected hits of a callee counter: the logged calls with its key, the
 * callbacks, and residue/unmodeled as the stubs and the case decided. */
static unsigned cfx_unmodeled(unsigned index,const uint32_t *a){
    if(index!=ES2_HOOK_INDEX_prefs_store)return 0;
    unsigned reads=0,hits=0;for(unsigned j=0;j<cfx_calls;++j)if(cfx_log[j].id==CF_READ)++reads;
    if(!reads)return 0;
    const int32_t count=(int16_t)a[0];
    for(int32_t i=0;i<count;++i)if(get16(cfx_host(a[1]+2u*(uint32_t)i))>=0x70u)++hits;
    return hits;
}
static unsigned cfx_expected_hits(const callee_counter *c,unsigned residue,unsigned unmodeled){
    if(c->key==ES2_CONFIG_RESIDUE)return residue;
    if(c->key==ES2_CONFIG_UNMODELED)return unmodeled;
    if(c->key==0x49630cu)return 1;   /* the frame setup runs as original code, unlogged, once */
    unsigned hits=0;
    for(unsigned j=0;j<cfx_calls;++j)if(c->key==ES2_CONFIG_CALLBACK?cfx_log[j].id>=CF_CALLBACK:cfx_addresses[cfx_log[j].id]==c->key)++hits;
    return hits;
}
static uint64_t cfx_core(unsigned index,const uint32_t *a,const uint32_t *r,uint32_t *ecx){
    BYTE scratch[ES2_CONFIG_SCRATCH]={0};es2_weapon_graph g=weapons_graph(mapped,scratch,index);g.observe=NULL;
    weapons_impact_context c={(uint32_t)(uintptr_t)mapped,(uint32_t)(uintptr_t)(scratch+ES2_CONFIG_FRAME)};
    g.call=weapons_impact_guest_call;g.context=&c;
    uint64_t v;
    switch(index){
    case ES2_HOOK_INDEX_prefs_video: return es2_prefs_video_registers(&math_native_memory,&g,a[0],r,ecx);
    case ES2_HOOK_INDEX_prefs_load: v=es2_prefs_load_registers(&math_native_memory,&g,a[0],r,ecx);cfx_set_fs0((uint32_t)v);return v;
    case ES2_HOOK_INDEX_prefs_store: v=es2_prefs_store_registers(&math_native_memory,&g,a[0],a[1],r,ecx);cfx_set_fs0((uint32_t)v);return v;
    case ES2_HOOK_INDEX_keyjoy_load: return es2_keyjoy_load_registers(&math_native_memory,&g,r,ecx);
    case ES2_HOOK_INDEX_sound_config: return es2_sound_config_registers(&math_native_memory,&g,a[0],r,ecx);
    case ES2_HOOK_INDEX_tape_save: return es2_tape_save_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
    default: return es2_tape_restore_registers(&math_native_memory,&g,a[0],a[1],r,ecx);
    }
}
static int cfx_compare(unsigned index,unsigned n,const uint32_t *a,const uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[CFX_GLOBALS],after[CFX_GLOBALS],actual[CFX_GLOBALS];static cfx_record expected[CFX_MAX];
    const uint32_t head=cfx_fs0(),length=cfx_tape_length;const int pending=cfx_length_pending;
    uint32_t out[3],core_ecx=0,fs_after;
    memcpy(cfx_before,cfx_arena,CFX_SIZE);cfx_globals(before,0);
    cfx_reset();cfx_length_pending=pending;cfx_machine(mapped+d->rva,a,r,out,0);
    fs_after=cfx_fs0();cfx_set_fs0(head);
    const unsigned calls=cfx_calls,residue=cfx_residue,unmodeled=cfx_unmodeled(index,a);
    memcpy(expected,cfx_log,calls*sizeof(*expected));
    memcpy(cfx_after,cfx_arena,CFX_SIZE);cfx_globals(after,0);
    uint64_t pair[2];pair[0]=(uint64_t)out[1]<<32|out[0];
    /* The adapter (the dispatcher's path in the integration pass). */
    memcpy(cfx_arena,cfx_before,CFX_SIZE);cfx_globals(before,1);cfx_reset();cfx_length_pending=pending;cfx_tape_length=length;
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[16];
    for(unsigned i=0;i<count&&i<16;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);
    const uint32_t fs_adapter=cfx_fs0();cfx_set_fs0(head);cfx_globals(actual,0);
    if(n<4096&&!rng_hooks[index].installed)for(unsigned i=0;i<count&&i<16;++i)
        if(c[i].count-counts[i]!=cfx_expected_hits(c+i,residue,unmodeled)){
            fprintf(stderr," counter %s delta %llu expected %u\n",c[i].name,(unsigned long long)(c[i].count-counts[i]),cfx_expected_hits(c+i,residue,unmodeled));
            return fail("config callee counter delta");
        }
    if(fs_after!=fs_adapter)return fail("config FS:0 after the original and the adapter differ");
    if(pair[0]!=pair[1]||calls!=cfx_calls||memcmp(expected,cfx_log,calls*sizeof(*expected))||memcmp(cfx_after,cfx_arena,CFX_SIZE)||
       memcmp(after,actual,CFX_GLOBALS)){
        for(unsigned j=0;j<CFX_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
        return cfx_report("adapter",n,pair,calls,expected);
    }
    /* The core with ECX. */
    memcpy(cfx_arena,cfx_before,CFX_SIZE);cfx_globals(before,1);cfx_reset();cfx_length_pending=pending;cfx_tape_length=length;
    pair[1]=cfx_core(index,a,r,&core_ecx);
    const uint32_t fs_core=cfx_fs0();cfx_set_fs0(head);cfx_globals(actual,0);
    const int ecx_known=!rng_hooks[index].installed;
    if(pair[0]!=pair[1]||(ecx_known&&core_ecx!=out[2])||fs_core!=fs_after||calls!=cfx_calls||memcmp(expected,cfx_log,calls*sizeof(*expected))||
       memcmp(cfx_after,cfx_arena,CFX_SIZE)||memcmp(after,actual,CFX_GLOBALS)){
        fprintf(stderr," ECX %08x/%08x\n",out[2],core_ecx);return cfx_report("core ECX",n,pair,calls,expected);
    }
    cfx_globals(before,1);
    return 0;
}
static int cfx_patch(BYTE saved[CFX_IDS][5],int active[CFX_IDS],int on){
    for(unsigned i=0;i<CFX_IDS;++i){
        if(on){active[i]=0;if(!cfx_addresses[i])continue;if(actor_patch(cfx_addresses[i]-0x400000u,cfx_stubs[i],saved[i],TRUE))return 1;active[i]=1;}
        else if(active[i]&&actor_patch(cfx_addresses[i]-0x400000u,NULL,saved[i],FALSE))return 1;
    }
    return 0;
}
static int cfx_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    cfx_mode=1;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;const uint32_t head=cfx_fs0();
        cfx_build(index,n,a,r);const int pending=cfx_length_pending;
        BYTE before[CFX_GLOBALS],after[CFX_GLOBALS],actual[CFX_GLOBALS],expected_fpu[108],actual_fpu[108];static cfx_record expected[CFX_MAX];
        memcpy(cfx_before,cfx_arena,CFX_SIZE);cfx_globals(before,0);
        cfx_reset();cfx_length_pending=pending;sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        const uint32_t fs_expected=cfx_fs0();cfx_set_fs0(head);
        unsigned calls=cfx_calls;memcpy(expected,cfx_log,calls*sizeof(*expected));memcpy(cfx_after,cfx_arena,CFX_SIZE);cfx_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("config poison install");
        memcpy(cfx_arena,cfx_before,CFX_SIZE);cfx_globals(before,1);cfx_reset();cfx_length_pending=pending;
        sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);const uint32_t fs_actual=cfx_fs0();cfx_set_fs0(head);cfx_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(fs_expected!=fs_actual||pair[0]!=pair[1]||calls!=cfx_calls||memcmp(expected,cfx_log,calls*sizeof(*expected))||
           memcmp(cfx_after,cfx_arena,CFX_SIZE)||memcmp(after,actual,CFX_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||
           memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=cfx_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("config poison uninstall");cfx_globals(before,1);++total;
    }
    cfx_mode=0;
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state/FS:0\n",d->name,total);
    return status;
}

/* ---- model pass: formats/cfg against the original -------------------- */
static char cfx_root[MAX_PATH];
static BYTE *cfx_read_cd(const char *relative,uint32_t *size){
    char path[MAX_PATH];snprintf(path,sizeof(path),"%s\\%s",cfx_root,relative);
    HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE)return NULL;
    DWORD high=0,low=GetFileSize(f,&high),got=0;BYTE *data=low!=INVALID_FILE_SIZE&&!high?malloc(low?low:1):NULL;
    if(data&&(!ReadFile(f,data,low,&got,NULL)||got!=low)){free(data);data=NULL;}
    CloseHandle(f);if(data)*size=low;return data;
}
static int cfx_model_setup(void){
    const char *root=getenv("ES2_DATA_ROOT");char probe[MAX_PATH];
    if(!root||!*root)return fail("config model pass needs ES2_DATA_ROOT");
    snprintf(probe,sizeof(probe),"%s/VER95/DBSIM.EXE",root);
    snprintf(cfx_root,sizeof(cfx_root),GetFileAttributesA(probe)!=INVALID_FILE_ATTRIBUTES?"%s":"%s\\extracted\\cd",root);
    if(!rtl_cw&&rtl_map_cw3220(root))return 1;
    if(!rtl_cw_export("_stricmp")||!rtl_cw_export("_atol"))return fail("config CW3220 exports");
    for(unsigned i=0;i<CFX_FILES;++i)if(!cfx_files[i].data&&!(cfx_files[i].data=calloc(1,CFX_FILE_SIZE)))return fail("config model files");
    return 0;
}
/* One model run of the original (unhooked) and the hook (installed,
 * replaced) from the same files and state: the files, the state, the
 * pair and FS:0 must agree. *state receives the global windows after the
 * original. */
typedef struct cfx_snapshot { BYTE data[CFX_FILE_SIZE]; uint32_t size,pos; int exists; char name[32]; } cfx_snapshot;
static cfx_snapshot cfx_files_before[CFX_FILES],cfx_files_after[CFX_FILES];
static void cfx_files_save(cfx_snapshot *s){
    for(unsigned i=0;i<CFX_FILES;++i){memcpy(s[i].data,cfx_files[i].data,cfx_files[i].size);s[i].size=cfx_files[i].size;s[i].pos=cfx_files[i].pos;s[i].exists=cfx_files[i].exists;memcpy(s[i].name,cfx_files[i].name,32);}
}
static void cfx_files_load(const cfx_snapshot *s){
    for(unsigned i=0;i<CFX_FILES;++i){memcpy(cfx_files[i].data,s[i].data,s[i].size);cfx_files[i].size=s[i].size;cfx_files[i].exists=s[i].exists;memcpy(cfx_files[i].name,s[i].name,32);cfx_files[i].pos=s[i].pos;}
}
static int cfx_files_equal(const cfx_snapshot *s){
    for(unsigned i=0;i<CFX_FILES;++i)if(s[i].size!=cfx_files[i].size||s[i].pos!=cfx_files[i].pos||s[i].exists!=cfx_files[i].exists||strcmp(s[i].name,cfx_files[i].name)||
        memcmp(s[i].data,cfx_files[i].data,s[i].size))return 0;
    return 1;
}
static int cfx_model_run(unsigned index,const uint32_t *a,unsigned calls_wanted,const uint32_t *a_list,BYTE *state,uint64_t *pair_out){
    const es2_hook_descriptor *d=function_hook_descriptor(index);
    BYTE before[CFX_GLOBALS],after[CFX_GLOBALS],actual[CFX_GLOBALS];const uint32_t head=cfx_fs0();
    uint32_t r[3]={0x81111111u,0x82222222u,0x83333333u},out[3],pair[2];uint64_t expected_pair=0,actual_pair=0;
    memcpy(cfx_before,cfx_arena,CFX_SIZE);cfx_globals(before,0);cfx_files_save(cfx_files_before);
    for(unsigned k=0;k<calls_wanted;++k){
        uint32_t b[6];memcpy(b,a,24);if(a_list)b[1]=a_list[k];
        cfx_reset();cfx_machine(mapped+d->rva,b,r,out,0);expected_pair=(uint64_t)out[1]<<32|out[0];
    }
    const uint32_t fs_expected=cfx_fs0();cfx_set_fs0(head);
    memcpy(cfx_after,cfx_arena,CFX_SIZE);cfx_globals(after,0);cfx_files_save(cfx_files_after);
    es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
    if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("config model install");
    memcpy(cfx_arena,cfx_before,CFX_SIZE);cfx_globals(before,1);cfx_files_load(cfx_files_before);
    for(unsigned k=0;k<calls_wanted;++k){
        uint32_t b[6];memcpy(b,a,24);if(a_list)b[1]=a_list[k];
        cfx_reset();sg_poison_call(mapped+d->rva,b,0xa7a7a7a7u,pair);actual_pair=(uint64_t)pair[1]<<32|pair[0];
    }
    const uint32_t fs_actual=cfx_fs0();cfx_set_fs0(head);cfx_globals(actual,0);
    const int files_equal=cfx_files_equal(cfx_files_after);
    if(!es2_hook_uninstall(&h))return fail("config model uninstall");
    if(fs_expected!=fs_actual||expected_pair!=actual_pair||!files_equal||memcmp(cfx_after,cfx_arena,CFX_SIZE)||memcmp(after,actual,CFX_GLOBALS)){
        fprintf(stderr,"FAIL: %s model run: pair %016llx/%016llx files %d\n",d->name,(unsigned long long)expected_pair,(unsigned long long)actual_pair,files_equal);
        for(unsigned j=0;j<CFX_GLOBALS;++j)if(after[j]!=actual[j]){fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);break;}
        return 1;
    }
    /* Leave the original's results in place for the formats checks. */
    memcpy(cfx_arena,cfx_after,CFX_SIZE);cfx_globals(after,1);cfx_files_load(cfx_files_after);
    if(state)memcpy(state,after,CFX_GLOBALS);
    if(pair_out)*pair_out=expected_pair;
    return 0;
}
static void cfx_model_fresh(void){
    memset(cfx_arena,0,CFX_SIZE);cfx_files_clear();cfx_write_fails=0;
    put32(mapped+0x2cbee4,(uint32_t)(uintptr_t)&cfx_counter);cfx_counter=0;
    memset(mapped+0xd1f38,0,0x36*3+0xd8);memset(mapped+0xd2530,0,0x50);mapped[0x9f254]=0;memset(mapped+0x9ea88,0,0x20);
    snprintf((char *)mapped+0x9e81c,0x20,"%s","data\\prefs.cfg");
}
static int cfx_model_pass(unsigned index){
    if(cfx_model_setup())return 1;
    uint32_t cd_prefs_size=0,cd_sound_size=0,cd_keyjoy_size=0,demo_size[3]={0,0,0};
    BYTE *cd_prefs=cfx_read_cd("DATA\\PREFS.CFG",&cd_prefs_size),*cd_sound=cfx_read_cd("DATA\\SOUND.CFG",&cd_sound_size),
         *cd_keyjoy=cfx_read_cd("DATA\\KEYJOY.CFG",&cd_keyjoy_size),*demo[3];
    for(unsigned i=0;i<3;++i){char name[32];snprintf(name,sizeof(name),"TAPES\\DEMO%u.TAP",i+1);demo[i]=cfx_read_cd(name,&demo_size[i]);}
    int status=0;unsigned runs=0,checks=0;
    if(!cd_prefs||!cd_sound||!cd_keyjoy||!demo[0]||!demo[1]||!demo[2]){status=fail("config model: CD files");goto done;}
    cfx_mode=2;
    const char *name=function_hook_descriptor(index)->name;
    if(index==ES2_HOOK_INDEX_prefs_video||index==ES2_HOOK_INDEX_prefs_load||index==ES2_HOOK_INDEX_prefs_store){
        for(unsigned v=0;v<40&&!status;++v){
            BYTE file[64];uint32_t size=v%8==0?cd_prefs_size:v%8==1?5:v%8==2?60:v%8==3?7:54;int exists=v%8!=4;
            if(v%8==0)memcpy(file,cd_prefs,size>64?64:size);else random_bytes(file,sizeof(file));
            if(v%3==0&&size>6){file[4]=(BYTE)(v%5);file[6]=(BYTE)(v&1);}
            cfx_model_fresh();cfx_file_set("data\\prefs.cfg",file,size,exists);
            es2_prefs prefs;es2_prefs_load(exists?(es2_span){file,size}:(es2_span){NULL,0},&prefs,NULL);
            if(index==ES2_HOOK_INDEX_prefs_video){
                const uint32_t a[6]={v%3==0?1u:v%3==1?3u:0u};BYTE state[CFX_GLOBALS];
                if(cfx_model_run(index,a,1,NULL,state,NULL)){status=1;break;}
                es2_prefs_video video;const int read=exists&&es2_prefs_video_read((es2_span){file,size},&video,NULL)==ES2_OK;
                const int mode=read?es2_prefs_video_mode(video.resolution):a[0]==1?0:3;
                ++checks;if(mapped[0xd2537]!=(mode?3:0)||get32(mapped+0xd255e)!=(read?video.display:0u)){status=fail("config model: prefs_video against es2_prefs_video_read");break;}
            } else if(index==ES2_HOOK_INDEX_prefs_load){
                const uint32_t a[6]={1};
                if(cfx_model_run(index,a,1,NULL,NULL,NULL)){status=1;break;}
                ++checks;if(memcmp(mapped+0xd1f38,prefs.byte,54)||memcmp(mapped+0xd1f6e,prefs.byte,54)||memcmp(mapped+0xd1fa4,prefs.byte,54)){
                    status=fail("config model: prefs_load against es2_prefs_load");break;}
            } else {
                if(size<54||!exists)continue; /* the original writes its stack residue there */
                static const uint32_t lists[3]={0x49e2dc,0x49e9a8,0};static const unsigned counts[3]={9,25,1};
                const unsigned k=v%3;uint32_t list=lists[k];uint8_t indices[25];
                if(!list){list=CFX_BASE+CFX_LIST;put16(cfx_arena+CFX_LIST,6);}
                for(unsigned i=0;i<counts[k];++i)indices[i]=(uint8_t)get16(cfx_host(list+2u*i));
                es2_prefs current;random_bytes(current.byte,54);memcpy(mapped+0xd1f38,current.byte,54);
                const uint32_t a[6]={counts[k],list};
                if(cfx_model_run(index,a,1,NULL,NULL,NULL)){status=1;break;}
                uint8_t expected[54];es2_prefs_overlay((es2_span){file,size},&current,indices,counts[k],expected,NULL);
                cfx_file *f=cfx_find("data\\prefs.cfg");
                ++checks;if(!f||f->size!=54||memcmp(f->data,expected,54)){status=fail("config model: prefs_store against es2_prefs_overlay");break;}
            }
            ++runs;
        }
    } else if(index==ES2_HOOK_INDEX_keyjoy_load||index==ES2_HOOK_INDEX_sound_config){
        const int keyjoy=index==ES2_HOOK_INDEX_keyjoy_load;
        static const char *const keyjoy_files[]={NULL,"[Keyjoy]\r\nTilt = Reverse\r\nBacktURN=reverse\r\nMissile =  REVERSE  \r\nRudder = Reversed\r\n",
            "[Keyjoy]\nTilt=\"Reverse\"\nRudder=Reverse\n;Missile=Reverse\n","[Other]\nTilt=Reverse\n","",
            "[keyjoy]\nmissile=Reverse\nbackturn=Default\n"};
        static const char *const sound_files[]={NULL,"[Sound]\r\nDriver = DirectSound\r\nBuffers = 64\r\nRate = 11\r\nWidth = Mono\r\n",
            "[Sound]\nDriver=directsound \nBuffers=65\nRate=22\nWidth=MONO\n","[Sound]\nBuffers=0\nRate= 11x\nWidth=Stereo\n",
            "[Sound]\nDriver=MME\nBuffers=-3\nRate=011\n","[Sound]\nBuffers=  7 \nRate=+11\nWidth=mono\n","[Sound]\nBuffers=abc\n",""};
        const unsigned variants=keyjoy?sizeof(keyjoy_files)/sizeof(keyjoy_files[0]):sizeof(sound_files)/sizeof(sound_files[0]);
        for(unsigned v=0;v<=variants&&!status;++v){
            cfx_model_fresh();
            const char *path=keyjoy?"data\\keyjoy.cfg":"data\\sound.cfg";es2_span file={NULL,0};
            if(v==0){file=keyjoy?(es2_span){cd_keyjoy,cd_keyjoy_size}:(es2_span){cd_sound,cd_sound_size};}
            else if(v<variants&&(keyjoy?keyjoy_files[v]:sound_files[v])){const char *t=keyjoy?keyjoy_files[v]:sound_files[v];file=(es2_span){(const BYTE *)t,strlen(t)};}
            if(v<variants&&(v==0||file.data))cfx_file_set(path,file.data,(uint32_t)file.size,1);
            if(keyjoy){
                const uint32_t a[6]={0};
                if(cfx_model_run(index,a,1,NULL,NULL,NULL)){status=1;break;}
                es2_cfg_keyjoy k;es2_cfg_keyjoy_parse(cfx_find(path)&&cfx_find(path)->exists?file:(es2_span){(const BYTE *)"",0},&k,NULL);
                ++checks;if(get32(mapped+0x9ea90)!=k.tilt||get32(mapped+0x9ea94)!=k.backturn||get32(mapped+0x9ea98)!=k.missile||get32(mapped+0x9ea9c)!=k.rudder){
                    fprintf(stderr," keyjoy variant %u: %u%u%u%u / %u%u%u%u\n",v,get32(mapped+0x9ea90),get32(mapped+0x9ea94),get32(mapped+0x9ea98),get32(mapped+0x9ea9c),
                        k.tilt,k.backturn,k.missile,k.rudder);
                    status=fail("config model: keyjoy_load against es2_cfg_keyjoy_parse");break;}
            } else {
                const uint32_t a[6]={CFX_BASE+CFX_OBJ};
                if(cfx_model_run(index,a,1,NULL,NULL,NULL)){status=1;break;}
                es2_cfg_sound s;es2_cfg_sound_parse(cfx_find(path)&&cfx_find(path)->exists?file:(es2_span){(const BYTE *)"",0},ES2_CFG_READER_DBSIM,&s,NULL);
                const BYTE *o=cfx_arena+CFX_OBJ;
                ++checks;if(get16(o+0x2a)!=s.driver||get16(o+0x30)!=s.buffers||get16(o+0x24)!=s.rate_code||get16(o+0x28)!=s.width_code||cfx_counter!=1){
                    fprintf(stderr," sound variant %u: %u %u %u %u / %u %u %u %u\n",v,get16(o+0x2a),get16(o+0x30),get16(o+0x24),get16(o+0x28),
                        s.driver,s.buffers,s.rate_code,s.width_code);
                    status=fail("config model: sound_config against es2_cfg_sound_parse");break;}
            }
            ++runs;
        }
    } else if(index==ES2_HOOK_INDEX_tape_save){
        /* Seven calls as the -r option makes them: the members of a CD tape
         * (one missing, one emptied) into a tape file; the tape's header
         * then parses to the same members. */
        for(unsigned v=0;v<6&&!status;++v){
            cfx_model_fresh();es2_tape_header h;
            if(es2_tape_header_parse((es2_span){demo[v%3],demo_size[v%3]},&h,NULL)!=ES2_OK){status=fail("config model: CD tape header");break;}
            for(unsigned i=0;i<7;++i)snprintf((char *)cfx_arena+CFX_NAMES+0x20*i,0x20,"%s",cfx_paths[i]);
            for(unsigned i=0;i<7;++i)cfx_file_set(cfx_paths[i],h.member[i].data,(uint32_t)h.member[i].size,i!=(v+1)%7);
            cfx_file *tape=cfx_slot("c1tape.tap");tape->exists=1;tape->size=tape->pos=0;
            uint32_t paths[7];for(unsigned i=0;i<7;++i)paths[i]=CFX_BASE+CFX_NAMES+0x20*i;
            const uint32_t a[6]={cfx_handle(tape)};
            if(cfx_model_run(index,a,7,paths,NULL,NULL)){status=1;break;}
            tape=cfx_find("c1tape.tap");es2_tape_header t;
            ++checks;if(es2_tape_header_parse((es2_span){tape->data,tape->size},&t,NULL)!=ES2_OK||t.size!=tape->size){status=fail("config model: tape_save header parse");break;}
            for(unsigned i=0;i<7;++i){
                const es2_span want=i==(v+1)%7?(es2_span){NULL,0}:h.member[i];
                ++checks;if(t.member[i].size!=want.size||(want.size&&memcmp(t.member[i].data,want.data,want.size))){status=fail("config model: tape_save member");break;}
            }
            ++runs;
        }
    } else {
        /* Seven calls as -p makes them: the CD tapes' headers restored to
         * the playback names (prefs and keyjoy below tapes\); a NULL path
         * skips its member. */
        static const char *const play[7]={"data\\script.dat","data\\player.mec","data\\mission.var","tapes\\prefs.cfg",
            "data\\restore.dat","data\\object.str","tapes\\keyjoy.cfg"};
        for(unsigned v=0;v<6&&!status;++v){
            cfx_model_fresh();es2_tape_header h;
            if(es2_tape_header_parse((es2_span){demo[v%3],demo_size[v%3]},&h,NULL)!=ES2_OK){status=fail("config model: CD tape header");break;}
            for(unsigned i=0;i<7;++i)snprintf((char *)cfx_arena+CFX_NAMES+0x20*i,0x20,"%s",play[i]);
            cfx_file_set("demo.tap",demo[v%3],demo_size[v%3]<CFX_FILE_SIZE?demo_size[v%3]:CFX_FILE_SIZE,1);
            uint32_t paths[7];for(unsigned i=0;i<7;++i)paths[i]=v>=3&&i==v?0:CFX_BASE+CFX_NAMES+0x20*i;
            const uint32_t a[6]={cfx_handle(cfx_find("demo.tap"))};
            if(cfx_model_run(index,a,7,paths,NULL,NULL)){status=1;break;}
            for(unsigned i=0;i<7;++i){
                cfx_file *f=cfx_find(play[i]);
                ++checks;if(paths[i]?!f||!f->exists||f->size!=h.member[i].size||memcmp(f->data,h.member[i].data,f->size):f!=NULL){status=fail("config model: tape_restore member");break;}
            }
            ++checks;if(cfx_find("demo.tap")->pos!=h.size){status=fail("config model: tape_restore position");break;}
            ++runs;
        }
    }
    if(!status)printf("PASS: %s model pass: %u runs of the original and the installed hook on the CD files and variants (files, state, pair equal), %u checks against formats/cfg\n",
        name,runs,checks);
done:
    cfx_mode=0;free(cd_prefs);free(cd_sound);free(cd_keyjoy);for(unsigned i=0;i<3;++i)free(demo[i]);
    return status;
}
static int config_cases(unsigned index,unsigned limit){
    BYTE saved[CFX_IDS][5],old_globals[CFX_GLOBALS];int active[CFX_IDS];
    cfx_globals(old_globals,0);cfx_hook=index;cfx_mode=0;
    if(cfx_patch(saved,active,1))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_CONFIG_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];cfx_build(index,n,a,r);status=cfx_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    current_case_valid=0;
    if(!limit&&!status)status=cfx_installed_probes(index,64);
    if(!limit&&!status)status=cfx_model_pass(index);
    if(cfx_patch(saved,active,0))return 1;
    cfx_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair and ECX, ordered calls with slots, entry registers, frame-relative addresses, written bytes and read strings, arena/globals/FS:0, callee counters\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
