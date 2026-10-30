/* The online manual's operations in the web build (help.h): the site has
 * no help files and no viewer; the manual is the project's page, which the
 * page opens in a new tab (docs/web_build.md section 13). */
#include "win/help.h"
#include "web/web_imports.h"

int es2_help_os_exists(void *ctx, const char *path) {
    (void)ctx;
    (void)path;
    return 1;
}

int es2_help_os_open(void *ctx, const char *path) {
    (void)ctx;
    (void)path;
    es2_web_manual();
    return 1;
}
