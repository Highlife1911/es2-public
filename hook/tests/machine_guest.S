/* "Guest" functions for machine_test.c: the x86 backend calls them; the test
 * registers C models of the same behaviour as dispatch entries for the sim
 * backend. Each reads registers and its slots, leaves registers, and writes
 * below its ESP and into its slots, so registers and stack bytes are both
 * compared. No push or pop: every stack byte written is one the model
 * writes too. */
.text

/* cdecl (a, b, c): EAX = EAX + ECX + a + 2b + 3c; [ESP-4] = EAX;
 * [ESP-12] = EDI; EDX ^= a; ECX = b - EBX; EBX += c; ESI ^= EBP; EDI += a;
 * EBP -= 1; c += 1. */
.globl _guest_cdecl3
_guest_cdecl3:
    addl %ecx, %eax
    addl 4(%esp), %eax
    addl 8(%esp), %eax
    addl 8(%esp), %eax
    addl 12(%esp), %eax
    addl 12(%esp), %eax
    addl 12(%esp), %eax
    movl %eax, -4(%esp)
    movl %edi, -12(%esp)
    xorl 4(%esp), %edx
    movl 8(%esp), %ecx
    subl %ebx, %ecx
    addl 12(%esp), %ebx
    xorl %ebp, %esi
    addl 4(%esp), %edi
    decl %ebp
    addl $1, 12(%esp)
    ret

/* cdecl (a, b): EDX:EAX = a * b + ECX; [ESP-8] = ESI; ESI = ~ESI. */
.globl _guest_cdecl_pair2
_guest_cdecl_pair2:
    movl 4(%esp), %eax
    mull 8(%esp)
    addl %ecx, %eax
    adcl $0, %edx
    movl %esi, -8(%esp)
    notl %esi
    ret

/* stdcall (a, b): [ESP-4] = EBX; EAX = a - b; ECX = a; EBX ^= b; a = 0. */
.globl _guest_stdcall2
_guest_stdcall2:
    movl %ebx, -4(%esp)
    movl 4(%esp), %eax
    subl 8(%esp), %eax
    movl 4(%esp), %ecx
    xorl 8(%esp), %ebx
    movl $0, 4(%esp)
    ret $8

/* stdcall (a, b, c, d): [ESP-16] = EDX; EAX = a + c; EDX = b - d;
 * EBP += d; EDI ^= c. */
.globl _guest_stdcall_pair4
_guest_stdcall_pair4:
    movl %edx, -16(%esp)
    movl 4(%esp), %eax
    addl 12(%esp), %eax
    movl 8(%esp), %edx
    subl 16(%esp), %edx
    addl 16(%esp), %ebp
    xorl 12(%esp), %edi
    ret $8+8

/* Borland (x0 EAX, x1 EDX, x2 ECX, a3 [ESP+8], a4 [ESP+4]): [ESP-4] = x2;
 * EAX = ((x0 - x1) ^ x2) + 2 a3 - a4; ECX = a4 - a3; EDX = x1 + EAX;
 * a3 = EAX. */
.globl _guest_borland5
_guest_borland5:
    movl %ecx, -4(%esp)
    subl %edx, %eax
    xorl %ecx, %eax
    addl 8(%esp), %eax
    addl 8(%esp), %eax
    subl 4(%esp), %eax
    movl 4(%esp), %ecx
    subl 8(%esp), %ecx
    addl %eax, %edx
    movl %eax, 8(%esp)
    ret $8

/* Borland (x0, x1, x2, a3 [ESP+12], a4 [ESP+8], a5 [ESP+4]):
 * EDX:EAX = (x1 ^ a4 ^ x2) : (x0 + a3 - a5); ESI += a4; [ESP-20] = EDI. */
.globl _guest_borland_pair6
_guest_borland_pair6:
    addl 12(%esp), %eax
    subl 4(%esp), %eax
    xorl 8(%esp), %edx
    xorl %ecx, %edx
    addl 8(%esp), %esi
    movl %edi, -20(%esp)
    ret $12

/* Borland (x0, x1), registers only: EAX = x0 * x1; ECX as the caller set it. */
.globl _guest_borland2
_guest_borland2:
    imull %edx, %eax
    ret

/* cdecl (): EAX = EBX ^ ESI; EBX = EDI; [ESP-4] = EBP. */
.globl _guest_cdecl0
_guest_cdecl0:
    movl %ebx, %eax
    xorl %esi, %eax
    movl %edi, %ebx
    movl %ebp, -4(%esp)
    ret

/* The hook and service rows' behaviour (registers only, as their C
 * contracts allow). cdecl (a, b, c): EAX = a - b * c. */
.globl _guest_hook_cdecl3
_guest_hook_cdecl3:
    movl 8(%esp), %eax
    imull 12(%esp), %eax
    negl %eax
    addl 4(%esp), %eax
    ret

/* stdcall (a, b): EDX:EAX = a * b. */
.globl _guest_hook_stdcall_pair2
_guest_hook_stdcall_pair2:
    movl 4(%esp), %eax
    mull 8(%esp)
    ret $8

/* Borland (x0, x1, x2, a3 [ESP+8], a4 [ESP+4]): EDX:EAX = (x1 - a4) : (x0 ^ a3 ^ x2). */
.globl _guest_hook_borland_pair5
_guest_hook_borland_pair5:
    xorl 8(%esp), %eax
    xorl %ecx, %eax
    subl 4(%esp), %edx
    ret $8

/* stdcall (a, b), a service: EAX = a + b; ECX = a ^ b. */
.globl _guest_service_stdcall2
_guest_service_stdcall2:
    movl 4(%esp), %eax
    addl 8(%esp), %eax
    movl 4(%esp), %ecx
    xorl 8(%esp), %ecx
    ret $8

/* cdecl (cw): loads the x87 control word from its slot. */
.globl _guest_fldcw
_guest_fldcw:
    fldcw 4(%esp)
    ret


/* cdecl (): EAX = the x86 backend's floor during the call. */
.globl _guest_floor
_guest_floor:
    movl _es2_machine_x86_floor, %eax
    ret
