/* What os_common.c needs from an implementation of es2/platform/os.h. */
#ifndef ES2_PLATFORM_OS_INTERNAL_H
#define ES2_PLATFORM_OS_INTERNAL_H

#include <stddef.h>

/* The process's environment block as a malloc'd copy, or NULL. */
char *es2_os_host_environment(void);
/* The process's variable, NULL if absent. */
const char *es2_os_host_getenv(const char *name);

#endif
