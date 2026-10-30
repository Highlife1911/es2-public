/* Frozen test-only copy of this project's hook.c; only its include path
 * differs. Namespace the public entry points, retaining the common ABI bridge.
 * Production never links this baseline. */
#define es2_hook_image_valid baseline_image_valid
#define es2_hook_prologue_valid baseline_prologue_valid
#define es2_hook_install_ex baseline_install_ex
#define es2_hook_install baseline_install
#define es2_hook_uninstall baseline_uninstall
#define es2_hook_read_mode baseline_read_mode
#define es2_hook_log_open baseline_log_open
#define es2_hook_log_close baseline_log_close
#define es2_hook_dispatch_extended baseline_dispatch_extended
#define es2_hook_entry_extended baseline_entry_extended
#include "hook_dispatch_baseline.h"
