#define WIN32_LEAN_AND_MEAN
#include "es2/formats/vfs.h"
#include "state.h"
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wincrypt.h>
static BYTE *mapped, *reserved;
static DWORD reservation_error;
static int reservation_in_stack;
#define STARTUP_IMAGE_SIZE 0x1ea000u
#include "tools/reference-game/invitro/startup_reservation.h"
extern void __cdecl mainCRTStartup(void);
void __cdecl es2_shell_start(void) {
  MEMORY_BASIC_INFORMATION region = {0};
  VirtualQuery(&region, &region, sizeof(region));
  reserved = claim_image_range(region.AllocationBase, &reservation_error,
                               &reservation_in_stack);
  mainCRTStartup();
  ExitProcess(120);
}
static uint16_t get16(const BYTE *p) {
  return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t get32(const BYTE *p) {
  return (uint32_t)get16(p) | (uint32_t)get16(p + 2) << 16;
}
static void put16(BYTE *p, uint16_t n) {
  p[0] = (BYTE)n;
  p[1] = (BYTE)(n >> 8);
}
static void put32(BYTE *p, uint32_t n) {
  put16(p, (uint16_t)n);
  put16(p + 2, (uint16_t)(n >> 16));
}
static int extent(uint32_t p, uint32_t n, uint32_t z) {
  return p <= z && n <= z - p;
}
static const char *phase = "startup";
static int fail(const char *s) {
  fprintf(stderr, "FAIL %s: %s (Win32 %lu)\n", phase, s,
          (unsigned long)GetLastError());
  return 1;
}
static void die(const char *s) {
  fail(s);
  ExitProcess(1);
}
static char imports[1024][192];
static unsigned import_count;
static void *first_import_trap;
static void __cdecl import_trap(unsigned id) {
  fprintf(stderr, "TRAP %s import %s\n", phase, imports[id]);
  ExitProcess(1);
}
extern void __cdecl es2_rt_memcpy(void), es2_rt_memset(void);
#define IN_VITRO_FILE_SIZE 566304
#define IN_VITRO_IMAGE_SIZE STARTUP_IMAGE_SIZE
#define IN_VITRO_SHA256                                                        \
  "1534aa8f732bdecd826484d893ad58b8a5e6ebd4d25ab3158a2469b8f69ba16e"
#define IN_VITRO_EXE "VSHELL.EXE"
#define IN_VITRO_LABEL "VSHELL"
#define IN_VITRO_FPU_INIT()                                                    \
  do {                                                                         \
    unsigned short cw = 0x1332;                                                \
    __asm__ volatile("fninit; fldcw %0" ::"m"(cw));                            \
  } while (0)
static int image_message(const char *format, ...) {
  va_list a;
  va_start(a, format);
  int n = vfprintf(stderr, format, a);
  va_end(a);
  return n;
}
#define printf image_message
#include "tools/reference-game/invitro/mapped_image.h"
#undef printf
#define P(a) ((BYTE *)(uintptr_t)(a))
static void trace_before(uint32_t a);
#define CALL0(a)                                                               \
  (trace_before((uint32_t)(a)), ((uint32_t(__cdecl *)(void))(uintptr_t)(a))())
#define CALL1(a, x)                                                            \
  (trace_before((uint32_t)(a)),                                                \
   ((uint32_t(__cdecl *)(uintptr_t))(uintptr_t)(a))((uintptr_t)(x)))
#define CALL2(a, x, y)                                                         \
  (trace_before((uint32_t)(a)),                                                \
   ((uint32_t(__cdecl *)(uintptr_t, uintptr_t))(uintptr_t)(a))(                \
       (uintptr_t)(x), (uintptr_t)(y)))
#define CALL3(a, x, y, z)                                                      \
  (trace_before((uint32_t)(a)),                                                \
   ((uint32_t(__cdecl *)(uintptr_t, uintptr_t, uintptr_t))(uintptr_t)(a))(     \
       (uintptr_t)(x), (uintptr_t)(y), (uintptr_t)(z)))
#include "trace.h"
static int ui_trap(EXCEPTION_POINTERS *e);
static LONG CALLBACK fault(EXCEPTION_POINTERS *e) {
  if (trace_fault(e))
    return EXCEPTION_CONTINUE_EXECUTION;
  if (ui_trap(e))
    return EXCEPTION_CONTINUE_EXECUTION;
  if (e->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT &&
      (uintptr_t)e->ExceptionRecord->ExceptionAddress == 0x44e560) {
    uint32_t *s = (uint32_t *)(uintptr_t)e->ContextRecord->Esp;
    fprintf(stderr,
            "ASSERT %s caller=%08x object=%08x level=%u condition=%u file=%s "
            "line=%u message=%s\n",
            phase, s[0], s[1], s[2], s[3], (char *)P(s[4]), s[5],
            s[6] ? (char *)P(s[6]) : "");
    ExitProcess(1);
  }
  fprintf(stderr,
          "FAULT %s code=%08lx EIP=%08lx EAX=%08lx EBX=%08lx ECX=%08lx "
          "EDX=%08lx address=%08lx\n",
          phase, (unsigned long)e->ExceptionRecord->ExceptionCode,
          (unsigned long)e->ContextRecord->Eip,
          (unsigned long)e->ContextRecord->Eax,
          (unsigned long)e->ContextRecord->Ebx,
          (unsigned long)e->ContextRecord->Ecx,
          (unsigned long)e->ContextRecord->Edx,
          (unsigned long)e->ExceptionRecord->ExceptionInformation[1]);
  for (uint32_t *b = (uint32_t *)(uintptr_t)e->ContextRecord->Ebp, i = 0;
       b && i < 12; i++) {
    MEMORY_BASIC_INFORMATION q;
    if (!VirtualQuery(b, &q, sizeof(q)) || q.State != MEM_COMMIT)
      break;
    fprintf(stderr, "FRAME %08x\n", b[1]);
    if (b[0] <= (uintptr_t)b)
      break;
    b = (uint32_t *)(uintptr_t)b[0];
  }
  ExitProcess(1);
  return EXCEPTION_CONTINUE_SEARCH;
}
static es2_span disk_read(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "read %s\n", path);
    die("input open");
  }
  if (fseek(f, 0, SEEK_END))
    die("input seek");
  long n = ftell(f);
  if (n < 0 || n > 128 * 1024 * 1024)
    die("input size");
  rewind(f);
  BYTE *b = malloc((size_t)n + 1);
  if (!b || fread(b, 1, (size_t)n, f) != (size_t)n)
    die("input read");
  fclose(f);
  b[n] = 0;
  return (es2_span){b, (size_t)n};
}
static void digest(es2_span s, char hex[65]) {
  HCRYPTPROV p = 0;
  HCRYPTHASH h = 0;
  BYTE d[32];
  DWORD n = 32;
  if (!CryptAcquireContextW(&p, NULL, NULL, PROV_RSA_AES,
                            CRYPT_VERIFYCONTEXT) ||
      !CryptCreateHash(p, CALG_SHA_256, 0, 0, &h) ||
      !CryptHashData(h, s.data, (DWORD)s.size, 0) ||
      !CryptGetHashParam(h, HP_HASHVAL, d, &n, 0))
    die("hash");
  for (unsigned i = 0; i < 32; i++)
    sprintf(hex + 2 * i, "%02x", d[i]);
  CryptDestroyHash(h);
  CryptReleaseContext(p, 0);
}
static es2_vol vols[32];
static es2_vfs_source sources[32];
static es2_vfs vfs;
static unsigned vol_count;
static void mount_cd(const char *root) {
  char pattern[MAX_PATH], path[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s/VOL/*.VOL", root);
  WIN32_FIND_DATAA f;
  HANDLE h = FindFirstFileA(pattern, &f);
  if (h == INVALID_HANDLE_VALUE)
    die("CD VOL directory");
  do {
    if (vol_count == 32)
      die("archive limit");
    snprintf(path, sizeof(path), "%s/VOL/%s", root, f.cFileName);
    es2_span s = disk_read(path);
    es2_error e;
    if (es2_vol_open(s, &vols[vol_count], &e) != ES2_OK)
      die("invalid VOL");
    sources[vol_count].kind = ES2_VFS_VOL;
    sources[vol_count].data.vol = &vols[vol_count];
    vol_count++;
  } while (FindNextFileA(h, &f));
  FindClose(h);
  for (unsigned i = 0; i < vol_count; i++)
    for (unsigned j = i + 1; j < vol_count; j++)
      if (sources[j].data.vol->priority > sources[i].data.vol->priority) {
        es2_vfs_source t = sources[i];
        sources[i] = sources[j];
        sources[j] = t;
      }
  if (es2_vfs_open(sources, vol_count, &vfs, NULL) != ES2_OK)
    die("VFS open");
}
/* Index 4 is the optional SAV image written by the original save writers
 * (--emit-save); it never reaches the four launch files. Index 5 is the
 * supplied results.dat of the results path (--results); it is only read. */
static const char *out_names[] = {"script.dat", "MISSION.STR", "mission.var",
                                  "player.mec", "output.sav", "results.dat"};
#define OUTPUT_COUNT 6
#include "memory.h"
static buffer outputs[OUTPUT_COUNT];
/* In results mode the original reads DATA files it or DBSIM wrote earlier:
 * mission.var and results.dat (supplied), MISSION.STR (written by 0x418014
 * during the same run). Those read opens are served from the sinks. */
static int results_mode;
typedef struct descriptor {
  const BYTE *input;
  buffer *output;
  size_t size, pos;
  int used;
  char name[256];
} descriptor;
static descriptor descriptors[64];
static es2_span save_bytes;
/* --overlay NAME FILE: synthetic test files (our own, never CD members) that
 * shadow a VFS path for read opens, e.g. an invented MSN for B3 probes. */
static struct { const char *name; es2_span bytes; } overlays[4];
static unsigned overlay_count;
static int same_path(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    char x = *a == '\\' ? '/' : *a, y = *b == '\\' ? '/' : *b;
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return 0;
  }
  return !*a && !*b;
}
/* Memory disk of the slot mode (--op): the save directory and the DATA
 * launch files as VSHELL names them (sav\gamefile.str, sav\GAME_R.SAV,
 * data\script.dat, ...). Inputs come from --file; every original write
 * lands here and the whole disk is returned. CD members still come from the
 * VFS. Names compare case-insensitively with either slash. */
typedef struct vfile {
  char name[64];
  buffer data;
} vfile;
static vfile vfiles[32];
static unsigned vfile_count;
static int vdisk_mode;
static vfile *vdisk_find(const char *path, int create) {
  for (unsigned i = 0; i < vfile_count; i++)
    if (same_path(vfiles[i].name, path))
      return &vfiles[i];
  if (!create)
    return NULL;
  if (vfile_count == 32 || strlen(path) >= sizeof(vfiles[0].name))
    die("memory disk limit");
  vfile *f = &vfiles[vfile_count++];
  snprintf(f->name, sizeof(f->name), "%s", path);
  for (char *p = f->name; *p; p++)
    if (*p == '/')
      *p = '\\';
  return f;
}
static void vdisk_add(const char *name, es2_span bytes) {
  vfile *f = vdisk_find(name, 1);
  f->data.size = 0;
  if (bytes.size && shell_buffer_write(&f->data, 0, bytes.data, bytes.size) < 0)
    die("memory disk input");
}
static int output_index(const char *path) {
  const char *base = path;
  for (const char *p = path; *p; p++)
    if (*p == '/' || *p == '\\')
      base = p + 1;
  for (int i = 0; i < OUTPUT_COUNT; i++)
    if (!_stricmp(base, out_names[i]))
      return i;
  return -1;
}
static int mem_open(const char *path, int flags, ...) {
  es2_span s = {0};
  int out = -1;
  if (flags != 0x8000 && flags != 0x8102 && flags != 0x8301)
    die("unsupported open flags");
  vfile *vf = NULL;
  if (vdisk_mode && strcmp(path, "input.sav")) {
    vf = vdisk_find(path, (flags & 3) != 0);
    if (!vf && !(flags & 3) && (!_strnicmp(path, "sav", 3) || !_strnicmp(path, "data", 4))) {
      fprintf(stderr, "OPEN-MISS %s flags=%x\n", path, flags);
      return -1;
    }
  }
  if (vf) {
    for (int fd = 3; fd < 64; fd++)
      if (!descriptors[fd].used) {
        descriptor *d = &descriptors[fd];
        memset(d, 0, sizeof(*d));
        d->used = 1;
        d->output = &vf->data;
        if (flags & 0x200)
          vf->data.size = 0;
        snprintf(d->name, sizeof(d->name), "%s", path);
        fprintf(stderr, "DISK %s %s\n", flags & 3 ? "write" : "read", path);
        return fd;
      }
    die("descriptor limit");
  }
  if (flags & 3) {
    out = output_index(path);
    if (out < 0)
      die("unexpected write path");
  } else if (!strcmp(path, "input.sav"))
    s = save_bytes;
  else if (results_mode && output_index(path) >= 0)
    out = output_index(path);
  else {
    for (unsigned i = 0; i < overlay_count; i++)
      if (same_path(path, overlays[i].name)) {
        s = overlays[i].bytes;
        goto found;
      }
    es2_vfs_copy c;
    if (es2_vfs_find(&vfs, path, &c, NULL) != ES2_OK) {
      fprintf(stderr, "OPEN-MISS %s flags=%x\n", path, flags);
      return -1;
    }
    s = c.payload;
  }
found:
  for (int fd = 3; fd < 64; fd++)
    if (!descriptors[fd].used) {
      descriptor *d = &descriptors[fd];
      memset(d, 0, sizeof(*d));
      d->used = 1;
      d->input = s.data;
      d->size = s.size;
      d->output = out < 0 ? NULL : &outputs[out];
      if (d->output && (flags & 3) && (flags & 0x200))
        d->output->size = 0;
      snprintf(d->name, sizeof(d->name), "%s", path);
      if (out < 0) {
        char hash[65];
        digest(s, hash);
        fprintf(stderr, "RESOURCE %s %zu %s\n", path, s.size, hash);
      }
      return fd;
    }
  die("descriptor limit");
  return -1;
}
static descriptor *fd_get(int f) {
  if (f < 3 || f >= 64 || !descriptors[f].used)
    die("invalid descriptor");
  return &descriptors[f];
}
static int mem_close(int f) {
  fd_get(f)->used = 0;
  return 0;
}
static int mem_read(int f, void *p, unsigned n) {
  descriptor *d = fd_get(f);
  size_t size = d->output ? d->output->size : d->size;
  const BYTE *b = d->output ? d->output->data : d->input;
  size_t got = shell_buffer_read(b, size, d->pos, p, n);
  d->pos += got;
  return (int)got;
}
static int mem_write(int f, const void *p, unsigned n) {
  descriptor *d = fd_get(f);
  if (vdisk_mode && getenv("ES2_DISK_TRACE")) /* slot mode: every write */
    fprintf(stderr, "WRITE %s at %zu n %u\n", d->name, d->pos, n);
  int got = shell_buffer_write(d->output, d->pos, p, n);
  if (got < 0)
    die("write limit/allocation");
  d->pos += (unsigned)got;
  return got;
}
static long mem_seek(int f, long offset, int whence) {
  descriptor *d = fd_get(f);
  int64_t base = whence == 0   ? 0
                 : whence == 1 ? (int64_t)d->pos
                 : whence == 2
                     ? (int64_t)(d->output ? d->output->size : d->size)
                     : -1;
  int64_t p = base + offset;
  if (base < 0 || p < 0 || p > 16 * 1024 * 1024)
    return -1;
  d->pos = (size_t)p;
  return (long)p;
}
static int mem_remove(const char *p) {
  int i = output_index(p);
  if (i < 0)
    die("unexpected remove path");
  outputs[i].size = 0;
  return 0;
}
/* Original allocator statistics read the dword immediately before a block. */
static void *rt_calloc(size_t n, size_t z) {
  if (n && z > 16 * 1024 * 1024 / n)
    die("allocation limit");
  size_t size = n * z;
  BYTE *p = calloc(1, size + 16);
  if (!p)
    die("allocation");
  put32(p + 12, (uint32_t)size);
  return p + 16;
}
/* Results batches restore a start-up snapshot of the image data section per
 * request; blocks reachable from that snapshot must stay valid, so frees are
 * deferred to process exit there (batches are bounded by the driver). */
static int defer_free;
static void rt_free(void *p) {
  if (p && !defer_free)
    free((BYTE *)p - 16);
}
static int exception_depth;
static BYTE empty_volume_group[121];
static void bind_import(const char *name, void *target) {
  BYTE *nt = mapped + get32(mapped + 60), *dir = mapped + get32(nt + 24 + 104);
  for (BYTE *d = dir; get32(d + 12); d += 20) {
    if (_stricmp((char *)mapped + get32(d + 12), "cw3220.dll"))
      continue;
    uint32_t l = get32(d), a = get32(d + 16);
    if (!l)
      l = a;
    for (unsigned j = 0; get32(mapped + l + j * 4); j++) {
      uint32_t t = get32(mapped + l + j * 4);
      if (!(t >> 31) && !strcmp((char *)mapped + t + 2, name)) {
        put32(mapped + a + j * 4, (uint32_t)(uintptr_t)target);
        return;
      }
    }
  }
  die("missing import binding");
}
static void runtime_init(void) {
  if (!first_import_trap)
    die("missing trap region");
#define B(n, f) bind_import(n, (void *)(uintptr_t)(f))
  B("_open", mem_open);
  B("_close", mem_close);
  B("_read", mem_read);
  B("_write", mem_write);
  B("_lseek", mem_seek);
  B("_remove", mem_remove);
  B("_calloc", rt_calloc);
  B("_free", rt_free);
  B("_strcpy", strcpy);
  B("_strcat", strcat);
  B("_strlen", strlen);
  B("_strchr", strchr);
  B("_memmove", memmove);
#undef B
  put32(P(0x5ddfa0), (uint32_t)(uintptr_t)&exception_depth);
  put32(P(0x46cae4), (uint32_t)(uintptr_t)empty_volume_group);
  DWORD old;
  if (*P(0x44e560) != 0x55)
    die("assert signature");
  VirtualProtect(P(0x44e560), 1, PAGE_EXECUTE_READWRITE, &old);
  *P(0x44e560) = 0xcc;
  VirtualProtect(P(0x44e560), 1, old, &old);
  FlushInstructionCache(GetCurrentProcess(), NULL, 0);
}
static void original_read(BYTE *stream, void *dst, unsigned n) {
  CALL3(get32(P(get32(stream)) + 0x18), stream, n, dst);
}
static void restore_save(void) {
  BYTE s[64] = {0};
  phase = "SAV open";
  CALL2(0x44ed84, s, "input.sav");
  phase = "inventory restore";
  CALL1(0x412781, s);
  /* Selection reader also copies old launch companions. Read only its proven
   * nine spans here: those copies are not preparation and must not be inputs.
   */
  const uint32_t addresses[] = {0x46fc58, 0x46fc5a, 0x4840cc,
                                0x4840ce, 0x4840e2, 0x4840e4,
                                0x484120, 0x484122, 0x48419c};
  const unsigned sizes[] = {2, 2, 2, 20, 2, 60, 2, 60, 2};
  for (unsigned i = 0; i < 9; i++)
    original_read(s, P(addresses[i]), sizes[i]);
  phase = "roster restore";
  CALL1(0x41029b, s);
  CALL2(0x4107dc, 0x482b5c, s);
  phase = "loadout restore";
  CALL2(0x410e2e, 0x482ba7, s);
  CALL1(0x411fad, s);
  original_read(s, P(0x482bd8), 4);
  original_read(s, P(0x482bdc), 2000);
  original_read(s, P(0x4826f2), 2);
  original_read(s, P(0x4833ac), 20);
  fprintf(stderr, "SAV consumed=%ld size=%zu group=%u mission=%u\n",
          mem_seek((int16_t)get16(s + 8), 0, 1), save_bytes.size,
          get16(P(0x46fc58)), get16(P(0x46fc5a)));
  CALL2(0x44edbc, s, 2);
}
static void original_write(BYTE *stream, const void *src, unsigned n) {
  CALL3(get32(P(get32(stream)) + 0x14), stream, n, src);
}
/* Original SAV writer sequence 0x40e9d2..0x40ea81 into private output 4.
 * The selection helper 0x413095 also copies launch companions, so its nine
 * proven spans are written directly through the same original stream. */
static void emit_save(void) {
  BYTE s[64] = {0};
  outputs[4].size = 0;
  phase = "SAV write open";
  CALL2(0x44e9d8, s, "output.sav");
  if ((int16_t)get16(s + 8) < 0)
    die("SAV output stream");
  phase = "inventory write";
  CALL1(0x4127f3, s);
  const uint32_t addresses[] = {0x46fc58, 0x46fc5a, 0x4840cc,
                                0x4840ce, 0x4840e2, 0x4840e4,
                                0x484120, 0x484122, 0x48419c};
  const unsigned sizes[] = {2, 2, 2, 20, 2, 60, 2, 60, 2};
  for (unsigned i = 0; i < 9; i++)
    original_write(s, P(addresses[i]), sizes[i]);
  phase = "roster write";
  CALL1(0x41023a, s);
  CALL2(0x410791, 0x482b5c, s);
  phase = "hangar write";
  CALL2(0x410c7c, 0x482ba7, s);
  CALL1(0x411f78, s);
  original_write(s, P(0x482bd8), 4);
  original_write(s, P(0x482bdc), 2000);
  original_write(s, P(0x4826f2), 2);
  original_write(s, P(0x4833ac), 20);
  CALL2(0x44ea10, s, 2);
  if (!outputs[4].size)
    die("empty SAV output");
}
/* UI and disk side effects called directly by the results path 0x40f09f
 * (static closure, vshell_preparation.md "Results path"). Their
 * entry byte is checked, then replaced in private memory by a breakpoint that
 * records the call and returns 0 to the caller; nothing below them runs. */
typedef struct ui_entry {
  uint32_t address;
  BYTE first;
} ui_entry;
static const ui_entry ui_entries[] = {{0x402266, 0x53}, {0x4077ee, 0x55}, {0x407b62, 0x55},
                  {0x407c1b, 0x80}, {0x40e933, 0x55}, {0x40f904, 0x55},
                  {0x413305, 0x55}, {0x41e8c0, 0x55}, {0x41e98c, 0x55},
                  {0x41f93a, 0x55}, {0x44d0e7, 0x55}};
#define UI_ENTRY_COUNT (sizeof(ui_entries) / sizeof(ui_entries[0]))
/* Slot mode (--op): the DEBUG dialog that ends a new game (0x413053 ->
 * 0x413305) and the map object a load rebuilds (0x40eaaa -> 0x40e75f). */
static const ui_entry slot_ui_entries[] = {{0x413305, 0x55}, {0x40e75f, 0x55}};
/* The slot mode with the box, launch-check and Preferences operations
 * (run_slots) arms these as well. Recorded: the launch warning box 0x44d90c(n),
 * the End of game box 0x44d55f(final word), the main menu 0x431718 and the
 * service-bay show 0x4317c3/0x43b7f2/0x43b758 (CONTINUE with final word 2).
 * Quiet: the busy cursor 0x408d2f, show/hide 0x41f93a/0x41fabd, the
 * Preferences control refreshes (0x436e20, 0x436fbe, 0x43716c, 0x4370b8,
 * 0x436e5d), CD music stop/start 0x42f7f0/0x42f894 and the display switch
 * 0x407636: they draw or touch devices and change no state the tests read. */
static const ui_entry box_ui_entries[] = {
    {0x413305, 0x55}, {0x40e75f, 0x55}, {0x44d90c, 0x55}, {0x44d55f, 0x55},
    {0x431718, 0xff}, {0x4317c3, 0xff}, {0x43b7f2, 0x55}, {0x43b758, 0xff},
    {0x408d2f, 0x55}, {0x41f93a, 0x55}, {0x41fabd, 0x55}, {0x436e20, 0x33},
    {0x436fbe, 0xa0}, {0x43716c, 0xa0}, {0x4370b8, 0xa0}, {0x436e5d, 0xa0},
    {0x42f7f0, 0x53}, {0x42f894, 0x53}, {0x407636, 0x55},
    /* The REPAIR and BUILD refreshes and row texts (0x433ad5, 0x43433f,
     * 0x434042, 0x4344cc, 0x433fcd, 0x447064, 0x447501), the squad panel
     * refreshes (0x43da0c, 0x43e0d7, 0x43e167, 0x43d80c), the scrap
     * warning's hide 0x447e25 and the redraw 0x41f3ed (quiet); the squad
     * panel's bay selection 0x43dcdd (recorded: the bay the scrap selects
     * in REPAIR). */
    {0x433ad5, 0x55}, {0x43433f, 0x53}, {0x434042, 0x55}, {0x4344cc, 0x55},
    {0x433fcd, 0x55}, {0x447064, 0x66}, {0x447501, 0x55}, {0x43da0c, 0x66},
    {0x43e0d7, 0x53}, {0x43e167, 0x55}, {0x43d80c, 0xe8}, {0x447e25, 0xff},
    {0x41f3ed, 0x55}, {0x43dcdd, 0x55},
    /* Practice screen: list-item colour 0x40ab3b, text update 0x40b8bb, the frame pump
     * 0x402266 and the display switch-back 0x4077ee (quiet) */
    {0x40ab3b, 0x55}, {0x40b8bb, 0x55}, {0x402266, 0x53}, {0x4077ee, 0x55},
    /* The help routine 0x431e79: WinHelpA's wrapper 0x407953(path)
     * (recorded, the path is the argument) and the sound focus loss 0x42f8ff
     * (quiet) */
    {0x407953, 0x55}, {0x42f8ff, 0xe8}};
static const uint32_t quiet_ui_entries[] = {0x42f8ff,   /* help */0x408d2f, 0x41f93a, 0x41fabd, 0x436e20,
                                            0x436fbe, 0x43716c, 0x4370b8, 0x436e5d,
                                            0x42f7f0, 0x42f894, 0x407636,
                                            /* REPAIR, BUILD */
                                            0x433ad5, 0x43433f, 0x434042, 0x4344cc,
                                            0x433fcd, 0x447064, 0x447501, 0x43da0c,
                                            0x43e0d7, 0x43e167, 0x43d80c, 0x447e25,
                                            0x41f3ed,
                                            /* practice screen */
                                            0x40ab3b, 0x40b8bb, 0x402266, 0x4077ee,
                                            /* CREW, ARMORY (armed by s17_arm) */
                                            0x4422df, 0x43da1a, 0x40b8bb, 0x40ab3b,
                                            0x4087f0, 0x44a33b, 0x4499b9, 0x40b808,
                                            0x40b7e9};
static int ui_quiet(uint32_t a) {
  for (unsigned i = 0; i < sizeof(quiet_ui_entries) / sizeof(quiet_ui_entries[0]); i++)
    if (quiet_ui_entries[i] == a)
      return 1;
  return 0;
}
static const ui_entry *ui_armed_list = ui_entries;
static unsigned ui_armed_count = UI_ENTRY_COUNT;
static int ui_armed;
#define UI_CALL_LIMIT 1024 /* batched launch checks */
static uint32_t ui_calls[UI_CALL_LIMIT][4];
static unsigned ui_call_count;
/* Entries armed only once an s17_op operation runs (s17_arm), so that
 * no earlier operation sees them trapped. */
static const ui_entry *ui_extra_list;
static unsigned ui_extra_count;
static int ui_trap(EXCEPTION_POINTERS *e) {
  uint32_t a = (uint32_t)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
  if (!ui_armed || e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT)
    return 0;
  for (unsigned i = 0; i < ui_armed_count + ui_extra_count; i++)
    if ((i < ui_armed_count ? ui_armed_list[i] : ui_extra_list[i - ui_armed_count]).address == a) {
      CONTEXT *c = e->ContextRecord;
      uint32_t *stack = (uint32_t *)(uintptr_t)c->Esp;
      if (!ui_quiet(a)) {
        if (ui_call_count == UI_CALL_LIMIT)
          die("UI call limit");
        ui_calls[ui_call_count][0] = a;
        for (unsigned j = 1; j < 4; j++)
          ui_calls[ui_call_count][j] = stack[j];
        ui_call_count++;
      }
      c->Eax = 0;
      c->Eip = stack[0];
      c->Esp += 4;
      if (trace_enabled) /* keep single-stepping after the skipped UI call */
        c->EFlags |= 0x100;
      return 1;
    }
  return 0;
}
static void ui_arm(void) {
  for (unsigned i = 0; i < ui_armed_count; i++) {
    DWORD old;
    BYTE *at = P(ui_armed_list[i].address);
    if (*at != ui_armed_list[i].first)
      die("UI entry signature");
    VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &old);
    *at = 0xcc;
    VirtualProtect(at, 1, old, &old);
  }
  FlushInstructionCache(GetCurrentProcess(), NULL, 0);
  ui_armed = 1;
}
#define MAX_OPS 512 /* --op (mount sequences and ARM tests need many) */
typedef struct options {
  const char *root, *save, *vars, *player_name;
  int group, index, mode;
  unsigned seed, language, difficulty;
  BYTE training[5];
  int pipe, list, batch, emit;
  unsigned player_rank;
  unsigned preset_flag;
  int preset_index;
  const char *results, *mission_var, *rng_state;
  unsigned repair, restock;
  int results_batch;
  /* --op: original slot and new-career operations on a memory disk */
  const char *ops[MAX_OPS];
  unsigned op_count;
  int not_loaded;
  /* --state FILE (repeatable): SAVs the op restore:N restores */
  const char *states[MAX_OPS];
  unsigned state_count;
} options;
static unsigned number(const char *s, unsigned limit) {
  char *end;
  errno = 0;
  unsigned long n = strtoul(s, &end, 10);
  if (!*s || *s == '-' || *end || errno || n > limit)
    die("invalid numeric option");
  return (unsigned)n;
}
static int parse_options(int argc, char **argv, options *o) {
  memset(o, 0, sizeof(*o));
  o->group = o->index = o->mode = -1;
  o->language = 2;
  for (int i = 1; i < argc; i++) {
    const char *key = argv[i];
    if (!strcmp(key, "--batch")) { o->batch = o->pipe = 1; continue; }
    if (!strcmp(key, "--pipe")) {
      o->pipe = 1;
      continue;
    }
    if (!strcmp(key, "--list")) {
      o->list = 1;
      continue;
    }
    if (!strcmp(key, "--emit-save")) {
      o->emit = 1;
      continue;
    }
    /* The SAV of the state after the preparation (what a VSHELL save
     * after Use Default writes), instead of the state handed to it. */
    if (!strcmp(key, "--emit-save-after")) {
      o->emit = 2;
      continue;
    }
    if (!strcmp(key, "--not-loaded")) {
      o->not_loaded = 1;
      continue;
    }
    if (!strcmp(key, "--results-batch")) {
      o->results_batch = o->pipe = 1;
      continue;
    }
    if (!strcmp(key, "--help")) {
      puts("VSHELL memory worker: --cd-root ROOT [--save SAV] [--campaign G:I] "
           "[--variables FILE] [--seed 0..65535] [--language E|F|G] [--mode "
           "0|1] [--difficulty N] [--training-options A,B,C,D,E] "
           "[--preset-flag N] [--preset-index N] [--player-name NAME] "
           "[--player-rank N] "
           "[--emit-save] [--pipe|--list|--batch]\n"
           "results path: --save SAV --results FILE --mission-var FILE "
           "[--mode 0|1] [--repair-option N] [--restock-option N] "
           "[--rng-state FILE|--seed N] [--campaign G:I] [--pipe]; "
           "or --results-batch; slot mode: --op new:RANK:NAME|load:K:FLAG|"
           "save:K[:TEXT]|practice:I|mount:B:S:I|guidance:B:S:M|rebalance:O|arm:B:S:I|launch|"
           "checks|rockroll|restore:N|final:W|replay-yes|replay-no|continue|mode:M|display:V|"
           "prefs-load|prefs:NAME|help[:ROOT] (repeatable) [--file NAME PATH]... [--state SAV]... "
           "[--save SAV [--not-loaded]] [--mode 0|1] [--rng-state FILE|--seed N] "
           "[--emit-save] [--pipe]");
      return 0;
    }
    if (++i == argc)
      die("option needs value");
    const char *v = argv[i];
    if (!strcmp(key, "--cd-root"))
      o->root = v;
    else if (!strcmp(key, "--save"))
      o->save = v;
    else if (!strcmp(key, "--variables"))
      o->vars = v;
    else if (!strcmp(key, "--results"))
      o->results = v;
    else if (!strcmp(key, "--op")) {
      if (o->op_count == MAX_OPS)
        die("too many --op");
      o->ops[o->op_count++] = v;
    } else if (!strcmp(key, "--state")) {
      if (o->state_count == MAX_OPS)
        die("too many --state");
      o->states[o->state_count++] = v;
    } else if (!strcmp(key, "--file")) {
      if (++i == argc)
        die("--file NAME PATH");
      vdisk_add(v, disk_read(argv[i]));
    }
    else if (!strcmp(key, "--overlay")) {
      if (overlay_count == 4 || ++i == argc)
        die("--overlay NAME FILE (at most four)");
      overlays[overlay_count].name = v;
      overlays[overlay_count++].bytes = disk_read(argv[i]);
    }
    else if (!strcmp(key, "--mission-var"))
      o->mission_var = v;
    else if (!strcmp(key, "--rng-state"))
      o->rng_state = v;
    else if (!strcmp(key, "--repair-option"))
      o->repair = number(v, 255);
    else if (!strcmp(key, "--restock-option"))
      o->restock = number(v, 255);
    else if (!strcmp(key, "--player-rank"))
      o->player_rank = number(v, 65535);
    else if (!strcmp(key, "--player-name")) {
      /* Runtime name storage is 30 bytes including NUL (pilot +4..+0x21). */
      size_t n = strlen(v);
      if (!n || n > 29)
        die("player name must have 1..29 bytes");
      o->player_name = v;
    }
    else if (!strcmp(key, "--campaign")) {
      char text[32];
      if (strlen(v) >= sizeof(text))
        die("campaign position");
      strcpy(text, v);
      char *colon = strchr(text, ':');
      if (!colon)
        die("campaign position must be G:I");
      *colon++ = 0;
      o->group = (int)number(text, 5);
      o->index = (int)number(colon, 10);
    } else if (!strcmp(key, "--seed"))
      o->seed = number(v, 65535);
    else if (!strcmp(key, "--difficulty"))
      o->difficulty = number(v, 65535);
    else if (!strcmp(key, "--preset-flag"))
      o->preset_flag = number(v, 255);
    else if (!strcmp(key, "--preset-index"))
      o->preset_index = (int16_t)number(v, 65535);
    else if (!strcmp(key, "--mode"))
      o->mode = (int)number(v, 1);
    else if (!strcmp(key, "--language")) {
      if (strlen(v) != 1 || !strchr("EFG", *v))
        die("language E, F or G required");
      o->language = (unsigned)(strchr("EFG", *v) - "EFG");
    } else if (!strcmp(key, "--training-options")) {
      char text[80];
      if (strlen(v) >= sizeof(text))
        die("training options");
      strcpy(text, v);
      char *part = text;
      for (unsigned j = 0; j < 5; j++) {
        char *comma = strchr(part, ',');
        if (j < 4) {
          if (!comma)
            die("five training options required");
          *comma = 0;
        } else if (comma)
          die("five training options required");
        o->training[j] = (BYTE)number(part, j == 3 ? 8 : 255);
        part = comma ? comma + 1 : NULL;
      }
    } else
      die("unknown option");
  }
  if (!o->root)
    die("--cd-root is required");
  return 1;
}
static const char *selected_mission(unsigned group, unsigned index) {
  if (group >= get16(P(0x46fc50)))
    die("campaign group");
  BYTE *g = P(get32(P(0x46fc54))) + 8 * group;
  if (index >= get16(g + 2))
    die("campaign index");
  return (char *)P(get32(P(get32(g + 4)) + 4 * index));
}
static void write_word(unsigned n) {
  BYTE b[4];
  put32(b, n);
  if (fwrite(b, 1, 4, stdout) != 4)
    die("pipe write");
}
static es2_span read_hex(void) {
  size_t n=0,capacity=4096;BYTE *p=malloc(capacity);int a,b;
  if(!p)die("batch allocation");
  while((a=getchar())!='\n') {
    if(a=='\r')continue;
    b=getchar();
    a=a>='0'&&a<='9'?a-'0':a>='a'&&a<='f'?a-'a'+10:-1;
    b=b>='0'&&b<='9'?b-'0':b>='a'&&b<='f'?b-'a'+10:-1;
    if(a<0 || b<0 || n>=1024*1024)die("batch hex");
    if(n==capacity){capacity*=2;p=realloc(p,capacity);if(!p)die("batch allocation");}
    p[n++]=(BYTE)(a*16+b);
  }
  return (es2_span){p,n};
}
static void hex_text(const BYTE *p,size_t n,char *out) {
  static const char digits[]="0123456789abcdef";
  for(size_t i=0;i<n;i++){out[2*i]=digits[p[i]>>4];out[2*i+1]=digits[p[i]&15];}
  out[2*n]=0;
}
/* Results path (vshell_preparation.md "Results path"): restored
 * SAV, supplied mission.var/results.dat and RNG, then original 0x40f09f with
 * the UI entries trapped. Output: SAV from the original writers, the
 * MISSION.STR written by 0x418014 and the composed debrief text (ES2R). */
static void put_block(const void *p, size_t n) {
  write_word((unsigned)n);
  if (n && fwrite(p, 1, n, stdout) != n)
    die("pipe write");
}
static void set_input(unsigned index, es2_span s) {
  outputs[index].size = 0;
  if (s.size && shell_buffer_write(&outputs[index], 0, s.data, s.size) < 0)
    die("input sink");
}
static int results_one(const options *o, es2_span rng) {
  shell_save state = {0};
  es2_error error;
  if (!save_bytes.data || shell_save_check(save_bytes, &state, &error) != ES2_OK) {
    fprintf(stderr, "SAV rejected at %zu\n", save_bytes.data ? error.offset : 0);
    return 1;
  }
  for (unsigned i = 0; i < OUTPUT_COUNT; i++)
    if (i != 2 && i != 5)
      outputs[i].size = 0;
  memset(descriptors, 0, sizeof(descriptors));
  ui_call_count = 0;
  restore_save();
  if (o->group >= 0) {
    put16(P(0x46fc58), (uint16_t)o->group);
    put16(P(0x46fc5a), (uint16_t)o->index);
  }
  unsigned group = get16(P(0x46fc58)), index = get16(P(0x46fc5a));
  if (group >= get16(P(0x46fc50)) || index >= get16(P(get32(P(0x46fc54))) + 8 * group + 2))
    die("campaign position");
  int mode = o->mode < 0 ? (group ? 1 : 0) : o->mode;
  put16(P(0x4826f0), (uint16_t)mode);
  *P(0x4825c8) = (BYTE)o->repair;
  *P(0x4825c9) = (BYTE)o->restock;
  if (rng.size == 114)
    memcpy(P(0x482409), rng.data, 114);
  else {
    phase = "RNG initialize";
    CALL2(0x465cbc, 0x482409, o->seed);
  }
  if (P(0x482409)[112] >= 56 || P(0x482409)[113] >= 56)
    die("RNG cursors");
  put16(P(0x48415e), 0xffff); /* sentinel: 0x4139aa did not run */
  fprintf(stderr, "RESULTS %s mode=%d\n", selected_mission(group, index), mode);
  phase = "results path";
  CALL0(0x40f09f);
  emit_save();
  char metadata[8192], rng_output[229], refs[256];
  int at = 0;
  hex_text(P(0x482409), 114, rng_output);
  for (unsigned i = 0; i < 30; i++)
    at += snprintf(refs + at, sizeof(refs) - (size_t)at, "%s%d", i ? "," : "",
                   (int16_t)get16(P(0x484160) + 2 * i));
  int length = snprintf(
      metadata, sizeof(metadata),
      "{\"results\":1,\"before\":[%u,%u],\"after\":[%u,%u],\"mode\":%d,"
      "\"repair\":%u,\"restock\":%u,\"language\":%u,\"final_word\":%d,"
      "\"total\":%u,\"lost\":%d,\"debrief_ran\":%d,\"debrief_count\":%d,"
      "\"debrief_refs\":[%s],\"debrief_aux\":%d,\"output_rng_state\":\"%s\","
      "\"save_consumed\":%zu,\"ui_calls\":[",
      group, index, get16(P(0x46fc58)), get16(P(0x46fc5a)), mode, o->repair,
      o->restock, o->language, (int16_t)get16(P(0x4826f2)), get32(P(0x482bd8)),
      (int16_t)get16(P(0x482bd3)), get16(P(0x48415e)) != 0xffff,
      (int16_t)get16(P(0x48415e)), refs, (int16_t)get16(P(0x48419e)),
      rng_output, state.consumed);
  for (unsigned i = 0; i < ui_call_count && length > 0; i++)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length,
                       "%s[%u,%u,%u,%u]", i ? "," : "", ui_calls[i][0],
                       ui_calls[i][1], ui_calls[i][2], ui_calls[i][3]);
  if (length > 0)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "]}");
  if (length < 0 || (size_t)length >= sizeof(metadata))
    die("metadata size");
  const char *text = "";
  size_t text_size = 0;
  if (get32(P(0x46fc68))) {
    text = (const char *)P(get32(P(0x46fc68)));
    text_size = strnlen(text, get32(P(get32(P(0x46fc68)) - 4)));
  }
  if (o->pipe) {
    _setmode(_fileno(stdout), _O_BINARY);
    fwrite("ES2R", 1, 4, stdout);
    put_block(metadata, (size_t)length);
    put_block(outputs[4].data, outputs[4].size);
    put_block(outputs[1].data, outputs[1].size);
    put_block(text, text_size);
    if (ferror(stdout))
      die("pipe output");
  } else {
    char h[65];
    puts(metadata);
    digest((es2_span){outputs[4].data, outputs[4].size}, h);
    printf("OUTPUT output.sav %zu %s\n", outputs[4].size, h);
    digest((es2_span){outputs[1].data, outputs[1].size}, h);
    printf("OUTPUT MISSION.STR %zu %s\n", outputs[1].size, h);
    digest((es2_span){(const BYTE *)text, text_size}, h);
    printf("OUTPUT debrief-text %zu %s\n", text_size, h);
  }
  fflush(stdout);
  return 0;
}
static int run_results(options *o) {
  results_mode = 1;
  phase = "damage load";
  CALL0(0x413c4c);
  phase = "weapons load";
  CALL0(0x4125e8);
  ui_arm();
  if (!o->results_batch) {
    if (!o->save || !o->results || !o->mission_var)
      die("--results needs --save, --results and --mission-var");
    es2_span bank = disk_read(o->mission_var), results = disk_read(o->results);
    es2_span rng = o->rng_state ? disk_read(o->rng_state) : (es2_span){0};
    if (bank.size != 2000 || (o->rng_state && rng.size != 114))
      die("mission.var must have 2000 bytes, RNG state 114");
    save_bytes = disk_read(o->save);
    set_input(2, bank);
    set_input(5, results);
    int status = results_one(o, rng);
    trace_finish();
    return status;
  }
  /* Each request starts from the same start-up image state, as a freshly
   * started VSHELL would; the snapshot covers the whole DATA section. */
  BYTE *snapshot = malloc(0x172000);
  if (!snapshot)
    die("snapshot allocation");
  memcpy(snapshot, P(0x46c000), 0x172000);
  defer_free = 1;
  char line[256];
  while (fgets(line, sizeof(line), stdin)) {
    int mode, repair, restock, group, index;
    if (sscanf(line, "%d %d %d %d %d", &mode, &repair, &restock, &group, &index) != 5 ||
        mode < 0 || mode > 1 || repair < 0 || repair > 255 || restock < 0 ||
        restock > 255 || group < -1 || group > 5 || index < -1 || index > 10)
      die("results batch header");
    es2_span save = read_hex(), bank = read_hex(), results = read_hex(), rng = read_hex();
    if (bank.size != 2000 || rng.size != 114)
      die("results batch sizes");
    memcpy(P(0x46c000), snapshot, 0x172000);
    options request = *o;
    request.mode = mode;
    request.repair = (unsigned)repair;
    request.restock = (unsigned)restock;
    request.group = group;
    request.index = index;
    save_bytes = save;
    set_input(2, bank);
    set_input(5, results);
    if (results_one(&request, rng))
      return 1;
    free((void *)save.data);
    free((void *)bank.data);
    free((void *)results.data);
    free((void *)rng.data);
  }
  /* With ES2_SHELL_TRACE the union of executed addresses of the whole batch
   * (start-up loaders included) is printed once, as instruction addresses. */
  trace_finish();
  return 0;
}
/* Slot mode (--op): VSHELL's own save-slot, load and new-career
 * operations on the memory disk. Start-up as game init 0x40e736 does for
 * the files involved: the slot table from sav\gamefile.str (0x40e380, which
 * appends ESTEXT 0x21 to a name without a fifth byte), DAMAGE, WEAPONS. Then
 * the optional restored SAV (--save, original readers), the loaded flag
 * 0x4826ee (set after --save unless --not-loaded), mode 0x4826f0 (default 1),
 * the RNG (--rng-state or --seed) and the operations in order:
 *   new:RANK:NAME  0x40e8a5(NAME, RANK); its DEBUG dialog 0x413305 trapped
 *   load:K:FLAG    0x40eaaa(K, FLAG); the map object 0x40e75f trapped
 *   save:K[:TEXT]  0x40e933(K, TEXT or NULL)
 *   practice:I     0x479c54 := I (the mode-0 CAREER position of a new game)
 * Service bay, WEAPONS screen:
 *   mount:B:S:I    0x411b10(hangar record of bay B, slot S, item I), the
 *                  mount the WEAPONS click 0x43e2d4 calls; refused when the
 *                  bay has no record (0x43e2d4 tests it)
 *   guidance:B:S:M bay selection 0x482bc9 := B, slot selection 0x476658 :=
 *                  S, then 0x43e346(M) (the guidance buttons' store)
 *   rebalance:O    option byte 0x4825c9 := O, then 0x412a37 (leaving
 *                  WEAPONS, REPAIR or BUILD, 0x43a537)
 *   arm:B:S:I      the ARM tables 0x41508f once, bay and slot selections
 *                  (S = 255: no slot), then 0x41501f(I); the answers are
 *                  listed in the metadata ("arm": bay, slot, item, answer)
 *   launch         0x40f68c: data\mission.var and data\player.mec of the
 *                  state (Rock & Roll's launch-file writer)
 * Launch checks, Replay and End of game boxes, Preferences:
 *   checks         the player bay word 0x482b82 and the answers of 0x4110c1
 *                  and 0x411135 for it (-1 when the bay is -1: Rock & Roll
 *                  never calls them then) and of 0x40fc7e, in the metadata
 *                  ("checks": bay, functional, armed, squad armed)
 *   rockroll       the Rock & Roll handler 0x445b99 without an event: the
 *                  four checks, a refusal shows the warning box 0x44d90c(n)
 *                  (recorded), a launch runs 0x40f68c, exit code 2, quit flag
 *   restore:N      the N-th --state SAV through the original readers (as
 *                  --save), loaded flag 1
 *   final:W        final word 0x4826f2 := W (the results path's 3 or 0
 *                  before the Replay box)
 *   replay-yes     the Replay box's Yes 0x44d1d4: 0x40eaaa(10, 0), exit 2
 *   replay-no      the Replay box's No 0x44d24d: 0x40e933(10, 0), main menu
 *   continue       CONTINUE GAME 0x431a73: mode 1 (0x40ec56, prefs.cfg
 *                  read-modify-write), 0x40eaaa(10, 1), then the bay (final
 *                  word 2) or the End of game box 0x44d55f(final word)
 *   mode:M         the main menu's mode store 0x40ec56(M)
 *   display:V      the display state 0x481f4c := V (full screen when not 0)
 *   prefs-load     the start-up prefs loader 0x40dc44: data\prefs.cfg into
 *                  the current (0x48259c) and committed (0x4825d2) blocks
 *   prefs:NAME     a Preferences control without an event: show 0x436d45,
 *                  music 0x437351, sfx 0x4373b2, high 0x437758, low 0x4376f7,
 *                  window 0x437597, full 0x437608, alert 0x437678 (the full
 *                  screen alert's ACCEPT), repair0..2 0x437413/0x437474/
 *                  0x4374d5, build0..1 0x437536/0x437696, accept 0x4372e1
 *                  (commit, writer 0x40dd0a), cancel 0x437220 (revert)
 *   help[:ROOT]    the help routine 0x431e79 (ONLINE MANUAL): byte 6
 *                  := 0, commit and write of prefs.cfg, language.cfg
 *                  (--file data\language.cfg), the path handed to WinHelpA's
 *                  wrapper 0x407953 (trapped) appended to the memory-disk
 *                  file `help.path`; ROOT first stored as the CD root
 *                  0x4823b9. Only language bytes E, F and G are safe: any
 *                  other reaches the severity-4 assertion that ends VSHELL
 * The metadata also carries both prefs blocks, the exit code 0x46e348 and
 * the quit flag 0x46c268.
 * Output ES2D: metadata, the whole memory disk, and with --emit-save the SAV
 * image of the final state from the original writers. */
static void slot_table(char *out, size_t capacity) {
  size_t at = 0;
  char file[2 * 13 + 1], text[2 * 80 + 1];
  for (unsigned i = 0; i < 12; i++) {
    BYTE *r = P(0x4826f4 + 0x5e * i);
    hex_text(r, strnlen((char *)r, 13), file);
    hex_text(r + 13, strnlen((char *)r + 13, 80), text);
    int n = snprintf(out + at, capacity - at, "%s[\"%s\",\"%s\",%u]", i ? "," : "",
                     file, text, r[0x5d]);
    if (n < 0 || (size_t)n >= capacity - at)
      die("slot table size");
    at += (size_t)n;
  }
}
typedef struct prefs_control {
  const char *name;
  uint32_t address;
} prefs_control;
static const prefs_control prefs_controls[] = {
    {"show", 0x436d45},    {"music", 0x437351},   {"sfx", 0x4373b2},
    {"high", 0x437758},    {"low", 0x4376f7},     {"window", 0x437597},
    {"full", 0x437608},    {"alert", 0x437678},   {"repair0", 0x437413},
    {"repair1", 0x437474}, {"repair2", 0x4374d5}, {"build0", 0x437536},
    {"build1", 0x437696},  {"accept", 0x4372e1},  {"cancel", 0x437220}};
static void restore_state(const char *path) {
  shell_save state;
  es2_error error;
  es2_span bytes = disk_read(path);
  if (shell_save_check(bytes, &state, &error) != ES2_OK)
    die("--state SAV rejected");
  save_bytes = bytes;
  restore_save();
}
/* Service-bay editors REPAIR, BUILD, Herc scrap:
 *   select:B       bay selection 0x482bc9 := B (65535: -1)
 *   screen:W       screen word 0x4758b8 := W (65535: -1)
 *   repair-pick:T:R  the REPAIR row selection 0x434549(T, R) (its UI calls
 *                  trapped): 0x474094/0x474098 and family/index
 *                  0x474096/0x47409a
 *   repair-snapshot  0x433f86: salvage into 0x48d33c, the selected bay's 33
 *                  condition words into 0x48d340 (0x435547)
 *   repair-one     REPAIR 0x4351bd, repair-all REPAIR ALL 0x4352e9,
 *   repair-cancel  CANCEL 0x435403 (handlers without an event; refreshes
 *                  trapped)
 *   build:T        BUILD row selection 0x478780 := T, then Build 0x4475ce
 *   scrap          the Herc scrap warning's ACCEPT 0x448326 (0x40ed0f; in
 *                  REPAIR, screen word 3, its 0x43dcdd(0x41124e) is trapped
 *                  and recorded)
 *   herc-type:T    BUILD row selection 0x478780 := T (65535: -1)
 *   editor-answers what the screens decide on, from the original functions:
 *                  [bay, salvage 0x482bd8, 0x41124e, 0x411101, 0x412baa,
 *                  record, 0x41232a, 0x411a78, 0x41401b(L, 100), 0x414174,
 *                  nine-word bank entry of the vehicle, HERC_INF[0x478780]
 *                  +8] for the selected bay and row ("editor_answers")
 *   editor-state   [0x482bc9, 0x474094, 0x474098, 0x474096, 0x47409a,
 *                  0x478780, 0x48d33c, the 66 bytes at 0x48d340 in hex]
 *                  ("editor")
 *   emit           the SAV of the state now from the original writers,
 *                  appended to the memory-disk file editors.emit (size
 *                  dword, bytes)
 */
#define EDITOR_RECORDS 256
static int editor_answers[EDITOR_RECORDS][12];
static unsigned editor_answer_count;
static char editor_states[EDITOR_RECORDS][200];
static unsigned editor_state_count;
static void editor_answer(void) {
  int *a;
  int16_t bay = (int16_t)get16(P(0x482bc9)), type = (int16_t)get16(P(0x478780));
  uint32_t record = bay >= 0 && bay < 8 ? get32(P(0x482ba7 + 4 * bay)) : 0;
  uint16_t family = get16(P(0x474096)), index = get16(P(0x47409a));
  if (editor_answer_count == EDITOR_RECORDS)
    die("editor record limit");
  a = editor_answers[editor_answer_count++];
  phase = "editor answers";
  a[0] = bay;
  a[1] = (int)get32(P(0x482bd8));
  a[2] = (int16_t)CALL1(0x41124e, 0x482ba7);
  a[3] = (int)(CALL1(0x411101, 0x482ba7) & 0xff);
  a[4] = (int)CALL0(0x412baa);
  a[5] = record != 0;
  a[6] = record ? (int16_t)CALL3(0x41232a, record + 8, family, index) : 100;
  a[7] = record ? (int32_t)CALL3(0x411a78, record, family, index) : 0;
  a[8] = record ? (int32_t)CALL2(0x41401b, record, 100) : 0;
  a[9] = record ? (int32_t)CALL1(0x414174, record) : 0;
  a[10] = record ? (int16_t)get16(P(0x483c46 + 16 * (int16_t)get16(P(record)))) : -1;
  a[11] = type >= 0 && type < 9 ? (int16_t)get16(P(0x483c40 + 16 * type)) : 0;
}
static void editor_state(void) {
  char hex[133];
  if (editor_state_count == EDITOR_RECORDS)
    die("editor record limit");
  hex_text(P(0x48d340), 66, hex);
  snprintf(editor_states[editor_state_count++], sizeof(editor_states[0]),
           "[%d,%d,%d,%d,%d,%d,%u,\"%s\"]", (int16_t)get16(P(0x482bc9)),
           (int16_t)get16(P(0x474094)), (int16_t)get16(P(0x474098)),
           (int16_t)get16(P(0x474096)), (int16_t)get16(P(0x47409a)),
           (int16_t)get16(P(0x478780)), get32(P(0x48d33c)), hex);
}
static void editor_emit(void) {
  BYTE size[4];
  vfile *f;
  vdisk_mode = 0;
  emit_save();
  vdisk_mode = 1;
  f = vdisk_find("editors.emit", 1);
  put32(size, (uint32_t)outputs[4].size);
  if (shell_buffer_write(&f->data, f->data.size, size, 4) < 0 ||
      shell_buffer_write(&f->data, f->data.size, outputs[4].data, outputs[4].size) < 0)
    die("memory disk emit");
}
/* Practice screen, INSTANT ACTION, VIEW DEMO, the DEBUG dialog:
 *   practice-show  PRACTICE MISSIONS 0x431f3a (hide main menu 0x4317c3
 *                  recorded, show 0x44c322 -> 0x44c40c(0), mode store
 *                  0x40ec56(0))
 *   practice-entry:I  the entry handler I (0x44caa3 ... 0x44cd4a) ->
 *                  0x44c40c(I): 0x479c54 and prefs +0x28 from 0x479c56
 *   practice-control:K:B  the parameter handler K (0 Ammo 0x44c676, 1 Damage
 *                  0x44c5b9, 2 Mission Difficulty 0x44c733, 3 Herc Type
 *                  0x44c8ad, 4 Time of Day 0x44c7f0) with an event whose
 *                  button byte +0x24 is B (1 left release: 0x40de5b, 3 right
 *                  release: 0x40de88), as the text button 0x409d8a hands it
 *   practice-state [0x479c54, 0x473784, Herc Type button +0x49] ("practice")
 *   practice-menu  Main Menu 0x44c96a (hide 0x44c343, main menu recorded)
 *   practice-begin Begin Mission 0x44ca26 (commit, writer 0x40dd0a, new game
 *                  0x40e8a5("TRAINEE", prefs +0x27); 0x413305 recorded)
 *   instant-action INSTANT ACTION 0x431935 (0x473784 := 1, 0x40ec56(0),
 *                  0x44c58b, 0x40e8a5, 0x40f68c, exit code 2, quit flag)
 *   use-default    the DEBUG dialog's Use Default 0x44dbea, i.e. the posted
 *                  click of 0x413305 (0x44db5c: preparation of the CAREER
 *                  entry; mode 0: 0x40f68c, exit code 2, quit flag)
 *   debug-accept:TEXT  the edit field text, then ACCEPT 0x44dc4d
 *                  (msn\TEXT.msn)
 *   view-demo      VIEW DEMO 0x431bfe (exit code 5, quit flag)
 *   practice-reset 0x479c54 := -1 and 0x473784 := 0 (their image values: a
 *                  new VSHELL process)
 *   practice-emit  appends to the memory-disk file practice.emit the five
 *                  files data\script.dat, data\MISSION.STR, data\mission.var,
 *                  data\player.mec, data\prefs.cfg (u32 size or 0xffffffff
 *                  when missing, bytes), the SAV of the state now (u32 size,
 *                  bytes) and eight dwords: 0x479c54, 0x473784, exit code,
 *                  quit flag, mode word, CAREER group and position, loaded
 *                  flag
 * The handlers write widgets the worker never builds: the practice screen's
 * and the DEBUG dialog's widget globals get blocks of our own memory (a
 * stub vtable at +1, a child block at +0x55), the entry-handler table
 * 0x48dc7c is filled as the constructor 0x44b310 fills it, and the drawing
 * calls 0x40ab3b, 0x40b8bb, 0x402266, 0x4077ee are trapped quietly. */
static const uint32_t practice_entries[8] = {0x44caa3, 0x44cb04, 0x44cb65, 0x44cbc6,
                                             0x44cc27, 0x44cc88, 0x44cce9, 0x44cd4a};
static const uint32_t practice_controls[5] = {0x44c676, 0x44c5b9, 0x44c733, 0x44c8ad, 0x44c7f0};
static void __cdecl practice_stub(void) {}
static uint32_t practice_class[32];   /* [7] = 0, [5] -> practice_zero, vtable at [8] */
static uint32_t practice_zero[16];
static BYTE practice_blocks[80][0x100];
static BYTE practice_event_block[0x40];
static int practice_ready;
#define PRACTICE_RECORDS 256
static int practice_states[PRACTICE_RECORDS][3];
static unsigned practice_state_count;
static void practice_widgets(void) {
  static const uint32_t globals[] = {
      0x48dbfc, 0x48dc00, 0x48dc04, 0x48dc08, 0x48dc0c, 0x48dc10, 0x48dc14, 0x48dc18,
      0x48dc1c, 0x48dc20, 0x48dc24, 0x48dc28, 0x48dc2c, 0x48dc30, 0x48dc34, 0x48dc38,
      0x48dc3c, 0x48dc40, 0x48dc44, 0x48dc48, 0x48dc4c, 0x48dc5c, 0x48dc60, 0x48dc64,
      0x48dc68, 0x48dc6c, 0x48dc70, 0x48dc74, 0x48dc78, 0x48dcdc, 0x48dce0, 0x48dcec,
      0x48dcf4, 0x48dcf8, 0x48dcfc};
  unsigned n = sizeof(globals) / sizeof(globals[0]);
  if (practice_ready)
    return;
  /* 0x46b66e reads [vptr - 4] (0), [vptr - 0xc] and that block's +0x20 */
  practice_class[5] = (uint32_t)(uintptr_t)practice_zero;
  for (unsigned i = 8; i < 32; i++)
    practice_class[i] = (uint32_t)(uintptr_t)practice_stub;
  for (unsigned i = 0; i < n; i++) {
    BYTE *w = practice_blocks[2 * i], *child = practice_blocks[2 * i + 1];
    put32(w + 1, (uint32_t)(uintptr_t)(practice_class + 8));
    put32(child + 1, (uint32_t)(uintptr_t)(practice_class + 8));
    put32(w + 0x55, (uint32_t)(uintptr_t)child);
    put32(P(globals[i]), (uint32_t)(uintptr_t)w);
  }
  for (unsigned i = 0; i < 8; i++)
    put32(P(0x48dc7c + 4 * i), practice_entries[i]);
  practice_ready = 1;
}
static uintptr_t practice_event(BYTE button) {
  memset(practice_event_block, 0, sizeof(practice_event_block));
  put32(practice_event_block, (uint32_t)(uintptr_t)(practice_class + 8));
  put32(practice_event_block + 0xc, 0x20);
  practice_event_block[0x24] = button;
  put32(practice_event_block + 0x25, 1);
  return (uintptr_t)practice_event_block;
}
static void practice_state(void) {
  if (practice_state_count == PRACTICE_RECORDS)
    die("practice record limit");
  int *s = practice_states[practice_state_count++];
  s[0] = (int16_t)get16(P(0x479c54));
  s[1] = (int)get32(P(0x473784));
  s[2] = (int)get32(P(get32(P(0x48dc1c))) + 0x49);
}
static void practice_put(vfile *f, const void *p, size_t n) {
  if (shell_buffer_write(&f->data, f->data.size, p, n) < 0)
    die("memory disk emit");
}
static void practice_emit(void) {
  static const char *names[] = {"data\\script.dat", "data\\MISSION.STR", "data\\mission.var",
                                "data\\player.mec", "data\\prefs.cfg"};
  static const uint32_t words[] = {0x479c54, 0x473784, 0x46e348, 0x46c268, 0x4826f0,
                                   0x46fc58, 0x46fc5a, 0x4826ee};
  static const BYTE widths[] = {2, 4, 4, 1, 2, 2, 2, 1};
  BYTE size[4];
  vfile *out = vdisk_find("practice.emit", 1);
  for (unsigned i = 0; i < 5; i++) {
    vfile *f = vdisk_find(names[i], 0);
    put32(size, f ? (uint32_t)f->data.size : 0xffffffffu);
    practice_put(out, size, 4);
    if (f && f->data.size)
      practice_put(out, f->data.data, f->data.size);
  }
  vdisk_mode = 0;
  emit_save();
  vdisk_mode = 1;
  out = vdisk_find("practice.emit", 1); /* emit_save may not move it, but look again */
  put32(size, (uint32_t)outputs[4].size);
  practice_put(out, size, 4);
  practice_put(out, outputs[4].data, outputs[4].size);
  for (unsigned i = 0; i < 8; i++) {
    uint32_t v = widths[i] == 1   ? *P(words[i])
                 : widths[i] == 2 ? (uint32_t)(int32_t)(int16_t)get16(P(words[i]))
                                  : get32(P(words[i]));
    put32(size, v);
    practice_put(out, size, 4);
  }
}
/* Service-bay editors CREW, ARMORY, weapons scrap (s17_op):
 *   building:V     weapons-building option byte 0x4825c9 := V
 *   s17-reset      the image values of 0x477778, 0x47777c[3] (0) and
 *                  0x479210 (-1)
 *   crew-show      the CREW show 0x442091 without its drawing: 0x442198, the
 *                  order rows 0x442215(0, 1, 2, 3, 0), then 0x43dcdd(k) for
 *                  every bay k with 0x411164(hangar, k) != -1 (as
 *                  0x442150..0x442179); the screen word must be 6
 *   crew-order:O   the order row 0x442215(O)
 *   crew-pilot:G   the pilot list 0x442548(G)
 *   crew-bay:K     a bay selector: 0x47665a := 1, 0x43dcdd(K), 0x47665a := 0
 *                  (the body of the handlers 0x43e477..)
 *   crew-clear     CLEAR 0x442624
 *   show-select    the selection step of the REPAIR and WEAPONS shows
 *                  (0x43397c, 0x43fbd8): 0x43dcdd(0x41124e(hangar)) when the
 *                  selection is -1, without a record or unfinished (0x411088)
 *   armory-show    the ARMORY show 0x449b87, armory-leave its leave 0x449c46
 *   armory-row:R:B the row press: B = 1 0x449d2f(R), else 0x44a06e(R)
 *   armory-clear   Clear 0x44a584
 *   armory-scrap   Scrap 0x44a508 (the warning 0x448227 is trapped and
 *                  recorded with the category)
 *   armory-accept:T  the weapons scrap warning's ACCEPT 0x448447 for the
 *                  category T (0x48da50 := T)
 *   s17-state      [0x482bc9, 0x477778, the pilots of orders 1..3 as roster
 *                  group (0x482b9b + 4g; -1 null, 9 another pointer),
 *                  0x479210, 0x46fa14, the five entries 0x46fa16, 0x4826ea]
 *                  ("s17")
 * Here 0x43dcdd runs for real (its trap is lifted for the call), and the
 * widget pointers the handlers dereference (squad panel 0x48d59c..0x48d5fc,
 * CREW 0x48d810..0x48d8a4, ARMORY 0x48da58..0x48dbfc) point to one zeroed
 * stand-in object whose virtual methods return at once; the drawing and text
 * functions below are trapped. */
static const ui_entry s17_ui_entries[] = {
    {0x4422df, 0x53}, {0x43da1a, 0x55}, {0x40b8bb, 0x55}, {0x40ab3b, 0x55},
    {0x4087f0, 0x55}, {0x44a33b, 0x55}, {0x4499b9, 0x55}, {0x40b808, 0x55},
    {0x40b7e9, 0x55}, {0x448227, 0x55}};
#define S17_RECORDS 256
static char s17_states[S17_RECORDS][128];
static unsigned s17_state_count;
static int s17_armed;
static void __cdecl s17_nop(void *self) { (void)self; }
static uint32_t s17_vtable[64];
static BYTE s17_widget[0x400];
static void s17_patch(uint32_t address, BYTE value) {
  DWORD old;
  VirtualProtect(P(address), 1, PAGE_EXECUTE_READWRITE, &old);
  *P(address) = value;
  VirtualProtect(P(address), 1, old, &old);
  FlushInstructionCache(GetCurrentProcess(), NULL, 0);
}
static void s17_arm(void) {
  if (s17_armed)
    return;
  for (unsigned i = 0; i < sizeof(s17_ui_entries) / sizeof(s17_ui_entries[0]); i++) {
    int armed = 0;
    /* The practice-screen entries trap 0x40ab3b and 0x40b8bb in every
     * slot-mode run: an entry the armed list already holds stays as it is. */
    for (unsigned j = 0; ui_armed && j < ui_armed_count; j++)
      if (ui_armed_list[j].address == s17_ui_entries[i].address)
        armed = 1;
    if (armed)
      continue;
    if (*P(s17_ui_entries[i].address) != s17_ui_entries[i].first)
      die("UI entry signature");
    s17_patch(s17_ui_entries[i].address, 0xcc);
  }
  ui_extra_list = s17_ui_entries;
  ui_extra_count = sizeof(s17_ui_entries) / sizeof(s17_ui_entries[0]);
  for (unsigned i = 0; i < 64; i++)
    s17_vtable[i] = (uint32_t)(uintptr_t)s17_nop;
  put32(s17_widget + 1, (uint32_t)(uintptr_t)s17_vtable);
  put32(s17_widget + 0x55, (uint32_t)(uintptr_t)s17_widget);
  put32(s17_widget + 0x5d, (uint32_t)(uintptr_t)s17_widget);
  s17_armed = 1;
}
static const uint32_t s17_widget_ranges[][2] = {
    {0x48d59c, 0x48d600}, {0x48d810, 0x48d8a8}, {0x48da58, 0x48dc00}};
static uint32_t s17_filled[256];
static unsigned s17_fill_count;
/* the stand-in object into every empty widget pointer; 0x43dcdd for real */
static void s17_enter(void) {
  s17_arm();
  s17_fill_count = 0;
  for (unsigned r = 0; r < 3; r++)
    for (uint32_t a = s17_widget_ranges[r][0]; a < s17_widget_ranges[r][1]; a += 4)
      if (!get32(P(a))) {
        if (s17_fill_count == 256)
          die("widget slots");
        put32(P(a), (uint32_t)(uintptr_t)s17_widget);
        s17_filled[s17_fill_count++] = a;
      }
  if (*P(0x43dcdd) != 0xcc)
    die("0x43dcdd not armed");
  s17_patch(0x43dcdd, 0x55);
}
static void s17_leave(void) {
  for (unsigned i = 0; i < s17_fill_count; i++)
    put32(P(s17_filled[i]), 0);
  s17_fill_count = 0;
  s17_patch(0x43dcdd, 0xcc);
}
static int s17_group(uint32_t pointer) {
  if (!pointer)
    return -1;
  for (int g = 0; g < 3; g++)
    if (get32(P(0x482b9b + 4 * g)) == pointer)
      return g;
  return 9;
}
static void s17_state(void) {
  if (s17_state_count == S17_RECORDS)
    die("record limit");
  snprintf(s17_states[s17_state_count++], sizeof(s17_states[0]),
           "[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]", (int16_t)get16(P(0x482bc9)),
           (int16_t)get16(P(0x477778)), s17_group(get32(P(0x47777c))), s17_group(get32(P(0x477780))),
           s17_group(get32(P(0x477784))), (int16_t)get16(P(0x479210)), (int16_t)get16(P(0x46fa14)),
           (int16_t)get16(P(0x46fa16)), (int16_t)get16(P(0x46fa18)), (int16_t)get16(P(0x46fa1a)),
           (int16_t)get16(P(0x46fa1c)), (int16_t)get16(P(0x46fa1e)), (int16_t)get16(P(0x4826ea)));
}
/* returns 1 when op was a CREW/ARMORY operation */
static int s17_op(const char *op) {
  unsigned a, b;
  if (sscanf(op, "building:%u", &a) == 1 && a < 256) {
    *P(0x4825c9) = (BYTE)a;
    return 1;
  }
  if (!strcmp(op, "s17-state")) {
    s17_state();
    return 1;
  }
  if (!strcmp(op, "s17-reset")) { /* the image values, between cases of one run */
    put16(P(0x477778), 0);
    put32(P(0x47777c), 0);
    put32(P(0x477780), 0);
    put32(P(0x477784), 0);
    put16(P(0x479210), 0xffff);
    return 1;
  }
  s17_enter();
  if (!strcmp(op, "crew-show")) {
    if (get16(P(0x4758b8)) != 6)
      die("crew-show: screen word must be 6");
    phase = "CREW show";
    CALL0(0x442198);
    static const unsigned rows[5] = {0, 1, 2, 3, 0};
    for (unsigned i = 0; i < 5; i++)
      CALL1(0x442215, rows[i]);
    for (unsigned k = 0; k < 8; k++)
      if ((int16_t)CALL2(0x411164, 0x482ba7, k) != -1)
        CALL1(0x43dcdd, k);
  } else if (sscanf(op, "crew-order:%u", &a) == 1 && a < 4) {
    phase = "CREW order row";
    CALL1(0x442215, a);
  } else if (sscanf(op, "crew-pilot:%u", &a) == 1 && a < 3) {
    phase = "CREW pilot";
    CALL1(0x442548, a);
  } else if (sscanf(op, "crew-bay:%u", &a) == 1 && a < 8) {
    phase = "CREW bay";
    *P(0x47665a) = 1;
    CALL1(0x43dcdd, a);
    *P(0x47665a) = 0;
  } else if (!strcmp(op, "crew-clear")) {
    phase = "CREW CLEAR";
    CALL1(0x4429e2, 0);
  } else if (!strcmp(op, "show-select")) {
    int16_t bay = (int16_t)get16(P(0x482bc9));
    phase = "show selection";
    if (bay == -1 || !get32(P(0x482ba7 + 4 * bay)) || !(CALL2(0x411088, 0x482ba7, bay) & 0xff))
      CALL1(0x43dcdd, (uint16_t)CALL1(0x41124e, 0x482ba7));
  } else if (!strcmp(op, "armory-show")) {
    phase = "ARMORY show";
    CALL0(0x449b87);
  } else if (!strcmp(op, "armory-leave")) {
    phase = "ARMORY leave";
    CALL0(0x449c46);
  } else if (sscanf(op, "armory-row:%u:%u", &a, &b) == 2 && a < 26) {
    phase = "ARMORY row";
    if (b == 1)
      CALL1(0x449d2f, a);
    else
      CALL1(0x44a06e, a);
  } else if (!strcmp(op, "armory-clear")) {
    phase = "ARMORY Clear";
    CALL1(0x44a584, 0);
  } else if (!strcmp(op, "armory-scrap")) {
    phase = "ARMORY Scrap";
    CALL1(0x44a508, 0);
  } else if (sscanf(op, "armory-accept:%u", &a) == 1 && a < 33) {
    put16(P(0x48da50), (uint16_t)a);
    phase = "weapons scrap ACCEPT";
    CALL1(0x448447, 0);
  } else {
    s17_leave();
    return 0;
  }
  s17_leave();
  return 1;
}
/* Images on the frame (docs/formats/dbm.md section "images and palettes on the frame"):
 *   blit    every record of the memory-disk file blit.in through VSHELL's
 *           blitter 0x45a09c(header, &pt, clip, mirror) into a memory frame
 *           of the record's size; the frame after the call is appended to
 *           blit.emit. Record (LE): u16 frame width, height, bitmap width,
 *           height; u8 flags, mirror, has_clip, 0; i32 x, y, left, top,
 *           right, bottom; the frame bytes; the bitmap pixels. The blitter's
 *           bounds [0x47c4a6], [0x47c4aa] and row table [0x47c482] (entry 0
 *           the pixel base, entries 1.. the row offsets) point at that frame
 *           for the call and are put back after it.
 *   unpack  every record of unpack.in ([u16 kind][u32 n][n stored bytes]:
 *           the U word and the stream) through the decompressor 0x453a50 on
 *           a stand-in bitmap object (+0xa stored size, +0xe stored data,
 *           +0x12 kind); appends [u32 returned U][U bytes of [0x48e26c]] to
 *           unpack.emit and frees the buffer with 0x408760, as the draw
 *           entry 0x4537e8 does. */
static int s26_op(const char *op) {
  int blit = !strcmp(op, "blit");
  if (!blit && strcmp(op, "unpack"))
    return 0;
  vfile *in = vdisk_find(blit ? "blit.in" : "unpack.in", 0);
  vfile *out = vdisk_find(blit ? "blit.emit" : "unpack.emit", 1);
  size_t at = 0;
  if (!in)
    die(blit ? "blit: blit.in missing" : "unpack: unpack.in missing");
  while (at < in->data.size) {
    const BYTE *r = in->data.data + at;
    size_t left = in->data.size - at;
    if (blit) {
      if (left < 36)
        die("blit: truncated record");
      unsigned fw = get16(r), fh = get16(r + 2), bw = get16(r + 4), bh = get16(r + 6);
      size_t frame_bytes = (size_t)fw * fh, pixel_bytes = (size_t)bw * bh;
      if (!fw || !fh || left - 36 < frame_bytes + pixel_bytes)
        die("blit: bad record");
      BYTE *frame = malloc(frame_bytes), *pixels = malloc(pixel_bytes + 1);
      uint32_t *rows = malloc(((size_t)fh + 1) * 4);
      BYTE header[14] = {0};
      int32_t pt[2], clip[4];
      if (!frame || !pixels || !rows)
        die("blit: memory");
      memcpy(frame, r + 36, frame_bytes);
      memcpy(pixels, r + 36 + frame_bytes, pixel_bytes);
      rows[0] = (uint32_t)(uintptr_t)frame;
      for (unsigned k = 0; k < fh; k++)
        rows[k + 1] = k * fw;
      put16(header, (uint16_t)bh);
      put16(header + 2, (uint16_t)bw);
      header[4] = 8;
      header[5] = r[8];
      put32(header + 6, (uint32_t)pixel_bytes);
      put32(header + 0xa, (uint32_t)(uintptr_t)pixels);
      pt[0] = (int32_t)get32(r + 12);
      pt[1] = (int32_t)get32(r + 16);
      for (unsigned k = 0; k < 4; k++)
        clip[k] = (int32_t)get32(r + 20 + 4 * k);
      uint32_t saved_rows = get32(P(0x47c482)), saved_w = get32(P(0x47c4a6)),
               saved_h = get32(P(0x47c4aa));
      put32(P(0x47c482), (uint32_t)(uintptr_t)rows);
      put32(P(0x47c4a6), fw);
      put32(P(0x47c4aa), fh);
      phase = "blitter";
      trace_before(0x45a09c);
      ((void(__cdecl *)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))(uintptr_t)0x45a09c)(
          (uintptr_t)header, (uintptr_t)pt, r[10] ? (uintptr_t)clip : 0, r[9]);
      put32(P(0x47c482), saved_rows);
      put32(P(0x47c4a6), saved_w);
      put32(P(0x47c4aa), saved_h);
      if (shell_buffer_write(&out->data, out->data.size, frame, frame_bytes) < 0)
        die("memory disk blit");
      free(frame);
      free(pixels);
      free(rows);
      at += 36 + frame_bytes + pixel_bytes;
    } else {
      if (left < 6 || left - 6 < get32(r + 2) || get32(r + 2) < 4)
        die("unpack: bad record");
      uint32_t n = get32(r + 2);
      BYTE *stored = malloc(n), object[0x20] = {0}, size[4];
      if (!stored)
        die("unpack: memory");
      memcpy(stored, r + 6, n);
      put32(object + 0xa, n);
      put32(object + 0xe, (uint32_t)(uintptr_t)stored);
      put16(object + 0x12, get16(r));
      phase = "decompressor";
      uint32_t u = CALL1(0x453a50, object);
      uint32_t buffer = get32(P(0x48e26c));
      put32(size, u);
      if (shell_buffer_write(&out->data, out->data.size, size, 4) < 0 ||
          (u && shell_buffer_write(&out->data, out->data.size, P(buffer), u) < 0))
        die("memory disk unpack");
      CALL1(0x408760, buffer);
      free(stored);
      at += 6 + (size_t)n;
    }
  }
  return 1;
}
/* VSHELL's own text drawing into a memory canvas (`text-draw`). The
 * cases come from the memory-disk file text.in (u16 canvas width, u16
 * height, then records: u8 kind, font, align, colour, flags, background
 * colour, set, prefix; i32 l, t, r, b, x, y, width; u32 seed; u16 n, n text
 * bytes). kind 0 string draw 0x401355 at (x, y) relative to the rectangle
 * with context alignment and width; 1 label draw 0x40b9e9(label, y); 2 edit
 * draw 0x40c6ff after setText 0x40c294; 3 edit typing: setText of the first
 * `prefix` bytes, +0xb7 := prefix, then 0x40c382 per byte (8: backspace
 * 0x40c406), then 0x40c6ff. Fonts: VSHELL's loadFont 0x4088da of dfn\font2,
 * black, font, map (font 0..3), after the GL class registration 0x453048
 * (a static initializer). The canvas is filled from the seed (LCG
 * 1103515245/12345, v = bits 16..23: < 48 -> 41, < 80 -> 0, else v), bound
 * as the screen (row table at 0x47c482, stride 0x47c4a6, height 0x47c4aa;
 * driver table 0x47e69c copied to 0x47e5dc as 0x459a96 does); the context
 * is a zeroed 0x239-byte block made current ([0x48e264], viewport
 * [0x47c444] = +4) with what 0x41fbd9 -> 0x41fe84 sets: clip rectangle =
 * the widget's, clip mode 1, offset mode 1, origin 0; 0x41fbd9 itself is
 * patched to return during the op. Per record text.emit gets the SHA-256 of
 * the canvas, for kinds 2 and 3 the field's length, width and 90 text bytes, and
 * with flag 0x08 the canvas itself. */
static BYTE s27_canvas[640 * 480];
static uint32_t s27_rows[481];
static BYTE s27_context[0x239 + 16];
static uint32_t s27_fonts[4];
static void s27_text_draw(void) {
  static const char *names[4] = {"dfn\\font2.dfn", "dfn\\black.dfn", "dfn\\font.dfn", "dfn\\map.dfn"};
  static const uint32_t sets[4] = {0, 0x46ee48, 0x475869, 0x476205};
  vfile *in = vdisk_find("text.in", 0), *out = vdisk_find("text.emit", 1);
  size_t at = 4;
  if (!in || in->data.size < 4)
    die("text-draw: text.in missing");
  const BYTE *d = in->data.data;
  unsigned cw = get16(d), ch = get16(d + 2);
  if (!cw || !ch || cw > 640 || ch > 480)
    die("text-draw: canvas size");
  if (!s27_fonts[0]) {
    /* The GL classes' registration (a static initializer the worker does
     * not run): table 0x47bd6c, count [0x47bdd8], through 0x404124; the
     * object loader 0x4041ab finds the GLFont tag there. */
    if (*P(0x453048) != 0xa1 || get32(P(0x453049)) != 0x47bdd8)
      die("text-draw: registration signature");
    phase = "GL class registration";
    CALL0(0x453048);
  }
  for (unsigned k = 0; k < 4; k++)
    if (!s27_fonts[k]) {
      phase = "loadFont";
      s27_fonts[k] = CALL1(0x4088da, names[k]);
      if (!s27_fonts[k])
        die("text-draw: font not loaded");
    }
  memcpy(P(0x47e5dc), P(0x47e69c), 0x24 * 4);
  s27_rows[0] = (uint32_t)(uintptr_t)s27_canvas;
  for (unsigned i = 0; i < ch; i++)
    s27_rows[1 + i] = i * cw;
  put32(P(0x47c482), (uint32_t)(uintptr_t)s27_rows);
  put32(P(0x47c4a6), cw);
  put32(P(0x47c4aa), ch);
  BYTE first = *P(0x41fbd9);
  s17_patch(0x41fbd9, 0xc3);
  while (at < in->data.size) {
    static BYTE widget[0x100];
    if (in->data.size - at < 42)
      die("text-draw: truncated record");
    const BYTE *r = d + at;
    unsigned kind = r[0], font = r[1], flags = r[4], set = r[6], prefix = r[7];
    int32_t l = (int32_t)get32(r + 8), t = (int32_t)get32(r + 12), rr = (int32_t)get32(r + 16),
            b = (int32_t)get32(r + 20), x = (int32_t)get32(r + 24), y = (int32_t)get32(r + 28),
            width = (int32_t)get32(r + 32);
    uint32_t seed = get32(r + 36);
    unsigned n = get16(r + 40);
    if (in->data.size - at - 42 < n || kind > 3 || font > 3 || set > 3 || l < 0 || t < 0 || rr >= (int32_t)cw ||
        b >= (int32_t)ch || l > rr || t > b)
      die("text-draw: bad record");
    char text[256];
    memcpy(text, r + 42, n);
    text[n] = 0;
    at += 42 + n;
    for (unsigned i = 0; i < cw * ch; i++) {
      seed = seed * 1103515245u + 12345u;
      unsigned v = (seed >> 16) & 0xff;
      s27_canvas[i] = (BYTE)(v < 48 ? 41 : v < 80 ? 0 : v);
    }
    memset(s27_context, 0, sizeof(s27_context));
    BYTE *c = s27_context;
    put32(c + 0x208, 1);
    put32(c + 0x20c, 1);
    put32(c + 0x210, (uint32_t)l);
    put32(c + 0x214, (uint32_t)t);
    put32(c + 0x218, (uint32_t)rr);
    put32(c + 0x21c, (uint32_t)b);
    put32(P(0x48e264), (uint32_t)(uintptr_t)c);
    put32(P(0x47c444), (uint32_t)(uintptr_t)(c + 4));
    memset(widget, 0, sizeof(widget));
    put32(widget + 0x25, (uint32_t)l);
    put32(widget + 0x29, (uint32_t)t);
    put32(widget + 0x2d, (uint32_t)rr);
    put32(widget + 0x31, (uint32_t)b);
    BYTE edit_state[98];
    if (kind == 0) {
      int32_t pt[2] = {x, y};
      if (n > 200)
        die("text-draw: string too long");
      put32(c + 0x231, r[2]);
      put32(c + 0x235, (uint32_t)width);
      phase = "string draw";
      CALL3(0x401355, s27_fonts[font], pt, text);
    } else if (kind == 1) {
      if (n > 99)
        die("text-draw: label text above 99 bytes");
      put32(widget + 0x45, r[2]);
      memcpy(widget + 0x4d, text, n + 1);
      put32(widget + 0xb1, s27_fonts[font]);
      put32(widget + 0xb5, r[3]);
      put32(widget + 0xc1, flags & 1);
      put32(widget + 0xc5, r[5]);
      phase = "label draw";
      CALL2(0x40b9e9, widget, (uint32_t)y);
    } else {
      if (prefix > n || (kind == 2 && n > 89))
        die("text-draw: edit text");
      char start[256];
      memcpy(start, text, kind == 2 ? n : prefix);
      start[kind == 2 ? n : prefix] = 0;
      put32(widget + 0x9f, sets[set]);
      put32(widget + 0xa3, s27_fonts[font]);
      put32(widget + 0xa7, (flags >> 1) & 1);
      put32(widget + 0xb3, (flags >> 2) & 1);
      put32(widget + 0xbb, r[3]);
      put32(widget + 0xbf, 1);
      phase = "edit setText";
      CALL2(0x40c294, widget, start);
      put32(widget + 0xb7, kind == 2 ? 0 : prefix);
      for (unsigned i = prefix; kind == 3 && i < n; i++) {
        phase = "edit key";
        if ((BYTE)text[i] == 8)
          CALL1(0x40c406, widget);
        else
          CALL2(0x40c382, widget, (BYTE)text[i]);
      }
      phase = "edit draw";
      CALL1(0x40c6ff, widget);
      memcpy(edit_state, widget + 0xab, 8);
      memcpy(edit_state + 8, widget + 0x45, 90);
    }
    char hex[65];
    BYTE hash[32];
    digest((es2_span){s27_canvas, (size_t)cw * ch}, hex);
    for (unsigned i = 0; i < 32; i++)
      hash[i] = (BYTE)strtoul((char[3]){hex[2 * i], hex[2 * i + 1], 0}, NULL, 16);
    if (shell_buffer_write(&out->data, out->data.size, hash, 32) < 0 ||
        (kind >= 2 && shell_buffer_write(&out->data, out->data.size, edit_state, 98) < 0) ||
        ((flags & 8) && shell_buffer_write(&out->data, out->data.size, s27_canvas, (size_t)cw * ch) < 0))
      die("memory disk text.emit");
  }
  s17_patch(0x41fbd9, first);
  put32(P(0x48e264), 0);
  put32(P(0x47c444), 0x47c208);
}
/* VSHELL's own widget draws into a memory canvas (`widget-draw`). The
 * cases come from the memory-disk file widget.in (u16 canvas width, u16
 * height, then 56-byte records: u8 kind, u8 flags, u16 0; i32 l, t, r, b,
 * colour (+0x4d), f55, f59, f5d, f61, f6d, f71, grid colour; u32 seed).
 * kind 0 the hidden-WinButton erase 0x409f61, 1 the ESRect frame 0x40acd6
 * (flag 0x01 = its fill argument), 2 ESButtonFont 0x40a129, 3 ESRegionFill
 * 0x40af56, 4 ESTitle 0x40b1d7, 5 ESArm 0x40cac3, 6 ESGrid 0x40bf2c (no
 * layers), 7 ESQuad 0x40abb0, 8 ESPal 0x40d0f0. The widget is a zeroed
 * 0x700-byte object with the rectangle at +0x15 and +0x25 and the fields of
 * its class: +0x45 pressed (flag 0x04), +0x49 enabled (0x08), +0x4d colour,
 * +0x51 frame on (0x10), +0x55..+0x71 as given (+0x59 = fill flag 0x01 for
 * ESRegionFill and ESTitle, +0x65 stripes 0x20, +0x5d bevel 0x40 for ESArm),
 * +0x6e5 grid lines (0x02) and +0x6e6 the grid colour; the label pointers of
 * ESButtonFont (+0x55) and ESQuad (+0x55..+0x61) point to a label without a
 * font (its text is text-draw's). Canvas, row table, driver table, context and
 * the patched 0x41fbd9 as in text-draw (clip = the rectangle, offset mode 1,
 * origin 0); ESPal's display flush runs on a zeroed stand-in display.
 * widget.emit: the canvas's SHA-256 per record, with flag 0x80 followed by
 * the canvas itself. */
static void s28_widget_draw(void) {
  static const uint32_t draws[9] = {0x409f61, 0x40acd6, 0x40a129, 0x40af56, 0x40b1d7,
                                    0x40cac3, 0x40bf2c, 0x40abb0, 0x40d0f0};
  vfile *in = vdisk_find("widget.in", 0), *out = vdisk_find("widget.emit", 1);
  size_t at = 4;
  if (!in || in->data.size < 4)
    die("widget-draw: widget.in missing");
  const BYTE *d = in->data.data;
  unsigned cw = get16(d), ch = get16(d + 2);
  if (!cw || !ch || cw > 640 || ch > 480)
    die("widget-draw: canvas size");
  memcpy(P(0x47e5dc), P(0x47e69c), 0x24 * 4);
  s27_rows[0] = (uint32_t)(uintptr_t)s27_canvas;
  for (unsigned i = 0; i < ch; i++)
    s27_rows[1 + i] = i * cw;
  put32(P(0x47c482), (uint32_t)(uintptr_t)s27_rows);
  put32(P(0x47c4a6), cw);
  put32(P(0x47c4aa), ch);
  BYTE first = *P(0x41fbd9);
  s17_patch(0x41fbd9, 0xc3);
  while (at < in->data.size) {
    static BYTE widget[0x700], label[0x100];
    if (in->data.size - at < 56)
      die("widget-draw: truncated record");
    const BYTE *r = d + at;
    unsigned kind = r[0], flags = r[1];
    int32_t v[12];
    for (unsigned i = 0; i < 12; i++)
      v[i] = (int32_t)get32(r + 4 + 4 * i);
    uint32_t seed = get32(r + 52);
    int32_t l = v[0], t = v[1], rr = v[2], b = v[3];
    if (kind > 8 || l < 0 || t < 0 || rr >= (int32_t)cw || b >= (int32_t)ch || l > rr || t > b)
      die("widget-draw: bad record");
    at += 56;
    for (unsigned i = 0; i < cw * ch; i++) {
      seed = seed * 1103515245u + 12345u;
      s27_canvas[i] = (BYTE)((seed >> 16) & 0xff);
    }
    memset(s27_context, 0, sizeof(s27_context));
    BYTE *c = s27_context;
    put32(c + 0x208, 1);
    put32(c + 0x20c, 1);
    put32(c + 0x210, (uint32_t)l);
    put32(c + 0x214, (uint32_t)t);
    put32(c + 0x218, (uint32_t)rr);
    put32(c + 0x21c, (uint32_t)b);
    put32(P(0x48e264), (uint32_t)(uintptr_t)c);
    put32(P(0x47c444), (uint32_t)(uintptr_t)(c + 4));
    memset(widget, 0, sizeof(widget));
    memset(label, 0, sizeof(label));
    put32(widget + 0x15, (uint32_t)l);
    put32(widget + 0x19, (uint32_t)t);
    put32(widget + 0x1d, (uint32_t)rr);
    put32(widget + 0x21, (uint32_t)b);
    put32(widget + 0x25, (uint32_t)l);
    put32(widget + 0x29, (uint32_t)t);
    put32(widget + 0x2d, (uint32_t)rr);
    put32(widget + 0x31, (uint32_t)b);
    put32(widget + 0x45, (flags >> 2) & 1);
    put32(widget + 0x49, (flags >> 3) & 1);
    put32(widget + 0x4d, (uint32_t)v[4]);
    put32(widget + 0x51, (flags >> 4) & 1);
    if (kind == 2 || kind == 7) {
      for (unsigned k = 0; k < (kind == 7 ? 4u : 1u); k++)
        put32(widget + 0x55 + 4 * k, (uint32_t)(uintptr_t)label);
    } else if (kind != 6) {
      put32(widget + 0x55, (uint32_t)v[5]);
      put32(widget + 0x59, kind == 3 || kind == 4 ? (uint32_t)(flags & 1) : (uint32_t)v[6]);
      put32(widget + 0x5d, kind == 5 ? (uint32_t)((flags >> 6) & 1) : (uint32_t)v[7]);
      put32(widget + 0x61, (uint32_t)v[8]);
      put32(widget + 0x65, (flags >> 5) & 1);
      put32(widget + 0x6d, (uint32_t)v[9]);
      put32(widget + 0x71, (uint32_t)v[10]);
    }
    if (kind == 6) {
      widget[0x6e5] = (BYTE)((flags >> 1) & 1);
      put16(widget + 0x6e6, (uint16_t)v[11]);
    }
    phase = "widget draw";
    /* ESPal's draw ends with the display flush 0x41fe49([0x48118c]): an empty
     * zeroed display object (queue count +0x659 = 0) stands in for it. */
    static BYTE display[0x700];
    uint32_t display_was = get32(P(0x48118c));
    memset(display, 0, sizeof(display));
    put32(P(0x48118c), (uint32_t)(uintptr_t)display);
    if (kind == 1)
      CALL2(draws[1], widget, (uint32_t)(flags & 1));
    else
      CALL1(draws[kind], widget);
    put32(P(0x48118c), display_was);
    char hex[65];
    BYTE hash[32];
    digest((es2_span){s27_canvas, (size_t)cw * ch}, hex);
    for (unsigned i = 0; i < 32; i++)
      hash[i] = (BYTE)strtoul((char[3]){hex[2 * i], hex[2 * i + 1], 0}, NULL, 16);
    if (shell_buffer_write(&out->data, out->data.size, hash, 32) < 0 ||
        ((flags & 0x80) && shell_buffer_write(&out->data, out->data.size, s27_canvas, (size_t)cw * ch) < 0))
      die("memory disk widget.emit");
  }
  s17_patch(0x41fbd9, first);
  put32(P(0x48e264), 0);
  put32(P(0x47c444), 0x47c208);
}

/* Movie queue and player (vshell_ui.md section 19):
 *   movie-init      the static initialiser 0x41f11e (ten empty records),
 *                   write/play indices 0x470fac/0x470fae and the player's
 *                   flags 0x470fb0, 0x471120, 0x471124 := 0, movie flag
 *                   0x482359 := 1 (the default 0x4079d5), once-flags
 *                   0x477946..0x477948 := 0
 *   movie-flag:V    0x482359 := V
 *   movie-once:A:B:C  0x477946, 0x477947, 0x477948 := A, B, C
 *   movie-enqueue:I:X:Y:W:H:P:C:F  0x41e8c0(I, rect X,Y,W,H, P, C, F), every
 *                   value an unsigned 32-bit number (the dwords pushed)
 *   movie-play:A:BAR:PANEL:FULL:PAGE  0x41e98c(A) with stand-in objects: the
 *                   bay bar [0x48d528] and the theatre panel [0x48d8c0]
 *                   visible (+0x11 bit 1 clear) when BAR/PANEL are 1, every
 *                   other object it touches a nop object; [0x46d878] := FULL,
 *                   page word 0x481108 := PAGE, focus 0x46c288 := 1. The
 *                   functions it calls are trapped (0x41e51b returns 0: the
 *                   avivideo device "does not open", so no MCI is reached;
 *                   the __stdcall busy cursor 0x408d2f is a plain "ret 4");
 *                   recorded: 0x42f6a8, 0x407b62, 0x407c1b, 0x445343,
 *                   0x41e51b, 0x406328, 0x43a430, 0x445095, 0x42f5d3,
 *                   0x44472f, 0x4022c5, 0x44483e and the log 0x46a834
 *   movie-dump      appends 304 bytes to movies.emit: the ring 0x485748
 *                   (280), words 0x470fac, 0x470fae, dwords 0x470fb0,
 *                   0x471120, 0x471124, bytes 0x482359, 0x477946..0x477948,
 *                   words 0x48419c, 0x48419e
 * The s17_op traps stay armed alongside (s15_arm extends its extra list). */
static const ui_entry s15_ui_entries[] = {
    {0x42f6a8, 0x53}, {0x407b62, 0x55}, {0x407c1b, 0x80}, {0x445343, 0x55},
    {0x41e51b, 0x55}, {0x406328, 0x55}, {0x43a430, 0x55}, {0x445095, 0x66},
    {0x42f5d3, 0xa1}, {0x44472f, 0x55}, {0x4022c5, 0x55}, {0x44483e, 0xff},
    {0x46a834, 0x55}, {0x41e640, 0x55}, {0x41e5bb, 0x55}, {0x4077df, 0x83},
    {0x401a81, 0x55}, {0x42f7c2, 0xe8}};
static const uint32_t s15_objects[] = {0x48d530, 0x48d534, 0x48d538, 0x48d53c, 0x48d540,
                                       0x48d544, 0x48d548, 0x48d54c, 0x48d8e0, 0x48d8b4,
                                       0x48d8b8, 0x48d8bc, 0x481180, 0x48d520};
static void __cdecl s15_nop(void *self) { (void)self; }
static uint32_t s15_vtable[16];
static BYTE s15_object[0x100], s15_bar[0x100], s15_panel[0x100];
static int s15_armed;
/* The extra trap list after s15_arm: the s17_op entries (every op reaches
 * s17_op first, which arms them) followed by the movie entries. */
static ui_entry s15_extra[128];
static void s15_arm(void) {
  if (s15_armed)
    return;
  unsigned n = 0;
  for (unsigned i = 0; i < ui_extra_count; i++) {
    if (n == 128)
      die("trap list");
    s15_extra[n++] = ui_extra_list[i];
  }
  for (unsigned i = 0; i < sizeof(s15_ui_entries) / sizeof(s15_ui_entries[0]); i++) {
    int armed = 0;
    for (unsigned j = 0; ui_armed && j < ui_armed_count; j++)
      if (ui_armed_list[j].address == s15_ui_entries[i].address)
        armed = 1;
    for (unsigned j = 0; j < n; j++)
      if (s15_extra[j].address == s15_ui_entries[i].address)
        armed = 1;
    if (armed)
      continue;
    if (*P(s15_ui_entries[i].address) != s15_ui_entries[i].first)
      die("UI entry signature");
    s17_patch(s15_ui_entries[i].address, 0xcc);
    if (n == 128)
      die("trap list");
    s15_extra[n++] = s15_ui_entries[i];
  }
  ui_extra_list = s15_extra;
  ui_extra_count = n;
  for (unsigned i = 0; i < 16; i++)
    s15_vtable[i] = (uint32_t)(uintptr_t)s15_nop;
  put32(s15_object + 1, (uint32_t)(uintptr_t)s15_vtable);
  put32(s15_bar + 1, (uint32_t)(uintptr_t)s15_vtable);
  put32(s15_panel + 1, (uint32_t)(uintptr_t)s15_vtable);
  s15_armed = 1;
}
typedef uint32_t(__cdecl *s15_enqueue_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                          uint32_t, uint32_t);
static int s15_op(const char *op) {
  unsigned v[8];
  if (!strcmp(op, "movie-init")) {
    phase = "movie static initialiser";
    CALL0(0x41f11e);
    put16(P(0x470fac), 0);
    put16(P(0x470fae), 0);
    put32(P(0x470fb0), 0);
    put32(P(0x471120), 0);
    put32(P(0x471124), 0);
    *P(0x482359) = 1;
    *P(0x477946) = *P(0x477947) = *P(0x477948) = 0;
  } else if (sscanf(op, "movie-flag:%u", &v[0]) == 1 && v[0] < 256) {
    *P(0x482359) = (BYTE)v[0];
  } else if (sscanf(op, "movie-once:%u:%u:%u", &v[0], &v[1], &v[2]) == 3 && v[0] < 256 && v[1] < 256 &&
             v[2] < 256) {
    *P(0x477946) = (BYTE)v[0];
    *P(0x477947) = (BYTE)v[1];
    *P(0x477948) = (BYTE)v[2];
  } else if (sscanf(op, "movie-enqueue:%u:%u:%u:%u:%u:%u:%u:%u", &v[0], &v[1], &v[2], &v[3], &v[4],
                    &v[5], &v[6], &v[7]) == 8) {
    phase = "movie enqueue";
    trace_before(0x41e8c0);
    ((s15_enqueue_fn)(uintptr_t)0x41e8c0)(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
  } else if (sscanf(op, "movie-play:%u:%u:%u:%u:%u", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5 &&
             v[0] < 256 && v[1] < 2 && v[2] < 2 && v[4] < 65536) {
    uint32_t saved[sizeof(s15_objects) / sizeof(s15_objects[0])], bar = get32(P(0x48d528)),
        panel = get32(P(0x48d8c0));
    s15_arm();
    for (unsigned i = 0; i < sizeof(s15_objects) / sizeof(s15_objects[0]); i++) {
      saved[i] = get32(P(s15_objects[i]));
      put32(P(s15_objects[i]), (uint32_t)(uintptr_t)s15_object);
    }
    s15_bar[0x11] = v[1] ? 0 : 2;
    s15_panel[0x11] = v[2] ? 0 : 2;
    put32(P(0x48d528), (uint32_t)(uintptr_t)s15_bar);
    put32(P(0x48d8c0), (uint32_t)(uintptr_t)s15_panel);
    put32(P(0x46d878), v[3]);
    put16(P(0x481108), (uint16_t)v[4]);
    put32(P(0x46c288), 1);
    /* The busy cursor 0x408d2f is __stdcall (ret 4) and the player does not
     * restore esp from ebp: its quiet trap would leak the argument, so for
     * this call it is replaced by its own "ret 4". */
    BYTE busy[3];
    memcpy(busy, P(0x408d2f), 3);
    s17_patch(0x408d2f, 0xc2);
    s17_patch(0x408d30, 0x04);
    s17_patch(0x408d31, 0x00);
    phase = "movie player";
    CALL1(0x41e98c, v[0]);
    for (unsigned i = 0; i < 3; i++)
      s17_patch(0x408d2f + i, busy[i]);
    for (unsigned i = 0; i < sizeof(s15_objects) / sizeof(s15_objects[0]); i++)
      put32(P(s15_objects[i]), saved[i]);
    put32(P(0x48d528), bar);
    put32(P(0x48d8c0), panel);
  } else if (!strcmp(op, "movie-dump")) {
    BYTE tail[24];
    vfile *f = vdisk_find("movies.emit", 1);
    memcpy(tail, P(0x470fac), 2);
    memcpy(tail + 2, P(0x470fae), 2);
    memcpy(tail + 4, P(0x470fb0), 4);
    memcpy(tail + 8, P(0x471120), 4);
    memcpy(tail + 12, P(0x471124), 4);
    tail[16] = *P(0x482359);
    tail[17] = *P(0x477946);
    tail[18] = *P(0x477947);
    tail[19] = *P(0x477948);
    memcpy(tail + 20, P(0x48419c), 2);
    memcpy(tail + 22, P(0x48419e), 2);
    if (shell_buffer_write(&f->data, f->data.size, P(0x485748), 280) < 0 ||
        shell_buffer_write(&f->data, f->data.size, tail, sizeof(tail)) < 0)
      die("memory disk movies");
  } else
    return 0;
  return 1;
}

/* The mission map's primitives (vshell_mission_map.md, "Own implementation"):
 *   map-raster  every record of the memory-disk file map.in through one of
 *               VSHELL's drawing routines into a memory canvas; map.emit gets
 *               the canvas's SHA-256 per record, with flag 0x01 followed by
 *               the canvas. map.in: u16 canvas width, height; records of 124
 *               bytes (LE): u8 kind, clip mode, offset flag, flags; i32 l, t,
 *               r, b (context rectangle), origin x, y (+0x220, +0x224); u32
 *               canvas seed (LCG 1103515245/12345, bits 16..23), colour; i32
 *               a[20]; u16 bitmap width, height; u8 bitmap flags, 3 zero
 *               bytes; then width * height bitmap pixels. Kinds: 0 the
 *               triangle 0x457de8(a[0..5], words a[6..8]); 1 the polygon
 *               0x455ad8(a[0], a + 1); 2 the textured quad 0x45399c(bitmap,
 *               &a[0..1], 0, &a[2..3], 0); 3 the line 0x455624(&a[0..1],
 *               &a[2..3]); 4 the ellipse 0x45886c(a[0..3]); 5 the pixel
 *               0x459cdc(&a[0..1]); 6 the rectangle 0x4576a4(a[0..3]) with a
 *               kind-4 brush, 7 with a kind-0 brush; 8 the bitmap 0x4537e8(
 *               bitmap, &a[0..1], 0); 9 the scaled bitmap 0x453908(bitmap,
 *               &a[0..1], 0, &a[2..3]). The pen and the brush carry the
 *               colour; the canvas is the bound screen (row table 0x47c482,
 *               width 0x47c4a6 and 0x47c486, height 0x47c4aa and 0x47c48a,
 *               driver table 0x47e69c copied to 0x47e5dc); the context is a
 *               zeroed 0x239-byte block made current. The bitmap object is a
 *               zeroed GLBitmap stand-in (header at +4, kind word +0x12 = 0). */
static void s40_raster(void) {
  vfile *in = vdisk_find("map.in", 0), *out = vdisk_find("map.emit", 1);
  size_t at = 4;
  if (!in || in->data.size < 4)
    die("map-raster: map.in missing");
  const BYTE *d = in->data.data;
  unsigned cw = get16(d), ch = get16(d + 2);
  if (!cw || !ch || cw > 640 || ch > 480)
    die("map-raster: canvas size");
  uint32_t saved[6] = {get32(P(0x47c482)), get32(P(0x47c4a6)), get32(P(0x47c4aa)),
                       get32(P(0x47c486)), get32(P(0x47c48a)), get32(P(0x47c444))};
  memcpy(P(0x47e5dc), P(0x47e69c), 0x24 * 4);
  s27_rows[0] = (uint32_t)(uintptr_t)s27_canvas;
  for (unsigned i = 0; i < ch; i++)
    s27_rows[1 + i] = i * cw;
  put32(P(0x47c482), (uint32_t)(uintptr_t)s27_rows);
  put32(P(0x47c4a6), cw);
  put32(P(0x47c4aa), ch);
  put32(P(0x47c486), cw);
  put32(P(0x47c48a), ch);
  while (at < in->data.size) {
    static BYTE object[0x20], pixels[65536];
    static uint32_t pen[2], brush[2];
    static int32_t a[20];
    if (in->data.size - at < 124)
      die("map-raster: truncated record");
    const BYTE *r = d + at;
    unsigned kind = r[0], bw = get16(r + 116), bh = get16(r + 118);
    if (kind > 9 || r[1] > 1 || (size_t)bw * bh > sizeof(pixels) || in->data.size - at - 124 < (size_t)bw * bh)
      die("map-raster: bad record");
    uint32_t seed = get32(r + 28), colour = get32(r + 32);
    for (unsigned i = 0; i < 20; i++)
      a[i] = (int32_t)get32(r + 36 + 4 * i);
    memcpy(pixels, r + 124, (size_t)bw * bh);
    at += 124 + (size_t)bw * bh;
    for (unsigned i = 0; i < cw * ch; i++) {
      seed = seed * 1103515245u + 12345u;
      s27_canvas[i] = (BYTE)((seed >> 16) & 0xff);
    }
    memset(s27_context, 0, sizeof(s27_context));
    BYTE *c = s27_context;
    put32(c + 0x208, r[1]);
    put32(c + 0x20c, r[2]);
    for (unsigned k = 0; k < 6; k++)
      put32(c + 0x210 + 4 * k, get32(r + 4 + 4 * k));
    pen[0] = 0;
    pen[1] = colour;
    brush[0] = kind == 6 ? 4 : 0;
    brush[1] = colour;
    put32(c + 0x228, (uint32_t)(uintptr_t)pen);
    put32(c + 0x22c, (uint32_t)(uintptr_t)brush);
    put32(P(0x48e264), (uint32_t)(uintptr_t)c);
    put32(P(0x47c444), (uint32_t)(uintptr_t)(c + 4));
    memset(object, 0, sizeof(object));
    put16(object + 4, (uint16_t)bh);
    put16(object + 6, (uint16_t)bw);
    object[8] = 8;
    object[9] = r[120];
    put32(object + 0xa, (uint32_t)bw * bh);
    put32(object + 0xe, (uint32_t)(uintptr_t)pixels);
    phase = "map raster";
    switch (kind) {
    case 0: {
      static uint16_t words[3];
      for (unsigned k = 0; k < 3; k++)
        words[k] = (uint16_t)a[6 + k];
      CALL2(0x457de8, a, words);
      break;
    }
    case 1:
      if (a[0] < 3 || a[0] > 8)
        die("map-raster: polygon size");
      CALL2(0x455ad8, a[0], a + 1);
      break;
    case 2:
      trace_before(0x45399c);
      ((void(__cdecl *)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t))(uintptr_t)0x45399c)(
          (uintptr_t)object, (uintptr_t)a, 0, (uintptr_t)(a + 2), 0);
      break;
    case 3:
      CALL2(0x455624, a, a + 2);
      break;
    case 4:
      CALL1(0x45886c, a);
      break;
    case 5:
      CALL1(0x459cdc, a);
      break;
    case 6:
    case 7:
      CALL1(0x4576a4, a);
      break;
    case 8:
      CALL3(0x4537e8, object, a, 0);
      break;
    default:
      trace_before(0x453908);
      ((void(__cdecl *)(uintptr_t, uintptr_t, uintptr_t, uintptr_t))(uintptr_t)0x453908)(
          (uintptr_t)object, (uintptr_t)a, 0, (uintptr_t)(a + 2));
      break;
    }
    char hex[65];
    BYTE hash[32];
    digest((es2_span){s27_canvas, (size_t)cw * ch}, hex);
    for (unsigned i = 0; i < 32; i++)
      hash[i] = (BYTE)strtoul((char[3]){hex[2 * i], hex[2 * i + 1], 0}, NULL, 16);
    if (shell_buffer_write(&out->data, out->data.size, hash, 32) < 0 ||
        ((r[3] & 1) && shell_buffer_write(&out->data, out->data.size, s27_canvas, (size_t)cw * ch) < 0))
      die("memory disk map.emit");
  }
  put32(P(0x47c482), saved[0]);
  put32(P(0x47c4a6), saved[1]);
  put32(P(0x47c4aa), saved[2]);
  put32(P(0x47c486), saved[3]);
  put32(P(0x47c48a), saved[4]);
  put32(P(0x48e264), 0);
  put32(P(0x47c444), saved[5]);
}

/* The map object itself:
 *   map-build:ID  VSHELL's build 0x40e75f on the memory disk: record ID of
 *                 maps.in ([u32 id][u32 n][n bytes data\script.dat][u32 n]
 *                 [n bytes data\player.mec][u32 n][n bytes data\mission.str]
 *                 per record) becomes data\script.dat, player.mec and
 *                 mission.str; data\mforms.dat, data\mat0.dat and
 *                 data\maplabel.str (the CD's DATA files) are --file inputs;
 *                 the shared tables 0x424408 (dba\mis_icon.dba ...) are
 *                 loaded once. The frame [0x481948] is a zeroed 640 x 480
 *                 buffer; the second RNG state 0x48110a starts as at process
 *                 start (0x465cbc with 0). Appends to map.emit: the SHA-256
 *                 of the terrain image [0x48a39a] (width [0x48a392] x height
 *                 [0x48a390]), both words, the 114 bytes of 0x48110a after
 *                 the build, then a probe (below).
 *   map-step:T    the fly-over step 0x4262cf with the tick 0x465d5c patched
 *                 to return a counter that starts at T and grows by one per
 *                 call (so a busy wait of n ticks takes n calls); probe.
 *   map-skip      0x425a43; probe.
 *   map-arrow:K   vtable slot K (0 north +0x0c, 1 south +0x10, 2 west +0x14,
 *                 3 east +0x18, 4 zoom in +0x04, 5 zoom out +0x08), then the
 *                 paint (slot 0), as the arrow handlers do per step; probe.
 *   map-paint     slot 0; probe.
 *   map-frame     appends the 640 x 480 frame itself to map.frames.
 * The probe (64 bytes): the frame's SHA-256, the state word +0x172, the step
 * result (or 0xff), the done byte +0x17a, the tick [0x471dc4], the view
 * +0x12 (12 bytes), the pan +0x3a (8 bytes), the pan step +0x42. */
static BYTE s40_frame[640 * 480];
static uint32_t s40_clock;
static int s40_ready;
static void s40_probe(vfile *out, unsigned result) {
  BYTE probe[64] = {0};
  uint32_t map = get32(P(0x46f3a4));
  char hex[65];
  digest((es2_span){s40_frame, sizeof(s40_frame)}, hex);
  for (unsigned i = 0; i < 32; i++)
    probe[i] = (BYTE)strtoul((char[3]){hex[2 * i], hex[2 * i + 1], 0}, NULL, 16);
  if (map) {
    memcpy(probe + 32, P(map + 0x172), 2);
    probe[35] = *P(map + 0x17a);
    memcpy(probe + 40, P(map + 0x12), 12);
    memcpy(probe + 52, P(map + 0x3a), 8);
    memcpy(probe + 60, P(map + 0x42), 4);
  }
  probe[34] = (BYTE)result;
  memcpy(probe + 36, P(0x471dc4), 4);
  if (shell_buffer_write(&out->data, out->data.size, probe, 64) < 0)
    die("memory disk map.emit");
}
static int __cdecl s40_atexit(void (*f)(void)) {
  (void)f;
  return 0;
}
static void s40_setup(void) {
  if (s40_ready)
    return;
  /* the CD's loose DATA files (the installer copies them to DATA) */
  static const char *cd_files[] = {"data\\mforms.dat", "data\\mat0.dat", "data\\maplabel.str"};
  for (unsigned i = 0; i < 3; i++)
    if (!vdisk_find(cd_files[i], 0))
      die("map-build: data\\mforms.dat, mat0.dat and maplabel.str must be --file inputs");
  bind_import("_strncpy", (void *)(uintptr_t)strncpy);
  bind_import("_atexit", (void *)(uintptr_t)s40_atexit); /* static destructors: never run here */
  bind_import("_stricmp", (void *)(uintptr_t)_stricmp);
  bind_import("_strcmp", (void *)(uintptr_t)strcmp);
  if (*P(0x453048) != 0xa1 || get32(P(0x453049)) != 0x47bdd8)
    die("map-build: registration signature");
  if (!s27_fonts[0]) {
    phase = "GL class registration";
    CALL0(0x453048);
  }
  /* the tick 0x465d5c: inc dword [clock]; mov eax, [clock]; ret */
  if (*P(0x465d5c) != 0xe8 || *P(0x465d6a) != 0xc3)
    die("map-build: tick signature");
  BYTE code[12] = {0xff, 0x05, 0, 0, 0, 0, 0xa1, 0, 0, 0, 0, 0xc3};
  put32(code + 2, (uint32_t)(uintptr_t)&s40_clock);
  put32(code + 7, (uint32_t)(uintptr_t)&s40_clock);
  for (unsigned i = 0; i < sizeof(code); i++)
    s17_patch(0x465d5c + i, code[i]);
  memcpy(P(0x47e5dc), P(0x47e69c), 0x24 * 4);
  /* the frame header [0x481948] as 0x4065b0 builds it: height 480, width
   * 640, 8 bit, flag 0x10, size, pixels */
  static BYTE frame_header[14];
  put16(frame_header, 480);
  put16(frame_header + 2, 640);
  frame_header[4] = 8;
  frame_header[5] = 0x10;
  put32(frame_header + 6, sizeof(s40_frame));
  put32(frame_header + 10, (uint32_t)(uintptr_t)s40_frame);
  put32(P(0x481948), (uint32_t)(uintptr_t)frame_header);
  /* a stand-in current screen [0x48e25c] (= [0x48e240 + 7 * 4]) whose
   * viewport (+0x40, +0x44) is the 640 x 480 frame's, and a current context
   * [0x48e264] holding a copy of it: the context constructor 0x452da0 copies
   * the screen's viewport, 0x427634 restores the context it found */
  static BYTE screen[0x100], viewport[0x240], context[0x240];
  put32(viewport + 0x20c, 0);
  put32(viewport + 0x210, 0);
  put32(viewport + 0x214, 639);
  put32(viewport + 0x218, 479);
  put32(screen + 0x40, (uint32_t)(uintptr_t)viewport);
  put32(screen + 0x44, (uint32_t)(uintptr_t)viewport);
  memcpy(context + 4, viewport, 0x22c);
  put32(P(0x48e25c), (uint32_t)(uintptr_t)screen);
  put32(P(0x48e264), (uint32_t)(uintptr_t)context);
  put32(P(0x47c444), (uint32_t)(uintptr_t)(context + 4));
  /* the 3Space identity matrix 0x5d8088 and the camera/object matrix
   * pointers, as the static initializer at 0x45fda5 leaves them */
  phase = "identity matrix";
  CALL1(0x465ab8, 0x5d8088);
  put32(P(0x5d8080), 0x5d8088);
  put32(P(0x5d8084), 0x5d8088);
  /* the static initializer 0x420839: the camera angles [0x4719b4] =
   * (0xc000, 0, 0) that every map camera points to */
  if (*P(0x420839) != 0x66)
    die("map-build: 0x420839 signature");
  phase = "camera angles";
  CALL0(0x420839);
  /* VSHELL's second RNG state as every process starts it (0x4023d3) */
  phase = "RNG 0x48110a";
  CALL2(0x465cbc, 0x48110a, 0);
  phase = "map tables";
  CALL0(0x424408);
  s40_ready = 1;
}
static int s40_op(const char *op) {
  unsigned a = 0;
  int build = sscanf(op, "map-build:%u", &a) == 1, step = sscanf(op, "map-step:%u", &a) == 1;
  int arrow = sscanf(op, "map-arrow:%u", &a) == 1 && a < 6;
  if (!build && !step && !arrow && strcmp(op, "map-skip") && strcmp(op, "map-paint") && strcmp(op, "map-frame"))
    return 0;
  vfile *out = vdisk_find("map.emit", 1);
  s40_setup();
  uint32_t map = get32(P(0x46f3a4));
  if (build) {
    vfile *in = vdisk_find("maps.in", 0);
    size_t at = 0;
    if (!in)
      die("map-build: maps.in missing");
    for (;;) {
      if (in->data.size - at < 8)
        die("map-build: record missing");
      const BYTE *r = in->data.data + at;
      size_t n[3], o = 4;
      for (unsigned k = 0; k < 3; k++) {
        if (in->data.size - at - o < 4)
          die("map-build: truncated record");
        n[k] = get32(r + o);
        if (in->data.size - at - o - 4 < n[k])
          die("map-build: truncated record");
        o += 4 + n[k];
      }
      if (get32(r) == a) {
        const BYTE *p = r + 4;
        vdisk_add("data\\script.dat", (es2_span){p + 4, n[0]});
        p += 4 + n[0];
        vdisk_add("data\\player.mec", (es2_span){p + 4, n[1]});
        p += 4 + n[1];
        vdisk_add("data\\mission.str", (es2_span){p + 4, n[2]});
        break;
      }
      at += o;
    }
    memset(s40_frame, 0, sizeof(s40_frame));
    /* the slot operations trap 0x40e75f (a load rebuilds the map): the
     * original first byte comes back for this call */
    BYTE trapped = *P(0x40e75f), cells = *P(0x4290b3);
    if (trapped != 0x55)
      s17_patch(0x40e75f, 0x55);
    /* 0x4290b3, the per-cell vertex pass of the 3D terrain (cell fields
     * +2..+0x16, never drawn by the map), needs a bound 3Space camera the
     * worker does not have: it returns at once here */
    if (cells != 0x55)
      die("map-build: 0x4290b3 signature");
    s17_patch(0x4290b3, 0xc3);
    phase = "map build";
    CALL0(0x40e75f);
    s17_patch(0x4290b3, cells);
    if (trapped != 0x55)
      s17_patch(0x40e75f, trapped);
    map = get32(P(0x46f3a4));
    if (!map)
      die("map-build: no map object");
    BYTE head[36 + 114] = {0};
    uint32_t pixels = get32(P(0x48a39a));
    unsigned w = get16(P(0x48a392)), h = get16(P(0x48a390));
    char hex[65];
    digest((es2_span){pixels ? P(pixels) : (BYTE *)"", pixels ? (size_t)w * h : 0}, hex);
    for (unsigned i = 0; i < 32; i++)
      head[i] = (BYTE)strtoul((char[3]){hex[2 * i], hex[2 * i + 1], 0}, NULL, 16);
    put16(head + 32, (uint16_t)w);
    put16(head + 34, (uint16_t)h);
    memcpy(head + 36, P(0x48110a), 114); /* the second RNG state after the material draws */
    if (shell_buffer_write(&out->data, out->data.size, head, sizeof(head)) < 0)
      die("memory disk map.emit");
    s40_probe(out, 0xff);
    return 1;
  }
  if (!map && strcmp(op, "map-frame"))
    die("map: no map object");
  uint32_t vtable = map ? get32(P(map)) : 0;
  if (step) {
    s40_clock = a - 1;
    phase = "map step";
    unsigned r = CALL1(0x4262cf, map) & 0xff;
    s40_probe(out, r);
  } else if (arrow) {
    static const unsigned slots[6] = {0x0c, 0x10, 0x14, 0x18, 0x04, 0x08};
    phase = "map arrow";
    CALL1(get32(P(vtable + slots[a])), map);
    CALL1(get32(P(vtable)), map);
    s40_probe(out, 0xff);
  } else if (!strcmp(op, "map-skip")) {
    CALL1(0x425a43, map);
    s40_probe(out, 0xff);
  } else if (!strcmp(op, "map-paint")) {
    phase = "map paint";
    CALL1(get32(P(vtable)), map);
    s40_probe(out, 0xff);
  } else {
    vfile *frames = vdisk_find("map.frames", 1);
    if (shell_buffer_write(&frames->data, frames->data.size, s40_frame, sizeof(s40_frame)) < 0)
      die("memory disk map.frames");
  }
  return 1;
}
/* The squad panel's condition: `squad-condition` appends to the
 * memory-disk file squad.condition, per bay k = 0..7 of the hangar
 * [0x482ba7 + 4k], a byte (1: a record) and the word 0x4121f8(record + 8,
 * record word +0x4c) (100 without a record, as 0x43e0d7 passes it). */
static void s47_condition(void) {
  BYTE out[24];
  vfile *f;
  phase = "squad condition";
  for (int k = 0; k < 8; k++) {
    uint32_t record = get32(P(0x482ba7 + 4 * k));
    uint16_t v = 100;
    if (record)
      v = (uint16_t)CALL2(0x4121f8, record + 8, (int16_t)get16(P(record + 0x4c)));
    out[k] = record != 0;
    out[8 + 2 * k] = (BYTE)v;
    out[9 + 2 * k] = (BYTE)(v >> 8);
  }
  f = vdisk_find("squad.condition", 1);
  if (shell_buffer_write(&f->data, f->data.size, out, sizeof(out)) < 0)
    die("memory disk squad.condition");
}
static int run_slots(options *o) {
  vdisk_mode = 1;
  bind_import("_sprintf", (void *)(uintptr_t)sprintf);
  /* The prefs loader and its read-modify-write clear and copy blocks */
  bind_import("_memset", (void *)(uintptr_t)memset);
  bind_import("_memcpy", (void *)(uintptr_t)memcpy);
  phase = "slot table load";
  CALL0(0x40e380);
  phase = "damage load";
  CALL0(0x413c4c);
  phase = "weapons load";
  CALL0(0x4125e8);
  char before[12 * 200];
  slot_table(before, sizeof(before));
  if (o->save) {
    restore_save();
    *P(0x4826ee) = (BYTE)!o->not_loaded;
  }
  put16(P(0x4826f0), (uint16_t)(o->mode < 0 ? 1 : o->mode));
  if (o->rng_state) {
    es2_span rng = disk_read(o->rng_state);
    if (rng.size != 114 || rng.data[112] >= 56 || rng.data[113] >= 56)
      die("RNG state must have 114 bytes and cursors below 56");
    memcpy(P(0x482409), rng.data, 114);
  } else {
    phase = "RNG initialize";
    CALL2(0x465cbc, 0x482409, o->seed);
  }
  /* The box and Preferences entries as well; every function they trap
   * dereferences screen objects the worker never builds (or calls Win32
   * imports it does not bind), so no earlier slot operation reaches them. */
  ui_armed_list = box_ui_entries;
  ui_armed_count = sizeof(box_ui_entries) / sizeof(box_ui_entries[0]);
  (void)slot_ui_entries;
  ui_arm();
  static char names[MAX_OPS][96];
  static unsigned arm_answers[MAX_OPS][4];
  static int check_answers[MAX_OPS][4];
  unsigned arm_count = 0, check_count = 0;
  int arm_loaded = 0;
  for (unsigned i = 0; i < o->op_count; i++) {
    const char *op = o->ops[i];
    char *text = names[i];
    unsigned a, b, c;
    int used = 0;
    if (strlen(op) >= sizeof(names[i]))
      die("operation too long");
    if (sscanf(op, "new:%u:%n", &a, &used) == 1 && used > 0 && a <= 65535) {
      strcpy(text, op + used);
      if (!*text || strlen(text) > 29)
        die("new game name must have 1..29 bytes");
      phase = "new game";
      CALL2(0x40e8a5, text, a);
    } else if (sscanf(op, "load:%u:%u", &a, &b) == 2 && a < 12 && b < 2) {
      phase = "slot load";
      CALL2(0x40eaaa, a, b);
    } else if (sscanf(op, "save:%u%n", &a, &used) == 1 && a < 12 &&
               (!op[used] || op[used] == ':')) {
      phase = "slot save";
      if (op[used] == ':')
        strcpy(text, op + used + 1);
      CALL2(0x40e933, a, op[used] == ':' ? (uintptr_t)text : 0);
    } else if (sscanf(op, "practice:%u", &a) == 1 && a < 65536) {
      put16(P(0x479c54), (uint16_t)a);
    } else if (sscanf(op, "mount:%u:%u:%u", &a, &b, &c) == 3 && a < 8 && b < 10 && c < 33) {
      uint32_t record = get32(P(0x482ba7 + 4 * a));
      if (!record)
        die("mount: the bay has no hangar record");
      phase = "mount";
      CALL3(0x411b10, record, b, c);
    } else if (sscanf(op, "guidance:%u:%u:%u", &a, &b, &c) == 3 && a < 8 && b < 10 && c < 65536) {
      put16(P(0x482bc9), (uint16_t)a);
      put16(P(0x476658), (uint16_t)b);
      phase = "guidance";
      CALL1(0x43e346, c);
    } else if (sscanf(op, "rebalance:%u", &a) == 1 && a < 256) {
      *P(0x4825c9) = (BYTE)a;
      phase = "rebalance";
      CALL0(0x412a37);
    } else if (sscanf(op, "arm:%u:%u:%u", &a, &b, &c) == 3 && a < 8 && (b < 10 || b == 255) && c < 33) {
      if (!arm_loaded) {
        phase = "ARM tables";
        CALL0(0x41508f);
        arm_loaded = 1;
      }
      put16(P(0x482bc9), (uint16_t)a);
      put16(P(0x476658), (uint16_t)(b == 255 ? 0xffff : b));
      phase = "ARM test";
      arm_answers[arm_count][0] = a;
      arm_answers[arm_count][1] = b;
      arm_answers[arm_count][2] = c;
      arm_answers[arm_count++][3] = CALL1(0x41501f, c) & 0xff;
    } else if (!strcmp(op, "launch")) {
      phase = "launch files";
      CALL0(0x40f68c);
    } else if (!strcmp(op, "checks")) {
      int bay = (int16_t)get16(P(0x482b82));
      phase = "launch checks";
      check_answers[check_count][0] = bay;
      check_answers[check_count][1] =
          bay == -1 ? -1 : (int)(CALL2(0x4110c1, 0x482ba7, (uint32_t)bay) & 0xff);
      check_answers[check_count][2] =
          bay == -1 ? -1 : (int)(CALL2(0x411135, 0x482ba7, (uint32_t)bay) & 0xff);
      check_answers[check_count++][3] = (int)(CALL0(0x40fc7e) & 0xff);
    } else if (!strcmp(op, "rockroll")) {
      phase = "Rock & Roll";
      CALL1(0x445b99, 0);
    } else if (sscanf(op, "restore:%u", &a) == 1 && a < o->state_count) {
      phase = "state restore";
      restore_state(o->states[a]);
      *P(0x4826ee) = 1;
    } else if (sscanf(op, "final:%u", &a) == 1 && a < 65536) {
      put16(P(0x4826f2), (uint16_t)a);
    } else if (!strcmp(op, "replay-yes")) {
      phase = "Replay Yes";
      CALL1(0x44d1d4, 0);
    } else if (!strcmp(op, "replay-no")) {
      phase = "Replay No";
      CALL1(0x44d24d, 0);
    } else if (!strcmp(op, "continue")) {
      phase = "CONTINUE GAME";
      CALL1(0x431a73, 0);
    } else if (sscanf(op, "mode:%u", &a) == 1 && a < 65536) {
      phase = "mode store";
      CALL1(0x40ec56, a);
    } else if (sscanf(op, "display:%u", &a) == 1) {
      put32(P(0x481f4c), a);
    } else if (!strcmp(op, "prefs-load")) {
      phase = "prefs load";
      CALL0(0x40dc44);
    } else if (!strncmp(op, "prefs:", 6)) {
      unsigned k = 0, n = sizeof(prefs_controls) / sizeof(prefs_controls[0]);
      while (k < n && strcmp(op + 6, prefs_controls[k].name))
        k++;
      if (k == n)
        die("unknown Preferences control");
      phase = "Preferences";
      CALL1(prefs_controls[k].address, 0);
    } else if (sscanf(op, "select:%u", &a) == 1 && (a < 8 || a == 65535)) {
      put16(P(0x482bc9), (uint16_t)a);
    } else if (sscanf(op, "screen:%u", &a) == 1 && (a < 8 || a == 65535)) {
      put16(P(0x4758b8), (uint16_t)a);
    } else if (sscanf(op, "herc-type:%u", &a) == 1 && (a < 9 || a == 65535)) {
      put16(P(0x478780), (uint16_t)a);
    } else if (sscanf(op, "repair-pick:%u:%u", &a, &b) == 2 && a < 2 && b < 16) {
      phase = "REPAIR row";
      CALL2(0x434549, a, b);
    } else if (!strcmp(op, "repair-snapshot")) {
      phase = "REPAIR snapshot";
      CALL0(0x433f86);
    } else if (!strcmp(op, "repair-one")) {
      phase = "REPAIR";
      CALL1(0x4351bd, 0);
    } else if (!strcmp(op, "repair-all")) {
      phase = "REPAIR ALL";
      CALL1(0x4352e9, 0);
    } else if (!strcmp(op, "repair-cancel")) {
      phase = "REPAIR CANCEL";
      CALL1(0x435403, 0);
    } else if (sscanf(op, "build:%u", &a) == 1 && a < 9) {
      put16(P(0x478780), (uint16_t)a);
      phase = "Build";
      CALL1(0x4475ce, 0);
    } else if (!strcmp(op, "scrap")) {
      phase = "Herc scrap";
      CALL1(0x448326, 0);
    } else if (!strcmp(op, "editor-answers")) {
      editor_answer();
    } else if (!strcmp(op, "editor-state")) {
      editor_state();
    } else if (!strcmp(op, "emit")) {
      editor_emit();
    /* Slot statistics (stats.cpp): stats = the start-up read 0x4309f0
     * of every flagged slot 0..10 into 0x48cebc + 0x43*slot; stats-refresh:K
     * = 0x430ec4(K); stats-dump appends the eleven records (11 * 0x43 bytes)
     * to the memory-disk file stats.emit. */
    } else if (!strcmp(op, "stats")) {
      phase = "statistics read";
      CALL0(0x4309f0);
    } else if (sscanf(op, "stats-refresh:%u", &a) == 1 && a < 11) {
      phase = "statistics refresh";
      CALL1(0x430ec4, a);
    } else if (!strcmp(op, "stats-dump")) {
      vfile *f = vdisk_find("stats.emit", 1);
      if (shell_buffer_write(&f->data, f->data.size, P(0x48cebc), 11 * 0x43) < 0)
        die("memory disk statistics");
    } else if (!strcmp(op, "practice-show")) { /* practice screen */
      practice_widgets();
      phase = "PRACTICE MISSIONS";
      CALL1(0x431f3a, 0);
    } else if (sscanf(op, "practice-entry:%u", &a) == 1 && a < 8) {
      practice_widgets();
      phase = "practice entry";
      CALL1(practice_entries[a], 0);
    } else if (sscanf(op, "practice-control:%u:%u", &a, &b) == 2 && a < 5 && (b == 1 || b == 3)) {
      practice_widgets();
      phase = "practice control";
      /* the text button 0x409d8a ignores a disabled button (+0x49 = 0); of
       * the five only Herc Type [0x48dc1c] is ever disabled (0x44c40c) */
      if (a != 3 || get32(P(get32(P(0x48dc1c))) + 0x49))
        CALL1(practice_controls[a], practice_event((BYTE)b));
    } else if (!strcmp(op, "practice-state")) {
      practice_widgets();
      practice_state();
    } else if (!strcmp(op, "practice-menu")) {
      practice_widgets();
      phase = "practice Main Menu";
      CALL1(0x44c96a, 0);
    } else if (!strcmp(op, "practice-begin")) {
      practice_widgets();
      phase = "Begin Mission";
      CALL1(0x44ca26, 0);
    } else if (!strcmp(op, "instant-action")) {
      practice_widgets();
      phase = "INSTANT ACTION";
      CALL1(0x431935, 0);
    } else if (!strcmp(op, "use-default")) {
      practice_widgets();
      phase = "DEBUG Use Default";
      CALL1(0x44dbea, 0);
    } else if (!strncmp(op, "debug-accept:", 13) && strlen(op + 13) < 80) {
      practice_widgets();
      strcpy((char *)P(get32(P(0x48dcf8))) + 0x45, op + 13);
      phase = "DEBUG ACCEPT";
      CALL1(0x44dc4d, 0);
    } else if (!strcmp(op, "view-demo")) {
      phase = "VIEW DEMO";
      CALL1(0x431bfe, 0);
    } else if (!strcmp(op, "practice-reset")) {
      put16(P(0x479c54), 0xffff);
      put32(P(0x473784), 0);
    } else if (!strcmp(op, "practice-emit")) {
      practice_emit();
    /* Mission-screen texts (vshell_ui.md section 16). Each op appends
     * its texts to the memory-disk file texts.emit as [u32 size][bytes]
     * (size 0xffffffff: null pointer):
     *   briefing        0x4135bb on the memory state and data\mission.str;
     *                   three texts [0x46fc5c], [0x46fc60], [0x46fc64]
     *   briefing:N      the same after record N of the memory-disk file
     *                   texts.in ([u32 n][n bytes MISSION.STR][148 bytes
     *                   selection], repeated) became data\mission.str and
     *                   0x4840cc..0x48419f
     *   campaign:N      0x40fd2d(N) (<lang>\campaign.str from the VFS); one
     *                   text [0x46f520]. Its path buffer [ebp-0x15c] is not
     *                   initialised before the strcat calls, so the op clears
     *                   the stack below its own frame first.
     *   debrief-stats:O:T:L:S  0x40f904 with the outcome word O, tally T,
     *                   lost word L and recovered word S as the results path
     *                   pushes them (0x40f510..0x40f51e), on the memory state;
     *                   the ten value labels are stand-in objects with bit
     *                   0x02 of +0x11 set (no draw, 0x40b9fb); ten texts
     *                   (+0x4d) in call order. */
    } else if (!strcmp(op, "briefing") || sscanf(op, "briefing:%u", &a) == 1 ||
               sscanf(op, "campaign:%u", &b) == 1 || !strncmp(op, "debrief-stats:", 14)) {
      static const uint32_t widgets[10] = {0x48d91c, 0x48d924, 0x48d92c, 0x48d94c, 0x48d958,
                                           0x48d950, 0x48d95c, 0x48d954, 0x48d960, 0x48d964};
      static BYTE labels[10][0xc9];
      uint32_t texts[10];
      unsigned count = 0;
      vfile *f;
      if (op[0] == 'b') {
        if (op[8] == ':') {
          vfile *in = vdisk_find("texts.in", 0);
          size_t at = 0;
          uint32_t n = 0;
          if (!in)
            die("briefing: texts.in missing");
          for (unsigned k = 0;; k++) {
            if (in->data.size - at < 4)
              die("briefing: no such record");
            n = get32(in->data.data + at);
            if (in->data.size - at - 4 < (size_t)n + 148)
              die("briefing: truncated record");
            if (k == a)
              break;
            at += 4 + (size_t)n + 148;
          }
          vdisk_add("data\\mission.str", (es2_span){in->data.data + at + 4, n});
          memcpy(P(0x4840cc), in->data.data + at + 4 + n, 148);
        }
        phase = "briefing texts";
        CALL0(0x4135bb);
        texts[0] = get32(P(0x46fc5c));
        texts[1] = get32(P(0x46fc60));
        texts[2] = get32(P(0x46fc64));
        count = 3;
      } else if (op[0] == 'c') {
        uintptr_t sp;
        if (b > 65535)
          die("campaign: index above 65535");
        __asm__ volatile("mov %%esp, %0" : "=r"(sp));
        for (volatile BYTE *z = (BYTE *)(sp - 4096); z < (BYTE *)(sp - 64); z++)
          *z = 0;
        phase = "campaign text";
        CALL1(0x40fd2d, b);
        texts[0] = get32(P(0x46f520));
        count = 1;
      } else {
        unsigned outcome, lost, recovered, tally;
        if (sscanf(op + 14, "%u:%u:%u:%u", &outcome, &tally, &lost, &recovered) != 4 ||
            outcome > 65535 || lost > 65535 || recovered > 65535)
          die("debrief-stats:O:T:L:S");
        memset(labels, 0, sizeof(labels));
        for (unsigned k = 0; k < 10; k++) {
          labels[k][0x11] = 0x02;
          put32(P(widgets[k]), (uint32_t)(uintptr_t)labels[k]);
        }
        phase = "debrief statistics";
        {
          /* The labels' setText 0x40b8bb must run for real here; the practice-screen
           * entries trap it quietly in every slot-mode run, so its
           * first byte is restored for this call and the trap put back. */
          BYTE first = 0;
          for (unsigned k = 0; ui_armed && k < ui_armed_count; k++)
            if (ui_armed_list[k].address == 0x40b8bb && *P(0x40b8bb) == 0xcc)
              first = ui_armed_list[k].first;
          if (first)
            s17_patch(0x40b8bb, first);
          CALL3(0x40f904, outcome | (tally & 0xffff) << 16, tally >> 16 | lost << 16, recovered);
          if (first)
            s17_patch(0x40b8bb, 0xcc);
        }
        for (unsigned k = 0; k < 10; k++) {
          texts[k] = (uint32_t)(uintptr_t)(labels[k] + 0x4d);
          put32(P(widgets[k]), 0);
        }
        count = 10;
      }
      f = vdisk_find("texts.emit", 1);
      for (unsigned k = 0; k < count; k++) {
        BYTE size[4];
        const BYTE *s = (const BYTE *)(uintptr_t)texts[k];
        uint32_t n = s ? (uint32_t)strlen((const char *)s) : 0xffffffffu;
        put32(size, n);
        if (shell_buffer_write(&f->data, f->data.size, size, 4) < 0 ||
            (s && n && shell_buffer_write(&f->data, f->data.size, s, n) < 0))
          die("memory disk texts");
      }
    /* Text drawing (s27_text_draw above). Before the s17_op branch:
     * s17_op arms its traps for every operation it tries. */
    } else if (!strcmp(op, "text-draw")) {
      s27_text_draw();
    /* Widget draws (s28_widget_draw above), also before s17_op */
    } else if (!strcmp(op, "widget-draw")) {
      s28_widget_draw();
    /* The help routine 0x431e79 (before s17_op as well). `help` or
     * `help:ROOT`: ROOT is stored as the CD root 0x4823b9 first (drive.cfg's
     * first token plus a backslash in the game). WinHelpA's wrapper 0x407953
     * is trapped; the path it was handed is added to the memory disk as
     * `help.path` (one NUL-terminated string per call, appended), the prefs.cfg write
     * lands on the disk as for Preferences. data\language.cfg is a --file. */
    } else if (!strcmp(op, "help") || !strncmp(op, "help:", 5)) {
      if (op[4] == ':') {
        if (strlen(op + 5) > 100)
          die("help: CD root too long");
        strcpy((char *)P(0x4823b9), op + 5);
      }
      unsigned first = ui_call_count;
      phase = "online manual";
      CALL0(0x431e79);
      static char help_line[1024];
      unsigned help_at = 0;
      for (unsigned k = first; k < ui_call_count; k++)
        if (ui_calls[k][0] == 0x407953) {
          /* the path is a local of the finished frame: copied at once, before
           * any call can reuse that stack */
          const char *src = (const char *)(uintptr_t)ui_calls[k][1];
          while (help_at + 1 < sizeof(help_line) && *src)
            help_line[help_at++] = *src++;
          help_line[help_at++] = 0;
        }
      vfile *hf = vdisk_find("help.path", 1);
      if (help_at && shell_buffer_write(&hf->data, hf->data.size, help_line, help_at) < 0)
        die("memory disk help path");
    /* The mission map's primitives (s40_raster above), before s17_op */
    } else if (!strcmp(op, "map-raster")) {
      s40_raster();
    } else if (s40_op(op)) {
    /* The squad panel's condition 0x4121f8 (s47_condition above) */
    } else if (!strcmp(op, "squad-condition")) {
      s47_condition();
    /* CREW, ARMORY, weapons scrap (s17_op above) */
    } else if (s17_op(op)) {
    /* Blitter and decompressor (s26_op above) */
    } else if (s26_op(op)) {
    /* Movie queue and player (s15_op above) */
    } else if (s15_op(op)) {
    } else
      die("unknown --op");
    fprintf(stderr, "OP %s\n", op);
  }
  if (o->emit) {
    vdisk_mode = 0;
    emit_save();
    vdisk_mode = 1;
  }
  trace_finish();
  static char metadata[65000];
  char after[12 * 200], rng_output[229], prefs_now[109], prefs_committed[109];
  slot_table(after, sizeof(after));
  hex_text(P(0x482409), 114, rng_output);
  hex_text(P(0x48259c), 54, prefs_now);
  hex_text(P(0x4825d2), 54, prefs_committed);
  int length = snprintf(
      metadata, sizeof(metadata),
      "{\"prefs\":\"%s\",\"prefs_committed\":\"%s\",\"exit_code\":%d,\"quit\":%u,"
      "\"slots\":1,\"loaded\":%u,\"mode\":%d,\"final_word\":%d,\"total\":%u,"
      "\"position\":[%d,%d],\"output_rng_state\":\"%s\",\"table_start\":[%s],"
      "\"table_end\":[%s],\"emitted_save\":%d,\"ui_calls\":[",
      prefs_now, prefs_committed, (int32_t)get32(P(0x46e348)), *P(0x46c268),
      *P(0x4826ee), (int16_t)get16(P(0x4826f0)), (int16_t)get16(P(0x4826f2)),
      get32(P(0x482bd8)), (int16_t)get16(P(0x46fc58)), (int16_t)get16(P(0x46fc5a)),
      rng_output, before, after, o->emit);
  for (unsigned i = 0; i < ui_call_count && length > 0; i++)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length,
                       "%s[%u,%u,%u,%u]", i ? "," : "", ui_calls[i][0],
                       ui_calls[i][1], ui_calls[i][2], ui_calls[i][3]);
  if (length > 0 && (size_t)length < sizeof(metadata))
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "],\"arm\":[");
  for (unsigned i = 0; i < arm_count && length > 0 && (size_t)length < sizeof(metadata); i++)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length,
                       "%s[%u,%u,%u,%u]", i ? "," : "", arm_answers[i][0],
                       arm_answers[i][1], arm_answers[i][2], arm_answers[i][3]);
  if (length > 0 && (size_t)length < sizeof(metadata))
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "],\"checks\":[");
  for (unsigned i = 0; i < check_count && length > 0 && (size_t)length < sizeof(metadata); i++)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length,
                       "%s[%d,%d,%d,%d]", i ? "," : "", check_answers[i][0],
                       check_answers[i][1], check_answers[i][2], check_answers[i][3]);
  /* REPAIR/BUILD editor answers and states */
  if (length > 0 && (size_t)length < sizeof(metadata))
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "],\"editor_answers\":[");
  for (unsigned i = 0; i < editor_answer_count && length > 0 && (size_t)length < sizeof(metadata); i++) {
    const int *x = editor_answers[i];
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length,
                       "%s[%d,%u,%d,%d,%u,%d,%d,%d,%d,%d,%d,%d]", i ? "," : "", x[0], (unsigned)x[1],
                       x[2], x[3], (unsigned)x[4], x[5], x[6], x[7], x[8], x[9], x[10], x[11]);
  }
  if (length > 0 && (size_t)length < sizeof(metadata))
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "],\"editor\":[");
  for (unsigned i = 0; i < editor_state_count && length > 0 && (size_t)length < sizeof(metadata); i++)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "%s%s", i ? "," : "",
                       editor_states[i]);
  /* Practice states */
  if (length > 0 && (size_t)length < sizeof(metadata))
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "],\"practice\":[");
  for (unsigned i = 0; i < practice_state_count && length > 0 && (size_t)length < sizeof(metadata); i++)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "%s[%d,%d,%d]", i ? "," : "",
                       practice_states[i][0], practice_states[i][1], practice_states[i][2]);
  /* CREW/ARMORY states */
  if (length > 0 && (size_t)length < sizeof(metadata))
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "],\"s17\":[");
  for (unsigned i = 0; i < s17_state_count && length > 0 && (size_t)length < sizeof(metadata); i++)
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "%s%s", i ? "," : "",
                       s17_states[i]);
  if (length > 0 && (size_t)length < sizeof(metadata))
    length += snprintf(metadata + length, sizeof(metadata) - (size_t)length, "]}");
  if (length < 0 || (size_t)length >= sizeof(metadata))
    die("metadata size");
  if (o->pipe) {
    _setmode(_fileno(stdout), _O_BINARY);
    fwrite("ES2D", 1, 4, stdout);
    put_block(metadata, (size_t)length);
    write_word(vfile_count);
    for (unsigned i = 0; i < vfile_count; i++) {
      put_block(vfiles[i].name, strlen(vfiles[i].name));
      put_block(vfiles[i].data.data, vfiles[i].data.size);
    }
    put_block(outputs[4].data, o->emit ? outputs[4].size : 0);
    if (ferror(stdout))
      die("pipe output");
  } else {
    puts(metadata);
    for (unsigned i = 0; i < vfile_count; i++) {
      char h[65];
      digest((es2_span){vfiles[i].data.data, vfiles[i].data.size}, h);
      printf("DISK %s %zu %s\n", vfiles[i].name, vfiles[i].data.size, h);
    }
    if (o->emit) {
      char h[65];
      digest((es2_span){outputs[4].data, outputs[4].size}, h);
      printf("OUTPUT output.sav %zu %s\n", outputs[4].size, h);
    }
  }
  fflush(stdout);
  return 0;
}
static int execute(int argc, char **argv) {
  options o;
  if (!parse_options(argc, argv, &o))
    return 0;
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  AddVectoredExceptionHandler(1, fault);
  shell_save state = {0};
  if (o.save) {
    save_bytes = disk_read(o.save);
    es2_error error;
    if (shell_save_check(save_bytes, &state, &error) != ES2_OK) {
      fprintf(stderr, "SAV rejected at %zu: %s\n", error.offset,
              es2_status_string(error.code));
      return 1;
    }
  }
  if (load_image(o.root))
    return 1;
  runtime_init();
  mount_cd(o.root);
  trace_enabled = getenv("ES2_SHELL_TRACE") != NULL;
  trace_edges = !trace_enabled || strcmp(getenv("ES2_SHELL_TRACE"), "exec") != 0;
  put32(P(0x48235e), o.language);
  phase = "catalogue load";
  put32(P(0x46ddf8), CALL1(0x408bb5, 0x46e014));
  phase = "HERC_INF load";
  CALL0(0x411e40);
  phase = "career load";
  CALL0(0x412f2a);
  if (o.results || o.results_batch)
    return run_results(&o);
  if (o.op_count)
    return run_slots(&o);
  if (o.list) {
    for (unsigned g = 0; g < get16(P(0x46fc50)); g++) {
      BYTE *group = P(get32(P(0x46fc54))) + 8 * g;
      for (unsigned j = 0; j < get16(group + 2); j++)
        printf("MISSION %u %u %s\n", g, j, selected_mission(g, j));
    }
    return 0;
  }
  for (;;) {
  es2_span raw={0},batch_save={0};int retain=0,bank_override=0;
  if(o.batch) {
    char line[256],mission_text[40],language[4];int g,p,m,d,pv,c1,c2,c3,c5,build,tv,pf,pi;
    if(!fgets(line,sizeof line,stdin))break;
    if(sscanf(line,"%39s %3s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",mission_text,language,&g,&p,&m,&d,&pv,&c1,&c2,&c3,&c5,&build,&tv,&pf,&pi,&retain,&bank_override)!=17)
      die("batch header");
    const char *expected[]={"ENG","FRE","GER"};
    if(strcmp(language,expected[o.language]) || g<0 || g>5 || p<0 || p>10 || (m!=0 && m!=1) || tv<0 || tv>8)
      die("batch controls");
    o.group=g;o.index=p;o.mode=m;o.difficulty=(uint16_t)d;
    o.training[0]=(BYTE)c1;o.training[1]=(BYTE)c2;o.training[2]=(BYTE)c3;o.training[3]=(BYTE)tv;o.training[4]=(BYTE)c5;
    o.preset_flag=(unsigned)pf;o.preset_index=pi;
    raw=read_hex();batch_save=read_hex();es2_span ignored=read_hex();free((void *)ignored.data);
    if(raw.size!=2174 || raw.data[2112]>=56 || raw.data[2113]>=56)die("batch state");
    for(unsigned i=0;i<30;i++){int s=(int16_t)get16(raw.data+2114+2*i);if(s < -1 || s>=1000)die("batch reset selector");}
    save_bytes=build==2?batch_save:(es2_span){0};
    memset(&state,0,sizeof state);
    if(save_bytes.size){es2_error error;if(shell_save_check(save_bytes,&state,&error)!=ES2_OK)die("batch SAV rejected");}
    if(!retain)memset(P(0x482bdc),0,2000);
  }
  /* Each preparation has fresh output sinks, matching the one-shot worker.
   * Retained state means game state, not stale tails of prior disk files. */
  for(unsigned i=0;i<4;i++)outputs[i].size=0;
  if (!retain && save_bytes.data)
    restore_save();
  else if(!retain) {
    phase = "default RNG";
    CALL2(0x465cbc, 0x482409, 0);
    phase = "default inventory";
    CALL0(0x4125e8);
    phase = "default roster";
    CALL0(0x410055);
    phase = "default player";
    CALL3(0x41072b, 0x482b5c, o.player_name ? o.player_name : "In vitro",
          o.player_rank);
    phase = "default hangar";
    CALL2(0x410b11, 0x482ba7, 1);
    put16(P(0x46fc58), 1);
    put16(P(0x46fc5a), 0);
  }
  if (o.group >= 0) {
    put16(P(0x46fc58), (uint16_t)o.group);
    put16(P(0x46fc5a), (uint16_t)o.index);
  }
  unsigned group = get16(P(0x46fc58)), index = get16(P(0x46fc5a));
  int mode = o.mode < 0 ? (group ? 1 : 0) : o.mode;
  put16(P(0x4826f0), (uint16_t)mode);
  put16(P(0x4826ea), (uint16_t)o.difficulty);
  memcpy(P(0x4825c1), o.training, 5);
  put32(P(0x473784),o.preset_flag);
  put16(P(0x479c54),(uint16_t)o.preset_index);
  if (o.vars) {
    es2_span bank = disk_read(o.vars);
    if (bank.size != 2000)
      die("variable bank must have 2000 bytes");
    memcpy(P(0x482bdc), bank.data, 2000);
    free((void *)bank.data);
  }
  char bank_hash[65], rng_hash[65];
  digest((es2_span){P(0x482bdc), 2000}, bank_hash);
  phase = "RNG initialize";
  CALL2(0x465cbc, 0x482409, o.seed);
  if(o.batch) {
    if(bank_override || !save_bytes.data)memcpy(P(0x482bdc),raw.data,2000);
    memcpy(P(0x482409),raw.data+2000,114);
    memcpy(P(0x48553e),raw.data+2114,60);
    digest((es2_span){P(0x482bdc),2000},bank_hash);
  }
  digest((es2_span){P(0x482409), 114}, rng_hash);
  /* The emitted SAV is the state handed to preparation: restored or default
   * state plus explicit position and bank, before mode-zero hangar clearing. */
  if (o.emit == 1)
    emit_save();
  if (!mode) {
    phase = "clear training hangar";
    CALL1(0x410aaf, 0x482ba7);
  }
  const char *mission = selected_mission(group, index);
  fprintf(stderr, "PREPARE %s mode=%d seed=%u\n", mission, mode, o.seed);
  phase = "mission preparation";
  CALL1(0x41cd61, mission);
  if (o.emit == 2)
    emit_save();
  trace_finish();
  for (unsigned i = 0; i < 4; i++)
    if (!outputs[i].size)
      die("missing output");
  char name[128];
  snprintf(name, sizeof(name), "%s", mission);
  for (char *p = name; *p; p++)
    if (*p == '\\')
      *p = '/';
  char metadata[2048],rng_output[229],reset_output[121];
  hex_text(P(0x482409),114,rng_output);hex_text(P(0x48553e),60,reset_output);
  int length = snprintf(
      metadata, sizeof(metadata),
      "{\"mission\":\"%s\",\"group\":%u,\"index\":%u,\"mode\":%d,\"seed\":%u,"
      "\"language\":%u,\"difficulty\":%u,\"training_options\":[%u,%u,%u,%u,%u],"
      "\"input_bank_sha256\":\"%s\",\"input_rng_sha256\":\"%s\",\"save_"
      "variable_offset\":%zu,\"save_consumed\":%zu,\"save_trailer\":%zu,"
      "\"output_rng_state\":\"%s\",\"output_reset_selectors\":\"%s\","
      "\"emitted_save\":%d}",
      name, group, index, mode, o.seed, o.language, o.difficulty, o.training[0],
      o.training[1], o.training[2], o.training[3], o.training[4], bank_hash,
      rng_hash, state.variable_offset, state.consumed, state.trailer_size,rng_output,reset_output,o.emit);
  if (length < 0 || (size_t)length >= sizeof(metadata))
    die("metadata size");
  if (o.pipe) {
    _setmode(_fileno(stdout), _O_BINARY);
    fwrite("ES2P", 1, 4, stdout);
    write_word((unsigned)length);
    fwrite(metadata, 1, (size_t)length, stdout);
    for (unsigned i = 0; i < (o.emit ? 5u : 4u); i++) {
      write_word((unsigned)outputs[i].size);
      fwrite(outputs[i].data, 1, outputs[i].size, stdout);
    }
    if (ferror(stdout))
      die("pipe output");
  } else {
    puts(metadata);
    for (int i = 0; i < (o.emit ? 5 : 4); i++) {
      char h[65];
      digest((es2_span){outputs[i].data, outputs[i].size}, h);
      printf("OUTPUT %s %zu %s\n", out_names[i], outputs[i].size, h);
    }
  }
  fflush(stdout);
  free((void *)raw.data);free((void *)batch_save.data);
  if(!o.batch)break;
  }
  return 0;
}
typedef struct arguments {
  int argc;
  char **argv;
} arguments;
static DWORD WINAPI worker(void *p) {
  arguments *a = p;
  return (DWORD)execute(a->argc, a->argv);
}
int main(int argc, char **argv) {
  if (!reserved)
    return fail("preferred image range unavailable");
  arguments a = {argc, argv};
  HANDLE h = CreateThread(NULL, 0x1000000, worker, &a,
                          STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
  if (!h)
    return fail("worker");
  DWORD r = 1;
  WaitForSingleObject(h, INFINITE);
  GetExitCodeThread(h, &r);
  CloseHandle(h);
  return (int)r;
}
