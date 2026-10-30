/* Services of the own process's dispatch (own_process.md section 4.2): host
 * code with the original function's calling contract, keyed by the original VA.
 * Each group of services puts its rows into its own file
 * sim/dispatch/services/<name>.h (for example shade_ranges.h) and defines the
 * functions (signature es2_sim_service_fn) in its sim/src file; the generated services_rows.h includes every fragment
 * (//sim:services_rows, sorted by file name). es2_sim_register_services
 * declares and registers every row. The order of the rows does not matter: the
 * table is sorted when sealed, and a VA twice or a VA that is also a registry
 * hook is refused then (table_test).
 *
 *   ES2_SIM_SERVICE(name, va, abi, result, slots, function)
 *     abi     CDECL (the caller removes the arguments) or STDCALL (the callee
 *             removes `slots` dwords)
 *     result  EAX or PAIR (EDX:EAX), documentation only: every service sets
 *             EAX, EDX and ECX as the original leaves them
 *     slots   stack argument slots the function reads at least
 *
 * Import thunks are the `jmp dword [slot]` stubs of DBSIM.EXE at
 * 0x496754..0x4968ec (each checked against its import name on the reference
 * file by //sim:table_test). */
#include "sim/dispatch/services_rows.h"
