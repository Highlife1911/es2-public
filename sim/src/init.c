/* The c0w32 entry, the 65 init records and the 26 exit records of DBSIM as own
 * code (es2/sim/init.h). Contract: own_process.md, "static initialisation, the
 * 65 init records and the 26 exit records" (every store of every record,
 * static) and "the static initialisation as own code" (confirmed in vitro: the image, the guest stack words and
 * the heap block after the entry, after every record and after the exit records
 * equal the original's,
 * //tools/reference-game/invitro:differential_init_records_test).
 *
 * Every store goes through the guest memory view in the order the original
 * makes it; "counter +n" is the construction counter [[0x6cbee4]] (the
 * dword at StackBase-4) as the original's constructors and destructors move
 * it. Constructors whose bodies store nothing (the array element
 * constructors of records 0, 2, 19, 21, 27, 28, 33, 52, 56, 59, 63, the
 * matrix_ctor calls of records 2 and 63) are not called; their effect on the
 * counter is counted where they have one. */
#include "es2/sim/init.h"
#include "es2/core/rtl_start.h"

/* ---- guest access */
static uint32_t R32(const es2_sim_init *c, uint32_t a) { return c->memory->read32(c->memory->context, a); }
static uint16_t R16(const es2_sim_init *c, uint32_t a) { return c->memory->read16(c->memory->context, a); }
static uint8_t R8(const es2_sim_init *c, uint32_t a) {
    const uint8_t *p = c->memory->host(c->memory->context, a);
    return p ? *p : 0u;
}
static void W32(const es2_sim_init *c, uint32_t a, uint32_t v) { c->memory->write32(c->memory->context, a, v); }
static void W16(const es2_sim_init *c, uint32_t a, uint16_t v) { c->memory->write16(c->memory->context, a, v); }
static void W8(const es2_sim_init *c, uint32_t a, uint8_t v) {
    uint8_t *p = c->memory->host(c->memory->context, a);
    if (p) *p = v;
}
/* The construction counter: [[0x6cbee4]] += n (a negative n lowers it). */
static void counter(const es2_sim_init *c, int32_t n) {
    uint32_t p = R32(c, 0x006cbee4u);
    W32(c, p, R32(c, p) + (uint32_t)n);
}
/* A call the original makes to other code, answered by the dispatch. */
static int call(es2_sim_init *c, uint32_t va, const uint32_t *args, unsigned count, uint32_t *eax) {
    static const uint32_t none[1] = {0};
    uint32_t regs[3] = {0, 0, 0};
    es2_sim_result r;
    if (es2_sim_call(c->dispatch, va, count ? args : none, count, regs, &r)) return 1;
    if (eax) *eax = r.eax;
    return 0;
}

/* Thunks and callees the records reach (DBSIM.EXE). */
#define VA_NEW 0x0047749cu              /* operator new */
#define VA_DELETE 0x00477548u           /* operator delete */
#define VA_DELETE_ARRAY 0x0047758cu     /* operator delete[] */
#define VA_MEMSET 0x0049681au           /* jmp [_memset] */
#define VA_MEMMOVE 0x00496814u          /* jmp [_memmove] */
#define VA_CREATE_FILE 0x004968feu      /* jmp [CreateFileA] */
#define VA_CLOSE_HANDLE 0x004968b0u     /* jmp [CloseHandle] */
#define VA_MODULE_HANDLE 0x00496898u    /* jmp [GetModuleHandleA] */

/* ---- shared constructor and destructor bodies */

/* 0x401d64(f, s): entry [count] of the stage registry, count + 1. */
static void stage(const es2_sim_init *c, uint16_t s, uint32_t f) {
    int16_t index = (int16_t)R16(c, ES2_SIM_VA_STAGE_COUNT);
    W16(c, ES2_SIM_VA_STAGE_COUNT, (uint16_t)(index + 1));
    uint32_t e = ES2_SIM_VA_STAGE_TABLE + (uint32_t)(6 * (int32_t)index);
    W32(c, e, f);
    W16(c, e + 4u, s);
}
/* 0x48156c(o): the GLBitmap constructor. */
static void bitmap_ctor(const es2_sim_init *c, uint32_t o) {
    W32(c, o, 0x004a69f0u);
    W32(c, o, 0x004a3368u);
    counter(c, 1);
    W32(c, o, 0x004a3040u);
    counter(c, 1);
    W32(c, o, 0x004a306cu);
    W32(c, o + 0xau, 0);
    W16(c, o + 0x14u, 0);
    W32(c, o + 0x16u, 0);
    W32(c, o + 0xeu, 0);
    counter(c, 1);
}
/* The inline GLRectangleRegion constructor of records 28 and 37. */
static void region_ctor(const es2_sim_init *c, uint32_t o) {
    W32(c, o, 0x004a69f0u);
    W32(c, o, 0x004a3368u);
    counter(c, 1);
    W32(c, o, 0x004a3390u);
    counter(c, 1);
    W32(c, o, 0x004a3264u);
    W32(c, o + 4u, 0);
    counter(c, 1);
}
/* 0x453824(o): five zero stores, counter +1. */
static void zero16(const es2_sim_init *c, uint32_t o) {
    W32(c, o + 4u, 0);
    W32(c, o, 0);
    W32(c, o + 8u, 0);
    W16(c, o + 0xeu, 0);
    W16(c, o + 0xcu, 0);
    counter(c, 1);
}
/* 0x453848(o, flags): counter -1; delete[] [o] when nonzero; delete o when
 * flags & 1. */
static int zero16_dtor(es2_sim_init *c, uint32_t o, uint32_t flags) {
    counter(c, -1);
    if (!o) return 0;
    uint32_t buffer = R32(c, o);
    if (buffer && call(c, VA_DELETE_ARRAY, &buffer, 1, NULL)) return 1;
    if ((flags & 1u) && call(c, VA_DELETE, &o, 1, NULL)) return 1;
    return 0;
}
/* 0x4815b8(o, flags): the GLBitmap destructor and its base 0x4838fd. */
static int bitmap_dtor(es2_sim_init *c, uint32_t o, uint32_t flags) {
    counter(c, -1);
    if (!o) return 0;
    W32(c, o, 0x004a306cu);
    if (R16(c, o + 0x14u)) {
        uint32_t p = R32(c, o + 0x16u);
        if (call(c, VA_DELETE_ARRAY, &p, 1, NULL)) return 1;
    }
    if (R32(c, o + 0xau)) {
        uint32_t p = R32(c, o + 0xeu);
        if (call(c, VA_DELETE_ARRAY, &p, 1, NULL)) return 1;
    }
    counter(c, -1);
    /* 0x4838fd(o, 0) */
    counter(c, -1);
    W32(c, o, 0x004a3368u);
    if ((flags & 1u) && call(c, VA_DELETE, &o, 1, NULL)) return 1;
    return 0;
}
/* 0x48bcc4(o, flags): the light destructor. */
static int light_dtor(es2_sim_init *c, uint32_t o, uint32_t flags) {
    counter(c, -1);
    if (!o) return 0;
    W32(c, o, 0x004a5b39u);
    if ((flags & 1u) && call(c, VA_DELETE, &o, 1, NULL)) return 1;
    return 0;
}
/* A pool pointer of an exit record: delete p (records 0 and 7 call it
 * unconditionally, the others only when nonzero). */
static int delete_pointer(es2_sim_init *c, uint32_t at, int always) {
    uint32_t p = R32(c, at);
    if (!always && !p) return 0;
    return call(c, VA_DELETE, &p, 1, NULL);
}

/* ---- the init records (record number = table index) */

static int i00(es2_sim_init *c) { (void)c; return 0; }   /* 0x401d30: arrays with empty elements */
static int i01(es2_sim_init *c) { stage(c, 6, 0x00401e5cu); return 0; }
static int i02(es2_sim_init *c) { (void)c; return 0; }   /* 0x402bb8: arrays (matrix_ctor), no store */
/* Records 3..10: stage registrations, a pool pointer zeroed, counter +1. */
static int i03(es2_sim_init *c) { stage(c, 2, 0x00405facu); stage(c, 5, 0x00405ea4u); W32(c, 0x004a95a0u, 0); counter(c, 1); return 0; }
static int i04(es2_sim_init *c) { stage(c, 2, 0x004076e4u); stage(c, 5, 0x004077e8u); W32(c, 0x004a95feu, 0); counter(c, 1); return 0; }
static int i05(es2_sim_init *c) { stage(c, 2, 0x00407b54u); stage(c, 5, 0x00407b3cu); W32(c, 0x004a961eu, 0); counter(c, 1); return 0; }
static int i06(es2_sim_init *c) { stage(c, 2, 0x00408860u); stage(c, 5, 0x004083e0u); W32(c, 0x004a9642u, 0); counter(c, 1); return 0; }
static int i07(es2_sim_init *c) {
    stage(c, 2, 0x004090ccu); stage(c, 5, 0x00408f9cu);
    W32(c, 0x004a966eu, 0); counter(c, 1);
    W32(c, 0x004a9684u, 0); counter(c, 1);
    return 0;
}
static int i08(es2_sim_init *c) { stage(c, 5, 0x00409904u); W32(c, 0x004a968du, 0); counter(c, 1); return 0; }
static int i09(es2_sim_init *c) { stage(c, 2, 0x00409a34u); stage(c, 5, 0x00409a1cu); W32(c, 0x004a96aau, 0); counter(c, 1); return 0; }
static int i10(es2_sim_init *c) { stage(c, 2, 0x0040a060u); stage(c, 5, 0x0040a108u); W32(c, 0x004a96c2u, 0); counter(c, 1); return 0; }
static int i11(es2_sim_init *c) { stage(c, 2, 0x0040a818u); return 0; }
static int i12(es2_sim_init *c) { stage(c, 2, 0x0040ac38u); return 0; }
static int i13(es2_sim_init *c) { stage(c, 2, 0x0040ade0u); return 0; }
static int i14(es2_sim_init *c) { stage(c, 2, 0x0040b6e0u); return 0; }
static int i15(es2_sim_init *c) { (void)c; return 0; }
static int i16(es2_sim_init *c) { stage(c, 2, 0x0040fc8cu); return 0; }

/* Records 17 and 18: the AI state records. 0x415028(dst, &w) spreads the
 * two bytes of w into 16 bytes 0/1, bit 0 first. */
static void bits16(const es2_sim_init *c, uint32_t dst, uint16_t w) {
    for (unsigned k = 0; k < 2; ++k)
        for (unsigned i = 0; i < 8; ++i) W8(c, dst + 8u * k + i, (uint8_t)((w >> (8u * k + i)) & 1u));
}
typedef struct state_row { uint8_t name; uint16_t value; uint8_t bits; uint8_t tail; } state_row;
static void state_record(const es2_sim_init *c, uint32_t at, uint32_t names, uint32_t constants, const state_row *r, int mech) {
    W32(c, at, names + r->name);
    W32(c, at + 4u, r->value);
    bits16(c, at + 8u, r->bits);
    for (uint32_t k = 0; k < 9u; ++k) W32(c, at + 0x18u + 4u * k, R32(c, constants + 4u * k));
    if (mech) W16(c, at + 0x3cu, r->tail);
}
static const state_row mech_states[22] = {
    {0x00, 0, 0x00, 3}, {0x09, 10, 0x01, 3}, {0x10, 10, 0x01, 3}, {0x1b, 50000, 0x02, 0},
    {0x25, 50000, 0x02, 0}, {0x2e, 50000, 0x02, 0}, {0x39, 50000, 0x02, 0}, {0x48, 50000, 0x02, 0},
    {0x58, 10, 0x01, 3}, {0x63, 10, 0x01, 3}, {0x6e, 10, 0x01, 3}, {0x78, 10, 0x09, 3},
    {0x87, 10, 0x01, 3}, {0x96, 10, 0x09, 3}, {0x9f, 10, 0x03, 0}, {0xa8, 10, 0x05, 3},
    {0xb1, 50000, 0x06, 0}, {0xc0, 10, 0x09, 0}, {0xc8, 15000, 0x12, 5}, {0xd0, 10, 0x21, 6},
    {0xd9, 0, 0x21, 6}, {0xde, 0, 0x21, 7},
};
static const state_row flyer_states[7] = {
    {0x00, 0, 0, 0}, {0x09, 50000, 1, 0}, {0x13, 5000, 0, 0}, {0x1e, 5000, 0, 0},
    {0x31, 500, 1, 0}, {0x3a, 5000, 0, 0}, {0x43, 500, 1, 0},
};
static int i17(es2_sim_init *c) {
    for (uint32_t k = 0; k < 22u; ++k)
        state_record(c, 0x004993a4u + 0x3eu * k, 0x00499c10u, 0x004998f8u + 0x24u * k, &mech_states[k], 1);
    return 0;
}
static int i18(es2_sim_init *c) {
    for (uint32_t k = 0; k < 7u; ++k)
        state_record(c, 0x00499cf8u + 0x3cu * k, 0x00499f98u, 0x00499e9cu + 0x24u * k, &flyer_states[k], 0);
    return 0;
}
static int i19(es2_sim_init *c) { stage(c, 2, 0x00415464u); stage(c, 5, 0x0041544cu); W32(c, 0x004a9b7au, 0); counter(c, 1); return 0; }
static int i20(es2_sim_init *c) { (void)c; return 0; }
static int i21(es2_sim_init *c) { stage(c, 2, 0x0041fdb0u); return 0; }
static int i22(es2_sim_init *c) { stage(c, 5, 0x004215a8u); W32(c, 0x004a9db9u, 0); counter(c, 1); return 0; }
static int i23(es2_sim_init *c) { stage(c, 2, 0x00422d90u); return 0; }
static int i24(es2_sim_init *c) { zero16(c, 0x004aa650u); return 0; }
/* 0x47a0b8(table, n): class-registry pair [count] = {table, n}, count + 1. */
static int i25(es2_sim_init *c) {
    uint32_t index = R32(c, 0x004a1690u);
    W32(c, 0x006c5838u + 8u * index, 1);
    index = R32(c, 0x004a1690u);
    W32(c, 0x006c5834u + 8u * index, 0x0049aa7cu);
    W32(c, 0x004a1690u, R32(c, 0x004a1690u) + 1u);
    return 0;
}
static int i26(es2_sim_init *c) { (void)c; return 0; }
static int i27(es2_sim_init *c) { stage(c, 2, 0x00428230u); return 0; }
static int i28(es2_sim_init *c) {
    region_ctor(c, 0x004cfa7au);
    region_ctor(c, 0x004cfc9cu);
    /* The rectangle as min/max of the points (0x140, 0xd2) and (0, 0xf0). */
    W32(c, 0x0049ac54u, 0);
    W32(c, 0x0049ac5cu, 0x140u);
    W32(c, 0x0049ac58u, 0xd2u);
    W32(c, 0x0049ac60u, 0xf0u);
    return 0;
}
static int i29(es2_sim_init *c) { stage(c, 2, 0x0043034cu); return 0; }
static int i30(es2_sim_init *c) {
    W32(c, 0x004d03acu, 0);
    W32(c, 0x004d03b4u, 4);
    W32(c, 0x004d03bcu, 0);
    W32(c, 0x0049b06eu, 0);
    W32(c, 0x0049b072u, 1000);
    W32(c, 0x0049b076u, 0);
    W32(c, 0x0049b07au, (uint32_t)-1000);
    W32(c, 0x0049b07eu, 0);
    W32(c, 0x0049b082u, 0);
    return 0;
}
static int i31(es2_sim_init *c) {
    for (uint32_t k = 0; k < 3u; ++k) zero16(c, 0x004d0414u + 16u * k);
    zero16(c, 0x004d0444u);
    zero16(c, 0x004d0454u);
    return 0;
}
static int i32(es2_sim_init *c) { zero16(c, 0x004d1298u); return 0; }
static int i33(es2_sim_init *c) { (void)c; return 0; }
static int i34(es2_sim_init *c) { (void)c; return 0; }
static int i35(es2_sim_init *c) { (void)c; return 0; }
static int i36(es2_sim_init *c) { (void)c; return 0; }
static int i37(es2_sim_init *c) { region_ctor(c, 0x004d1a2cu); return 0; }
static int i38(es2_sim_init *c) {
    W16(c, 0x0049c454u, 0xc000u);
    W16(c, 0x0049c456u, 0);
    W16(c, 0x0049c458u, 0);
    return 0;
}
static int i39(es2_sim_init *c) { W32(c, 0x0049d240u, 0); W32(c, 0x0049d244u, 0); return 0; }
static int i40(es2_sim_init *c) {
    W16(c, 0x0049d6c0u, 0xc000u);
    W16(c, 0x0049d6c2u, 0);
    W16(c, 0x0049d6c4u, 0);
    bitmap_ctor(c, 0x004d1cf6u);
    return 0;
}
static int i41(es2_sim_init *c) { (void)c; return 0; }
static int i42(es2_sim_init *c) { bitmap_ctor(c, 0x004d1e38u); return 0; }
static int i43(es2_sim_init *c) { W32(c, 0x004d1e78u, 0); return 0; }
/* The key-direction pairs (input.md), 0x45b8a4..0x45b918. */
static int i44(es2_sim_init *c) {
    static const int8_t pairs[28] = {
        -1, -1, 0, -1, 1, -1, -1, 0, 1, 0, -1, 1, 0, 1, 1, 1,
        0, -1, -1, 0, 1, 0, 0, 1, 0, -1, 0, 1,
    };
    for (uint32_t i = 0; i < 28u; ++i) W8(c, 0x0049eb45u + i, (uint8_t)pairs[i]);
    return 0;
}
static int i45(es2_sim_init *c) { (void)c; return 0; }
/* 0x492e88(stream): the RNG indices 24/55, then the 112 seed bytes of
 * 0x4a6940 through _memmove; 0x492f48 stores nothing. */
static int rng_stream(es2_sim_init *c, uint32_t stream) {
    W8(c, stream + 0x70u, 24);
    W8(c, stream + 0x71u, 55);
    uint32_t args[3] = {stream, 0x004a6940u, 0x70u};
    return call(c, VA_MEMMOVE, args, 3, NULL);
}
/* The globals object 0x4d24bc (rng.md, launcher.md, display_modes.md). */
static int i46(es2_sim_init *c) {
    const uint32_t g = 0x004d24bcu;
    bitmap_ctor(c, g + 0xc3u);
    uint32_t args[3] = {g, 0, 0xc3u};
    if (call(c, VA_MEMSET, args, 3, NULL)) return 1;
    W32(c, g + 0x4au, 1);
    W8(c, g + 0x0cu, 1);
    W32(c, g + 0x0eu, 0);
    W16(c, g + 0x56u, 0xffffu);
    W16(c, g + 0x5au, 0xffffu);
    W16(c, g + 0x58u, 0xffffu);
    W16(c, g + 0x5cu, 1);
    W16(c, g + 0x32u, 2);
    W16(c, g + 0x5eu, 2);
    W8(c, g + 0x7au, 0x72);
    W8(c, g + 0x7cu, 0);
    W8(c, g + 0x7bu, 0);
    W16(c, g + 0xaau, 1);
    W32(c, g + 0x82u, 320);
    W32(c, g + 0x86u, 200);
    W32(c, g + 0x8au, 640);
    W32(c, g + 0x8eu, 400);
    W32(c, g + 0x92u, 320);
    W32(c, g + 0x96u, 400);
    W8(c, g + 0xb0u, 0);
    W8(c, g + 0x7du, 1);
    W32(c, g + 0xa2u, 1);
    W8(c, g + 0xb5u, 0);
    W8(c, g + 0xb6u, 0);
    W32(c, g + 0xbbu, 13);
    counter(c, 1);
    if (rng_stream(c, 0x004d2599u)) return 1;
    return rng_stream(c, 0x004d260bu);
}
static int i47(es2_sim_init *c) { (void)c; return 0; }
static int i48(es2_sim_init *c) { zero16(c, 0x004d2c68u); return 0; }
static int i49(es2_sim_init *c) { (void)c; return 0; }
static int i50(es2_sim_init *c) { (void)c; return 0; }
static int i51(es2_sim_init *c) { stage(c, 2, 0x00467b64u); return 0; }
static int i52(es2_sim_init *c) { (void)c; return 0; }
/* The one heap allocation: new(0x808); on success 0x469ddc(p, 0); the
 * pointer (also NULL) to 0x4a08b4. */
static int i53(es2_sim_init *c) {
    uint32_t n = 0x808u, p = 0;
    if (call(c, VA_NEW, &n, 1, &p)) return 1;
    if (p) {
        W32(c, p + 0x800u, 0);
        W32(c, p + 0x804u, 0);
        for (uint32_t k = 0; k < 0x100u; ++k) {
            W32(c, p + 8u * k, 0);
            W32(c, p + 8u * k + 4u, 0x100u);
        }
    }
    W32(c, ES2_SIM_VA_TABLE_808, p);
    return 0;
}
static int i54(es2_sim_init *c) { bitmap_ctor(c, 0x006b4f04u); return 0; }
static int i55(es2_sim_init *c) { stage(c, 2, 0x0046b1a8u); stage(c, 5, 0x0046b188u); W32(c, 0x006b4f30u, 0); counter(c, 1); return 0; }
static int i56(es2_sim_init *c) { (void)c; return 0; }
/* 52 elements of 0x110 bytes at 0x6b7b70 (0x476ccc): ten lights of 0x16
 * bytes at +0x34 (0x48bc40: the vtable dword, counter +1), counter +1. */
static int i57(es2_sim_init *c) {
    for (uint32_t i = 0; i < 52u; ++i) {
        uint32_t e = 0x006b7b70u + 0x110u * i;
        for (uint32_t j = 0; j < 10u; ++j) {
            W32(c, e + 0x34u + 0x16u * j, 0x004a5b39u);
            counter(c, 1);
        }
        counter(c, 1);
    }
    return 0;
}
/* 0x476dd8(0x6bb2f8): the arena header. */
static int i58(es2_sim_init *c) {
    W32(c, 0x006bb2f8u, 0);
    W32(c, 0x006bb300u, 0);
    W32(c, 0x006bb304u, 0);
    W32(c, 0x006bb2fcu, 0);
    return 0;
}
static int i59(es2_sim_init *c) { (void)c; return 0; }
static int i60(es2_sim_init *c) { (void)c; return 0; }
/* 0x479bf8(0x6c56c8): the log function with type 0 returns at once;
 * counter +1. */
static int i61(es2_sim_init *c) { counter(c, 1); return 0; }
static int i62(es2_sim_init *c) { (void)c; return 0; }
static int i63(es2_sim_init *c) {
    W32(c, 0x006c604cu, 0);
    W32(c, 0x006c6054u, 1);
    W32(c, 0x006c605cu, 0);
    W32(c, 0x006c6060u, 0);
    return 0;
}
/* 0x495500(0x6cbc50): CreateFileA("\\.\DARKMONO.VXD" at 0x4a6a60, 0, 0,
 * NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL) into 0x6cbc50, counter +1. */
static int i64(es2_sim_init *c) {
    uint32_t args[7] = {0x004a6a60u, 0, 0, 0, 0, 0x04000000u, 0};
    uint32_t handle = 0;
    if (call(c, VA_CREATE_FILE, args, 7, &handle)) return 1;
    W32(c, ES2_SIM_VA_VXD_HANDLE, handle);
    counter(c, 1);
    return 0;
}

/* ---- the exit records (rtl_imports.md "The 26 exit records of DBSIM") */

static int x00(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a95a0u, 1); }
static int x01(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a95feu, 0); }
static int x02(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a961eu, 0); }
static int x03(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a9642u, 0); }
static int x04(es2_sim_init *c) {
    counter(c, -1);
    if (delete_pointer(c, 0x004a9684u, 0)) return 1;
    counter(c, -1);
    return delete_pointer(c, 0x004a966eu, 0);
}
static int x05(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a968du, 0); }
static int x06(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a96aau, 0); }
static int x07(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a96c2u, 1); }
static int x08(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a9b7au, 0); }
static int x09(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x004a9db9u, 0); }
static int x10(es2_sim_init *c) { return zero16_dtor(c, 0x004aa650u, 2); }
static int x11(es2_sim_init *c) { counter(c, -6); return 0; }
/* 0x49656c(base, size, count, 1, dtor): the elements last first, each
 * dtor(element, 2). */
static int x12(es2_sim_init *c) {
    if (zero16_dtor(c, 0x004d0454u, 2) || zero16_dtor(c, 0x004d0444u, 2)) return 1;
    for (uint32_t k = 3; k-- > 0;)
        if (zero16_dtor(c, 0x004d0414u + 16u * k, 2)) return 1;
    return 0;
}
static int x13(es2_sim_init *c) { return zero16_dtor(c, 0x004d1298u, 2); }
static int x14(es2_sim_init *c) { counter(c, -3); return 0; }
static int x15(es2_sim_init *c) { if (R8(c, 0x0049cc26u)) counter(c, -3); return 0; }
static int x16(es2_sim_init *c) { return bitmap_dtor(c, 0x004d1cf6u, 2); }
static int x17(es2_sim_init *c) { return bitmap_dtor(c, 0x004d1e38u, 2); }
static int x18(es2_sim_init *c) { counter(c, -1); return bitmap_dtor(c, 0x004d257fu, 2); }
static int x19(es2_sim_init *c) {
    if (R8(c, 0x0049f1ecu)) {
        counter(c, -2);
        if (light_dtor(c, 0x004d2754u, 0)) return 1;
    }
    if (R8(c, 0x0049f032u)) return zero16_dtor(c, 0x004d26e0u, 2);
    return 0;
}
static int x20(es2_sim_init *c) { return zero16_dtor(c, 0x004d2c68u, 2); }
static int x21(es2_sim_init *c) { return bitmap_dtor(c, 0x006b4f04u, 2); }
static int x22(es2_sim_init *c) { counter(c, -1); return delete_pointer(c, 0x006b4f30u, 0); }
/* 0x476d26(element, 2) for the 52 elements, last first: counter -1, then
 * its ten lights last first. */
static int x23(es2_sim_init *c) {
    for (uint32_t i = 52; i-- > 0;) {
        uint32_t e = 0x006b7b70u + 0x110u * i;
        counter(c, -1);
        for (uint32_t j = 10; j-- > 0;)
            if (light_dtor(c, e + 0x34u + 0x16u * j, 2)) return 1;
    }
    return 0;
}
/* 0x479c20(0x6c56c8, 2): counter -1; the log function with type 1 returns
 * at once. */
static int x24(es2_sim_init *c) { counter(c, -1); return 0; }
/* 0x495530(0x6cbc50, 2): counter -1; CloseHandle unless the handle is -1. */
static int x25(es2_sim_init *c) {
    counter(c, -1);
    uint32_t handle = R32(c, ES2_SIM_VA_VXD_HANDLE);
    if (handle != 0xffffffffu && call(c, VA_CLOSE_HANDLE, &handle, 1, NULL)) return 1;
    return 0;
}

typedef struct record { uint32_t va; es2_sim_record_fn fn; } record;
static const record init_records[ES2_SIM_INIT_RECORDS] = {
    {0x00401d30u, i00}, {0x00401ef8u, i01}, {0x00402bb8u, i02}, {0x0040695cu, i03}, {0x004078c8u, i04},
    {0x00408284u, i05}, {0x00408e9cu, i06}, {0x0040948cu, i07}, {0x00409940u, i08}, {0x00409f78u, i09},
    {0x0040a14cu, i10}, {0x0040ab18u, i11}, {0x0040acccu, i12}, {0x0040b5d8u, i13}, {0x0040c2c0u, i14},
    {0x0040f780u, i15}, {0x00411110u, i16}, {0x00413ed4u, i17}, {0x00414c68u, i18}, {0x0041bc5cu, i19},
    {0x0041fdacu, i20}, {0x00420654u, i21}, {0x00421fb0u, i22}, {0x00423068u, i23}, {0x00426eacu, i24},
    {0x004277f0u, i25}, {0x00428194u, i26}, {0x00428de8u, i27}, {0x0042db40u, i28}, {0x00430e90u, i29},
    {0x00434a6cu, i30}, {0x00437270u, i31}, {0x00437898u, i32}, {0x0043874cu, i33}, {0x00438b1cu, i34},
    {0x0043b35cu, i35}, {0x0043e23cu, i36}, {0x00440444u, i37}, {0x00440818u, i38}, {0x0044c02cu, i39},
    {0x00450458u, i40}, {0x004531b4u, i41}, {0x00454178u, i42}, {0x004554c4u, i43}, {0x0045b8a4u, i44},
    {0x0045bda0u, i45}, {0x0045caf4u, i46}, {0x0045fba0u, i47}, {0x004635a0u, i48}, {0x00464ad4u, i49},
    {0x00467824u, i50}, {0x00467b8cu, i51}, {0x00469870u, i52}, {0x0046a3c0u, i53}, {0x0046a57cu, i54},
    {0x0046b860u, i55}, {0x0047149cu, i56}, {0x00476c70u, i57}, {0x004775d0u, i58}, {0x004785e8u, i59},
    {0x00478decu, i60}, {0x00479d4cu, i61}, {0x00480ad8u, i62}, {0x0048dd88u, i63}, {0x004956a4u, i64},
};
static const record exit_records[ES2_SIM_EXIT_RECORDS] = {
    {0x004069b0u, x00}, {0x0040791cu, x01}, {0x004082d8u, x02}, {0x00408ef0u, x03}, {0x004094f0u, x04},
    {0x00409984u, x05}, {0x00409fccu, x06}, {0x0040a1a0u, x07}, {0x0041bcc8u, x08}, {0x00421ff4u, x09},
    {0x00426edcu, x10}, {0x0042dc40u, x11}, {0x004372c4u, x12}, {0x004378c8u, x13}, {0x004404a4u, x14},
    {0x004476f0u, x15}, {0x004504a4u, x16}, {0x004541a8u, x17}, {0x0045cc0cu, x18}, {0x0045e460u, x19},
    {0x004635d0u, x20}, {0x0046a5acu, x21}, {0x0046b8b4u, x22}, {0x00476cb0u, x23}, {0x00479d7cu, x24},
    {0x004956d4u, x25},
};

es2_sim_record_fn es2_sim_record_function(uint32_t va, unsigned mode) {
    const record *t = mode ? exit_records : init_records;
    unsigned n = mode ? ES2_SIM_EXIT_RECORDS : ES2_SIM_INIT_RECORDS;
    for (unsigned i = 0; i < n; ++i)
        if (t[i].va == va) return t[i].fn;
    return 0;
}
uint32_t es2_sim_record_va(unsigned mode, unsigned index) {
    if (mode) return index < ES2_SIM_EXIT_RECORDS ? exit_records[index].va : 0u;
    return index < ES2_SIM_INIT_RECORDS ? init_records[index].va : 0u;
}

/* ---- the c0w32 entry 0x401000..0x40106e and its callee 0x496425(0) */
int es2_sim_c0w32_entry(es2_sim_init *c) {
    const uint32_t sb = c->stack_base;
    c->current = ES2_SIM_VA_ENTRY;
    /* 0x401000: [0x49705d] = [0x497059] << 2. */
    W32(c, 0x0049705du, R32(c, 0x00497059u) << 2);
    /* 0x40100f..0x401022: rep stosb over [0x4a7bfc, 0x6cbee8). */
    for (uint32_t a = ES2_SIM_VA_BSS_START; a < ES2_SIM_VA_BSS_END; a += 4u) W32(c, a, 0);
    /* 0x401026..0x40103f: the two dwords below StackBase saved; the
     * construction counter's address. (ESP is never above StackBase-8 here.) */
    W32(c, 0x00497061u, R32(c, sb - 8u));
    W32(c, 0x00497065u, R32(c, sb - 4u));
    W32(c, 0x006cbee4u, sb - 4u);
    /* 0x496425(0): the byte 0x497058 selects a DLL's exception info; it is 0
     * in the reference file, so 0x4964e2(&local) runs: [0x4a78a0] =
     * [StackBase-8] + 0x24, local = {0x82727349, 0x4a7884}. */
    if (R8(c, 0x00497058u)) {
        /* GetModuleHandleA(0), GetProcAddress(.., "__GetExceptDLLinfo"):
         * not reached with the reference file; refused, not guessed. */
        static const uint32_t zero[1] = {0};
        uint32_t regs[3] = {0, 0, 0};
        es2_sim_result r;
        es2_sim_call(c->dispatch, 0x004968f8u, zero, 1, regs, &r);
        return 1;
    }
    W32(c, 0x004a78a0u, R32(c, sb - 8u) + 0x24u);
    W32(c, sb - 8u, ES2_SIM_VA_EXCEPT_CONTEXT);
    W32(c, 0x006cbee4u, sb - 4u);
    W32(c, sb - 4u, 0);
    W32(c, R32(c, sb - 8u) + 4u, 0);
    /* 0x401056..0x401062: hInstance. */
    uint32_t zero = 0, module = 0;
    if (call(c, VA_MODULE_HANDLE, &zero, 1, &module)) return 1;
    W32(c, 0x0049706au, module);
    c->current = 0;
    return 0;
}

/* ---- the selector (CW3220 0x41a634, the runtime lifecycle model) */
static uint8_t sel_read8(void *context, uint32_t a) { return R8(context, a); }
static void sel_write8(void *context, uint32_t a, uint8_t v) { W8(context, a, v); }
static uint32_t sel_read32(void *context, uint32_t a) { return R32(context, a); }

int es2_sim_run_next_record(es2_sim_init *c, unsigned mode, uint32_t *va) {
    es2_rtl_runtime rt = {0};
    rt.module_count = 1;
    rt.module_table = ES2_SIM_VA_MODULE_TABLE;
    es2_rtl_start_env env = {0};
    env.context = c;
    env.read8 = sel_read8;
    env.write8 = sel_write8;
    env.read32 = sel_read32;
    uint32_t f = es2_rtl_select_record(&rt, &env, mode ? 1u : 0u);
    if (va) *va = f;
    if (!f) return 2;
    c->current = f;
    es2_sim_record_fn fn = es2_sim_record_function(f, mode);
    if (!fn) {
        /* No own function: the dispatch refuses the VA (and names it). */
        uint32_t regs[3] = {0, 0, 0};
        es2_sim_result r;
        static const uint32_t none[1] = {0};
        es2_sim_call(c->dispatch, f, none, 0, regs, &r);
        return 1;
    }
    if (fn(c)) return 1;
    if (mode) ++c->exit_run; else ++c->init_run;
    c->current = 0;
    return 0;
}

static int run_all(es2_sim_init *c, unsigned mode) {
    for (;;) {
        int r = es2_sim_run_next_record(c, mode, NULL);
        if (r == 2) return 0;
        if (r) return 1;
    }
}
int es2_sim_run_init_records(es2_sim_init *c) { return run_all(c, 0); }
int es2_sim_run_exit_records(es2_sim_init *c) { return run_all(c, 1); }
