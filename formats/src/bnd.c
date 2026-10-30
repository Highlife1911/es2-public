#include "es2/formats/bnd.h"

es2_status es2_bnd_inspect(es2_span input, es2_bnd *out, es2_error *error)
{
    es2_reader r;
    es2_bnd value = {0};
    es2_status status;
    if (out == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    status = es2_reader_init(&r, input, error);
    if (status != ES2_OK) return status;
    value.input = input;
    value.opaque_length = input.size;
    if (input.size >= 4) {
        status = es2_read_u32(&r, &value.prefix_candidate);
        if (status != ES2_OK) return status;
        value.has_prefix_candidate = 1;
    }
    *out = value;
    return ES2_OK;
}
