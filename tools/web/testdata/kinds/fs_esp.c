/* %fs and ESP: reads, writes, stack limit, other TEB fields, intrinsics. */
unsigned read_head(void) { unsigned v; __asm__ volatile("movl %%fs:0, %0" : "=r"(v)); return v; }   /* EXPECT: inline-asm,fs-read */
void write_head(unsigned v) { __asm__ volatile("movl %0, %%fs:0" : : "r"(v) : "memory"); }       /* EXPECT: inline-asm,fs-write */
unsigned stack_limit(void) { unsigned v; __asm__ volatile("movl %%fs:8, %0" : "=r"(v)); return v; } /* EXPECT: inline-asm,fs-stack */
unsigned teb_field(void) { unsigned v; __asm__ volatile("movl %%fs:0x18, %0" : "=r"(v)); return v; } /* EXPECT: inline-asm,fs-other */
const void *teb(void) { return NtCurrentTeb(); }                   /* EXPECT: fs-stack */
unsigned stack_pointer(void) { unsigned v; __asm__ volatile("movl %%esp, %0" : "=r"(v)); return v; } /* EXPECT: inline-asm,esp */
void *frame(void) { return __builtin_frame_address(0); }           /* EXPECT: esp */
const char *text = "movl %fs:0, %eax; movl %esp, %ebx";
int fs_count;
int esp;
int use_esp(int esp2) { return esp + esp2; }
