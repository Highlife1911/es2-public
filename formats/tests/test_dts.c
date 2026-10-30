#include "es2/formats/dts.h"
#include "test_support.h"

#include <string.h>

typedef struct dts_fixture {
    uint8_t bytes[8192];
    size_t size;
} dts_fixture;

static void fixture_word(dts_fixture *f, uint16_t value)
{
    test_put16(f->bytes + f->size, value);
    f->size += 2;
}

static void fixture_zeros(dts_fixture *f, size_t count)
{
    memset(f->bytes + f->size, 0, count);
    f->size += count;
}

static size_t fixture_begin(dts_fixture *f, uint32_t tag)
{
    size_t start = f->size;
    test_put32(f->bytes + f->size, tag);
    f->size += 4;
    fixture_zeros(f, 4);
    return start;
}

static void fixture_end(dts_fixture *f, size_t start)
{
    test_put32(f->bytes + start + 4, (uint32_t)(f->size - start - 8));
    if ((f->size & 1u) != 0) f->bytes[f->size++] = 0xacu;
}

/* Each tiny fixture is constructed from the documented field lengths. */
static void fixture_minimal(dts_fixture *f, uint32_t tag)
{
    size_t start = fixture_begin(f, tag);
    switch (tag) {
    case 0x00140001u: fixture_zeros(f, 8); break;
    case 0x00140002u:
    case 0x00140003u:
    case 0x0014000fu:
    case 0x00140010u:
    case 0x00140011u: fixture_zeros(f, 10); break;
    case 0x00140009u:
    case 0x00140012u:
    case 0x00140005u: fixture_zeros(f, 12); break;
    case 0x0014000eu: break;
    case 0x00140013u: fixture_zeros(f, 16); break;
    case 0x00140007u:
    case 0x0014000cu: fixture_zeros(f, 14); break;
    case 0x00140008u: fixture_zeros(f, 18); break;
    case 0x0014000bu:
    case 0x00140015u: fixture_zeros(f, 16); break;
    case 0x00140014u: fixture_zeros(f, 20); break;
    case 0x0014000au: fixture_zeros(f, 22); break;
    case 0x001e0001u:
    case 0x001e0004u:
    case 0x001e0002u: fixture_zeros(f, 10); break;
    case 0x001e0003u:
        fixture_zeros(f, 18);
        fixture_minimal(f, 0x001e0002u);
        break;
    default: CHECK(0); break;
    }
    fixture_end(f, start);
}

static void fixture_sequence(dts_fixture *f, uint32_t tag)
{
    size_t sequence = fixture_begin(f, tag);
    fixture_word(f, 17); /* Arbitrary synthetic ticks, priority and movement. */
    fixture_word(f, 3);
    fixture_word(f, 0);
    fixture_word(f, 2); /* Two frames. */
    fixture_zeros(f, 2 * 6);
    fixture_word(f, 3); /* Three part slots and six frame/part indices. */
    fixture_zeros(f, 3 * 2 + 2 * 3 * 2);
    fixture_end(f, sequence);
}

static void fixture_rich(dts_fixture *f)
{
    size_t root, part, group, animation;
    root = fixture_begin(f, 0x001e0003u);
    fixture_zeros(f, 12);
    fixture_word(f, 3);

    part = fixture_begin(f, 0x0014000cu); /* Detail thresholds reuse count. */
    fixture_zeros(f, 12);
    fixture_word(f, 1);
    group = fixture_begin(f, 0x00140014u);
    fixture_zeros(f, 12);
    fixture_word(f, 3);
    fixture_word(f, 4);
    fixture_word(f, 4);
    fixture_word(f, 1);
    fixture_zeros(f, 3 * 2 + 4 * 6 + 4 * 4);
    fixture_minimal(f, 0x00140009u);
    fixture_end(f, group);
    fixture_word(f, 25);
    fixture_end(f, part);

    part = fixture_begin(f, 0x0014000bu);
    fixture_zeros(f, 12);
    fixture_word(f, 1);
    fixture_minimal(f, 0x00140013u);
    fixture_word(f, 0);
    fixture_end(f, part);

    part = fixture_begin(f, 0x00140015u);
    fixture_zeros(f, 12);
    fixture_word(f, 1);
    fixture_minimal(f, 0x00140005u);
    fixture_word(f, 2);
    fixture_zeros(f, 2 * 14 + 2 * 2);
    fixture_end(f, part);

    fixture_word(f, 1); /* One transform and two cell-animation slots. */
    fixture_word(f, 2);
    fixture_zeros(f, 2 * 2 + 32);
    animation = fixture_begin(f, 0x001e0002u);
    fixture_word(f, 2);
    fixture_sequence(f, 0x001e0001u);
    fixture_sequence(f, 0x001e0004u);
    fixture_word(f, 2);
    fixture_zeros(f, 2 * 8);
    fixture_word(f, 2);
    fixture_zeros(f, 2 * 12);
    fixture_word(f, 2);
    fixture_zeros(f, 2 * 2);
    fixture_word(f, 2);
    fixture_zeros(f, 2 * 4);
    fixture_end(f, animation);
    fixture_end(f, root);
}

typedef struct visit_state {
    const uint8_t *input;
    size_t size;
    size_t visited;
    size_t root_size;
    size_t child_offsets[32];
    size_t parent_offsets[32];
    size_t body_lengths[32];
} visit_state;

static es2_status fixture_visit(const es2_dts_object *object, void *user)
{
    visit_state *state = user;
    size_t i, j;
    CHECK(object->encoded.data == state->input + object->offset);
    CHECK(object->body.data == object->encoded.data + 8);
    CHECK(object->encoded.size >= object->body.size + 8);
    CHECK(object->offset + object->encoded.size <= state->size);
    CHECK(object->child_run_count <= 2);
    CHECK(object->root_index == 0);
    for (i = 0; i < object->child_run_count; ++i) {
        const es2_dts_children *run = &object->children[i];
        size_t actual = 0;
        CHECK(run->encoded.data >= object->body.data);
        CHECK(run->encoded.data + run->encoded.size <=
              object->body.data + object->body.size);
        for (j = 0; j < state->visited; ++j) {
            const uint8_t *child = state->input + state->child_offsets[j];
            if (state->parent_offsets[j] == object->offset &&
                child >= run->encoded.data &&
                child < run->encoded.data + run->encoded.size) ++actual;
        }
        CHECK(actual == run->count);
    }
    if (object->depth == 0) {
        CHECK(object->parent_offset == SIZE_MAX);
        CHECK(object->child_run_count == 2);
        CHECK(object->children[0].count == 3);
        CHECK(object->children[1].count == 1);
        CHECK(state->visited == 10);
        state->root_size = object->encoded.size;
    }
    CHECK(state->visited < 32);
    state->child_offsets[state->visited] = object->offset;
    state->parent_offsets[state->visited] = object->parent_offset;
    state->body_lengths[state->visited] = object->body.size;
    ++state->visited;
    return ES2_OK;
}

static es2_status fixture_cancel(const es2_dts_object *object, void *user)
{
    size_t *offset = user;
    *offset = object->offset;
    return ES2_ERR_ARGUMENT;
}

static es2_status fixture_walk(dts_fixture *f, size_t depth,
                                es2_dts_stats *stats, es2_error *error)
{
    es2_span span = {f->bytes, f->size};
    return es2_dts_walk(span, depth, NULL, NULL, stats, error);
}

static void test_tag_layouts(void)
{
    dts_fixture f = {{0}, 0};
    es2_dts_stats stats;
    es2_error error;
    size_t i;
    for (i = 0; i < ES2_DTS_TAG_COUNT; ++i) {
        uint32_t tag = es2_dts_tag_at(i);
        CHECK(tag != 0);
        CHECK(es2_dts_tag_name(tag) != NULL);
        fixture_minimal(&f, tag);
    }
    CHECK(es2_dts_tag_at(ES2_DTS_TAG_COUNT) == 0);
    CHECK(es2_dts_tag_name(0x31415926u) == NULL);
    CHECK(fixture_walk(&f, 1, &stats, &error) == ES2_OK);
    CHECK(stats.roots == ES2_DTS_TAG_COUNT);
    CHECK(stats.objects == ES2_DTS_TAG_COUNT + 1);
    CHECK(stats.max_depth == 1);
    for (i = 0; i < ES2_DTS_TAG_COUNT; ++i) {
        CHECK(stats.counts[i] == (es2_dts_tag_at(i) == 0x001e0002u ? 2u : 1u));
    }
    /* The unobserved BSPGroup layout has ten bytes per node. */
    f.size = 0;
    {
        size_t group = fixture_begin(&f, 0x0014000au);
        fixture_zeros(&f, 20);
        fixture_word(&f, 2);
        fixture_zeros(&f, 20);
        fixture_end(&f, group);
    }
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_OK);
}

static void test_rich_tree(void)
{
    dts_fixture f = {{0}, 0};
    es2_dts_stats stats;
    es2_error error;
    es2_span span;
    visit_state visits;
    size_t cut, i, cancelled = SIZE_MAX;
    fixture_rich(&f);
    memset(&visits, 0, sizeof(visits));
    visits.input = f.bytes;
    visits.size = f.size;
    span.data = f.bytes;
    span.size = f.size;
    CHECK(es2_dts_walk(span, 3, fixture_visit, &visits, &stats, &error) == ES2_OK);
    CHECK(stats.objects == 11);
    CHECK(stats.roots == 1);
    CHECK(stats.max_depth == 3);
    CHECK(visits.visited == 11 && visits.root_size == f.size);
    CHECK(fixture_walk(&f, 2, &stats, &error) == ES2_ERR_DEPTH);
    CHECK(es2_dts_walk(span, 3, fixture_cancel, &cancelled, &stats, &error)
          == ES2_ERR_CALLBACK);
    CHECK(error.offset == cancelled);
    for (cut = 0; cut < f.size; ++cut) {
        span.size = cut;
        CHECK(es2_dts_walk(span, 3, NULL, NULL, &stats, &error) == ES2_ERR_TRUNCATED);
        CHECK(error.offset <= cut);
    }
    /* Independently shorten nested declared bodies while leaving their parent
       and the full input intact. Outer truncation alone cannot test the inner
       count/array reads: these mutations exercise those bounded subreaders. */
    span.size = f.size;
    for (i = 0; i < visits.visited; ++i) {
        uint8_t *length = f.bytes + visits.child_offsets[i] + 4;
        for (cut = 0; cut < visits.body_lengths[i]; ++cut) {
            test_put32(length, (uint32_t)cut);
            CHECK(es2_dts_walk(span, 3, NULL, NULL, &stats, &error) != ES2_OK);
            CHECK(error.offset <= f.size);
        }
        test_put32(length, (uint32_t)visits.body_lengths[i]);
    }
}

static void test_bad_objects(void)
{
    dts_fixture f = {{0}, 0};
    es2_dts_stats stats;
    es2_error error;
    es2_span span = {f.bytes, 0};
    size_t root;
    CHECK(es2_dts_walk(span, 0, NULL, NULL, NULL, &error) == ES2_ERR_ARGUMENT);
    CHECK(es2_dts_walk(span, ES2_DTS_DEPTH_LIMIT + 1u, NULL, NULL,
                       &stats, &error) == ES2_ERR_ARGUMENT);
    span.data = NULL;
    span.size = 1;
    CHECK(es2_dts_walk(span, 0, NULL, NULL, &stats, &error) == ES2_ERR_ARGUMENT);
    fixture_minimal(&f, 0x00140001u);
    test_put32(f.bytes, 0x31415926u);
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_ERR_UNSUPPORTED);
    CHECK(error.offset == 0);
    test_put32(f.bytes, 0x00140001u);
    test_put32(f.bytes + 4, UINT32_MAX);
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_ERR_TRUNCATED);
    test_put32(f.bytes + 4, 6);
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_ERR_TRUNCATED);
    test_put32(f.bytes + 4, 10);
    fixture_zeros(&f, 2);
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_ERR_LENGTH);
    CHECK(error.offset == 16);
    /* Odd bodies still need alignment, but no documented class consumes
       this extra body byte. Padding cannot hide failed exact consumption. */
    test_put32(f.bytes + 4, 9);
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_ERR_LENGTH);
    --f.size;
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_ERR_TRUNCATED);

    f.size = 0;
    root = fixture_begin(&f, 0x00140007u);
    fixture_zeros(&f, 12);
    fixture_word(&f, UINT16_MAX);
    fixture_end(&f, root);
    CHECK(fixture_walk(&f, 1, &stats, &error) == ES2_ERR_TRUNCATED);
    CHECK(error.offset == 22);
    test_put16(f.bytes + 20, 0);
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_OK);
    fixture_minimal(&f, 0x0014000eu); /* Concatenated roots are legal. */
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_OK);
    CHECK(stats.roots == 2);
    f.bytes[f.size++] = 0;
    CHECK(fixture_walk(&f, 0, &stats, &error) == ES2_ERR_TRUNCATED);

    f.size = 0;
    root = fixture_begin(&f, 0x00140007u);
    fixture_zeros(&f, 12);
    fixture_word(&f, 1);
    fixture_minimal(&f, 0x0014000eu);
    fixture_end(&f, root);
    test_put32(f.bytes + 26, 2); /* Child body reaches beyond parent. */
    fixture_zeros(&f, 2); /* File bytes outside the parent cannot satisfy it. */
    CHECK(fixture_walk(&f, 1, &stats, &error) == ES2_ERR_TRUNCATED);
    CHECK(error.offset == 30);
}

static void test_child_classes(void)
{
    dts_fixture f = {{0}, 0};
    es2_dts_stats stats;
    es2_error error;
    size_t object;
    object = fixture_begin(&f, 0x001e0003u);
    fixture_zeros(&f, 18);
    fixture_minimal(&f, 0x0014000eu);
    fixture_end(&f, object);
    CHECK(fixture_walk(&f, 1, &stats, &error) == ES2_ERR_MAGIC);
    CHECK(error.offset == 26);
    f.size = 0;
    object = fixture_begin(&f, 0x001e0002u);
    fixture_word(&f, 1);
    fixture_minimal(&f, 0x0014000eu);
    fixture_zeros(&f, 8);
    fixture_end(&f, object);
    CHECK(fixture_walk(&f, 1, &stats, &error) == ES2_ERR_MAGIC);
    CHECK(error.offset == 10);
    f.size = 0;
    object = fixture_begin(&f, 0x00140014u);
    fixture_zeros(&f, 18);
    fixture_word(&f, 1);
    fixture_minimal(&f, 0x0014000eu);
    fixture_end(&f, object);
    CHECK(fixture_walk(&f, 1, &stats, &error) == ES2_ERR_MAGIC);
    CHECK(error.offset == 28);
}

static void test_depth_bound(void)
{
    dts_fixture f = {{0}, 0};
    es2_dts_stats stats;
    es2_error error;
    size_t starts[ES2_DTS_DEPTH_LIMIT + 1u], i;
    for (i = 0; i < ES2_DTS_DEPTH_LIMIT + 1u; ++i) {
        starts[i] = fixture_begin(&f, 0x00140007u);
        fixture_zeros(&f, 12);
        fixture_word(&f, 1);
    }
    fixture_minimal(&f, 0x0014000eu);
    for (i = ES2_DTS_DEPTH_LIMIT + 1u; i != 0; --i) fixture_end(&f, starts[i - 1]);
    CHECK(fixture_walk(&f, ES2_DTS_DEPTH_LIMIT, &stats, &error) == ES2_ERR_DEPTH);
    /* Removing one container makes the final null object exactly depth 256. */
    {
        es2_span shortened = {f.bytes + 22, f.size - 22};
        CHECK(es2_dts_walk(shortened, ES2_DTS_DEPTH_LIMIT, NULL, NULL,
                           &stats, &error) == ES2_OK);
        CHECK(stats.max_depth == ES2_DTS_DEPTH_LIMIT);
    }
}

static void test_large_count_product(void)
{
    /* Enough bytes for 65535 frame headers and part IDs, but no pose matrix.
       The product overflows 32-bit size_t; on 64-bit it is simply truncated. */
    static uint8_t bytes[524298];
    es2_dts_stats stats;
    es2_error error;
    es2_span span = {bytes, sizeof(bytes)};
    test_put32(bytes, 0x001e0001u);
    test_put32(bytes + 4, (uint32_t)sizeof(bytes) - 8u);
    test_put16(bytes + 14, UINT16_MAX);
    test_put16(bytes + 16 + 65535u * 6u, UINT16_MAX);
    CHECK(es2_dts_walk(span, 0, NULL, NULL, &stats, &error) ==
          (SIZE_MAX == UINT32_MAX ? ES2_ERR_COUNT : ES2_ERR_TRUNCATED));
}

int test_dts(void)
{
    test_tag_layouts();
    test_rich_tree();
    test_bad_objects();
    test_child_classes();
    test_depth_bound();
    test_large_count_product();
    return 0;
}
