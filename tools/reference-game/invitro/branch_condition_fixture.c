/* branch_condition.h against the CPU: for every Jcc condition and every
 * combination of CF, PF, ZF, SF, OF (AF varies too and must not matter) the
 * decision is made by a real jcc instruction on that EFLAGS value. Own
 * synthetic code only; nothing from DBSIM is involved. */
#include <stdint.h>
#include <stdio.h>
#include "branch_condition.h"
#define PROBE(JCC,CONDITION) static unsigned probe_##CONDITION(uint32_t flags) { unsigned taken; \
    __asm__ volatile("pushfl; pushl %1; popfl; " #JCC " 1f; movl $0,%0; jmp 2f; 1: movl $1,%0; 2: popfl" \
        :"=&r"(taken):"r"(flags):"cc","memory"); return taken; }
PROBE(je,je) PROBE(jne,jne) PROBE(jl,jl) PROBE(jle,jle) PROBE(jg,jg) PROBE(jge,jge)
PROBE(jb,jb) PROBE(ja,ja) PROBE(jns,jns) PROBE(jae,jae) PROBE(jbe,jbe) PROBE(js,js)
PROBE(jp,jp) PROBE(jnp,jnp) PROBE(jo,jo) PROBE(jno,jno)
/* Aliases assemble to the same byte as their canonical condition. */
PROBE(jc,jc) PROBE(jnc,jnc) PROBE(jz,jz) PROBE(jnz,jnz) PROBE(jnae,jnae) PROBE(jnb,jnb) PROBE(jna,jna)
PROBE(jnbe,jnbe) PROBE(jnl,jnl) PROBE(jnge,jnge) PROBE(jnle,jnle) PROBE(jng,jng) PROBE(jpe,jpe) PROBE(jpo,jpo)
static const struct { const char *name; enum branch_condition condition; unsigned (*probe)(uint32_t); } conditions[] = {
#define ROW(C) {#C,C,probe_##C},
    ROW(je) ROW(jne) ROW(jl) ROW(jle) ROW(jg) ROW(jge) ROW(jb) ROW(ja) ROW(jns) ROW(jae) ROW(jbe) ROW(js)
    ROW(jp) ROW(jnp) ROW(jo) ROW(jno)
    ROW(jc) ROW(jnc) ROW(jz) ROW(jnz) ROW(jnae) ROW(jnb) ROW(jna) ROW(jnbe) ROW(jnl) ROW(jnge) ROW(jnle) ROW(jng) ROW(jpe) ROW(jpo)
#undef ROW
};
int main(void) {
    unsigned checked = 0;
    for(unsigned i = 0; i < sizeof(conditions) / sizeof(conditions[0]); ++i) {
        unsigned seen[2] = {0, 0};
        for(unsigned f = 0; f < 64; ++f) {
            uint32_t flags = 0x202 | ((f & 1) ? 0x001 : 0) | ((f & 2) ? 0x040 : 0) | ((f & 4) ? 0x080 : 0)
                | ((f & 8) ? 0x800 : 0) | ((f & 16) ? 0x010 : 0) | ((f & 32) ? 0x004 : 0);
            unsigned expected = conditions[i].probe(flags), actual = branch_condition_taken(conditions[i].condition, flags);
            if(expected != actual) {
                fprintf(stderr, "FAIL: %s flags=%03x cpu=%u branch_condition_taken=%u\n", conditions[i].name, flags, expected, actual);
                return 1;
            }
            ++seen[expected]; ++checked;
        }
        if(!seen[0] || !seen[1]) { fprintf(stderr, "FAIL: %s did not show both outcomes\n", conditions[i].name); return 1; }
    }
    /* Spot checks of the cases that motivated the unsigned conditions: JBE is taken on CF or ZF. */
    if(!branch_condition_taken(jbe, 0x203) || !branch_condition_taken(jbe, 0x242) || branch_condition_taken(jbe, 0x202) ||
       branch_condition_taken(ja, 0x203) || !branch_condition_taken(ja, 0x202) || !branch_condition_taken(jbe, 0x241)) {
        fputs("FAIL: jbe/ja spot checks\n", stderr); return 1;
    }
    printf("PASS: %zu conditions (16 canonical + aliases) x 64 CF/ZF/SF/OF/PF/AF states equal the real jcc decision (%u comparisons)\n",
        sizeof(conditions) / sizeof(conditions[0]), checked);
    return 0;
}
