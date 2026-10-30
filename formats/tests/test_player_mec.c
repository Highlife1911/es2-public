#include "es2/formats/player_mec.h"
#include <stdio.h>
#include <string.h>

static unsigned checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; fprintf(stderr, "line %d: %s\n", __LINE__, #x); } } while (0)
static void w(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
int main(void)
{
    uint8_t bytes[235], out[300], guard[300], mutated[236]; size_t i, n;
    es2_mec m, v; es2_mec_record a, b; es2_error e;
    int16_t items[] = {2,1,0,2,1}, id; uint16_t secondary;
    es2_mec_edit edits[] = {{0,5,7,items}, {1,0,7,NULL}};
    /* Synthetic arbitrary opaque bytes, N=2, K=3 and 8. */
    for (i = 0; i < sizeof bytes; ++i) bytes[i] = (uint8_t)(i*37 + 11);
    w(bytes, 123); w(bytes+2, 2); w(bytes+8, 7); w(bytes+10, 3);
    w(bytes+96, 7); w(bytes+98, 8); w(bytes+200, 33);
    CHECK(es2_mec_parse((es2_span){bytes,sizeof bytes}, &m, &e) == ES2_OK);
    CHECK(m.count == 2 && m.header == 123 && m.flags.size == 33);
    for (i = 0; i < sizeof bytes; ++i) {
        v = m;
        CHECK(es2_mec_parse((es2_span){bytes,i}, &v, &e) != ES2_OK);
        CHECK(v.input.data == m.input.data && v.count == m.count);
    }
    memcpy(mutated, bytes, sizeof bytes); mutated[235] = 0;
    CHECK(es2_mec_parse((es2_span){mutated,sizeof mutated}, &v, &e) == ES2_ERR_LENGTH);
    for (i = 0; i < 4; ++i) {
        const size_t offsets[] = {2,10,98,200};
        memcpy(mutated, bytes, sizeof bytes); w(mutated + offsets[i], 65535);
        CHECK(es2_mec_parse((es2_span){mutated,sizeof bytes}, &v, &e) == ES2_ERR_COUNT);
    }
    CHECK(es2_mec_write(&m,NULL,0,5,NULL,0,&n,&e) == ES2_OK && n == sizeof bytes);
    memset(out, 0xa5, sizeof out); memcpy(guard,out,sizeof out);
    CHECK(es2_mec_write(&m,NULL,0,5,out,n-1,&n,&e) == ES2_ERR_CAPACITY);
    CHECK(!memcmp(out,guard,sizeof out));
    CHECK(es2_mec_write(&m,NULL,0,5,out,sizeof out,&n,&e) == ES2_OK);
    CHECK(n == sizeof bytes && !memcmp(out,bytes,n));
    CHECK(es2_mec_write(&m,NULL,0,5,bytes,sizeof bytes,&n,&e) == ES2_ERR_OVERLAP);
    CHECK(es2_mec_record_at(&m,0,&a,&e) == ES2_OK);
    CHECK(es2_mec_record_at(&m,2,&a,&e) == ES2_ERR_OFFSET);
    CHECK(es2_mec_item_at(&a,3,&id,&secondary,&e) == ES2_ERR_OFFSET);
    CHECK(es2_mec_write(&m,edits,2,5,out,sizeof out,&n,&e) == ES2_OK);
    CHECK(n == 211 && es2_mec_parse((es2_span){out,n},&v,&e) == ES2_OK);
    CHECK(es2_mec_record_at(&v,0,&b,&e) == ES2_OK && b.count == 5 && b.vehicle_id == 7);
    CHECK(!memcmp(a.tail.data,b.tail.data,68) && !memcmp(a.pilot.data,b.pilot.data,4));
    CHECK(!memcmp(a.secondary.data,b.secondary.data,6));
    CHECK(es2_mec_item_at(&b,4,&id,&secondary,&e) == ES2_OK && id == 1 && secondary == 5);
    CHECK(!memcmp(m.flags.data,v.flags.data,33));
    edits[1].record = 0;
    CHECK(es2_mec_write(&m,edits,2,5,out,sizeof out,&n,&e) == ES2_ERR_ARGUMENT);
    edits[1].record = 1; edits[1].count = 32768;
    CHECK(es2_mec_write(&m,edits,2,5,out,sizeof out,&n,&e) == ES2_ERR_ARGUMENT);
    {
        uint8_t hb[18] = {0}, wb[32] = {0}, lb[24] = {0};
        es2_dat h, weapons, loadout; unsigned warnings;
        w(hb,1); w(hb+2,7); w(hb+8,5);
        w(wb,3);
        for (i=0;i<3;++i) { w(wb+2+10*i,(unsigned)i); w(wb+4+10*i,1); }
        /* Each shell item: id, length=1, NUL, cost, flag, priority = 10. */
        { uint8_t weapon_bytes[34]; memcpy(weapon_bytes,wb,32); w(weapon_bytes+32,0);
          CHECK(es2_dat_parse(ES2_DAT_SHELL_WEAPONS,(es2_span){weapon_bytes,34},0,&weapons,&e)==ES2_OK);
          CHECK(es2_dat_parse(ES2_DAT_HERC_INF,(es2_span){hb,18},0,&h,&e)==ES2_OK);
          w(lb,7); w(lb+6,2); w(lb+8,0); w(lb+10,2); w(lb+16,4); w(lb+18,1);
          CHECK(es2_dat_parse(ES2_DAT_LOADOUT,(es2_span){lb,24},0,&loadout,&e)==ES2_OK);
          CHECK(es2_mec_validate(&v,&h,&weapons,&loadout,1,&warnings,&e)==ES2_OK);
          CHECK((warnings & ES2_MEC_WARN_MOUNT) && (warnings & ES2_MEC_WARN_K));
          w(out+8,9); CHECK(es2_mec_validate(&v,&h,&weapons,&loadout,1,&warnings,&e)==ES2_ERR_NOT_FOUND);
          w(out+8,7); w(out+12,99); CHECK(es2_mec_validate(&v,&h,&weapons,&loadout,1,&warnings,&e)==ES2_ERR_NOT_FOUND);
          { int16_t eleven[11]={0}; uint8_t empty[39]={0};
            edits[0]=(es2_mec_edit){0,11,7,eleven}; edits[1]=(es2_mec_edit){1,0,7,NULL};
            CHECK(es2_mec_write(&m,edits,2,5,out,sizeof out,&n,&e)==ES2_OK);
            CHECK(es2_mec_parse((es2_span){out,n},&v,&e)==ES2_OK);
            CHECK(es2_mec_validate(&v,&h,&weapons,&loadout,1,&warnings,&e)==ES2_ERR_COUNT);
            w(empty+4,33);
            CHECK(es2_mec_parse((es2_span){empty,sizeof empty},&v,&e)==ES2_OK);
            CHECK(es2_mec_validate(&v,&h,&weapons,&loadout,1,&warnings,&e)==ES2_ERR_COUNT);
          }
        }
    }
    printf("player_mec: %u checks, %u failures\n",checks,failed);
    return failed ? 1 : 0;
}
