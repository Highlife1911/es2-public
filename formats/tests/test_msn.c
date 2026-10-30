#include "es2/formats/msn.h"
#include "test_support.h"

#include <string.h>

static void msn_put16(uint8_t *bytes, size_t offset, uint16_t value)
{
    bytes[offset] = (uint8_t)(value & UINT16_C(255));
    bytes[offset + 1u] = (uint8_t)(value >> 8);
}

/* This fixture has one opaque record in every fixed-width list and two
 * routes, containing three and zero children. Values are invented here.
 */
static size_t msn_fixture(uint8_t *bytes, size_t *count_offsets,
                          size_t *record_offsets)
{
    static const size_t widths[ES2_MSN_LIST_COUNT] = {
        14u, 82u, 8u, 144u, 64u, 22u, 10u, 0u, 12u,
        82u, 30u, 144u, 102u, 62u, 22u, 164u, 58u
    };
    size_t offset = 2u;
    size_t index;
    memset(bytes, 0, 2048u);
    msn_put16(bytes, 0u, 5u);
    for (index = 0u; index < ES2_MSN_LIST_COUNT; ++index) {
        count_offsets[index] = offset;
        msn_put16(bytes, offset, index == ES2_MSN_ROUTE_LIST ? 2u : 1u);
        offset += 2u;
        record_offsets[index] = offset;
        if (index == ES2_MSN_ROUTE_LIST) {
            msn_put16(bytes, offset + 8u, 3u);
            msn_put16(bytes, offset + 10u, 41u);
            msn_put16(bytes, offset + 12u, 83u);
            msn_put16(bytes, offset + 14u, 127u);
            offset += 26u;
        } else {
            memset(bytes + offset, (int)(index + 1u), widths[index]);
            offset += widths[index];
        }
    }
    return offset;
}

int test_msn(void)
{
    uint8_t bytes[2048];
    uint8_t empty[2u + 2u * ES2_MSN_LIST_COUNT] = {0};
    size_t counts[ES2_MSN_LIST_COUNT];
    size_t records[ES2_MSN_LIST_COUNT];
    const size_t length = msn_fixture(bytes, counts, records);
    es2_msn parsed;
    es2_msn saved;
    es2_error error;
    es2_span input = {bytes, length};
    size_t index;

    CHECK(es2_msn_parse(input, &parsed, &error) == ES2_OK);
    CHECK(parsed.version == 5u);
    for (index = 0u; index < ES2_MSN_LIST_COUNT; ++index) {
        const es2_msn_list *list = &parsed.lists[index];
        CHECK(list->records.data == bytes + records[index]);
        if (index == ES2_MSN_ROUTE_LIST) {
            CHECK(list->count == 2u);
            CHECK(list->stride == 0u);
            CHECK(list->records.size == 26u);
        } else {
            const size_t end = index + 1u == ES2_MSN_LIST_COUNT
                             ? length : counts[index + 1u];
            CHECK(list->count == 1u);
            CHECK(list->stride == end - records[index]);
            CHECK(list->records.size == list->stride);
        }
    }

    /* Every proper prefix, including all count fields and route children,
     * is rejected. In particular, the final list never borrows bytes past
     * the caller's span to fill a short objective record.
     */
    memset(&saved, 0x5a, sizeof(saved));
    for (index = 0u; index < length; ++index) {
        es2_span prefix = {bytes, index};
        memcpy(&parsed, &saved, sizeof(parsed));
        CHECK(es2_msn_parse(prefix, &parsed, &error) == ES2_ERR_TRUNCATED);
        CHECK(error.code == ES2_ERR_TRUNCATED);
        CHECK(error.offset <= index);
        CHECK(memcmp(&parsed, &saved, sizeof(parsed)) == 0);
    }

    for (index = 0u; index < ES2_MSN_LIST_COUNT; ++index) {
        const uint16_t original = index == ES2_MSN_ROUTE_LIST ? 2u : 1u;
        msn_put16(bytes, counts[index], UINT16_MAX);
        CHECK(es2_msn_parse(input, &parsed, &error) == ES2_ERR_COUNT);
        CHECK(error.offset == counts[index]);
        msn_put16(bytes, counts[index], original);
    }
    msn_put16(bytes, records[ES2_MSN_ROUTE_LIST] + 8u, UINT16_MAX);
    CHECK(es2_msn_parse(input, &parsed, &error) == ES2_ERR_COUNT);
    CHECK(error.offset == records[ES2_MSN_ROUTE_LIST] + 8u);
    msn_put16(bytes, records[ES2_MSN_ROUTE_LIST] + 8u, 3u);

    msn_put16(bytes, 0u, 4u);
    CHECK(es2_msn_parse(input, &parsed, &error) == ES2_ERR_VERSION);
    CHECK(error.offset == 0u);
    msn_put16(bytes, 0u, UINT16_MAX);
    CHECK(es2_msn_parse(input, &parsed, &error) == ES2_ERR_VERSION);
    msn_put16(bytes, 0u, 5u);

    input.size = length + 1u;
    CHECK(es2_msn_parse(input, &parsed, &error) == ES2_ERR_LENGTH);
    CHECK(error.offset == length);
    input.size = length;
    CHECK(es2_msn_parse(input, NULL, &error) == ES2_ERR_ARGUMENT);
    input.data = NULL;
    CHECK(es2_msn_parse(input, &parsed, &error) == ES2_ERR_ARGUMENT);
    input.size = 0u;
    CHECK(es2_msn_parse(input, &parsed, &error) == ES2_ERR_TRUNCATED);

    msn_put16(empty, 0u, 5u);
    input.data = empty;
    input.size = sizeof(empty);
    CHECK(es2_msn_parse(input, &parsed, NULL) == ES2_OK);
    for (index = 0u; index < ES2_MSN_LIST_COUNT; ++index) {
        CHECK(parsed.lists[index].count == 0u);
        CHECK(parsed.lists[index].records.size == 0u);
    }
    return 0;
}
