/* The guest image's own tables (es2/sim/image.h; own_process.md, "es2sim
 * without DBSIM.EXE"): layout, imports and resources from
 * sim/data/dbsim_image_tables.c. The file path (gate, parse, placement) is in
 * image_file.c, testonly. */
#include "es2/sim/image.h"
#include <string.h>

static int fail(const char **error, const char *text) { if (error) *error = text; return 1; }

void es2_sim_image_info_own(es2_sim_image_info *info) { *info = es2_sim_dbsim_layout; }

int es2_sim_imports_own(es2_sim_import *imports, unsigned capacity, unsigned *count, const char **error) {
    *count = 0;
    for (unsigned i = 0; i < es2_sim_dbsim_import_count; ++i) {
        const es2_sim_own_import *s = &es2_sim_dbsim_imports[i];
        if (*count >= capacity) return fail(error, "too many imports");
        es2_sim_import *x = &imports[(*count)++];
        x->slot_va = s->slot_va;
        size_t dll_length = strlen(s->dll);
        if (dll_length > ES2_SIM_IMPORT_NAME - 16) return fail(error, "import DLL name length");
        memcpy(x->name, s->dll, dll_length);
        x->name[dll_length] = '!';
        char *rest = x->name + dll_length + 1;
        size_t room = ES2_SIM_IMPORT_NAME - dll_length - 1;
        if (!s->symbol) {
            unsigned ordinal = s->ordinal & 0xffffu, digits = 0;
            char tmp[8];
            do { tmp[digits++] = (char)('0' + ordinal % 10); ordinal /= 10; } while (ordinal);
            rest[0] = '#';
            for (unsigned k = 0; k < digits; ++k) rest[1 + k] = tmp[digits - 1 - k];
            rest[1 + digits] = 0;
        } else {
            size_t length = strlen(s->symbol);
            if (length >= room) return fail(error, "import symbol length");
            memcpy(rest, s->symbol, length + 1);
        }
    }
    return 0;
}

/* ASCII names equal without regard to case (FindResourceA upper-cases the
 * name it is given; the stored names are upper case). */
static int same_name(const char *a, const char *b) {
    for (;; ++a, ++b) {
        unsigned c = (unsigned char)*a, d = (unsigned char)*b;
        if (c >= 'a' && c <= 'z') c -= 0x20u;
        if (d >= 'a' && d <= 'z') d -= 0x20u;
        if (c != d) return 0;
        if (!c) return 1;
    }
}

int es2_sim_resource_own(uint32_t type, const char *name, uint32_t id, const uint8_t **bytes, uint32_t *size,
                         const char **error) {
    const es2_sim_own_resource *first = NULL;
    for (unsigned i = 0; i < es2_sim_dbsim_resource_count; ++i) {
        const es2_sim_own_resource *r = &es2_sim_dbsim_resources[i];
        if (r->type != type) continue;
        if (name ? !r->name || !same_name(r->name, name) : r->name || r->id != id) continue;
        if (!r->language) { first = r; break; }
        if (!first) first = r;
    }
    if (!first) return fail(error, "resource not found");
    *bytes = first->bytes;
    *size = first->size;
    return 0;
}
