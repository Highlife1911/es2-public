/* Terrain draw pass helpers: poly_clip 472bcc, poly_spans 472b14, view_rect
 * 46d324, cell_uv 46bdf8 (docs/reverse-engineering/terrain.md). Direct cases
 * run the original body and the core on one guarded arena and compare EDX:EAX,
 * the whole arena and the globals the functions read (0x6b4f40..0x6b4f4b,
 * 0x4a0be0). Buffers overlap on purpose in a share of the cases (output over
 * input, extent over the span list, points over the map fields or the texture
 * entry), because the core reproduces the original's read/write order.
 * poly_spans' only callee, the scan converter 4930c1, is a logging stub (count,
 * point payload by content, span buffer address, entry EAX/EDX/ECX) that writes
 * a generated span list; a second pass runs the real converter. Then installed
 * 31/a7 poison probes. */
typedef struct wd_record { uint32_t args[3],regs[3],input[33]; } wd_record;
enum { WD_SIZE=0x1000, WD_IN=0x00, WD_OUT=0x10, WD_EXTENT=0x40, WD_POINTS=0x100, WD_OUTPOINTS=0x400, WD_SPANS=0x800,
    WD_MAP=0x600, WD_TABLE=0xa00, WD_MAX=4, WD_GLOBALS=16 };
#define WD_BASE ((uint32_t)(uintptr_t)wd_arena)
static BYTE wd_arena[WD_SIZE],wd_before[WD_SIZE],wd_after[WD_SIZE];
static wd_record wd_log[WD_MAX];
static unsigned wd_calls;
static uint32_t wd_seed,wd_stream;
__attribute__((used)) static uint32_t wd_ecx;
static uint32_t wd_next(void){
    uint32_t z=wd_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);
}
static BYTE *wd_host(uint32_t a){return (BYTE *)(uintptr_t)a;}
static int wd_in_arena(uint32_t a,uint32_t size){return a-WD_BASE<=WD_SIZE-size;}
/* 4930c1(count, points, spans): records its inputs, then writes a span list
 * (first row, row count, 2 * rows x values) or, in a share of the calls,
 * nothing (as the real converter for count 0). */
__attribute__((used,noinline)) static uint64_t __cdecl wd_stub_impl(const uint32_t *regs,const uint32_t *a){
    if(wd_calls==WD_MAX){fail("world synthetic call overflow");ExitProcess(1);}
    wd_record *v=wd_log+wd_calls++;memset(v,0,sizeof(*v));memcpy(v->args,a,12);memcpy(v->regs,regs,12);
    uint32_t count=a[0];v->input[0]=count;
    if(wd_in_arena(a[1],8))for(unsigned j=0;j<16&&j<count&&wd_in_arena(a[1]+8*j,8);++j){v->input[1+2*j]=get32(wd_host(a[1]+8*j));v->input[2+2*j]=get32(wd_host(a[1]+8*j+4));}
    uint32_t pick=wd_next(),eax=wd_next(),edx=wd_next();wd_ecx=wd_next();
    if(pick%8&&wd_in_arena(a[2],8)){
        static const uint32_t rows[]={0,1,2,3,5,8,16,0xffffffffu,0x80000000u,0xfffffff0u};
        uint32_t n=rows[(pick>>3)%10];
        if(!wd_in_arena(a[2]+8,n<=16?8*n:0))n=0;
        put32(wd_host(a[2]),wd_next());put32(wd_host(a[2]+4),n);
        for(uint32_t j=0;n<=16&&j<2*n;++j)put32(wd_host(a[2]+8+4*j),wd_next());
    }
    return (uint64_t)edx<<32|eax;
}
__attribute__((naked)) static void wd_stub(void){__asm__ volatile(
    "pushl %ecx\n\tpushl %edx\n\tpushl %eax\n\tleal 16(%esp),%eax\n\tpushl %eax\n\tleal 4(%esp),%eax\n\tpushl %eax\n\t"
    "calll _wd_stub_impl\n\taddl $20,%esp\n\tmovl _wd_ecx,%ecx\n\tret\n\t");}
/* The texture table pointer and the eye position (0x6b4f40..4b), the grid
 * pointer 0x4a0be0. */
static void wd_globals(BYTE *out,int restore){
    static const unsigned offsets[]={0x2b4f40,0xa0be0},sizes[]={12,4};
    for(unsigned i=0;i<2;++i){if(restore)memcpy(mapped+offsets[i],out,sizes[i]);else memcpy(out,mapped+offsets[i],sizes[i]);out+=sizes[i];}
}
static void wd_reset(void){wd_calls=0;wd_stream=wd_seed;}
/* Coordinates: around a line, on it, small, large, extremes. */
static uint32_t wd_coordinate(uint32_t line){
    switch(random32()%8){
    case 0:return line;
    case 1:return line+random32()%7u-3u;
    case 2:return line+random32()%2001u-1000u;
    case 3:return random32()%3?0x7fffffffu-random32()%4u:0x80000000u+random32()%4u;
    case 4:return random32();
    default:return random32()%65536u-32768u;
    }
}
/* The only original fault: a divisor of -1 needs one point at line+INT_MAX
 * and the other at line+INT_MIN on the clipping axis; keep the clipping
 * coordinate off line+INT_MIN (terrain.md, poly_clip divisor). */
static uint32_t wd_safe(uint32_t c,uint32_t line){return c-line==0x80000000u?c+1u:c;}
static void wd_build(unsigned index,unsigned n,uint32_t a[6],uint32_t r[3]){
    uint32_t b=WD_BASE;wd_seed=random32();random_bytes(wd_arena,WD_SIZE);memset(a,0,24);
    for(unsigned i=0;i<3;++i)r[i]=random32();
    if(index==ES2_HOOK_INDEX_poly_clip){
        static const uint32_t counts[]={0,1,2,3,4,5,6,8,0xffffffffu,0x80000000u};
        uint32_t count=n<1024?counts[n%10]:random32()%5==0?counts[random32()%10]:1u+random32()%20u;
        uint32_t keep=random32(),axis=random32(),line;
        if(n<1024){keep=(keep&~0xffu)|(n>>4&1u);axis=(axis&~0xffu)|(n>>5&1u);}
        else if(random32()%2)keep&=~0xffu;
        if(n>=1024&&random32()%2)axis&=~0xffu;
        switch(random32()%4){case 0:line=0;break;case 1:line=random32();break;default:line=random32()%4096u-2048u;}
        uint32_t points=b+WD_POINTS,outpoints=b+WD_OUTPOINTS,out=b+WD_OUT;
        int x_line=(uint8_t)axis!=0;
        for(uint32_t j=0;j<count&&j<20;++j){uint32_t x=wd_coordinate(line),y=wd_coordinate(line);
            if(random32()%4==0){x=wd_coordinate(line);y=x;}
            if(x_line)x=wd_safe(x,line);else y=wd_safe(y,line);
            put32(wd_arena+WD_POINTS+8*j,x);put32(wd_arena+WD_POINTS+8*j+4,y);}
        /* Overlaps: in place, shifted by one point, the same record. */
        switch(n<1024?n%8:random32()%8){
        case 0:outpoints=points;break;
        case 1:outpoints=points+8u;break;
        case 2:out=b+WD_IN;break;
        }
        put32(wd_arena+WD_IN,count);put32(wd_arena+WD_IN+4,points);
        if(out!=b+WD_IN)put32(wd_host(out)+4,outpoints);
        a[0]=keep;a[1]=axis;a[2]=line;a[3]=b+WD_IN;a[4]=out;return;
    }
    if(index==ES2_HOOK_INDEX_poly_spans){
        /* INT_MIN would make the minimum loop run 2^31 - 1 times (in the
         * original too); INT_MIN + 1 covers the negative extreme. */
        static const uint32_t counts[]={0,1,2,3,4,7,0xffffffffu,0x80000001u};
        uint32_t count=n<1024?counts[n%8]:random32()%6==0?counts[random32()%8]:1u+random32()%16u;
        for(uint32_t j=0;j<count&&j<16;++j)for(unsigned k=0;k<2;++k){uint32_t c;
            switch(random32()%4){case 0:c=random32();break;case 1:c=0x7fffffffu-random32()%3u;break;default:c=random32()%512u-256u;}
            put32(wd_arena+WD_POINTS+8*j+4*k,c);}
        uint32_t spans=b+WD_SPANS,extent=b+WD_EXTENT;
        /* A span list the converter stub may leave untouched: a bounded or
         * negative row count. Overlaps: extent over the first row word (its
         * +8 rewrites the row count with itself) or over the x values. */
        static const uint32_t rows[]={0,1,2,5,16,0xffffffffu,0x80000000u};
        put32(wd_arena+WD_SPANS,random32());put32(wd_arena+WD_SPANS+4,rows[random32()%7]);
        switch(n<1024?n%6:random32()%6){case 0:extent=spans-4u;break;case 1:extent=spans+8u+4u*(random32()%4);break;}
        put32(wd_arena+WD_IN,count);put32(wd_arena+WD_IN+4,b+WD_POINTS);
        a[0]=b+WD_IN;a[1]=spans;a[2]=extent;return;
    }
    if(index==ES2_HOOK_INDEX_view_rect){
        BYTE *map=wd_arena+WD_MAP;
        uint32_t shift=random32()%3?random32()%13:random32(),cols=random32()%3?random32()%9:random32(),rows=random32()%3?random32()%9:random32();
        put32(map+0x108,shift);put32(map+0x100,cols);put32(map+0x104,rows);
        put32(map+0x10c,random32()%3?random32()%16:random32());
        uint32_t ox=random32()%2?random32()%4096u-2048u:random32(),oy=random32()%2?random32()%4096u-2048u:random32();
        put32(map+0xf4,ox);put32(map+0xf8,oy);put32(map+0xfc,random32());
        uint32_t s=shift&31u,r=get32(map+0x10c)<<s;
        uint32_t xmax=(((1u<<(cols&31u))-2u)<<s)+ox,ymax=(((1u<<(rows&31u))-2u)<<s)+oy;
        uint32_t ex,ey;
        /* Eyes that put x0/x1, y0/y1 exactly on their clamps, or anywhere. */
        switch(random32()%6){case 0:ex=ox+r;ey=oy+r;break;case 1:ex=xmax-r;ey=ymax-r;break;
            case 2:ex=ox+r+random32()%3u-1u;ey=ymax-r+random32()%3u-1u;break;case 3:ex=random32();ey=random32();break;
            default:ex=ox+random32()%(xmax-ox+1u?xmax-ox+1u:1u);ey=oy+random32()%(ymax-oy+1u?ymax-oy+1u:1u);}
        put32(mapped+0x2b4f44,ex);put32(mapped+0x2b4f48,ey);
        uint32_t polygon=b+WD_OUT,points=b+WD_OUTPOINTS;
        switch(n<1024?n%8:random32()%8){
        case 0:points=b+WD_MAP+0xec+4u*(random32()%8);break;   /* over the map fields, incl. +fc */
        case 1:points=polygon-4u*(1u+random32()%4);break;          /* over the polygon record */
        }
        put32(wd_host(polygon)+4,points);
        a[0]=b+WD_MAP;a[1]=polygon;return;
    }
    /* cell_uv: the entry address is chosen inside the arena and the table
     * pointer derived from the (full-range) index, so every index works. */
    uint32_t index_value=random32()%4?random32()%64u:random32(),entry=b+WD_TABLE+4u*(random32()%64);
    put32(mapped+0x2b4f40,entry-20u*index_value);put32(mapped+0xa0be0,b+WD_MAP);
    put32(wd_arena+WD_MAP+0x108,random32()%3?6u+random32()%8u:random32());
    static const uint32_t levels[]={0,0,1,2,3,13,0xffffffffu,0x80000000u};
    uint32_t level=n<1024?levels[n%8]:random32()%3?levels[random32()%8]:random32();
    put32(wd_arena+WD_IN,index_value);put32(wd_arena+WD_IN+4,level);
    uint32_t out=b+WD_OUTPOINTS;
    if((n<1024?n%4:random32()%4)==0)out=entry+4u*(random32()%12)-28u;     /* over the entry and its word +10h */
    a[0]=b+WD_IN;a[1]=random32()%2?random32():random32()%512u;a[2]=random32()%2?random32():random32()%512u;a[3]=out;
}
static int wd_report(const char *what,unsigned n,const uint64_t pair[2],unsigned calls,const wd_record *expected){
    fprintf(stderr,"FAIL: %s case %u (%s) pair %016llx/%016llx calls %u/%u\n",current_function,n,what,
        (unsigned long long)pair[0],(unsigned long long)pair[1],calls,wd_calls);
    for(unsigned j=0;j<calls&&j<wd_calls&&expected;++j)if(memcmp(expected+j,wd_log+j,sizeof(*expected))){
        const uint32_t *e=(const uint32_t *)(expected+j),*f=(const uint32_t *)(wd_log+j);
        for(unsigned k=0;k<sizeof(*expected)/4;++k)if(e[k]!=f[k])fprintf(stderr," call%u word%u %08x/%08x\n",j,k,e[k],f[k]);
        break;
    }
    for(unsigned j=0;j<WD_SIZE;++j)if(wd_after[j]!=wd_arena[j]){fprintf(stderr," arena +%x %02x/%02x\n",j,wd_after[j],wd_arena[j]);break;}
    return 1;
}
static int wd_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    BYTE before[WD_GLOBALS],after[WD_GLOBALS],actual[WD_GLOBALS];wd_record expected[WD_MAX];
    memcpy(wd_before,wd_arena,WD_SIZE);wd_globals(before,0);
    wd_reset();uint64_t pair[2];pair[0]=weapons_original(index,a,r);unsigned calls=wd_calls;memcpy(expected,wd_log,calls*sizeof(*expected));
    memcpy(wd_after,wd_arena,WD_SIZE);wd_globals(after,0);
    memcpy(wd_arena,wd_before,WD_SIZE);wd_globals(before,1);wd_reset();
    unsigned count;callee_counter *c=weapons_graph_counters(index,&count);uint64_t counts[2]={0,0};for(unsigned i=0;i<count;++i)counts[i]=c[i].count;
    pair[1]=x->replacement_context(a,d->user,r);wd_globals(actual,0);
    if(n<4096)for(unsigned i=0;i<count;++i)if(c[i].count-counts[i]!=wd_calls)return fail("world callee counter delta");
    if(pair[0]!=pair[1]||calls!=wd_calls||memcmp(expected,wd_log,calls*sizeof(*expected))||memcmp(wd_after,wd_arena,WD_SIZE)||memcmp(after,actual,WD_GLOBALS)){
        for(unsigned j=0;j<WD_GLOBALS;++j)if(after[j]!=actual[j])fprintf(stderr," global byte %u %02x/%02x\n",j,after[j],actual[j]);
        return wd_report("direct",n,pair,calls,expected);
    }
    return 0;
}
static int wd_installed_probes(unsigned index,unsigned states){
    const es2_hook_descriptor *d=function_hook_descriptor(index);BYTE saved_fpu[108];es2_hook_fpu_save(saved_fpu);
    __asm__ volatile("fninit; fld1; fldpi");int status=0;unsigned total=0;
    for(unsigned fast=0;fast<2&&!status;++fast)for(unsigned poison=0;poison<2&&!status;++poison)for(unsigned n=0;n<states&&!status;++n){
        uint32_t a[6],r[3],expected_pair[2],actual_pair[2],pattern=poison?0xa7a7a7a7u:0x31313131u;wd_build(index,n,a,r);
        BYTE before[WD_GLOBALS],after[WD_GLOBALS],actual[WD_GLOBALS],expected_fpu[108],actual_fpu[108];wd_record expected[WD_MAX];
        memcpy(wd_before,wd_arena,WD_SIZE);wd_globals(before,0);
        wd_reset();sg_poison_call(mapped+d->rva,a,pattern,expected_pair);es2_hook_fpu_save(expected_fpu);
        unsigned calls=wd_calls;memcpy(expected,wd_log,calls*sizeof(*expected));memcpy(wd_after,wd_arena,WD_SIZE);wd_globals(after,0);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=ES2_HOOK_REPLACED;
        es2_hook_set_fast_path(fast);if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("world poison install");
        memcpy(wd_arena,wd_before,WD_SIZE);wd_globals(before,1);wd_reset();sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        es2_hook_fpu_save(actual_fpu);wd_globals(actual,0);
        uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
        if(pair[0]!=pair[1]||calls!=wd_calls||memcmp(expected,wd_log,calls*sizeof(*expected))||memcmp(wd_after,wd_arena,WD_SIZE)||
           memcmp(after,actual,WD_GLOBALS)||memcmp(expected_fpu,actual_fpu,12)||memcmp(expected_fpu+28,actual_fpu+28,80)||h.calls!=1)
            status=wd_report(fast?"installed fast":"installed preserving",n,pair,calls,expected);
        if(!es2_hook_uninstall(&h))return fail("world poison uninstall");++total;
    }
    es2_hook_set_fast_path(TRUE);__asm__ volatile("frstor %0"::"m"(saved_fpu));
    if(!status)printf("PASS: %s %u installed 31/a7 cases, fast/preserving dispatch, nonempty x87, full pair/trace/state\n",d->name,total);
    return status;
}
/* poly_spans over the real scan converter 4930c1 (original code, no stub):
 * convex and arbitrary polygons of 0..10 points within 64 units of a
 * random corner, so the span list stays inside the arena; the caller hook
 * in original and replaced mode, 31/a7 stack, full pair and memory. */
static int wd_real_pass(unsigned index){
    const es2_hook_descriptor *d=function_hook_descriptor(index);int status=0;unsigned total=0,rows=0;
    for(unsigned mode=0;mode<2&&!status;++mode)for(unsigned n=0;n<1536&&!status;++n){
        uint32_t a[6]={0},expected_pair[2],actual_pair[2],pattern=n&1?0xa7a7a7a7u:0x31313131u,b=WD_BASE;
        random_bytes(wd_arena,WD_SIZE);
        uint32_t count=n%11,cx=random32()%2?random32()%4096u-2048u:random32(),cy=random32()%4096u-2048u;
        static const int32_t ring[][2]={{0,0},{40,-10},{60,20},{50,55},{20,63},{-5,40},{-10,10},{10,-5},{30,-12},{55,5}};
        for(uint32_t j=0;j<count;++j){int32_t px,py;
            if(n%3){px=ring[j][0];py=ring[j][1];}else{px=(int32_t)(random32()%65u);py=(int32_t)(random32()%65u);}
            put32(wd_arena+WD_POINTS+8*j,cx+(uint32_t)px);put32(wd_arena+WD_POINTS+8*j+4,cy+(uint32_t)py);}
        put32(wd_arena+WD_IN,count);put32(wd_arena+WD_IN+4,b+WD_POINTS);
        /* The converter writes nothing for count 0: leave a valid empty list. */
        put32(wd_arena+WD_SPANS+4,0);
        a[0]=b+WD_IN;a[1]=b+WD_SPANS;a[2]=b+WD_EXTENT;
        memcpy(wd_before,wd_arena,WD_SIZE);
        sg_poison_call(mapped+d->rva,a,pattern,expected_pair);memcpy(wd_after,wd_arena,WD_SIZE);
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;
        o.mode=mode?ES2_HOOK_REPLACED:ES2_HOOK_ORIGINAL;
        if(!weapons_install_hook(&h,d,&o,function_hook_extension(index)))return fail("world real install");
        memcpy(wd_arena,wd_before,WD_SIZE);sg_poison_call(mapped+d->rva,a,pattern,actual_pair);
        if(memcmp(expected_pair,actual_pair,8)||memcmp(wd_after,wd_arena,WD_SIZE)){
            uint64_t pair[2]={(uint64_t)expected_pair[1]<<32|expected_pair[0],(uint64_t)actual_pair[1]<<32|actual_pair[0]};
            fprintf(stderr,"real mode=%u\n",mode);wd_calls=0;status=wd_report("real",n,pair,0,NULL);
        }
        if(count)rows+=get32(wd_after+WD_SPANS+4);
        if(!es2_hook_uninstall(&h))return fail("world real uninstall");++total;
    }
    if(!status&&!rows){fprintf(stderr,"FAIL: %s real converter wrote no rows\n",d->name);status=1;}
    if(!status)printf("PASS: %s %u real-converter cases (%u rows), original/replaced, 31/a7 stack, full pair and memory\n",d->name,total,rows);
    return status;
}
static int world_cases(unsigned index,unsigned limit){
    static BYTE old_globals[WD_GLOBALS];BYTE saved[5];int spans=index==ES2_HOOK_INDEX_poly_spans;
    wd_globals(old_globals,0);if(spans&&actor_patch(0x930c1,(void *)wd_stub,saved,TRUE))return 1;
    unsigned cases=case_budget(limit,1001024);const char *quick=getenv("ES2_INVITRO_WORLD_CASES");if(!limit&&quick)cases=(unsigned)strtoul(quick,NULL,10);
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==16384)coverage_enable(0);
        uint32_t a[6],r[3];wd_build(index,n,a,r);status=wd_compare(index,n,a,r);}
    if(!limit)coverage_enable(0);
    if(!limit&&!status)status=wd_installed_probes(index,64);
    if(spans&&actor_patch(0x930c1,NULL,saved,FALSE))return 1;
    current_case_valid=0;
    if(!limit&&!status&&spans)status=wd_real_pass(index);
    wd_globals(old_globals,1);
    if(!status)printf("PASS: %-20s %u cases; full return pair, converter calls with entry registers and payload, arena and globals\n",
        function_hook_descriptor(index)->name,cases);
    return status;
}
