/* memory_probe.exe [FILE]: the values es2launch.exe's memory check passes to
 * es2l_memory_check, taken as es2launch.c's memory_ok takes them (the
 * GlobalAlloc loop from 20,000,000 bytes down in steps of 200,000, then
 * GlobalMemoryStatus while the block is held), in a process linked like
 * es2launch.exe (i686, not large-address-aware, so GlobalMemoryStatus clamps
 * as it does for ES.EXE). Writes one line per value and the native check's
 * result to FILE (default: stdout); the web launcher loop
 * (web/launcher.mjs) passes the recorded values to es2l_memory_check. */
#include "launch_logic.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    uint32_t size = 20000000u;
    HGLOBAL block;
    while (!(block = GlobalAlloc(GMEM_FIXED, size))) size -= 200000u;
    MEMORYSTATUS status;
    memset(&status, 0, sizeof status);
    status.dwLength = sizeof status;
    GlobalMemoryStatus(&status);
    GlobalFree(block);
    FILE *out = argc > 1 ? fopen(argv[1], "w") : stdout;
    if (!out) return 2;
    fprintf(out, "total_phys %lu\n", (unsigned long)status.dwTotalPhys);
    fprintf(out, "total_pagefile %lu\n", (unsigned long)status.dwTotalPageFile);
    fprintf(out, "avail_pagefile %lu\n", (unsigned long)status.dwAvailPageFile);
    fprintf(out, "allocated %lu\n", (unsigned long)size);
    fprintf(out, "avail_phys %lu\n", (unsigned long)status.dwAvailPhys);
    fprintf(out, "total_virtual %lu\n", (unsigned long)status.dwTotalVirtual);
    fprintf(out, "check %d\n", es2l_memory_check((uint32_t)status.dwTotalPhys, (uint32_t)status.dwTotalPageFile,
                                                (uint32_t)status.dwAvailPageFile, size));
    if (out != stdout) fclose(out);
    return 0;
}
