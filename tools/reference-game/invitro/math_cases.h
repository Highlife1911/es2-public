static int math_round1_cases(unsigned index,unsigned limit) {
    const es2_hook_descriptor *d=function_hook_descriptor(index); current_function=d->name;
    unsigned cases=case_budget(limit,1000000);
    uint64_t modes[11]={0},aliases[3]={0};
    BYTE arena[512],before[512],after[512];
    static const uint32_t edges[]={0,1,0xffffffffu,0x80000000u,0x7fffffffu,0x8000,0x7fff,0xffff,1024,0x4000,0xc000};
    if(!limit) coverage_enable(1);
    for(unsigned n=0;n<cases;++n) {
        if(!limit && n==65536) coverage_enable(0);
        random_bytes(arena,sizeof(arena));
        uint32_t a[ES2_HOOK_MAX_ARGS]={0};
        for(unsigned j=0;j<ES2_HOOK_MAX_ARGS;++j) a[j]=n<121?edges[(n+j)%11]:random32();
        BYTE *p=arena+32,*q=arena+128,*out=arena+256;
        unsigned alias=n%5;
        if(alias==1) out=p; else if(alias==2) out=p+(int)((n/5)%23)-11;
        a[0]=(uint32_t)(uintptr_t)p;a[1]=(uint32_t)(uintptr_t)q;a[2]=(uint32_t)(uintptr_t)out;
        switch(index) {
        case ES2_HOOK_INDEX_euler_matrix:
        case ES2_HOOK_INDEX_pose_matrix: {
            unsigned mode=n%4;
            if(mode<3) put16(p,0);
            if(mode<2) put16(p+2,0);
            if(mode==0) put16(p+4,0);
            else if(mode==1) put16(p+4,(uint16_t)(n*251u));
            if(index==ES2_HOOK_INDEX_pose_matrix && (n&16))
                for(unsigned j=6;j<12;j+=2) put16(p+j,(uint16_t)((int)(random32()%4001)-2000));
            ++modes[mode]; a[1]=(uint32_t)(uintptr_t)out;break;
        }
        case ES2_HOOK_INDEX_matrix_compose: {
            unsigned ma=n%3,mb=(n/3)%3;
            if(n&8) { /* Matrices built by original Euler as well as raw words. */
                BYTE angles[6]; random_bytes(angles,6);
                if(ma<2) { put16(angles,0);put16(angles+2,0);if(!ma)put16(angles+4,0); }
                uint32_t v[ES2_HOOK_MAX_ARGS]={(uint32_t)(uintptr_t)angles,(uint32_t)(uintptr_t)p}; original(ES2_HOOK_INDEX_euler_matrix,v);
                random_bytes(angles,6);if(mb<2) { put16(angles,0);put16(angles+2,0);if(!mb)put16(angles+4,0); }
                v[1]=(uint32_t)(uintptr_t)q;original(ES2_HOOK_INDEX_euler_matrix,v);
            } else {
                p[18]=(BYTE)(ma==0?(n&1?128+n%128:0):ma==1?1:2+n%126);
                q[18]=(BYTE)(mb==0?(n&1?128+(n/3)%128:0):mb==1?1:2+(n/3)%126);
            }
            if(alias==3) a[2]=(uint32_t)(uintptr_t)q;
            if(alias==4) a[2]=(uint32_t)(uintptr_t)(q+(int)((n/5)%63)-31);
            ++modes[ma*3+mb];break;
        }
        case ES2_HOOK_INDEX_rotate_point:
            q[18]=(BYTE)n;++modes[q[18]>=128||!q[18]?0:q[18]==1?1:2];
            if(n<121) for(unsigned j=0;j<3;++j) put32(p+4*j,edges[(n+j)%11]);
            if(alias==3) a[2]=(uint32_t)(uintptr_t)q;
            if(alias==4) a[2]=(uint32_t)(uintptr_t)(q+(int)((n/5)%43)-11);
            break;
        case ES2_HOOK_INDEX_matrix_transpose: ++modes[0];break;
        case ES2_HOOK_INDEX_pose_interpolate:
            a[2]=n<65536?n:random32();a[3]=(uint32_t)(uintptr_t)out;
            if(alias==3)a[3]=(uint32_t)(uintptr_t)q;
            if(alias==4)a[3]=(uint32_t)(uintptr_t)(q+(int)((n/5)%23)-11);
            if(n%7==0) for(unsigned j=0;j<6;j+=2) put16(q+j,get16(p+j));
            if(n%7==1) for(unsigned j=0;j<6;j+=2) put16(q+j,(uint16_t)(get16(p+j)+0x8000));
            ++modes[n%7];break;
        case ES2_HOOK_INDEX_scalar_interpolate: {
            a[1]=random32();a[0]=a[1]+(uint32_t)(int32_t)(int16_t)random32();
            a[2]=a[1]+(n&1?0xffff0000u:0x10000u);a[3]=random32();a[4]=random32();
            if(n<4) { a[0]=n&1?0x80000000u:0x7fffffffu;a[1]=0;a[2]=1;a[3]=0;a[4]=1; }
            ++modes[n&1];break;
        }
        case ES2_HOOK_INDEX_track_count: {
            unsigned count=n%11;
            for(unsigned j=0;j<10;++j) put32(p+0x1a+4*j,j==count?0:random32()|1);
            ++modes[count];break;
        }
        case ES2_HOOK_INDEX_inverse_sine:
            a[0]=n&65535;++modes[(a[0]&0x8000)?1:0];break;
        case ES2_HOOK_INDEX_angle_ratio:
            a[0]=n<121?edges[n%11]:random32();a[1]=n<121?edges[n/11]:random32();
            if(n%29==0) a[1]=0;
            if(n%31==0) a[0]=0;
            if(!es2_angle_ratio_valid(a[0],a[1])) { a[0]=0x80000000u;a[1]=0; }
            ++modes[(a[0]>>31)*2+(a[1]>>31)];break;
        case ES2_HOOK_INDEX_matrix_angles: {
            if(n&1) { BYTE angles[6];random_bytes(angles,6);uint32_t v[ES2_HOOK_MAX_ARGS]={(uint32_t)(uintptr_t)angles,(uint32_t)(uintptr_t)p};original(ES2_HOOK_INDEX_euler_matrix,v); }
            if(n%8==0) put16(p+10,0x4000);if(n%8==1) put16(p+10,0xc000);
            a[1]=(uint32_t)(uintptr_t)out;++modes[n%8];break;
        }
        case ES2_HOOK_INDEX_multiply_q10:
            a[0]=n<121?edges[n%11]:random32();a[1]=n<121?edges[n/11]:random32();++modes[(a[0]>>31)*2+(a[1]>>31)];break;
        case ES2_HOOK_INDEX_approach_word:
            a[1]=random32();a[2]=random32();
            if(n<65536) { put16(p,(uint16_t)n);a[1]=(n&1)?0x12348000:0xabcd7fff;a[2]=(n%3)?65:0x8000; }
            if(n%11==0) a[1]=(a[1]&0xffff0000u)|get16(p);
            ++modes[n%3];break;
        case ES2_HOOK_INDEX_energy_accumulate:
            a[1]=n&65535;
            if(n<121) for(unsigned j=0;j<8;j+=2) put16(p+j,(uint16_t)edges[(n+j)%11]);
            ++modes[n&1];break;
        case ES2_HOOK_INDEX_clip_pose: {
            /* Base pointers are chosen modulo 2^32 so that any clip, frame,
             * stride and pose index select the fixed slots below. Mode 3 puts
             * the pose record over T's written words; mode 4 places
             * resource+0x12 at T+0x22 so the reread pose base is the value
             * the translation write just stored. */
            /* Installed runs (limit!=0) also use compare mode, whose shallow
             * snapshot of T cannot isolate a resource graph that points back
             * into T; the alias modes are covered by the direct cases. */
            unsigned mode=limit?n%3:n%5;
            BYTE *t=p,*res=arena+300,*clp=arena+340,*lst=arena+380,*idx=arena+400,*pose=arena+420,*pose2=arena+440;
            uint32_t clip,frame; int16_t stride,pi;
            if(mode==2) { clip=random32()%64;frame=random32()%64;stride=(int16_t)(1+random32()%64);pi=(int16_t)(random32()%512); }
            else { clip=n<121?edges[n%11]:random32();frame=n<121?edges[(n/11)%11]:random32();stride=(int16_t)random32();pi=(int16_t)random32(); }
            if(mode==3) pose=t+0x1c+(n/5)%12;
            if(mode==4) res=t+0x10;
            uint32_t index=frame*(uint32_t)(int32_t)stride,off=12u*(uint32_t)(int32_t)pi;
            put32(t,(uint32_t)(uintptr_t)res);
            put32(res+6,(uint32_t)(uintptr_t)lst-4u*clip);
            put32(lst,(uint32_t)(uintptr_t)clp);
            put16(clp+8,mode==0?0:(uint16_t)(random32()|1));
            put16(clp+0x10,(uint16_t)stride);
            put32(clp+0x16,(uint32_t)(uintptr_t)idx-2u*index);
            put16(idx,(uint16_t)pi);
            put32(res+0x12,(uint32_t)(uintptr_t)pose-off);
            if(mode==4) put32(pose+6,(uint32_t)(uintptr_t)pose2-off);
            a[0]=(uint32_t)(uintptr_t)t;a[1]=clip;a[2]=frame;
            ++modes[mode];break;
        }
        default:return fail("missing math generator");
        }
        unsigned layout=0;
        for(unsigned i=0;i<d->span_count;++i) for(unsigned j=0;j<i;++j) {
            uintptr_t x=a[d->spans[i].arg_index],y=a[d->spans[j].arg_index];
            size_t nx=d->spans[i].offset+d->spans[i].size,ny=d->spans[j].offset+d->spans[j].size;
            if(x<y+ny && y<x+nx) { if(x==y)layout=1;else if(layout!=1)layout=2; }
        }
        ++aliases[layout];
        memcpy(before,arena,sizeof(arena));
        uint32_t got=original(index,a),got_edx=original_edx;memcpy(after,arena,sizeof(arena));memcpy(arena,before,sizeof(arena));
        const uint32_t regs[3]={(uint32_t)(uintptr_t)initial_fpu,(uint32_t)(uintptr_t)a,0};
        uint64_t result=function_hook_extension(index)->replacement_context(a,d->user,regs);
        uint32_t want=(uint32_t)result,want_edx=(uint32_t)(result>>32);
        if(got!=want || got_edx!=want_edx || memcmp(arena,after,sizeof(arena))) {
            unsigned diff=0;while(diff<sizeof(arena)&&arena[diff]==after[diff])++diff;
            fprintf(stderr,"FAIL: %s case %u EAX original=%08x core=%08x EDX original=%08x core=%08x state_offset=%u original_byte=%u core_byte=%u\n",d->name,n,got,want,got_edx,want_edx,diff,diff<512?after[diff]:0,diff<512?arena[diff]:0);
            coverage_enable(0);return 1;
        }
    }
    if(!limit) coverage_enable(0);
    printf("PASS: %-20s %u cases; exact EDX:EAX/state/512 guard bytes; aliases separate=%llu same=%llu partial=%llu; modes",d->name,cases,
        (unsigned long long)aliases[0],(unsigned long long)aliases[1],(unsigned long long)aliases[2]);
    for(unsigned i=0;i<11;++i) if(modes[i]) printf(" %u:%llu",i,(unsigned long long)modes[i]);
    puts("");fflush(stdout);return 0;
}
