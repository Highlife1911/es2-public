#ifndef ES2_CORE_KEYBOARD_H
#define ES2_CORE_KEYBOARD_H
#include "es2/core/weapons.h"
/* The keyboard message handler 477bec, offered every window message
 * by the window procedure 466034 (stdcall: window, message, wParam,
 * lParam). Exact contract: docs/reverse-engineering/input.md (section
 * "keyboard message handler 477bec"). The key table 4a10ec, the key
 * ring 6bb6b0 with its write/read indices 6bb8b0/6bb8b4, the ten hot-key
 * entries 6bb8b8 and the device pointer 6c5eb8 are reached through
 * g->image. Every call goes through g->call with the registers the
 * original holds there: GetKeyState through its import thunk 49695e (so
 * the proxy's input journal answers it), the focus helper 4669b4 and the
 * hot-key callbacks read from the entries. EAX (0/1) and EDX are exact;
 * ECX is dead at the only caller. g->scratch is unused. */
uint64_t es2_key_message_registers(const es2_guest_memory *,const es2_weapon_graph *,
    uint32_t window,uint32_t message,uint32_t key,uint32_t lparam,const uint32_t entry[3]);
#endif
