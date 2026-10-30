#include "es2/formats/vfs.h"
#include <string.h>

static uint8_t fold(uint8_t c)
{
    if (c >= 'A' && c <= 'Z') c = (uint8_t)(c + ('a' - 'A'));
    return c == '/' ? (uint8_t)'\\' : c;
}

static uint8_t path_byte(es2_vfs_path path, size_t index)
{
    return index < path.prefix.size ? path.prefix.data[index]
         : path.name.data[index - path.prefix.size];
}

static int path_equal(es2_vfs_path a, es2_vfs_path b)
{
    size_t i, length = a.prefix.size + a.name.size;
    if (length != b.prefix.size + b.name.size) return 0;
    for (i = 0; i < length; ++i)
        if (fold(path_byte(a, i)) != fold(path_byte(b, i))) return 0;
    return 1;
}

static es2_status validate_path(es2_vfs_path path, es2_error *error)
{
    size_t i, length, component = 0, separators = 0;
    if ((path.prefix.data == NULL && path.prefix.size != 0)
        || (path.name.data == NULL && path.name.size != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (path.prefix.size > SIZE_MAX - path.name.size)
        return es2_fail(error, ES2_ERR_LENGTH, 0);
    length = path.prefix.size + path.name.size;
    for (i = 0; i <= length; ++i) {
        uint8_t c = i == length ? 0 : path_byte(path, i);
        if (i != length && (c < 32 || c >= 127 || c == ':'))
            return es2_fail(error, ES2_ERR_STRING, i);
        if (i == length || c == '/' || c == '\\') {
            size_t count = i - component;
            if (count == 0 || (count <= 2 && path_byte(path, component) == '.'
                && (count == 1 || path_byte(path, component + 1) == '.')))
                return es2_fail(error, ES2_ERR_STRING, component);
            if (i == length) break;
            component = i + 1;
            ++separators;
        }
    }
    return separators == 0 ? es2_fail(error, ES2_ERR_STRING, 0) : ES2_OK;
}

static es2_status source_count(const es2_vfs_source *source, size_t *count,
                               es2_error *error)
{
    switch (source->kind) {
    case ES2_VFS_VOL:
        if (source->data.vol == NULL) return es2_fail(error, ES2_ERR_ARGUMENT, 0);
        *count = source->data.vol->member_count;
        return ES2_OK;
    case ES2_VFS_LOOSE:
        if (source->data.loose.files == NULL && source->data.loose.count != 0)
            return es2_fail(error, ES2_ERR_ARGUMENT, 0);
        if (source->data.loose.count > SIZE_MAX / sizeof(es2_vfs_file))
            return es2_fail(error, ES2_ERR_COUNT, 0);
        *count = source->data.loose.count;
        return ES2_OK;
    default: return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    }
}

static es2_status get_copy(const es2_vfs_source *source, size_t source_index,
                           size_t index, es2_vfs_copy *out, es2_error *error)
{
    es2_vfs_copy value = {0};
    if (source->kind == ES2_VFS_VOL) {
        es2_vol_member member;
        es2_status status = es2_vol_member_at(source->data.vol, index, &member, error);
        if (status != ES2_OK) return status;
        value.path.prefix = member.directory;
        value.path.name = member.name;
        value.payload = member.payload;
    } else {
        const es2_vfs_file *file = &source->data.loose.files[index];
        if (file->path == NULL || (file->data.data == NULL && file->data.size != 0))
            return es2_fail(error, ES2_ERR_ARGUMENT, index);
        value.path.name.data = (const uint8_t *)file->path;
        value.path.name.size = strlen(file->path);
        value.payload = file->data;
    }
    value.source_index = source_index;
    value.file_index = index;
    value.next_copy = SIZE_MAX;
    *out = value;
    return ES2_OK;
}

es2_status es2_vfs_open(const es2_vfs_source *sources, size_t count,
                        es2_vfs *out, es2_error *error)
{
    es2_vfs value = {0};
    size_t i, total = 0;
    size_t maximum = SIZE_MAX / sizeof(es2_vfs_copy);
    if (maximum > SIZE_MAX / sizeof(es2_vfs_entry))
        maximum = SIZE_MAX / sizeof(es2_vfs_entry);
    if (out == NULL || (sources == NULL && count != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (count > SIZE_MAX / sizeof(es2_vfs_source))
        return es2_fail(error, ES2_ERR_COUNT, 0);
    /* Check all aggregate sizes before traversing potentially large lists. */
    for (i = 0; i < count; ++i) {
        size_t n;
        es2_status status = source_count(&sources[i], &n, error);
        if (status != ES2_OK) return status;
        if (n > maximum - total) return es2_fail(error, ES2_ERR_COUNT, i);
        total += n;
    }
    for (i = 0; i < count; ++i) {
        size_t j, n;
        (void)source_count(&sources[i], &n, error);
        for (j = 0; j < n; ++j) {
            es2_vfs_copy current;
            es2_status status = get_copy(&sources[i], i, j, &current, error);
            if (status != ES2_OK) return status;
            status = validate_path(current.path, error);
            if (status != ES2_OK) return status;
            if (sources[i].kind == ES2_VFS_LOOSE) {
                size_t k;
                for (k = 0; k < j; ++k) {
                    es2_vfs_copy previous;
                    status = get_copy(&sources[i], i, k, &previous, error);
                    if (status != ES2_OK) return status;
                    if (path_equal(current.path, previous.path))
                        return es2_fail(error, ES2_ERR_STRING, j);
                }
            }
        }
    }
    value.sources = sources; value.source_count = count; value.file_count = total;
    *out = value;
    es2_error_clear(error);
    return ES2_OK;
}

static size_t ordered_count(const es2_vfs *vfs)
{
    return vfs->policy == ES2_VFS_DBSIM_ORDER ? vfs->order_count : vfs->source_count;
}

static size_t ordered_source(const es2_vfs *vfs, size_t position)
{
    return vfs->policy == ES2_VFS_DBSIM_ORDER ? vfs->source_order[position] : position;
}

es2_status es2_vfs_open_dbsim_order(const es2_vfs_source *sources,
    size_t count, uint32_t archive_mask,
    size_t *order, size_t capacity, es2_vfs *out, es2_error *error)
{
    es2_vfs value;
    size_t i, loose_count = 0, accepted = 0, total = 0;
    es2_status status;
    if (out == NULL || (order == NULL && count != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (count > SIZE_MAX / sizeof(size_t))
        return es2_fail(error, ES2_ERR_COUNT, 0);
    if (capacity < count) return es2_fail(error, ES2_ERR_CAPACITY, count);
    status = es2_vfs_open(sources, count, &value, error);
    if (status != ES2_OK) return status;
    for (i = 0; i < count; ++i) {
        if (sources[i].kind == ES2_VFS_LOOSE) {
            order[loose_count++] = i;
            total += sources[i].data.loose.count;
        }
    }
    for (i = 0; i < count; ++i) {
        const es2_vol *vol;
        uint32_t use;
        size_t carried, position;
        if (sources[i].kind != ES2_VFS_VOL) continue;
        vol = sources[i].data.vol;
        use = (uint32_t)vol->flag0 | ((uint32_t)vol->flag1 << 8)
            | ((uint32_t)vol->reserved << 16);
        if ((use & archive_mask) == 0) continue;
        if (accepted == 30) return es2_fail(error, ES2_ERR_CAPACITY, i);
        carried = i;
        for (position = loose_count; position < loose_count + accepted; ++position) {
            size_t occupant = order[position];
            if (sources[carried].data.vol->priority > sources[occupant].data.vol->priority) {
                order[position] = carried;
                carried = occupant;
            }
        }
        order[loose_count + accepted++] = carried;
        total += vol->member_count;
    }
    value.policy = ES2_VFS_DBSIM_ORDER;
    value.source_order = order;
    value.order_count = loose_count + accepted;
    value.file_count = total;
    *out = value;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_vfs_find(const es2_vfs *vfs, const char *path,
                        es2_vfs_copy *out, es2_error *error)
{
    es2_vfs_path wanted = {0};
    size_t i;
    es2_status status;
    if (vfs == NULL || path == NULL || out == NULL)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    wanted.name.data = (const uint8_t *)path; wanted.name.size = strlen(path);
    status = validate_path(wanted, error);
    if (status != ES2_OK) return status;
    for (i = 0; i < ordered_count(vfs); ++i) {
        size_t j, n, source_index = ordered_source(vfs, i);
        if (vfs->policy == ES2_VFS_DBSIM_ORDER
            && vfs->sources[source_index].kind == ES2_VFS_VOL
            && strchr(path, '/') != NULL) continue;
        status = source_count(&vfs->sources[source_index], &n, error);
        if (status != ES2_OK) return status;
        for (j = 0; j < n; ++j) {
            es2_vfs_copy current;
            status = get_copy(&vfs->sources[source_index], source_index, j, &current, error);
            if (status != ES2_OK) return status;
            if (path_equal(current.path, wanted)) {
                *out = current;
                es2_error_clear(error);
                return ES2_OK;
            }
        }
    }
    return es2_fail(error, ES2_ERR_NOT_FOUND, 0);
}

static int same_payload(es2_span a, es2_span b)
{
    return a.size == b.size && (a.size == 0 || memcmp(a.data, b.data, a.size) == 0);
}

es2_status es2_vfs_enumerate(const es2_vfs *vfs,
    es2_vfs_entry *entries, size_t entry_capacity,
    es2_vfs_copy *copies, size_t copy_capacity,
    es2_vfs_stats *stats, es2_error *error)
{
    es2_vfs_stats value = {0};
    size_t i;
    if (vfs == NULL || stats == NULL || (entries == NULL && entry_capacity != 0)
        || (copies == NULL && copy_capacity != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (copy_capacity < vfs->file_count)
        return es2_fail(error, ES2_ERR_CAPACITY, vfs->file_count);
    for (i = 0; i < ordered_count(vfs); ++i) {
        size_t j, n, source_index = ordered_source(vfs, i);
        es2_status status = source_count(&vfs->sources[source_index], &n, error);
        if (status != ES2_OK) return status;
        for (j = 0; j < n; ++j) {
            es2_vfs_copy current;
            es2_vfs_entry *entry;
            size_t group;
            status = get_copy(&vfs->sources[source_index], source_index, j, &current, error);
            if (status != ES2_OK) return status;
            for (group = 0; group < value.unique_count; ++group)
                if (path_equal(current.path, entries[group].path)) break;
            if (group == value.unique_count) {
                if (group == entry_capacity)
                    return es2_fail(error, ES2_ERR_CAPACITY, group);
                entry = &entries[group];
                entry->path = current.path;
                entry->first_copy = value.copy_count;
                entry->last_copy = value.copy_count;
                entry->copy_count = 1;
                entry->all_identical = 1;
                ++value.unique_count;
            } else {
                entry = &entries[group];
                copies[entry->last_copy].next_copy = value.copy_count;
                entry->last_copy = value.copy_count;
                ++entry->copy_count;
                if (!same_payload(copies[entry->first_copy].payload, current.payload))
                    entry->all_identical = 0;
            }
            copies[value.copy_count++] = current;
        }
    }
    for (i = 0; i < value.unique_count; ++i) {
        if (entries[i].copy_count > 1) {
            ++value.duplicate_count;
            if (entries[i].all_identical) ++value.identical_duplicate_count;
        }
    }
    *stats = value;
    es2_error_clear(error);
    return ES2_OK;
}

es2_status es2_vfs_duplicate_indices(const es2_vfs_entry *entries,
    size_t entry_count, size_t *indices, size_t capacity,
    size_t *required, es2_error *error)
{
    size_t i, count = 0;
    if (required == NULL || (entries == NULL && entry_count != 0)
        || (indices == NULL && capacity != 0))
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    if (entry_count > SIZE_MAX / sizeof(es2_vfs_entry))
        return es2_fail(error, ES2_ERR_COUNT, 0);
    for (i = 0; i < entry_count; ++i) if (entries[i].copy_count > 1) ++count;
    *required = count;
    if (capacity < count) return es2_fail(error, ES2_ERR_CAPACITY, count);
    count = 0;
    for (i = 0; i < entry_count; ++i)
        if (entries[i].copy_count > 1) indices[count++] = i;
    es2_error_clear(error);
    return ES2_OK;
}

/* The member lookup of DBSIM 0x47cdf8 (see vfs.h): split 0x47d198 (strrchr
 * of the backslash, strchr '.'), rebuild 0x47d2c4, _stricmp on the directory
 * table and on the record names. */
static uint8_t dbsim_upper(uint8_t c)
{
    return c >= 'a' && c <= 'z' ? (uint8_t)(c - ('a' - 'A')) : c;
}

static int dbsim_equal(const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size)
{
    size_t i;
    if (a_size != b_size) return 0;
    for (i = 0; i < a_size; ++i)
        if (dbsim_upper(a[i]) != dbsim_upper(b[i])) return 0;
    return 1;
}

es2_status es2_vfs_dbsim_member(const es2_vol *vol, const char *path,
                                size_t *index, es2_error *error)
{
    const uint8_t *p = (const uint8_t *)path;
    size_t length = 0, start = 0, split, i;
    unsigned directory = 0xffu;
    if (vol == NULL || path == NULL || index == NULL)
        return es2_fail(error, ES2_ERR_ARGUMENT, 0);
    while (length < ES2_VFS_DBSIM_PATH_MAX && p[length] != 0) ++length;
    if (length >= ES2_VFS_DBSIM_PATH_MAX) return es2_fail(error, ES2_ERR_LENGTH, length);
    /* Drive: two bytes when the second is ':' (the first byte is nonzero
     * because the path is not empty). The directory part keeps it. */
    if (length >= 2 && p[1] == ':') start = 2;
    split = start;
    for (i = start; i < length; ++i) if (p[i] == '\\') split = i + 1;
    if (split != 0) {
        es2_reader r;
        if (es2_reader_init(&r, vol->directory_bytes, error) != ES2_OK) return r.status;
        for (i = 0; i < vol->directory_count && i < 0xffu; ++i) {
            size_t begin = r.offset;
            uint8_t c;
            do {
                if (es2_read_u8(&r, &c) != ES2_OK) return r.status;
            } while (c != 0);
            if (dbsim_equal(r.span.data + begin, r.offset - begin - 1, p, split)) {
                directory = (unsigned)i;
                break;
            }
        }
        if (directory == 0xffu) return es2_fail(error, ES2_ERR_NOT_FOUND, 0);
    }
    for (i = 0; i < vol->member_count; ++i) {
        const uint8_t *slot = vol->member_bytes.data + 18 * i;
        size_t n = 0;
        if (slot[13] != directory) continue;
        while (n < 13 && slot[n] != 0) ++n;
        if (n == 13) return es2_fail(error, ES2_ERR_STRING, vol->table_offset + 18 * i);
        if (dbsim_equal(slot, n, p + split, length - split)) {
            *index = i;
            es2_error_clear(error);
            return ES2_OK;
        }
    }
    return es2_fail(error, ES2_ERR_NOT_FOUND, 0);
}
