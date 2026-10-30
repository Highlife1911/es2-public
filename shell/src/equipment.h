#ifndef ES2_SHELL_EQUIPMENT_INTERNAL_H
#define ES2_SHELL_EQUIPMENT_INTERNAL_H
#include "es2/shell/prepare.h"
es2_status shell_equipment_default(const es2_vfs *, const es2_shell_options *,
    es2_shell_equipment *, es2_error *);
es2_status shell_equipment_new(const es2_vfs *, const es2_shell_options *, es2_shell_rng *,
    es2_shell_equipment *, es2_error *);
es2_status shell_equipment_mission(const es2_vfs *, const es2_shell_options *,
    es2_shell_equipment *, size_t, const uint8_t *const[4], es2_error *);
es2_status shell_equipment_project(const es2_shell_equipment *,
    es2_shell_loadout *, es2_shell_actor[4], es2_error *);
#endif
