#include "es2/formats/msn.h"

es2_status es2_msn_parse(es2_span input, es2_msn *out, es2_error *error)
{
    /* Disk widths from the complete MSN framing table, in file order. */
    static const size_t strides[ES2_MSN_LIST_COUNT] = {
        14u, 82u, 8u, 144u, 64u, 22u, 10u, 0u, 12u,
        82u, 30u, 144u, 102u, 62u, 22u, 164u, 58u
    };
    es2_reader reader;
    es2_msn parsed = {0};
    int16_t version;
    size_t list_index;
    es2_status status;

    if (out == NULL) {
        return es2_fail(error, ES2_ERR_ARGUMENT, 0u);
    }
    status = es2_reader_init(&reader, input, error);
    if (status != ES2_OK) {
        return status;
    }
    status = es2_read_i16(&reader, &version);
    if (status != ES2_OK) {
        return status;
    }
    if (version != 5) {
        return es2_fail(error, ES2_ERR_VERSION, 0u);
    }
    parsed.version = (uint16_t)version;

    for (list_index = 0u; list_index < ES2_MSN_LIST_COUNT; ++list_index) {
        const size_t count_offset = reader.offset;
        int16_t signed_count;
        size_t payload_offset;
        es2_msn_list *list = &parsed.lists[list_index];

        status = es2_read_i16(&reader, &signed_count);
        if (status != ES2_OK) {
            return status;
        }
        if (signed_count < 0) {
            return es2_fail(error, ES2_ERR_COUNT, count_offset);
        }
        list->count = (size_t)signed_count;
        list->stride = strides[list_index];
        payload_offset = reader.offset;

        if (list_index == ES2_MSN_ROUTE_LIST) {
            size_t record_index;
            for (record_index = 0u; record_index < list->count; ++record_index) {
                size_t child_count_offset;
                int16_t child_count;

                status = es2_skip(&reader, 8u);
                if (status != ES2_OK) {
                    return status;
                }
                child_count_offset = reader.offset;
                status = es2_read_i16(&reader, &child_count);
                if (status != ES2_OK) {
                    return status;
                }
                if (child_count < 0) {
                    return es2_fail(error, ES2_ERR_COUNT, child_count_offset);
                }
                status = es2_skip(&reader, (size_t)child_count * 2u);
                if (status != ES2_OK) {
                    return status;
                }
            }
        } else {
            /* Both factors are small: signed i16 count and stride <= 164. */
            status = es2_skip(&reader, list->count * list->stride);
            if (status != ES2_OK) {
                return status;
            }
        }
        status = es2_subspan(input, payload_offset,
                            reader.offset - payload_offset,
                            &list->records, error);
        if (status != ES2_OK) {
            return status;
        }
    }
    status = es2_reader_end(&reader);
    if (status != ES2_OK) {
        return status;
    }
    *out = parsed;
    return ES2_OK;
}
