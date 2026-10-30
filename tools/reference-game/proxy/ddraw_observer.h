/* IDirectDraw v1 / IDirectDrawSurface v1 ABI forwarding. Objects retain their
 * identity. No system vtable is modified: each observed object gets its own
 * copy. Other interface versions are left untouched and reported. */
#ifndef ES2_DDRAW_OBSERVER_H
#define ES2_DDRAW_OBSERVER_H
#include <ddraw.h>
#define DD_OBJECT_MAX 128
typedef struct {
    void *object; void **original; void *table[36]; unsigned kind;
    BOOL held, logical_locked; unsigned unlocks;
    DDSURFACEDESC description;
} dd_observed;
static dd_observed dd_objects[DD_OBJECT_MAX];
static BOOL dd_observe, dd_defer_flip;
static DWORD dd_present_every;
static unsigned dd_unobserved, dd_deferred, dd_drained;
static dd_observed *dd_pending;
static HRESULT dd_pending_result;
static dd_observed *dd_held;
static HRESULT (WINAPI *dd_real_create)(GUID *, LPDIRECTDRAW *, IUnknown *);
static uintptr_t dd_invoke(void *object, unsigned slot, uintptr_t *a, unsigned count);
static BOOL dd_track(void *object, unsigned kind);
static dd_observed *dd_find(void *object) {
    for (unsigned i=0; i<DD_OBJECT_MAX; ++i) if (dd_objects[i].object == object) return &dd_objects[i];
    return NULL;
}
/* Each method's argument count includes this. All v1 results and arguments
 * are one x86 word; floating-point or aggregate arguments are absent. */
#define DD_METHOD1(N) static uintptr_t WINAPI dd_m##N(void *s) { uintptr_t a[]={0}; return dd_invoke(s,N,a,1); }
#define DD_METHOD2(N) static uintptr_t WINAPI dd_m##N(void *s,uintptr_t a1) { uintptr_t a[]={a1}; return dd_invoke(s,N,a,2); }
#define DD_METHOD3(N) static uintptr_t WINAPI dd_m##N(void *s,uintptr_t a1,uintptr_t a2) { uintptr_t a[]={a1,a2}; return dd_invoke(s,N,a,3); }
#define DD_METHOD4(N) static uintptr_t WINAPI dd_m##N(void *s,uintptr_t a1,uintptr_t a2,uintptr_t a3) { uintptr_t a[]={a1,a2,a3}; return dd_invoke(s,N,a,4); }
#define DD_METHOD5(N) static uintptr_t WINAPI dd_m##N(void *s,uintptr_t a1,uintptr_t a2,uintptr_t a3,uintptr_t a4) { uintptr_t a[]={a1,a2,a3,a4}; return dd_invoke(s,N,a,5); }
#define DD_METHOD6(N) static uintptr_t WINAPI dd_m##N(void *s,uintptr_t a1,uintptr_t a2,uintptr_t a3,uintptr_t a4,uintptr_t a5) { uintptr_t a[]={a1,a2,a3,a4,a5}; return dd_invoke(s,N,a,6); }
/* Object and surface slots with different arities use separate method sets. */
DD_METHOD3(0) DD_METHOD1(1) DD_METHOD1(2)
DD_METHOD2(3) DD_METHOD2(4) DD_METHOD6(5) DD_METHOD4(6) DD_METHOD6(7)
DD_METHOD3(8) DD_METHOD3(9) DD_METHOD4(10) DD_METHOD3(11) DD_METHOD3(12)
DD_METHOD2(13) DD_METHOD2(14) DD_METHOD2(15) DD_METHOD3(16) DD_METHOD2(17)
DD_METHOD2(18) DD_METHOD3(19) DD_METHOD2(20) DD_METHOD2(21) DD_METHOD2(22)
DD_METHOD3(23) DD_METHOD1(24) DD_METHOD5(25) DD_METHOD2(26) DD_METHOD1(27)
DD_METHOD2(28) DD_METHOD3(29) DD_METHOD3(30) DD_METHOD2(31) DD_METHOD2(32)
DD_METHOD6(33) DD_METHOD2(34) DD_METHOD3(35)
/* DirectDraw-only slots 3..22, offset in the wrapper identifier by 100. */
DD_METHOD1(103) DD_METHOD4(104) DD_METHOD5(105) DD_METHOD4(106) DD_METHOD3(107)
DD_METHOD5(108) DD_METHOD5(109) DD_METHOD1(110) DD_METHOD3(111) DD_METHOD2(112)
DD_METHOD3(113) DD_METHOD2(114) DD_METHOD2(115) DD_METHOD2(116) DD_METHOD2(117)
DD_METHOD2(118) DD_METHOD1(119) DD_METHOD3(120) DD_METHOD4(121) DD_METHOD3(122)
#undef DD_METHOD1
#undef DD_METHOD2
#undef DD_METHOD3
#undef DD_METHOD4
#undef DD_METHOD5
#undef DD_METHOD6
static void *const dd_surface_methods[] = {
    dd_m0,dd_m1,dd_m2,dd_m3,dd_m4,dd_m5,dd_m6,dd_m7,dd_m8,dd_m9,dd_m10,dd_m11,
    dd_m12,dd_m13,dd_m14,dd_m15,dd_m16,dd_m17,dd_m18,dd_m19,dd_m20,dd_m21,dd_m22,
    dd_m23,dd_m24,dd_m25,dd_m26,dd_m27,dd_m28,dd_m29,dd_m30,dd_m31,dd_m32,dd_m33,dd_m34,dd_m35};
static void *const dd_draw_methods[] = {dd_m0,dd_m1,dd_m2,dd_m103,dd_m104,dd_m105,dd_m106,
    dd_m107,dd_m108,dd_m109,dd_m110,dd_m111,dd_m112,dd_m113,dd_m114,dd_m115,dd_m116,
    dd_m117,dd_m118,dd_m119,dd_m120,dd_m121,dd_m122};
static uintptr_t dd_forward(dd_observed *o,unsigned slot,uintptr_t *a,unsigned n) {
    void *f=o->original[slot], *s=o->object;
    switch(n) {
    case 1:return ((uintptr_t(WINAPI *)(void *))f)(s);
    case 2:return ((uintptr_t(WINAPI *)(void *,uintptr_t))f)(s,a[0]);
    case 3:return ((uintptr_t(WINAPI *)(void *,uintptr_t,uintptr_t))f)(s,a[0],a[1]);
    case 4:return ((uintptr_t(WINAPI *)(void *,uintptr_t,uintptr_t,uintptr_t))f)(s,a[0],a[1],a[2]);
    case 5:return ((uintptr_t(WINAPI *)(void *,uintptr_t,uintptr_t,uintptr_t,uintptr_t))f)(s,a[0],a[1],a[2],a[3]);
    default:return ((uintptr_t(WINAPI *)(void *,uintptr_t,uintptr_t,uintptr_t,uintptr_t,uintptr_t))f)(s,a[0],a[1],a[2],a[3],a[4]);
    }
}
static void dd_drain(void) {
    if (!dd_pending) return;
    dd_observed *p=dd_pending; dd_pending=NULL;
    uintptr_t a[]={0,DDFLIP_WAIT};
    dd_pending_result=(HRESULT)dd_forward(p,11,a,3); ++dd_drained; ++perf_dd_drained;
    if (FAILED(dd_pending_result)) perf_error=3;
}
/* A skipped primary Unlock keeps the actual surface lock and its memory
 * alive. The next eligible Lock returns that same memory/description. Every
 * Nth Unlock, and every other operation that can observe lock state, goes
 * to the driver. This is experimental: asynchronous loss can still change
 * the HRESULT a suppressed operation would have returned. */
static void dd_unlock_held(void) {
    if (!dd_held || dd_held->logical_locked) return;
    dd_observed *o=dd_held;dd_held=NULL;o->held=FALSE;
    uintptr_t a[]={0};
    HRESULT result=(HRESULT)dd_forward(o,32,a,2);
    if (FAILED(result)) perf_error=3;
    ++perf_dd_drained;
}
static void dd_finish(void) {
    unsigned phase=perf_begin(PERF_DDRAW);dd_drain();dd_unlock_held();perf_end(phase);
}
static BOOL dd_cached_lock(dd_observed *o,unsigned slot,uintptr_t *a,uintptr_t *result) {
    if (dd_present_every<2 || !dd_held || dd_held!=o || o->logical_locked || slot!=25 ||
        a[0] || !a[1] || a[2]!=DDLOCK_WAIT || a[3] || ((DDSURFACEDESC *)a[1])->dwSize!=sizeof(DDSURFACEDESC)) return FALSE;
    if (FAILED((HRESULT)dd_forward(o,24,a,1))) return FALSE;
    memcpy((void *)a[1],&o->description,sizeof(o->description));
    o->logical_locked=TRUE;*result=DD_OK;return TRUE;
}
static BOOL dd_cached_unlock(dd_observed *o,unsigned slot,uintptr_t *a,uintptr_t *result) {
    if (dd_present_every<2 || dd_held!=o || !o->logical_locked || slot!=32 || a[0]) return FALSE;
    o->logical_locked=FALSE;
    if (FAILED((HRESULT)dd_forward(o,24,a,1)) || ++o->unlocks%dd_present_every==0) {
        dd_held=NULL;o->held=FALSE;return FALSE;
    }
    ++perf_dd_deferred;*result=DD_OK;return TRUE;
}
static uintptr_t dd_invoke(void *object,unsigned slot,uintptr_t *a,unsigned count) {
    if (slot>=100) slot-=100;
    dd_observed *o=dd_find(object);
    if (!o) return (uintptr_t)E_UNEXPECTED;
    DWORD error=GetLastError(); unsigned phase=perf_begin(PERF_DDRAW); SetLastError(error);
    /* Delayed presentation is settled before ANY subsequent observed method,
     * including Lock, GetDC, IsLost, Release and another Flip. No virtual
     * surface state is invented. This deliberately limits possible speedup. */
    dd_drain();
    uintptr_t result;
    if (dd_cached_lock(o,slot,a,&result) || dd_cached_unlock(o,slot,a,&result)) {
        error=GetLastError();perf_end(phase);SetLastError(error);return result;
    }
    /* IsLost/GetCaps are forwarded while the lock is retained. All content,
     * DC, status, alias, flip and lifetime operations settle it first. */
    if (!(o==dd_held && o->kind==1 && (slot==24 || slot==14))) dd_unlock_held();
    BOOL defer=dd_defer_flip && o->kind==1 && slot==11 && !a[0] && a[1]==DDFLIP_WAIT;
    if (defer) {
        DDSCAPS caps={0}; uintptr_t c[]={(uintptr_t)&caps};
        result=dd_forward(o,14,c,2);
        defer=SUCCEEDED((HRESULT)result) && (caps.dwCaps&DDSCAPS_PRIMARYSURFACE) && (caps.dwCaps&DDSCAPS_FLIP);
        if (defer) {
            uintptr_t status[]={DDGFS_CANFLIP};
            result=dd_forward(o,24,a,1);
            if (SUCCEEDED((HRESULT)result)) result=dd_forward(o,18,status,2);
            defer=SUCCEEDED((HRESULT)result);
        }
    }
    if (defer) { dd_pending=o; ++dd_deferred; ++perf_dd_deferred; result=DD_OK; }
    else result=dd_forward(o,slot,a,count);
    error=GetLastError();
    if (slot==2 && !result) { if(dd_held==o)dd_held=NULL;memset(o,0,sizeof(*o)); }
    else if (SUCCEEDED((HRESULT)result)) {
        if (o->kind==1 && slot==32 && dd_held==o) { dd_held=NULL;o->held=FALSE;o->logical_locked=FALSE; }
        if (dd_present_every>1 && o->kind==1 && slot==25 && !a[0] && a[1] && a[2]==DDLOCK_WAIT && !a[3] &&
            ((DDSURFACEDESC *)a[1])->dwSize==sizeof(DDSURFACEDESC)) {
            DDSCAPS caps={0};uintptr_t caparg[]={(uintptr_t)&caps};
            if (SUCCEEDED((HRESULT)dd_forward(o,14,caparg,2)) && (caps.dwCaps&DDSCAPS_PRIMARYSURFACE) && !(caps.dwCaps&DDSCAPS_FLIP)) {
                memcpy(&o->description,(void *)a[1],sizeof(o->description));
                o->held=TRUE;o->logical_locked=TRUE;dd_held=o;
            }
        }
        if (o->kind==0 && slot==6 && a[1]) dd_track(*(void **)a[1],1);
        if (o->kind==0 && slot==7 && a[1]) dd_track(*(void **)a[1],1);
        if (o->kind==1 && slot==12 && a[1]) dd_track(*(void **)a[1],1);
        if (slot==0 && a[1] && *(void **)a[1]!=object) {
            ++dd_unobserved; ++perf_dd_unobserved; dd_defer_flip=FALSE; dd_present_every=0; perf_error=5;
        }
    }
    perf_end(phase); SetLastError(error); return result;
}
static BOOL dd_track(void *object,unsigned kind) {
    if (!object || dd_find(object)) return TRUE;
    for(unsigned i=0;i<DD_OBJECT_MAX;++i) if (!dd_objects[i].object) {
        dd_observed *o=&dd_objects[i]; o->object=object; o->kind=kind; o->original=*(void ***)object;
        memcpy(o->table,kind?dd_surface_methods:dd_draw_methods,(kind?36:23)*sizeof(void *));
        *(void ***)object=o->table; return TRUE;
    }
    ++dd_unobserved; ++perf_dd_unobserved; dd_defer_flip=FALSE; dd_present_every=0; perf_error=5; return FALSE;
}
static HRESULT WINAPI dd_create(GUID *id,LPDIRECTDRAW *out,IUnknown *outer) {
    DWORD error=GetLastError(); unsigned phase=perf_begin(PERF_DDRAW); SetLastError(error);
    HRESULT result=dd_real_create(id,out,outer); error=GetLastError();
    if (SUCCEEDED(result) && out && *out && !outer) dd_track(*out,0);
    perf_end(phase); SetLastError(error); return result;
}
#endif
