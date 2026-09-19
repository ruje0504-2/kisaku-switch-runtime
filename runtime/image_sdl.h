#ifndef KISAKU_IMAGE_SDL_H
#define KISAKU_IMAGE_SDL_H
#include <SDL.h>
#include "rmt.h"
/* Texture owns its uploaded copy; caller can immediately free the KImage. */
SDL_Texture *kimage_texture(SDL_Renderer *renderer, const KImage *image);
/* Center image inside area, preserving aspect ratio. */
SDL_Rect kimage_fit(uint32_t width, uint32_t height, SDL_Rect area);
#endif
