/* Five matrix wrappers whose output matrix byte 19 can be stack residue
 * (an uninitialized local). Direct cases poison the stack below the call with
 * 0x31 or 0xa7 (es2_probe_call, which also sets incoming EAX/EDX to that
 * pattern) and require all bytes, EAX and EDX to equal core called with the
 * same padding value. Installed cases go through the detour, where compare
 * mode returns the original phase's own residue, so only that byte is
 * masked there. */
static unsigned padding_class(unsigned c) { return c==0?0:c==1?128+random32()%128:c==2?1:2+random32()%126; }
static void padding_angles(BYTE *angles,unsigned c) {
    random_bytes(angles,6);
    if(c<3) put16(angles,0);
    if(c<2) put16(angles+2,0);
    if(c==0) put16(angles+4,0);
    else if(c==1 && !get16(angles+4)) put16(angles+4,1);
}
static int padding_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    unsigned cases=case_budget(limit,1000000);
    uint64_t modes[8]={0},residue=0,aliased=0;
    BYTE arena[512],before[512],after[512];
    static const int16_t scales[][2]={{-32768,-1},{-32768,-32768},{32767,-32768},{1,1},{-1,1},{0,7},{32767,32767},{-32768,1}};
    if(!limit) coverage_enable(1);
    for(unsigned n=0;n<cases;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(arena,sizeof(arena));
        BYTE *t=arena+32,*m=arena+160,*pose=arena+224,*out=arena+256;
        unsigned local=(n/2)%4,other=(n/8)%4,enabled=n%5!=0,alias=n%7==6;
        uint32_t a[ES2_HOOK_MAX_ARGS]={0};
        size_t residue_offset;
        BYTE *output;
        if(index==ES2_HOOK_INDEX_pose_parent) {
            padding_angles(pose,local);
            m[18]=(BYTE)padding_class(other);
            if(alias) { if(n&8) out=m; else pose=out+20; }
            a[0]=(uint32_t)(uintptr_t)pose;a[1]=(uint32_t)(uintptr_t)m;a[2]=(uint32_t)(uintptr_t)out;
            output=out;residue_offset=19;modes[1+local]++;
        } else {
            put16(t+0x20,enabled?(uint16_t)(random32()|1):0);
            padding_angles(t+0x28,local);
            t[0x2e + 18]=(BYTE)padding_class(other);
            if(n<64) { put16(t+0x1c,(uint16_t)scales[n%8][0]);put16(t+0x1e,(uint16_t)scales[n%8][1]); }
            if(!get16(t+0x1e)) put16(t+0x1e,1);
            a[0]=(uint32_t)(uintptr_t)t;
            output=t+0x2e;residue_offset=19;
            if(index==ES2_HOOK_INDEX_track_scaled) {
                m[18]=(BYTE)padding_class(other);
                if(alias) out=t+0x2e + 4*(int)((n/7)%5)-8;
                a[1]=(uint32_t)(uintptr_t)out;output=out;
            } else if(index==ES2_HOOK_INDEX_track_scaled_inverse) {
                m[18]=(BYTE)padding_class((other+1)%4);
                BYTE *input=alias?t+0x2e + 4*(int)((n/7)%5)-8:m;
                a[1]=(uint32_t)(uintptr_t)input;
            }
            modes[enabled?1+local:0]++;
        }
        aliased+=alias;
        memcpy(before,arena,sizeof(arena));
        uint32_t got[2],eax,edx;
        uint8_t padding;
        if(!limit) {
            padding=(n&1)?0x31:0xa7;
            uint32_t pattern=padding*0x01010101u;
            es2_probe_call(mapped+d->rva,a[0],a[1],a[2],pattern,got);
            eax=edx=pattern;
        } else {
            got[0]=original(index,a);got[1]=original_edx;padding=0;
            eax=(uint32_t)(uintptr_t)initial_fpu;edx=(uint32_t)(uintptr_t)a;
        }
        memcpy(after,arena,sizeof(arena));memcpy(arena,before,sizeof(arena));
        const BYTE *q=MATH_QUARTER(d->user);
        uint64_t want;
        switch(index) {
        case ES2_HOOK_INDEX_pose_parent: want=es2_pose_parent_registers((void *)(uintptr_t)a[0],(void *)(uintptr_t)a[1],(void *)(uintptr_t)a[2],q,padding);break;
        case ES2_HOOK_INDEX_track_forward: want=es2_track_forward_registers(t,q,padding,eax,edx);break;
        case ES2_HOOK_INDEX_track_inverse: want=es2_track_inverse_registers(t,q,padding,eax,edx);break;
        case ES2_HOOK_INDEX_track_scaled: want=es2_track_scaled_registers(t,(void *)(uintptr_t)a[1],q,padding);break;
        default: want=es2_track_scaled_inverse_registers(t,(void *)(uintptr_t)a[1],q,padding);break;
        }
        size_t masked=(size_t)(output-arena)+residue_offset;
        if(!limit && after[masked]==padding && before[masked]!=padding) ++residue;
        if(limit) arena[masked]=after[masked];
        if(got[0]!=(uint32_t)want || got[1]!=(uint32_t)(want>>32) || memcmp(arena,after,sizeof(arena))) {
            unsigned diff=0;while(diff<sizeof(arena)&&arena[diff]==after[diff])++diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x state_offset=%u original_byte=%u core_byte=%u padding=%02x\n",
                d->name,n,got[0],(uint32_t)want,got[1],(uint32_t)(want>>32),diff,diff<512?after[diff]:0,diff<512?arena[diff]:0,padding);
            coverage_enable(0);return 1;
        }
    }
    if(!limit) coverage_enable(0);
    printf("PASS: %-20s %u cases; exact EDX:EAX/state/512 guard bytes%s; aliases=%llu; residue byte from padding=%llu; modes",d->name,cases,
        limit?" except output byte 19":" with stack padding 31/a7",(unsigned long long)aliased,(unsigned long long)residue);
    for(unsigned i=0;i<8;++i) if(modes[i]) printf(" %u:%llu",i,(unsigned long long)modes[i]);
    puts("");fflush(stdout);return 0;
}
