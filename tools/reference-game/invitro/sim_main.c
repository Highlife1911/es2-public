/* One sim suite per binary: the original machine code of one module against its
 * own code in sim/src, through the full service dispatch. The suite comes from
 * the generated sim_suite.h (suites.bzl): it includes the suite's case header,
 * which includes the case headers it builds on, and defines sim_suite_entry().
 * A change to one suite's cases rebuilds and reruns that suite and the suites
 * including it only; the hook-group harness (main.c) does not link sim code at
 * all. */
#include "harness_base.h"
#include "sim_suite.h"
#include "es2/hook/machine_x86.h"
#include "es2/sim/machine_sim.h"

static int harness_main(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    AddVectoredExceptionHandler(1,fault_handler);
    print_stack("main",main_stack); print_stack("worker",current_stack());
    printf("STARTUP reservation=%p mode=%s before-large-worker\n",reserved,reservation_in_stack?"main-stack-bottom":"free-range");
    const char *root=getenv("ES2_DATA_ROOT"); if(!root||!*root) { puts("SKIP: ES2_DATA_ROOT is not set; no original data read"); return 0; }
    if(load_image(root)) return 1;
    printf("Seed=0x%08x; x87 fixture CW=0x1332; suite %s\n",random_state,ES2_SIM_SUITE_NAME);
    printf("SHARD index=%u total=%u; ownership=suite-part-modulo\n",shard_index,shard_count);
    if(setjmp(trap_return)) return 1;
    /* The original's records and the services' share the real chain. */
    es2_machine_sim_fs0_host(es2_machine_x86_fs0,es2_machine_x86_set_fs0);
    return sim_suite_entry();
}

static DWORD WINAPI harness_worker(void *argument) { (void)argument; return (DWORD)harness_main(); }
int main(int argc,char **argv) {
    /* The suites take no arguments; the launcher passes the binary only. */
    if(argc>1) { fprintf(stderr,"FAIL: unexpected argument %s\n",argv[1]); return 1; }
    if(configure_sharding()) return 1;
    return run_harness_worker(harness_worker);
}
