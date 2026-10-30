#ifndef ES2_CORE_MATRIX_H
#define ES2_CORE_MATRIX_H
#include <stdint.h>
/* 32-byte matrices: nine LE signed Q14 words, signed mode at 18,
 * reserved byte 19, three LE signed32 translations at 20. Sequential reads
 * and writes are intentional: aliases follow the reference's write order. */
void es2_matrix_euler(const void *angles,void *output,const void *quarter_table);
void es2_matrix_compose(const void *first,const void *second,void *output);
void es2_matrix_transpose(void *matrix);
void es2_matrix_rotate(const void *point,const void *matrix,void *output);
void es2_pose_matrix(const void *pose,void *output,const void *quarter_table);
/* Return the reference EDX scratch value; incoming EDX is needed on paths
 * that leave it untouched. EAX is the output pointer (rotate copy: out+12). */
uint32_t es2_matrix_euler_registers(const void *,void *,const void *,uint32_t);
uint32_t es2_matrix_rotate_registers(const void *,const void *,void *,uint32_t);
uint32_t es2_matrix_compose_registers(const void *,const void *,void *,uint32_t);
uint32_t es2_pose_matrix_registers(const void *,void *,const void *);
/* (docs/reverse-engineering/flyer.md, section "the rotation helpers `0x47f058`
 * and `0x47f4f4`"): 47f058 rotation_compose(a, b, out) writes the 20 rotation
 * bytes of a x b by the mode bytes (an identity operand: 20-byte copy of the
 * other, byte 19 included; planar and general cases as matrix_compose; byte 19
 * untouched otherwise), no translation. 47f4f4 rotation_place(a, b, out) first
 * copies b's translation dwords to out, then does the same. EAX is out; the
 * return is EDX (the entry EDX on the copy paths), *ecx the exit ECX. */
uint32_t es2_matrix_rotation_compose_registers(const void *,const void *,void *,uint32_t edx,uint32_t *ecx);
uint32_t es2_matrix_rotation_place_registers(const void *,const void *,void *,uint32_t edx,uint32_t *ecx);
#endif
