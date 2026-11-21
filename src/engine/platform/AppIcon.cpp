#include "engine/platform/AppIcon.h"

#include <SDL3/SDL.h>

#include "core/Embedded.h"

namespace Mdk::Engine::Platform
{
    SDL_Surface* AppIcon::Load()
    {
        auto png = Embedded::Find(Resource);
        if (png.empty())
        {
            return nullptr;
        }

        // SDL decodes from memory; closeio: the stream is freed with the call.
        SDL_IOStream* io = SDL_IOFromConstMem(png.data(), png.size());
        return io == nullptr ? nullptr : SDL_LoadPNG_IO(io, true);
    }
}
