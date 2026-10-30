/* The reference DBSIM.EXE as a file (own_process.md, "es2sim without
 * DBSIM.EXE"). es2sim reads no DBSIM.EXE; this is the testonly path
 * the tests use to compare the own tables (es2/sim/image.h,
 * es2/sim/image_data.h) with the user's file. Parsing and placing work on host
 * buffers at any address (synthetic images in the unit tests). Every reader is
 * bounds-checked and little-endian; nothing is read into structs. */
#ifndef ES2_SIM_IMAGE_FILE_H
#define ES2_SIM_IMAGE_FILE_H
#include "es2/sim/image.h"

/* The reference DBSIM.EXE (docs/reference_version.md). */
#define ES2_SIM_DBSIM_FILE_SIZE 724512u
#define ES2_SIM_DBSIM_SHA256 "8561aa47e5bb1bf1f837044b08e5d807e8de492784331708a7ddeabf6d44c4ad"

/* Lowercase hex SHA-256 of a buffer (65 bytes with NUL). */
void es2_sim_sha256_hex(const uint8_t *bytes, size_t size, char out[65]);

/* 0 when `size` and the SHA-256 match; else 1 and *error. */
int es2_sim_image_gate(const uint8_t *file, size_t size, size_t expected_size, const char *expected_sha256,
                       const char **error);

/* Reads the PE headers of `file`: i386, PE32, sections inside the image and
 * their raw data inside the file, headers inside both. 0 or 1 and *error. */
int es2_sim_image_parse(const uint8_t *file, size_t size, es2_sim_image_info *info, const char **error);

/* Copies the headers and every section's raw bytes to `image` (info->image_size
 * bytes, zero beyond what is copied: the caller passes zeroed memory). */
int es2_sim_image_place(const uint8_t *file, size_t size, const es2_sim_image_info *info, uint8_t *image,
                        const char **error);

/* Finds a resource of a placed image as FindResourceA does: the type by
 * number, the name by string (`name`, ASCII, compared without regard to
 * case: FindResourceA upper-cases it and the stored names are upper case)
 * or, with `name` NULL, by number `id`; of the languages the neutral one
 * (0), else the first. Every read inside the resource directory, the data
 * inside the image. 0 with *rva and *size of the data, or 1 and *error. */
int es2_sim_image_resource(const uint8_t *image, const es2_sim_image_info *info, uint32_t type, const char *name,
                           uint32_t id, uint32_t *rva, uint32_t *size, const char **error);

/* Lists the import slots of a placed image (bounds-checked against
 * image_size). 0 or 1 and *error. */
int es2_sim_image_imports(const uint8_t *image, const es2_sim_image_info *info, es2_sim_import *imports,
                          unsigned capacity, unsigned *count, const char **error);

/* LoadIconA(hInstance, name) on a placed image: the group icon `name`
 * (es2_sim_image_resource), its best image for cx x cy as Windows chooses it
 * (LookupIconIdFromDirectoryEx) and the HICON of that size
 * (CreateIconFromResourceEx); NULL and *error when there is none. The
 * window takes its icon from es2_sim_window_load_icon_own (platform_sdl.h). */
void *es2_sim_window_load_icon(const uint8_t *image, const es2_sim_image_info *info, const char *name, int cx, int cy,
                               const char **error);

#endif
