/* Swept-query leaves 426b04 and 427238 on generated grids and words. */
typedef struct sl_record { uint32_t args[2],word,eax,edx; } sl_record;
static BYTE sl_arena[0x1000],sl_before[0x1000],sl_after[0x1000];
static sl_record sl_log[2];
static unsigned sl_calls;
static uint32_t sl_destination,sl_stream;
__attribute__((used)) static uint32_t sl_ecx;
static uint32_t sl_next(void){uint32_t z=sl_stream+=0x9e3779b9u;z=(z^(z>>16))*0x85ebca6bu;z=(z^(z>>13))*0xc2b2ae35u;return z^(z>>16);}
/* Q10 multiply stand-in: records both slots and the destination word, which
 * must still hold the old value when the callee runs. */
__attribute__((used,noinline)) static uint64_t __cdecl sl_multiply_impl(const uint32_t *a){
    if(sl_calls==2){fail("side_scale call overflow");ExitProcess(1);}
    sl_record *v=sl_log+sl_calls++;v->args[0]=a[0];v->args[1]=a[1];v->word=get16((BYTE *)(uintptr_t)sl_destination);
    v->eax=sl_next();v->edx=sl_next();sl_ecx=sl_next();return (uint64_t)v->edx<<32|v->eax;
}
__attribute__((naked)) static void sl_multiply_stub(void){__asm__ volatile(
    "leal 4(%esp),%eax\n\tpushl %eax\n\tcalll _sl_multiply_impl\n\taddl $4,%esp\n\tmovl _sl_ecx,%ecx\n\tret\n\t");}
static void side_scale_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint32_t sides[]={0,1,0x80000000u,0x10000u,0xffffffffu,0x100u};
    static const uint16_t difficulties[]={0,1,2,3,0xffff,0x8000,0x7fff,4};
    random_bytes(sl_arena,64);memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    sl_destination=(uint32_t)(uintptr_t)(sl_arena+1+n%7);a[0]=sl_destination;
    a[1]=n<4096?sides[n%6]:random32()%3?0:random32();
    put16(mapped+0xa9e5c,n<4096?difficulties[(n/6)%8]:(uint16_t)(random32()%5?random32()%4:random32()));
    sl_stream=random32();
}
static int side_scale_compare(unsigned index,unsigned n,const uint32_t *a,uint32_t *r,int stubbed){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    uint32_t seed=sl_stream;sl_record expected[2];memcpy(sl_before,sl_arena,64);
    sl_calls=0;uint64_t want=weapons_original(index,a,r);unsigned calls=sl_calls;memcpy(expected,sl_log,sizeof(expected));memcpy(sl_after,sl_arena,64);
    memcpy(sl_arena,sl_before,64);sl_stream=seed;sl_calls=0;uint64_t got=x->replacement_context(a,d->user,r);
    if(want!=got||calls!=sl_calls||memcmp(expected,sl_log,calls*sizeof(*expected))||memcmp(sl_after,sl_arena,64)||(stubbed&&calls!=1)){
        fprintf(stderr,"FAIL: side_scale case %u pair %016llx/%016llx calls %u/%u\n",n,(unsigned long long)want,(unsigned long long)got,calls,sl_calls);return 1;
    }
    return 0;
}
static int side_scale_cases(unsigned index,unsigned limit){
    BYTE saved[5];uint16_t difficulty=get16(mapped+0xa9e5c);unsigned cases=case_budget(limit,1001024);
    if(actor_patch(0x7e0b0,sl_multiply_stub,saved,TRUE))return 1;
    int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==4096)coverage_enable(0);
        uint32_t a[6],r[3];side_scale_build(n,a,r);status=side_scale_compare(index,n,a,r,1);}
    if(!limit)coverage_enable(0);
    if(actor_patch(0x7e0b0,NULL,saved,FALSE))return 1;
    /* The real Q10 multiply under all three of its own hook modes. */
    unsigned real=0;
    for(unsigned mode=0;mode<3&&!status&&!limit;++mode){
        es2_hook h={0};es2_hook_options o={0};o.image=mapped;o.expected_base=0x400000;o.expected_image_size=0x2d8000;o.mode=(es2_hook_mode)mode;
        if(!weapons_install_hook(&h,function_hook_descriptor(ES2_HOOK_INDEX_multiply_q10),&o,function_hook_extension(ES2_HOOK_INDEX_multiply_q10)))return fail("side_scale multiply install");
        for(unsigned n=0;n<4096&&!status;++n){uint32_t a[6],r[3];side_scale_build(n,a,r);status=side_scale_compare(index,n,a,r,0);++real;}
        if(h.mismatches)status=fail("side_scale multiply compare mismatch");
        if(!es2_hook_uninstall(&h))return fail("side_scale multiply uninstall");
    }
    put16(mapped+0xa9e5c,difficulty);current_case_valid=0;
    if(!status)printf("PASS: %-20s %u cases; full pair, callee slots and order, destination word, plus %u real Q10 cases in three modes\n",
        function_hook_descriptor(index)->name,cases,real);
    return status;
}

enum { CELL_RESOURCE=0x10, CELL_ROWS=0x500, CELL_DATA=0x600, CELL_MAX=48 };
static uint16_t cell_word(uint32_t value,uint32_t shift){shift&=31u;return (uint16_t)(shift?(uint32_t)((int32_t)value>>shift):value);}
/* Keeps the generated domain finite and readable: no upper bound of 32767
 * and a bounded rectangle. The original has the same precondition. */
static int cell_domain(uint32_t x,uint32_t y,uint32_t shift,uint32_t radius_slot){
    uint32_t threshold=1u<<(((uint32_t)(int16_t)shift-1u)&31u);int32_t radius=(int16_t)radius_slot;
    if((int32_t)threshold>=radius)return 1;
    int16_t x0=(int16_t)cell_word(x-(uint32_t)radius,shift),x1=(int16_t)cell_word(x+(uint32_t)radius,shift);
    int16_t y0=(int16_t)cell_word(y-(uint32_t)radius,shift),y1=(int16_t)cell_word(y+(uint32_t)radius,shift);
    if(x1==32767||y1==32767)return 0;
    int64_t w=x1>=x0?(int64_t)x1-x0+1:0,h=y1>=y0?(int64_t)y1-y0+1:0;
    return y1<y0||w*h<=256;
}
static void cell_build(unsigned n,uint32_t a[6],uint32_t r[3]){
    static const uint16_t shifts[]={0,1,2,4,8,12,16,20,31,32,33,0xffff,0x8000,3,5,6};
    static const uint32_t corners[]={0,0x7fffffffu,0x80000000u,0xffffffffu,0x7ffff000u,0x80000800u};
    BYTE *resource=sl_arena+CELL_RESOURCE;uint32_t b=(uint32_t)(uintptr_t)sl_arena;
    random_bytes(sl_arena,sizeof(sl_arena));memset(a,0,24);for(unsigned i=0;i<3;++i)r[i]=random32();
    for(;;){
        uint16_t shift=n<4096?shifts[n%16]:random32()%4?(uint16_t)(random32()%14):shifts[random32()%16];
        int16_t width=(int16_t)(random32()%(CELL_MAX+4))-3,height=(int16_t)(random32()%(CELL_MAX+4))-3;
        unsigned s=shift&31u;uint32_t span=s<26?(uint32_t)CELL_MAX<<s:0x40000000u;
        uint32_t x=random32()%8?(random32()%(span+(span>>2)+1))-(span>>3):corners[random32()%6]+random32()%64;
        /* Shifted coordinates straddling the signed word limit give an empty
         * column range under a non-empty row range. */
        if(random32()%16==0)x=((uint32_t)32767<<s)+(random32()&((1u<<s)-1u));
        uint32_t y=random32()%8?(random32()%(span+(span>>2)+1))-(span>>3):corners[random32()%6]+random32()%64;
        uint32_t threshold=1u<<(((uint32_t)(int16_t)shift-1u)&31u);
        int32_t edge=(int32_t)threshold;uint32_t low;
        switch(random32()%6){
        case 0:low=(uint32_t)(edge>32767?32767:edge<-32768?-32768:edge);break;
        case 1:low=(uint32_t)(edge>32766?32767:edge<-32768?-32768:edge+1);break;
        case 2:low=random32()%(s<5?40u:(uint32_t)(20u<<(s<12?s:12)));break;
        case 3:low=(uint32_t)-(int32_t)(random32()%40);break;
        case 4:low=random32()%8;break;
        default:low=random32();break;
        }
        uint32_t slot=(random32()&0xffff0000u)|(uint16_t)low;
        if(!cell_domain(x,y,shift,slot))continue;
        put16(resource+0x2a,(uint16_t)width);put16(resource+0x2c,(uint16_t)height);put16(resource+0x32,shift);
        put32(resource+0x434,b+CELL_ROWS);
        for(unsigned row=0;row<CELL_MAX;++row)put32(sl_arena+CELL_ROWS+4*row,b+CELL_DATA+CELL_MAX*row+random32()%2);
        BYTE *point=sl_arena+0x480+n%5;put32(point,x);put32(point+4,y);
        a[0]=b+CELL_RESOURCE;a[1]=(uint32_t)(uintptr_t)point;a[2]=slot;return;
    }
}
static int cell_cases(unsigned index,unsigned limit){
    const es2_hook_descriptor *d=function_hook_descriptor(index);const es2_hook_extension *x=function_hook_extension(index);
    unsigned cases=case_budget(limit,1001024);int status=0;if(!limit)coverage_enable(1);
    for(unsigned n=0;n<cases&&!status;++n){current_case=n;current_case_valid=1;if(!limit&&n==8192)coverage_enable(0);
        uint32_t a[6],r[3];cell_build(n,a,r);memcpy(sl_before,sl_arena,sizeof(sl_arena));
        uint64_t want=weapons_original(index,a,r);memcpy(sl_after,sl_arena,sizeof(sl_arena));
        uint64_t got=x->replacement_context(a,d->user,r);
        if(want!=got||memcmp(sl_before,sl_after,sizeof(sl_arena))||memcmp(sl_before,sl_arena,sizeof(sl_arena))){
            fprintf(stderr,"FAIL: cell case %u pair %016llx/%016llx shift %u radius %08x\n",n,(unsigned long long)want,(unsigned long long)got,
                get16(sl_arena+CELL_RESOURCE+0x32),a[2]);status=1;}
    }
    if(!limit)coverage_enable(0);current_case_valid=0;
    if(!status)printf("PASS: %-20s %u cases; full pair, single/rectangle paths, signed word bounds and wraps, no writes\n",d->name,cases);
    return status;
}
static int sweep_leaf_cases(unsigned index,unsigned limit){
    return index==ES2_HOOK_INDEX_side_scale?side_scale_cases(index,limit):cell_cases(index,limit);
}
