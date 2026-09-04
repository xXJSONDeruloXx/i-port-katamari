#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>

#include "platform.h"
#include "io_util.h"

/*
 * Open Citadel never asks the loader to read a guest library out of an APK:
 * the donor importer publishes libUnrealEngine3.so as a normal file and the
 * OBB remains a normal file for FFileManagerAndroid. Avoid carrying libzip in
 * the runtime solely for an unreachable fallback.
 */
uint64_t fnv1a_64(const char *data, size_t len)
{
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint8_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

int zip_load_file(struct zip *, const char *, size_t *, void **, size_t)
{
    return 0;
}

int io_load_file(const char *filename, void **buf, size_t *size)
{
    if (!filename || !buf || !size)
        return 0;

    SDL_RWops *io = SDL_RWFromFile(filename, "rb");
    if (!io)
        return 0;

    Sint64 sz = SDL_RWsize(io);
    if (sz <= 0 || (uint64_t)sz > SIZE_MAX) {
        SDL_RWclose(io);
        return 0;
    }

    void *data = malloc((size_t)sz);
    if (!data) {
        SDL_RWclose(io);
        return 0;
    }

    if (SDL_RWread(io, data, (size_t)sz, 1) != 1) {
        free(data);
        SDL_RWclose(io);
        return 0;
    }

    SDL_RWclose(io);
    *buf = data;
    *size = (size_t)sz;
    return 1;
}
