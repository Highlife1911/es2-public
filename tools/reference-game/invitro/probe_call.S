/* The emulated runtime imports: CW3220 memset and memcpy semantics (dwords
 * forward, then the remaining bytes; EAX = destination, EDX = count,
 * ECX = 0). Own implementations of the documented behaviour. */
.text
.intel_syntax noprefix
.globl _es2_rt_memset
_es2_rt_memset:
    push ebp
    mov ebp,esp
    push edi
    mov edi,[ebp+8]
    movzx eax,byte ptr [ebp+12]
    imul eax,eax,0x01010101
    mov ecx,[ebp+16]
    mov edx,ecx
    shr ecx,2
    cld
    rep stosd
    mov ecx,edx
    and ecx,3
    rep stosb
    mov eax,[ebp+8]
    pop edi
    pop ebp
    ret

.globl _es2_rt_memcpy
_es2_rt_memcpy:
    push ebp
    mov ebp,esp
    push esi
    push edi
    mov edi,[ebp+8]
    mov esi,[ebp+12]
    mov ecx,[ebp+16]
    mov edx,ecx
    shr ecx,2
    cld
    rep movsd
    mov ecx,edx
    and ecx,3
    rep movsb
    mov eax,[ebp+8]
    pop edi
    pop esi
    pop ebp
    ret
.att_syntax prefix

/* Own test bridge: a reproducible initial stack pattern for inspecting
 * whether a wrapper exposes bytes that its callees never initialize. */
.text
.intel_syntax noprefix
.globl _es2_probe_call
_es2_probe_call:
    push ebp
    mov ebp,esp
    push ebx
    push esi
    push edi
    sub esp,512
    mov edi,esp
    mov eax,[ebp+24]
    mov ecx,128
    rep stosd
    /* Incoming EAX and EDX also carry the pattern (paths that return them). */
    mov edx,eax
    add esp,384
    push dword ptr [ebp+20]
    push dword ptr [ebp+16]
    push dword ptr [ebp+12]
    call dword ptr [ebp+8]
    mov ecx,[ebp+28]
    mov [ecx],eax
    mov [ecx+4],edx
    lea esp,[ebp-12]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
