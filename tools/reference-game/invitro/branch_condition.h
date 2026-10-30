/* Jcc conditions of the in-vitro branch-site machinery (coverage.h) and their
 * evaluation on an EFLAGS value, exactly as the CPU decides a short/near Jcc:
 * CF bit 0, PF bit 2, ZF bit 6, SF bit 7, OF bit 11. The enumerators are the
 * spellings that branch_sites.h uses. Aliases of the same condition share the
 * value (jc = jb, jnc = jae, jz = je, jnz = jne, jnae/jnb/jna/jnbe/jnl/jnge/
 * jnle/jng/jpe/jpo are the remaining synonyms). The live engine keeps its own
 * copy (proxy/live_coverage_engine.h); both are checked against the real CPU
 * by their fixtures. */
#ifndef ES2_BRANCH_CONDITION_H
#define ES2_BRANCH_CONDITION_H
#include <stdint.h>
enum branch_condition { je,jne,jl,jle,jg,jge,jb,ja,jns,jae,jbe,js,jp,jnp,jo,jno,
    jz=je,jnz=jne,jc=jb,jnc=jae,jnae=jb,jnb=jae,jna=jbe,jnbe=ja,jnl=jge,jnge=jl,jnle=jg,jng=jle,jpe=jp,jpo=jnp };
static unsigned branch_condition_taken(enum branch_condition condition,uint32_t flags) {
    unsigned c=flags&1,p=(flags>>2)&1,z=(flags>>6)&1,sign=(flags>>7)&1,o=(flags>>11)&1,signed_less=sign^o;
    switch(condition) {
    case je:return z;case jne:return !z;
    case jl:return signed_less;case jle:return signed_less||z;
    case jg:return !signed_less&&!z;case jge:return !signed_less;
    case jb:return c;case jae:return !c;
    case jbe:return c||z;case ja:return !c&&!z;
    case js:return sign;case jns:return !sign;
    case jp:return p;case jnp:return !p;
    case jo:return o;case jno:return !o;
    }
    return 0;
}
#endif
