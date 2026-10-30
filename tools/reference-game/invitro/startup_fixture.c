/* Synthetic Windows API responses; no original data or processes. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
static unsigned mode,commits,commit_fails;
static void *const stack=(void *)(uintptr_t)0x400000;
static void *fake_alloc(void *address,SIZE_T size,DWORD type,DWORD protection) {
    if(type==MEM_RESERVE) {
        if(address!=(void *)(uintptr_t)0x400000 || size!=0x2d8000 || protection!=PAGE_NOACCESS) return NULL;
        return mode==0?address:NULL;
    }
    if(type!=MEM_COMMIT || address!=(void *)(uintptr_t)0x6d8000 || size!=0x1000 || protection!=PAGE_NOACCESS) return NULL;
    ++commits;
    return commit_fails?NULL:address;
}
static SIZE_T fake_query(const void *address,MEMORY_BASIC_INFORMATION *region,SIZE_T size) {
    (void)size;
    if(mode==7) return 0;
    region->BaseAddress=(void *)address;
    region->AllocationBase=mode==2?(void *)(uintptr_t)0x10000000:stack;
    region->RegionSize=mode==6?0x2d8000:0xffc000;
    region->Type=mode==3?MEM_MAPPED:MEM_PRIVATE;
    region->State=mode==4?MEM_COMMIT:MEM_RESERVE;
    return sizeof(*region);
}
static DWORD fake_error(void) { return ERROR_INVALID_ADDRESS; }
#define VirtualAlloc fake_alloc
#define VirtualQuery fake_query
#define GetLastError fake_error
#include "startup_reservation.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    DWORD error=1; int in_stack=1;
    /* Free range: plain reservation, no barrier. */
    CHECK(claim_image_range(stack,&error,&in_stack)==(void *)(uintptr_t)0x400000);
    CHECK(!in_stack && error==ERROR_SUCCESS && commits==0);
    /* Bottom of the caller's own stack reservation: used in place with a barrier. */
    mode=1; commits=0;
    CHECK(claim_image_range(stack,&error,&in_stack)==(void *)(uintptr_t)0x400000);
    CHECK(in_stack && error==ERROR_SUCCESS && commits==1);
    /* Barrier commit failure, another allocation, a mapped file, committed
     * pages, a reservation too small for image plus barrier, query failure. */
    commit_fails=1; commits=0;
    CHECK(!claim_image_range(stack,&error,&in_stack) && !in_stack && commits==1);
    commit_fails=0;
    static const unsigned refused[]={2,3,4,6,7};
    for(unsigned i=0;i<5;++i) { mode=refused[i];
        commits=0;
        CHECK(!claim_image_range(stack,&error,&in_stack));
        CHECK(!in_stack && commits==0 && error==ERROR_INVALID_ADDRESS);
    }
    puts("PASS: startup free range and own-stack bottom claimed; barrier failure, foreign/mapped/committed/short regions and query failure refused");
    return 0;
}
