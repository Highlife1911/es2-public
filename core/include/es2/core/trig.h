#ifndef ES2_CORE_TRIG_H
#define ES2_CORE_TRIG_H
#include <stdint.h>
/* Tables are borrowed LE bytes. Asin's large-table argument denotes index 0;
 * only indices 3584..8192 are read; small indices are 0..448.
 * Atan correction indices are 1..512 on its legal domain. */
uint32_t es2_inverse_sine(uint16_t value,const void *large_table,const void *small_table);
int es2_angle_ratio_valid(uint32_t x,uint32_t y);
uint32_t es2_angle_ratio(uint32_t x,uint32_t y,const void *corrections);
void es2_matrix_angles(const void *matrix,void *angles,const void *large_table,const void *small_table,const void *corrections);
uint64_t es2_angle_ratio_registers(uint32_t,uint32_t,const void *);
uint32_t es2_matrix_angles_registers(const void *,void *,const void *,const void *,const void *);
#endif
