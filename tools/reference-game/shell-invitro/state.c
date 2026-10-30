#include "state.h"
#include <string.h>

static int word(es2_reader *r, uint16_t *v) {
  return es2_read_u16(r, v) == ES2_OK;
}
static int bounded(es2_reader *r, unsigned limit, unsigned *value) {
  uint16_t v;
  if (!word(r, &v))
    return 0;
  if (v > limit) {
    r->status = es2_fail(r->error, ES2_ERR_COUNT, r->offset - 2);
    return 0;
  }
  *value = v;
  return 1;
}
static int skip(es2_reader *r, size_t n) { return es2_skip(r, n) == ES2_OK; }
static int item(es2_reader *r) {
  unsigned id;
  return bounded(r, 32, &id) && skip(r, 8);
}
static int pilot(es2_reader *r) {
  unsigned length;
  es2_span name;
  if (!skip(r, 4) || !bounded(r, 30, &length) || !length ||
      es2_read_span(r, length, &name) != ES2_OK)
    return 0;
  if (name.data[length - 1] || memchr(name.data, 0, length - 1)) {
    r->status = es2_fail(r->error, ES2_ERR_STRING, r->offset - length);
    return 0;
  }
  uint16_t bay, order;
  if (!word(r, &bay) || !skip(r, 3) || !word(r, &order))
    return 0;
  if ((bay != 65535 && bay > 7) || (order != 65535 && order > 3)) {
    r->status = es2_fail(r->error, ES2_ERR_OFFSET, r->offset - 7);
    return 0;
  }
  return skip(r, 18);
}
es2_status shell_save_check(es2_span input, shell_save *out, es2_error *error) {
  if (!out)
    return es2_fail(error, ES2_ERR_ARGUMENT, 0);
  es2_reader r;
  shell_save result = {0};
  unsigned n, id, seen = 0;
  es2_reader_init(&r, input, error);
  for (unsigned i = 0; i < 33; i++) {
    if (!skip(&r, 1) || !bounded(&r, 4096, &n))
      goto invalid;
    for (unsigned j = 0; j < n; j++)
      if (!item(&r))
        goto invalid;
  }
  if (!skip(&r, 2))
    goto invalid;
  for (unsigned i = 0; i < 5; i++) {
    if (!bounded(&r, 4, &id) || !skip(&r, 2))
      goto invalid;
    if (seen & (1u << id))
      goto invalid;
    seen |= 1u << id;
  }
  result.selection_offset = r.offset;
  if (!bounded(&r, 5, &result.group) || !bounded(&r, 10, &result.mission) ||
      !skip(&r, 148))
    goto invalid;
  for (unsigned i = 0; i < 36; i++)
    if (!pilot(&r))
      goto invalid;
  for (unsigned i = 0; i < 6; i++)
    if (!bounded(&r, 11, &id))
      goto invalid;
  if (!bounded(&r, 4, &n) || !bounded(&r, 4, &n) || !pilot(&r))
    goto invalid;
  if (!bounded(&r, 8, &n))
    goto invalid;
  seen = 0;
  for (unsigned i = 0; i < n; i++) {
    unsigned capacity, occupied, slots = 0;
    if (!bounded(&r, 7, &id))
      goto invalid;
    if (seen & (1u << id))
      goto invalid;
    seen |= 1u << id;
    /* Both leading words are used in the nine-vehicle table domain. */
    if (!bounded(&r, 8, &id) || !bounded(&r, 8, &id) || !skip(&r, 70) ||
        !bounded(&r, 10, &capacity) || !bounded(&r, capacity, &occupied))
      goto invalid;
    for (unsigned j = 0; j < occupied; j++) {
      if (!capacity || !bounded(&r, capacity - 1, &id) || !item(&r))
        goto invalid;
      if (slots & (1u << id))
        goto invalid;
      slots |= 1u << id;
    }
  }
  if (!skip(&r, 22))
    goto invalid;
  result.variable_offset = r.offset;
  if (!skip(&r, 2022))
    goto invalid;
  result.consumed = r.offset;
  result.trailer_size = input.size - r.offset;
  *out = result;
  return ES2_OK;
invalid:
  return r.status == ES2_OK ? es2_fail(error, ES2_ERR_COUNT, r.offset)
                            : r.status;
}
