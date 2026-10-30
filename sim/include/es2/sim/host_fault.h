#ifndef ES2_SIM_HOST_FAULT_H
#define ES2_SIM_HOST_FAULT_H

/* Hardware faults of es2sim's own code, caught before the OS walks FS:0.
 *
 * Guest code links DBSIM's frame records on the real FS:0 (0x49630c); their
 * handler 0x4967f0 lies in the no-access CODE range. A fault in own code under
 * such a record never reaches the unhandled-exception filter: the OS's walk
 * finds the record, refuses the handler (STATUS_INVALID_EXCEPTION_HANDLER) and
 * ends the process without a word. A vectored handler at the end of the list
 * runs before that walk. It passes on everything the first handlers (the
 * dispatch trap, the divide primitive, the image-access guard) did not take,
 * every exception raised outside this module (system and driver code keep
 * their own handling) and every code without error severity, and calls
 * `on_fault` with the EXCEPTION_POINTERS for the rest. `on_fault` is expected
 * to end the process; if it returns, the search goes on.
 * docs/reference-game/own_process.md, "Host faults under guest frame records". */

typedef void (*es2_sim_host_fault_fn)(void *exception_pointers);

/* 0 on success; 1 when a handler is already installed, `on_fault` is NULL,
 * or the module or the handler could not be registered. */
int es2_sim_host_fault_install(es2_sim_host_fault_fn on_fault);
void es2_sim_host_fault_remove(void);
/* Whether the handler would act on an exception with this code raised at this
 * address (no handler installed: 0). */
int es2_sim_host_fault_owned(unsigned long code, const void *address);

/* The NULL page [0, ES2_SIM_NULL_PAGE_END): Windows maps nothing there, in
 * DBSIM's process neither. 1 with the accessed address in *address when the
 * EXCEPTION_RECORD `record` is a read or write access violation there; 0 for
 * any other code, an execute fault or an address at or above the end. */
#define ES2_SIM_NULL_PAGE_END 0x10000u
int es2_sim_host_fault_null_page(const void *record, unsigned *address);

/* 1 when the EXCEPTION_RECORD `record` is an integer divide fault: #DE by zero
 * (0xc0000094) or a quotient that does not fit (0xc0000095); 0 otherwise. */
int es2_sim_host_fault_divide(const void *record);

#endif
