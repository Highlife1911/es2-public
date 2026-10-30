#include "es2/core/matrix.h"
#include "es2/core/movement.h"
#include "fixed.h"

static uint32_t cosine(const uint8_t *a,unsigned i,const void *table) { return es2_heading_cos(fx_r16(a+i),table); }
static uint32_t sine(const uint8_t *a,unsigned i,const void *table) { return es2_heading_sin(fx_r16(a+i),table); }
static uint32_t cos_pair(const uint8_t *a,unsigned i,unsigned j,int add,const void *t) {
    return es2_heading_cos((uint16_t)(fx_r16(a+i)+(add ? fx_r16(a+j) : 0u-fx_r16(a+j))),t);
}
static uint32_t sin_pair(const uint8_t *a,unsigned i,unsigned j,int add,const void *t) {
    return es2_heading_sin((uint16_t)(fx_r16(a+i)+(add ? fx_r16(a+j) : 0u-fx_r16(a+j))),t);
}
uint32_t es2_matrix_euler_registers(const void *angles,void *output,const void *table,uint32_t edx) {
    const uint8_t *a=angles; uint8_t *m=output;
    unsigned mode=fx_r16(a) ? 3 : fx_r16(a+2) ? 2 : fx_r16(a+4) ? 1 : 0;
    m[18]=(uint8_t)mode;
    if(!mode) {
        fx_w16(m,16384); fx_w16(m+6,16384); fx_w16(m+16,16384);
        static const unsigned zero[]={2,8,4,10,12,14};
        for(unsigned i=0;i<6;++i) fx_w16(m+zero[i],0);
    } else if(mode==1) {
        uint32_t c=cosine(a,4,table); fx_w16(m,c); fx_w16(m+6,c);
        edx=fx_s16((uint16_t)(fx_r16(a+4)-16384u)) < -8 ? UINT32_MAX : 0;
        uint32_t s=sine(a,4,table); fx_w16(m+2,s); fx_w16(m+4,0u-s);
        for(unsigned i=8;i<16;i+=2) fx_w16(m+i,0);
        fx_w16(m+16,16384);
    } else {
        uint32_t s=sine(a,0,table); fx_w16(m+10,s);
        uint32_t cm=cos_pair(a,2,4,0,table),cp=cos_pair(a,2,4,1,table);
        uint32_t half_sum=fx_sar(cm+cp,1),half_difference=fx_sar(cm-cp,1);
        fx_w16(m,half_sum-fx_q14(s,half_difference));
        fx_w16(m+14,half_difference-fx_q14(s,half_sum));
        uint32_t sp=sin_pair(a,2,4,1,table),sm=sin_pair(a,4,2,0,table);
        half_sum=fx_sar(sp+sm,1); half_difference=fx_sar(sp-sm,1);
        fx_w16(m+2,fx_q14(s,half_difference)+half_sum);
        fx_w16(m+12,fx_q14(s,half_sum)+half_difference);
        fx_w16(m+6,fx_sar(cos_pair(a,0,4,0,table)+cos_pair(a,0,4,1,table),1));
        fx_w16(m+16,fx_sar(cos_pair(a,0,2,0,table)+cos_pair(a,0,2,1,table),1));
        fx_w16(m+8,fx_sar(0u-(sin_pair(a,2,0,0,table)+sin_pair(a,2,0,1,table)),1));
        edx=fx_s16((uint16_t)(fx_r16(a+4)+fx_r16(a)-16384u)) < -8 ? UINT32_MAX : 0;
        fx_w16(m+4,0u-fx_sar(sin_pair(a,4,0,0,table)+sin_pair(a,4,0,1,table),1));
    }
    return edx;
}
void es2_matrix_euler(const void *a,void *o,const void *t) { (void)es2_matrix_euler_registers(a,o,t,0); }
void es2_matrix_transpose(void *matrix) {
    uint8_t *m=matrix; static const unsigned pairs[3][2]={{2,4},{8,12},{10,14}};
    for(unsigned i=0;i<3;++i) {
        uint16_t a=fx_r16(m+pairs[i][0]),b=fx_r16(m+pairs[i][1]);
        fx_w16(m+pairs[i][1],a); fx_w16(m+pairs[i][0],b);
    }
}
uint32_t es2_matrix_rotate_registers(const void *point,const void *matrix,void *output,uint32_t edx) {
    const uint8_t *p=point,*m=matrix; uint8_t *q=output;
    int mode=m[18]<128 ? m[18] : (int)m[18]-256;
    static const unsigned coeff[3][3]={{0,4,12},{2,6,14},{8,10,16}};
    for(unsigned row=0;row<3;++row) {
        uint32_t value;
        if(mode<1 || (mode==1 && row==2)) value=fx_r32(p+4*row);
        else {
            int64_t sum=8192;
            for(unsigned col=0;col<(mode==1?2u:3u);++col)
                sum+=fx_s32(fx_r32(p+4*col))*fx_s16(fx_r16(m+coeff[row][col]));
            edx=(uint32_t)((uint64_t)sum>>32);
            value=(uint32_t)((uint64_t)sum>>14);
        }
        fx_w32(q+4*row,value);
    }
    return edx;
}
void es2_matrix_rotate(const void *p,const void *m,void *o) { (void)es2_matrix_rotate_registers(p,m,o,0); }
/* Storage is column-major with the Z entries interleaved after the XY block. */
static const unsigned matrix_offset[3][3]={{0,2,8},{4,6,10},{12,14,16}};
static void matrix_dot(const uint8_t *a,const uint8_t *b,uint8_t *out,unsigned col,unsigned row,unsigned terms) {
    uint32_t sum=8192;
    for(unsigned k=0;k<terms;++k)
        sum+=(uint32_t)fx_s16(fx_r16(a+matrix_offset[col][k]))*(uint32_t)fx_s16(fx_r16(b+matrix_offset[k][row]));
    fx_w16(out+matrix_offset[col][row],fx_sar(sum,14));
}
static void copy_rotation(const uint8_t *a,uint8_t *out) {
    for(unsigned i=0;i<20;i+=4) fx_w32(out+i,fx_r32(a+i));
}
uint32_t es2_matrix_compose_registers(const void *first,const void *second,void *output,uint32_t edx) {
    const uint8_t *a=first,*b=second; uint8_t *out=output;
    int ma=a[18]<128?a[18]:(int)a[18]-256,mb=b[18]<128?b[18]:(int)b[18]-256;
    if(ma<1) copy_rotation(b,out);
    else if(mb<1) {
        for(unsigned i=20;i<32;i+=4) fx_w32(out+i,fx_r32(a+i)+fx_r32(b+i));
        copy_rotation(a,out); return edx;
    } else if(ma==1 && mb==1) {
        for(unsigned col=0;col<2;++col) for(unsigned row=0;row<2;++row) matrix_dot(a,b,out,col,row,2);
        for(unsigned i=8;i<16;i+=2) fx_w16(out+i,0);
        fx_w16(out+16,16384); out[18]=1;
    } else if(ma==1) {
        for(unsigned col=0;col<2;++col) for(unsigned row=0;row<3;++row) matrix_dot(a,b,out,col,row,2);
        for(unsigned i=12;i<18;i+=2) fx_w16(out+i,fx_r16(b+i));
        out[18]=2;
    } else if(mb==1) {
        for(unsigned col=0;col<3;++col) for(unsigned row=0;row<2;++row) matrix_dot(a,b,out,col,row,2);
        fx_w16(out+8,fx_r16(a+8)); fx_w16(out+10,fx_r16(a+10)); fx_w16(out+16,fx_r16(a+16)); out[18]=2;
    } else {
        for(unsigned col=0;col<3;++col) for(unsigned row=0;row<3;++row) matrix_dot(a,b,out,col,row,3);
        out[18]=2;
    }
    /* Composition always takes the full three-term translation path, even
     * when either mode denotes an identity or planar matrix. */
    for(unsigned row=0;row<3;++row) {
        int64_t sum=8192;
        for(unsigned col=0;col<3;++col) sum+=fx_s32(fx_r32(a+20+4*col))*fx_s16(fx_r16(b+matrix_offset[col][row]));
        edx=(uint32_t)((uint64_t)sum>>32);
        fx_w32(out+20+4*row,(uint32_t)((uint64_t)sum>>14)+fx_r32(b+20+4*row));
    }
    return edx;
}
void es2_matrix_compose(const void *a,const void *b,void *o) { (void)es2_matrix_compose_registers(a,b,o,0); }
uint32_t es2_pose_matrix_registers(const void *pose,void *output,const void *table) {
    const uint8_t *p=pose; uint8_t *m=output;
    uint32_t z=(uint32_t)fx_s16(fx_r16(p+10)),x=(uint32_t)fx_s16(fx_r16(p+6)),y=(uint32_t)fx_s16(fx_r16(p+8));
    fx_w32(m+28,z); fx_w32(m+20,x); fx_w32(m+24,y);
    return es2_matrix_euler_registers(p,m,table,(uint32_t)(uintptr_t)p);
}

void es2_pose_matrix(const void *p,void *o,const void *t) { (void)es2_pose_matrix_registers(p,o,t); }

/* The enter-prologue rotation helpers 47f058 and 47f4f4 (flyer.md, section "the
 * rotation helpers `0x47f058` and `0x47f4f4`"). Both compose the 3 x 3 Q14
 * blocks of a and b by the signed mode bytes as matrix_compose does, but
 * without its translation row; 47f4f4 first copies b's translation. Every dot
 * is (sum of the word products + 2000h) SAR 14 on a wrapped 32-bit sum; the
 * result registers are the last product's: IMUL EDX,ECX leaves the low product
 * dword in EDX and MOVSX ECX,[b+...] b's sign-extended word in ECX. */
static void rotation_dot(const uint8_t *a,const uint8_t *b,uint8_t *out,unsigned col,unsigned row,unsigned terms,
    uint32_t *edx,uint32_t *ecx) {
    uint32_t sum=0;
    for(unsigned k=0;k<terms;++k) {
        const uint32_t w=(uint32_t)fx_s16(fx_r16(b+matrix_offset[k][row]));
        *edx=(uint32_t)fx_s16(fx_r16(a+matrix_offset[col][k]))*w; *ecx=w;
        sum+=*edx;
    }
    fx_w16(out+matrix_offset[col][row],fx_sar(sum+0x2000u,14));
}
/* Original: DBSIM.EXE 0x47f058 (and 0x47f4f4 after its translation copy).
 * Byte 19 of out is written only by the 20-byte copies (REP MOVSD, which
 * leaves ECX = 0; EDX keeps its entry value there). */
static uint32_t rotation_compose(const uint8_t *a,const uint8_t *b,uint8_t *out,uint32_t edx,uint32_t *ecx) {
    const int ma=a[18]<128?a[18]:(int)a[18]-256;
    if(ma<1) { copy_rotation(b,out); *ecx=0; return edx; }
    const int mb=b[18]<128?b[18]:(int)b[18]-256;
    if(mb<1) { copy_rotation(a,out); *ecx=0; return edx; }
    if(mb==1 && ma==1) {
        for(unsigned col=0;col<2;++col) for(unsigned row=0;row<2;++row) rotation_dot(a,b,out,col,row,2,&edx,ecx);
        for(unsigned i=8;i<16;i+=2) fx_w16(out+i,0);
        fx_w16(out+16,16384); out[18]=1;
    } else if(mb==1) {
        for(unsigned col=0;col<3;++col) for(unsigned row=0;row<2;++row) rotation_dot(a,b,out,col,row,2,&edx,ecx);
        fx_w16(out+8,fx_r16(a+8)); fx_w16(out+10,fx_r16(a+10)); fx_w16(out+16,fx_r16(a+16)); out[18]=2;
    } else if(ma==1) {
        for(unsigned col=0;col<2;++col) for(unsigned row=0;row<3;++row) rotation_dot(a,b,out,col,row,2,&edx,ecx);
        for(unsigned i=12;i<18;i+=2) fx_w16(out+i,fx_r16(b+i));
        out[18]=2;
    } else {
        for(unsigned col=0;col<3;++col) for(unsigned row=0;row<3;++row) rotation_dot(a,b,out,col,row,3,&edx,ecx);
        out[18]=2;
    }
    return edx;
}
uint32_t es2_matrix_rotation_compose_registers(const void *first,const void *second,void *output,uint32_t edx,uint32_t *ecx) {
    return rotation_compose(first,second,output,edx,ecx);
}
/* Original: DBSIM.EXE 0x47f4f4. The three translation dwords of b go to
 * out first (each read, then written), so the mode bytes are read after
 * them. */
uint32_t es2_matrix_rotation_place_registers(const void *first,const void *second,void *output,uint32_t edx,uint32_t *ecx) {
    const uint8_t *b=second; uint8_t *out=output;
    for(unsigned i=20;i<32;i+=4) fx_w32(out+i,fx_r32(b+i));
    return rotation_compose(first,second,output,edx,ecx);
}
