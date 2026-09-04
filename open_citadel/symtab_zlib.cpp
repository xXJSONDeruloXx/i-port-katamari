#include <zlib.h>

#include "so_util.h"
#include "thunk_gen.h"

DynLibFunction symtable_zlib[] = {
    THUNK_DIRECT(compress),
    THUNK_DIRECT(compress2),
    THUNK_DIRECT(inflate),
    THUNK_DIRECT(inflateEnd),
    THUNK_DIRECT(inflateInit_),
    THUNK_DIRECT(inflateReset),
    THUNK_DIRECT(uncompress),
    {nullptr, 0},
};
