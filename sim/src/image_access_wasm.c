/* The image-access measurement in the web build (es2/sim/image_access.h):
 * wasm has no page protection to measure with, so --image-access is refused
 * and nothing is ever armed. */
#include "es2/sim/image_access.h"

int es2_sim_image_access_configure(const char *path) { return path && *path; }
int es2_sim_image_access_configured(void) { return 0; }
int es2_sim_image_access_arm(const es2_sim_image_info *info, const uint32_t *sites, unsigned site_count, const char **error) {
    (void)info;
    (void)sites;
    (void)site_count;
    (void)error;
    return 0;
}
int es2_sim_image_access_armed(void) { return 0; }
void es2_sim_image_access_pause(void) {}
void es2_sim_image_access_resume(void) {}
int es2_sim_image_access_write(const char *reason) {
    (void)reason;
    return 0;
}
void es2_sim_image_access_disarm(void) {}
int es2_sim_image_access_page(uint32_t va, es2_sim_access_page_stats *out) {
    (void)va;
    (void)out;
    return 1;
}
unsigned es2_sim_image_access_byte(uint32_t va) {
    (void)va;
    return 0;
}
unsigned es2_sim_image_access_code_reads(const es2_sim_code_read **reads) {
    if (reads) *reads = 0;
    return 0;
}
