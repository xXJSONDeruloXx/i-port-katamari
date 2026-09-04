#if defined(__i386__)
#include <stdint.h>
#include <string.h>

#include "platform.h"
#include "so_util.h"

/*
 * Minimal i386 hook used by the host-side Open Citadel bring-up target.
 *
 * The guest and compatibility host are both 32-bit processes, so a normal
 * near JMP can reach every address in the process. None of Epic Citadel's
 * current bring-up profile relies on static hooks, but the loader references
 * hook_address unconditionally and keeping it functional makes the i386 path
 * equivalent to ARM rather than a compile-only stub.
 */
void hook_address(so_module *mod, uintptr_t addr, uintptr_t dst)
{
    (void)mod;
    if (!addr)
        return;

    uint8_t patch[5];
    patch[0] = 0xE9;
    const int32_t rel = (int32_t)(dst - (addr + sizeof(patch)));
    memcpy(&patch[1], &rel, sizeof(rel));
    memcpy((void *)addr, patch, sizeof(patch));
    __builtin___clear_cache((char *)addr, (char *)addr + sizeof(patch));
}
#endif
