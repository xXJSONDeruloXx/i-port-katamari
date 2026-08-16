/*
 * Fault reporting for the loaded module.
 *
 * has no debugger, the .so is not on disk, and qemu only reports the faulting
 * address. Naming the faulting instruction's offset inside the module is what
 * turns "segfault at 0x4" into something objdump can answer.
 */
#ifndef KATAMARI_CRASH_H
#define KATAMARI_CRASH_H

#include "so_util.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Installs the handler and remembers where the module was mapped. */
void crash_report_init(so_module *mod, const char *soname);

#ifdef __cplusplus
}
#endif

#endif
