/* The VOL registration of DBSIM's start-up as own code (launcher.md, "DBSIM's
 * start-up as own code", vol.md "Original registration and lookup order"): the
 * manager 0x473260 / 0x467b9c / 0x467cd8, the path helpers 0x47d2c4 and
 * 0x467c54, the insertion 0x467f94 and the archive open 0x47cab0 with the
 * objects it builds: the read stream R (0x47ad40 and its methods, vtable
 * 0x4a1738), the buffered stream B (0x47c644 and its methods, vtable 0x4a1ad8)
 * and the archive A (0x47c9a0). Every store follows the listing's order; file
 * access goes through the CW3220 file services (_findfirst, _findnext, _open,
 * _read, _lseek, _close), allocation through operator new / new[] and 0x47747c.
 * "Listing" = llvm-objdump -d -M intel of the reference DBSIM.EXE. */
#include "startup_internal.h"
#include "es2/sim/image_data.h"

#define VT_BASE 0x004a16e8u        /* stream base class (0x47a888) */
#define VT_READ 0x004a1738u        /* read stream (0x47ad40) */
#define VT_BUFFERED 0x004a1ad8u    /* buffered stream (0x47c644) */
#define VA_SIGNATURE_POINTER 0x004a1c2cu   /* -> the VOL signature literal */
#define VA_STREAM_BUFFER 0x004a088cu       /* buffer size of B (0x467b9c sets 0x400) */
#define VA_PATTERN 0x006c5dabu             /* 0x47d2c4's static result buffer */
#define VA_ARCHIVE_PATH 0x004d3bb4u        /* 0x467c54's static result buffer */
#define READ_HANDLE_NONE 0xffffu

static uint32_t sx16(uint16_t v) { return (uint32_t)(int32_t)(int16_t)v; }

/* ---- the read stream R (0xb bytes: +4 status, +8 handle word, +0xa open flag) */

/* 0x47a888(o): base class: vtable 0x4a16e8, [o+4] = 0, counter + 1. */
static void base_ctor(es2_sim_startup *s, uint32_t o) {
    su_w32(s, o, VT_BASE);
    su_w32(s, o + 4u, 0);
    su_counter(s, 1);
}
/* 0x47ae28(R): close. AL 1, or 0 when _close failed (status 3). */
static int r_close(es2_sim_startup *s, uint32_t r, int *al) {
    if (su_r8(s, r + 0xau) && su_r16(s, r + 8u) != READ_HANDLE_NONE) {
        uint32_t a[1] = {sx16(su_r16(s, r + 8u))}, eax = 0;
        if (su_call(s, VA_CLOSE, a, 1, &eax)) return 1;
        if (eax == 0xffffffffu) { su_w32(s, r + 4u, 3); *al = 0; return 0; }
    }
    su_w16(s, r + 8u, READ_HANDLE_NONE);
    su_w32(s, r + 4u, 5);
    *al = 1;
    return 0;
}
/* 0x47ae68(R, path): close, _open(path, 0x8000) into the word; -1: status 1,
 * AL 0; else status 0 (0x47b35d), open flag 1, AL 1. */
static int r_open(es2_sim_startup *s, uint32_t r, uint32_t path, int *al) {
    int closed;
    if (r_close(s, r, &closed)) return 1;
    uint32_t a[3] = {path, 0x8000u, 0}, eax = 0;
    if (su_call(s, VA_OPEN, a, 3, &eax)) return 1;
    su_w16(s, r + 8u, (uint16_t)eax);
    if ((uint16_t)eax == READ_HANDLE_NONE) { su_w32(s, r + 4u, 1); *al = 0; return 0; }
    su_w32(s, r + 4u, 0);
    su_w8(s, r + 0xau, 1);
    *al = 1;
    return 0;
}
/* 0x47ad40(R, path): constructor and open; counter + 1 after the open. */
static int r_ctor_open(es2_sim_startup *s, uint32_t r, uint32_t path) {
    base_ctor(s, r);
    su_w32(s, r, VT_READ);
    su_w16(s, r + 8u, READ_HANDLE_NONE);
    su_w8(s, r + 0xau, 0);
    int al;
    if (r_open(s, r, path, &al)) return 1;
    su_counter(s, 1);
    return 0;
}
/* 0x47aeb0(R): _lseek(handle, 0, SEEK_CUR). */
static int r_tell(es2_sim_startup *s, uint32_t r, uint32_t *pos) {
    uint32_t a[3] = {sx16(su_r16(s, r + 8u)), 0, 1};
    return su_call(s, VA_LSEEK, a, 3, pos);
}
/* 0x47aecc(R, pos): status 2 -> 0; another nonzero status: AL 0; _lseek(h,
 * pos, SEEK_SET) = -1: status 1 (0x47af60: its test reads the _errno thunk's
 * bytes, always nonzero), AL 0; else AL 1. */
static int r_seek(es2_sim_startup *s, uint32_t r, uint32_t pos, int *al) {
    if (su_r32(s, r + 4u) == 2u) su_w32(s, r + 4u, 0);
    if (su_r32(s, r + 4u)) { *al = 0; return 0; }
    uint32_t a[3] = {sx16(su_r16(s, r + 8u)), pos, 0}, eax = 0;
    if (su_call(s, VA_LSEEK, a, 3, &eax)) return 1;
    if (eax == 0xffffffffu) { su_w32(s, r + 4u, 1); *al = 0; return 0; }
    *al = 1;
    return 0;
}
/* 0x47af98(R, n, buf): status nonzero: AL 0; n = 0: AL 1; _read = -1: status
 * 1, AL 0; short: status 2, AL 0; else AL 1. */
static int r_read(es2_sim_startup *s, uint32_t r, uint32_t n, uint32_t buf, int *al) {
    if (su_r32(s, r + 4u)) { *al = 0; return 0; }
    if (!n) { *al = 1; return 0; }
    uint32_t a[3] = {sx16(su_r16(s, r + 8u)), buf, n}, got = 0;
    if (su_call(s, VA_READ, a, 3, &got)) return 1;
    if (got == 0xffffffffu) { su_w32(s, r + 4u, 1); *al = 0; return 0; }
    if (got != n) { su_w32(s, r + 4u, 2); *al = 0; return 0; }
    *al = 1;
    return 0;
}
/* The virtual destructor (slot 0, 0x47ad78) with flags 3, as 0x47cab0's
 * failure paths call it: counter - 1, vtable, close, the base destructor
 * 0x47a8a4(R, 0) (counter - 1, base vtable, its slot +0x10 0x47a904 only
 * sets AL), delete R. Before it the caller adds 0x496406(R, 0) to the
 * counter (listing 0x496406..0x496424): t = [vt - [vt - 4] - 0xc], the
 * class's type record, and the dword [t + 0x20] (both read from the image;
 * for the read stream it balances the destructor's two decrements, in vitro
 * the counter ends at 0). */
static int r_destroy(es2_sim_startup *s, uint32_t r) {
    uint32_t vt = su_r32(s, r);
    uint32_t type = su_r32(s, vt - su_r32(s, vt - 4u) - 0xcu), n;
    /* The count from the own RTTI_COUNT runs (CODE is no-access). */
    if (es2_sim_dbsim_rtti_count(type + 0x20u, &n)) n = su_r32(s, type + 0x20u);
    su_counter(s, (int32_t)n);
    if (vt != VT_READ) return su_refuse(s, vt, "read stream", "destructor of another class than the read stream");
    su_counter(s, -1);
    su_w32(s, r, VT_READ);
    int al;
    if (r_close(s, r, &al)) return 1;
    su_counter(s, -1);
    su_w32(s, r, VT_BASE);
    uint32_t a[1] = {r};
    return su_call(s, VA_DELETE, a, 1, NULL);
}

/* ---- the buffered stream B (0x21 bytes: +4 status, +8 source R, +0xc buffer
 * size, +0x10 file position of the buffer, +0x14 buffer, +0x18 owns it, +0x19
 * valid bytes, +0x1d cursor). Its source is always a read stream here. */

/* 0x47c95c(B): status <- the source's status, or 6 without a source. */
static void b_status(es2_sim_startup *s, uint32_t b) {
    uint32_t src = su_r32(s, b + 8u);
    su_w32(s, b + 4u, src ? su_r32(s, src + 4u) : 6u);
}
/* 0x47c644(B, src, size, 0): base class, vtable, 0x47c6d0 (fields, the
 * buffer from 0x47747c(size)), 0x47c718 (source, its tell as the buffer's
 * position, status), counter + 1. */
static int b_ctor(es2_sim_startup *s, uint32_t b, uint32_t src, uint32_t size) {
    base_ctor(s, b);
    su_w32(s, b, VT_BUFFERED);
    su_w32(s, b + 8u, 0);
    su_w32(s, b + 0x1du, 0);
    su_w32(s, b + 0x10u, 0);
    su_w32(s, b + 0x19u, 0);
    su_w32(s, b + 0xcu, size);
    su_w8(s, b + 0x18u, 1);
    uint32_t a[1] = {size}, buffer = 0;
    if (su_call(s, VA_GAME_CALLOC, a, 1, &buffer)) return 1;
    su_w32(s, b + 0x14u, buffer);
    /* 0x47c718: [B+8] is 0, so 0x47c754 (detach) is skipped. */
    su_w32(s, b + 8u, src);
    uint32_t pos = 0;
    if (r_tell(s, src, &pos)) return 1;
    su_w32(s, b + 0x10u, pos);
    su_w32(s, b + 0x1du, 0);
    su_w32(s, b + 0x19u, 0);
    b_status(s, b);
    su_counter(s, 1);
    return 0;
}
/* 0x47c8c4(B, pos): source seek; source read of the buffer size; a short
 * read at the file end keeps what it got (tell - pos), none gives status 2. */
static int b_refill(es2_sim_startup *s, uint32_t b, uint32_t pos, int *al) {
    uint32_t src = su_r32(s, b + 8u);
    int ok;
    if (r_seek(s, src, pos, &ok)) return 1;
    if (!ok) { b_status(s, b); *al = 0; return 0; }
    uint32_t valid = su_r32(s, b + 0xcu);
    if (r_read(s, src, su_r32(s, b + 0xcu), su_r32(s, b + 0x14u), &ok)) return 1;
    if (!ok) {
        if (su_r32(s, src + 4u) != 2u) { b_status(s, b); *al = 0; return 0; }
        uint32_t end = 0;
        if (r_tell(s, src, &end)) return 1;
        valid = end - pos;
        if (!valid) {
            su_w32(s, b + 4u, 2);
            su_w32(s, b + 0x10u, pos);
            su_w32(s, b + 0x19u, 0);
            su_w32(s, b + 0x1du, 0);
            *al = 0;
            return 0;
        }
    }
    su_w32(s, b + 0x19u, valid);
    su_w32(s, b + 0x10u, pos);
    su_w32(s, b + 0x1du, 0);
    *al = 1;
    return 0;
}
/* 0x47c82c(B, n, dst): copies from the window (memcpy thunk, here inline),
 * refills at base + valid while the window holds less than n (signed). */
static void copy_bytes(es2_sim_startup *s, uint32_t d, uint32_t p, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) su_w8(s, d + i, su_r8(s, p + i));
}
static int b_read(es2_sim_startup *s, uint32_t b, uint32_t n, uint32_t dst, int *al) {
    if (su_r32(s, b + 4u)) { *al = 0; return 0; }
    int32_t want = (int32_t)n;
    int32_t avail = (int32_t)(su_r32(s, b + 0x19u) - su_r32(s, b + 0x1du));
    while (avail < want) {
        copy_bytes(s, dst, su_r32(s, b + 0x14u) + su_r32(s, b + 0x1du), (uint32_t)avail);
        int ok;
        if (b_refill(s, b, su_r32(s, b + 0x10u) + su_r32(s, b + 0x19u), &ok)) return 1;
        if (!ok) { *al = 0; return 0; }
        want -= avail;
        dst += (uint32_t)avail;
        avail = (int32_t)(su_r32(s, b + 0x19u) - su_r32(s, b + 0x1du));
    }
    copy_bytes(s, dst, su_r32(s, b + 0x14u) + su_r32(s, b + 0x1du), (uint32_t)want);
    su_w32(s, b + 0x1du, su_r32(s, b + 0x1du) + (uint32_t)want);
    *al = 1;
    return 0;
}

/* ---- the archive open 0x47cab0(path, mask, size): 0 or the archive A.
 * `local` holds its frame locals [ebp-0x48..-0x38): +0 the use mask, +4 the
 * fourth signature byte, +5 the signature, +8 the record bytes, +0xe the
 * directory bytes. */
static int archive_body(es2_sim_startup *s, uint32_t path, uint32_t mask, uint32_t size, uint32_t local, uint32_t *archive) {
    uint32_t a[1] = {0xbu}, r = 0;
    if (su_call(s, VA_NEW, a, 1, &r)) return 1;
    if (!r) return su_null_refuse(s, 0x0047cad5u, "operator new", "the read stream's allocation failed (the original reads [0+4])");
    if (r_ctor_open(s, r, path)) return 1;
    su_counter(s, -2);
    int ok;
    if (su_r32(s, r + 4u)) return r_destroy(s, r);                  /* 0x4930ab: not opened */
    if (r_read(s, r, 3, local + 5u, &ok)) return 1;                 /* 0x47d02d: the signature */
    /* strncmp(signature, [0x4a1c2c], 3) (thunk 0x496778, pure): */
    uint32_t literal = su_r32(s, VA_SIGNATURE_POINTER);
    int differ = 0;
    for (uint32_t i = 0; i < 3u; ++i) {
        uint8_t x = su_r8(s, local + 5u + i), y = su_r8(s, literal + i);
        if (x != y) { differ = 1; break; }
        if (!x) break;
    }
    if (differ) return r_destroy(s, r);
    if (r_read(s, r, 1, local + 4u, &ok)) return 1;                 /* 0x47d046: one byte */
    if (r_read(s, r, 4, local, &ok)) return 1;                      /* 0x47d05f: the use mask */
    uint32_t use = su_r32(s, local);
    if (!(use & mask)) return r_destroy(s, r);
    a[0] = 0x21u;
    uint32_t b = 0;
    if (su_call(s, VA_NEW, a, 1, &b)) return 1;
    if (!b) return su_null_refuse(s, 0x0047cc41u, "operator new", "the buffered stream's allocation failed");
    if (b_ctor(s, b, r, size)) return 1;
    su_counter(s, -2);
    a[0] = 0x30u;
    uint32_t ar = 0;
    if (su_call(s, VA_NEW, a, 1, &ar)) return 1;
    if (!ar) return su_null_refuse(s, 0x0047cc7du, "operator new", "the archive's allocation failed");
    /* 0x47c9a0(A), then the counter - 1 at 0x47cca0. */
    su_w32(s, ar + 0x24u, 0);
    su_w32(s, ar + 0x28u, 0);
    su_w32(s, ar + 0x2cu, 0);
    su_w32(s, ar + 4u, 0);
    su_w32(s, ar, 0);
    su_counter(s, 1);
    su_counter(s, -1);
    su_w32(s, ar + 0x20u, use);
    su_w32(s, ar, r);
    su_w32(s, ar + 4u, b);
    /* strcpy(A+8, path) without a bound (vol.md: paths of 20 characters or
     * more overwrite +0x1c.. before those fields are read); a path that would
     * leave the 0x30-byte block is refused rather than written past it. */
    if (su_strlen(s, path) + 1u > 0x28u)
        return su_refuse(s, 0x0047ccddu, "archive path", "the path would overrun the 0x30-byte archive object");
    su_strcpy(s, ar + 8u, path);
    if (b_read(s, b, 1, ar + 0x1cu, &ok) || b_read(s, b, 1, ar + 0x1du, &ok) || b_read(s, b, 2, local + 0xeu, &ok))
        return 1;
    if (su_r8(s, ar + 0x1du)) {
        a[0] = 4u * su_r8(s, ar + 0x1du);
        uint32_t dirs = 0, bytes = 0;
        if (su_call(s, VA_NEW_ARRAY, a, 1, &dirs)) return 1;
        su_w32(s, ar + 0x24u, dirs);
        a[0] = su_r16(s, local + 0xeu);
        if (su_call(s, VA_NEW_ARRAY, a, 1, &bytes)) return 1;
        su_w32(s, ar + 0x28u, bytes);
        su_w32(s, su_r32(s, ar + 0x24u), bytes);
        if (b_read(s, b, su_r16(s, local + 0xeu), su_r32(s, ar + 0x28u), &ok)) return 1;
    }
    if (b_read(s, b, 2, ar + 0x1eu, &ok) || b_read(s, b, 4, local + 8u, &ok)) return 1;
    uint16_t members = su_r16(s, ar + 0x1eu);
    if (members) {
        a[0] = 18u * members;
        uint32_t records = 0;
        if (su_call(s, VA_NEW_ARRAY, a, 1, &records)) return 1;
        su_w32(s, ar + 0x2cu, records);
        uint32_t field = su_r32(s, local + 8u);
        if (field > 18u * members + 8u)
            return su_refuse(s, 0x0047cd8cu, "archive records", "the record size field exceeds the record table's block");
        if (b_read(s, b, field, su_r32(s, ar + 0x2cu), &ok)) return 1;
    }
    if (su_r32(s, b + 4u))
        return su_refuse(s, 0x0047c9c8u, "archive destructor", "a short archive (buffered stream in error): destructor path not modelled");
    /* The directory string pointers: one after each NUL of the bytes. */
    uint32_t p = su_r32(s, ar + 0x28u);
    for (uint16_t i = 1; (uint32_t)i < su_r8(s, ar + 0x1du);) {
        if (!su_r8(s, p)) { ++p; su_w32(s, su_r32(s, ar + 0x24u) + 4u * i, p); ++i; }
        else ++p;
    }
    *archive = ar;
    return 0;
}
static int archive_open(es2_sim_startup *s, uint32_t path, uint32_t mask, uint32_t size, uint32_t *archive) {
    *archive = 0;
    if (size <= 0x80u) size = 0x80u;
    uint32_t top = s->frame_hi;
    int refused = archive_body(s, path, mask, size, su_frame_push(s, 0x10u), archive);
    su_frame_pop(s, top);
    return refused;
}

/* 0x467f94(M, path, mask): the archive, inserted by carry (a slot is
 * replaced only by a strictly higher priority byte +0x1c, unsigned), count +1. */
static int insert(es2_sim_startup *s, uint32_t m, uint32_t path, uint32_t mask) {
    uint32_t ar = 0;
    if (archive_open(s, path, mask, su_r32(s, VA_STREAM_BUFFER), &ar)) return 1;
    if (!ar) return 0;
    if (su_r8(s, m + 0x78u) >= 0x1eu)
        return su_refuse(s, 0x00467fe1u, "archive manager", "more than 30 archives (the original writes past its 30 slots)");
    uint32_t carried = ar;
    for (uint32_t i = 0; (uint8_t)i <= su_r8(s, m + 0x78u); ++i) {
        uint32_t slot = su_r32(s, m + 4u * i);
        if (!slot) { su_w32(s, m + 4u * i, carried); continue; }
        if (su_r8(s, slot + 0x1cu) < su_r8(s, carried + 0x1cu)) { su_w32(s, m + 4u * i, carried); carried = slot; }
    }
    su_w8(s, m + 0x78u, (uint8_t)(su_r8(s, m + 0x78u) + 1u));
    ++s->archives;
    return 0;
}

/* 0x47d2c4(a, b, c, d, 0): the concatenation into the static 0x6c5dab. */
static uint32_t build_pattern(es2_sim_startup *s, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    su_w8(s, VA_PATTERN, 0);
    if (a) su_strcat(s, VA_PATTERN, a);
    if (b) su_strcat(s, VA_PATTERN, b);
    if (c) su_strcat(s, VA_PATTERN, c);
    if (d) su_strcat(s, VA_PATTERN, d);
    return VA_PATTERN;
}

/* One pass of 0x467cd8: enumerate dir + "vol\" + "*" + name, skip a file
 * whose "vol\"+name equals a registered archive's path after its drive
 * (0x467cbc; case-sensitive), register the others with 0x467c54(dir, ...). */
static int pass(es2_sim_startup *s, uint32_t m, uint32_t mask, uint32_t name, uint32_t dir, uint32_t vol, uint32_t star,
                uint32_t prefix, uint32_t ffblk, uint32_t candidate) {
    uint32_t pattern = build_pattern(s, dir, vol, star, name);
    uint32_t a[3] = {pattern, ffblk, 0}, r = 0;
    if (su_call(s, VA_FINDFIRST, a, 3, &r)) return 1;
    while (r == 0) {
        int found = 0;
        su_strcpy(s, candidate, prefix);
        su_strcat(s, candidate, ffblk + 0x10u);
        for (uint32_t i = 0; !found && (uint8_t)i < su_r8(s, m + 0x78u); ++i) {
            uint32_t path = su_r32(s, m + 4u * i) + 8u;
            if (su_r8(s, path + 1u) == ':') path += 2u;
            found = su_streq(s, path, candidate);
        }
        if (!found) {
            /* 0x467c54(dir, candidate): (dir or "") + candidate after a drive. */
            uint32_t rel = candidate + (su_r8(s, candidate + 1u) == ':' ? 2u : 0u);
            su_strcpy(s, VA_ARCHIVE_PATH, dir ? dir : 0x004a0890u);
            su_strcat(s, VA_ARCHIVE_PATH, rel);
            if (insert(s, m, VA_ARCHIVE_PATH, mask)) return 1;
        }
        uint32_t b[1] = {ffblk};
        if (su_call(s, VA_FINDNEXT, b, 1, &r)) return 1;
    }
    return 0;
}

int es2_sim_startup_register_volumes(es2_sim_startup *s, uint32_t mask, uint32_t name, uint32_t root, uint32_t alternate_first) {
    uint32_t m = su_r32(s, ES2_SIM_VA_ARCHIVES);
    if (m) return su_refuse(s, 0x004732cau, "archive manager", "a second registration (0x467cd8's refresh) is not modelled");
    uint32_t a[1] = {0x79u};
    if (su_call(s, VA_NEW, a, 1, &m)) return 1;
    if (!m) { su_w32(s, ES2_SIM_VA_ARCHIVES, 0); return 0; }
    /* 0x467b9c: 30 zero slots, the buffer size 0x400, count 0, 0x467cd8,
     * counter + 1; then the counter - 1 at 0x4732b2. */
    for (uint32_t i = 0; i < 30u; ++i) su_w32(s, m + 4u * i, 0);
    su_w32(s, VA_STREAM_BUFFER, 0x400u);
    su_w8(s, m + 0x78u, 0);
    uint32_t top = s->frame_hi;
    uint32_t ffblk = su_frame_push(s, 0x110u), first = su_frame_push(s, 0x20u), second = su_frame_push(s, 0x20u);
    int refused = 0;
    /* The removal loop over registered archives finds none (count 0); the
     * full check (count 30) cannot hold. */
    if (!refused) refused = pass(s, m, mask, name, alternate_first ? root : 0, 0x004a0891u, 0x004a0896u, 0x004a0898u, ffblk, first);
    if (!refused) refused = pass(s, m, mask, name, alternate_first ? 0 : root, 0x004a089du, 0x004a08a2u, 0x004a08a4u, ffblk, second);
    su_frame_pop(s, top);
    if (refused) return 1;
    su_counter(s, 1);
    su_counter(s, -1);
    su_w32(s, ES2_SIM_VA_ARCHIVES, m);
    return 0;
}
