#ifndef ES2_CORE_ENERGY_H
#define ES2_CORE_ENERGY_H
#include <stdint.h>
uint32_t es2_multiply_q10(uint32_t a,uint32_t b);
/* EAX bit patterns, including the address-derived upper word, are intentional.
 * Target and step consume low words, but target high bits survive one branch. */
uint32_t es2_approach_word(void *state,uint32_t target,uint32_t step);
uint32_t es2_energy_accumulate(void *state,uint16_t request);
uint64_t es2_approach_word_registers(void *,uint32_t,uint32_t);
uint64_t es2_energy_accumulate_registers(void *,uint16_t);
uint64_t es2_multiply_q10_registers(uint32_t,uint32_t);
#endif
