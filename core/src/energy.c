#include "es2/core/energy.h"
#include "fixed.h"
uint32_t es2_multiply_q10(uint32_t a,uint32_t b) { return (uint32_t)((uint64_t)(fx_s32(a)*fx_s32(b))>>10); }
uint64_t es2_approach_word_registers(void *state,uint32_t target,uint32_t step) {
    uint16_t value=fx_r16(state); int32_t t=fx_s16(target),v=fx_s16(value);
    if(t<v) {
        value=(uint16_t)(value-step); fx_w16(state,value);
        if(t>=fx_s16(value)) { fx_w16(state,target); return (uint64_t)target<<32; }
        return ((uint64_t)target<<32) | ((uint32_t)(uintptr_t)state&UINT32_C(0xffff0000)) | (uint16_t)(value-target);
    }
    if(t>v) {
        value=(uint16_t)(value+step); fx_w16(state,value);
        if(t<=fx_s16(value)) { fx_w16(state,target); return (uint64_t)target<<32; }
        uint32_t result=(target&UINT32_C(0xffff0000)) | (uint16_t)(target-value);
        return ((uint64_t)result<<32)|result;
    }
    return (uint64_t)target<<32;
}
uint64_t es2_energy_accumulate_registers(void *state,uint16_t request) {
    uint8_t *s=state;
    uint16_t sum=(uint16_t)(fx_r16(s)+fx_r16(s+2));
    int32_t gap=fx_s16((uint32_t)fx_r16(s+6)-sum),delta=fx_s16(request);
    if(delta>=5) delta=5;
    if(delta>gap) delta=gap;
    sum=(uint16_t)(sum+delta);
    uint32_t target=es2_multiply_q10((uint32_t)fx_s16(fx_r16(s+4)),(uint32_t)fx_s16(sum));
    uint64_t result=es2_approach_word_registers(s,target,gap<0?10000:65);
    fx_w16(s+2,(uint32_t)sum-fx_r16(s));
    return (result&UINT64_C(0xffffffffffff0000))|(uint16_t)(request-delta);
}

uint32_t es2_approach_word(void *s,uint32_t t,uint32_t step) { return (uint32_t)es2_approach_word_registers(s,t,step); }
uint32_t es2_energy_accumulate(void *s,uint16_t r) { return (uint32_t)es2_energy_accumulate_registers(s,r); }

uint64_t es2_multiply_q10_registers(uint32_t a,uint32_t b) { uint64_t p=(uint64_t)(fx_s32(a)*fx_s32(b)); return (p&UINT64_C(0xffffffff00000000))|(uint32_t)(p>>10); }
