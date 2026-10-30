#ifndef ES2_HOOK_REGISTRY_IDS_H
#define ES2_HOOK_REGISTRY_IDS_H
#define ES2_HOOK(name,rva,descriptor,extension,group) ES2_HOOK_INDEX_##name,
enum es2_hook_index {
#include "hook_registry.h"
    ES2_HOOK_COUNT
};
#undef ES2_HOOK
#define ES2_HOOK(name,rva,descriptor,extension,group) ES2_HOOK_RVA_##name = rva,
enum es2_hook_rva {
#include "hook_registry.h"
};
#undef ES2_HOOK
#define ES2_HOOK_ID(name) #name, ES2_HOOK_RVA_##name
#endif
