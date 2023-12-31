/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2005,2006,2007,2008,2009  Free Software Foundation, Inc.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#define grub_video_render_target grub_video_fbrender_target

#include <config-util.h>
#include <config.h>

#include <grub/err.h>
#include <grub/types.h>
#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/video.h>
#include <grub/video_fb.h>
#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif

GRUB_MOD_LICENSE ("GPLv3+");

#ifdef HAVE_SDL2
static SDL_Window *window = NULL;
static SDL_Texture *texture = NULL;
static SDL_Renderer *renderer = NULL;
#else
static SDL_Surface *window = NULL;
#endif
static SDL_Surface *surface = NULL;
static struct grub_video_render_target *sdl_render_target;
static struct grub_video_mode_info mode_info;

static grub_err_t
grub_video_sdl_set_palette (unsigned int start, unsigned int count,
                            struct grub_video_palette_data *palette_data);

static grub_err_t
grub_video_sdl_init (void)
{
  window = NULL;

  if (SDL_Init (SDL_INIT_VIDEO) < 0)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not init SDL: %s",
		       SDL_GetError ());

  grub_memset (&mode_info, 0, sizeof (mode_info));

  return grub_video_fb_init ();
}

static grub_err_t
grub_video_sdl_fini (void)
{
  SDL_Quit ();
  window = NULL;

  grub_memset (&mode_info, 0, sizeof (mode_info));

  return grub_video_fb_fini ();
}

static inline unsigned int
get_mask_size (grub_uint32_t mask)
{
  unsigned i;
  for (i = 0; mask > 1U << i; i++);
  return i;
}

static grub_err_t
grub_video_sdl_setup (unsigned int width, unsigned int height,
                      unsigned int mode_type, unsigned int mode_mask __attribute__ ((unused)))
{
  int depth;
  int flags = 0;
  grub_err_t err;

  /* Decode depth from mode_type.  If it is zero, then autodetect.  */
  depth = (mode_type & GRUB_VIDEO_MODE_TYPE_DEPTH_MASK)
          >> GRUB_VIDEO_MODE_TYPE_DEPTH_POS;

  if (depth == 0)
    depth = 32;

  if (width == 0 && height == 0)
    {
      width = 800;
      height = 600;
    }

#ifdef HAVE_SDL2
  window = SDL_CreateWindow ("grub-emu",
			     SDL_WINDOWPOS_UNDEFINED,
			     SDL_WINDOWPOS_UNDEFINED,
			     width, height, flags);
  if(window == NULL)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not open window: %s",
		       SDL_GetError ());
  renderer = SDL_CreateRenderer (window, -1, 0);
  if (renderer == NULL)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not open renderer: %s",
		       SDL_GetError ());
  texture = SDL_CreateTexture (renderer,
			       SDL_PIXELFORMAT_ARGB8888,
			       SDL_TEXTUREACCESS_STREAMING,
			       width, height);
  if (texture == NULL)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not create texture: %s",
		       SDL_GetError ());

  /*
   * An empty surface that acts as the pixel buffer, the texture will receive the pixels
   * from here.
   */
  surface = SDL_CreateRGBSurface (0, width, height, depth, 0, 0, 0, 0);
  if (surface == NULL)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not open surface: %s",
		       SDL_GetError ());
#else
  if ((mode_type & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED)
      || !(mode_mask & GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED))
    flags |= SDL_DOUBLEBUF;

  window = SDL_SetVideoMode (width, height, depth, flags | SDL_HWSURFACE);
  if (window == NULL)
    window = SDL_SetVideoMode (width, height, depth, flags | SDL_SWSURFACE);
  if (window == NULL)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not open window: %s",
		       SDL_GetError ());

  surface = window;
#endif
  grub_memset (&sdl_render_target, 0, sizeof (sdl_render_target));

  mode_info.mode_type = 0;
  mode_info.width = surface->w;
  mode_info.height = surface->h;
#ifndef HAVE_SDL2
  if (surface->flags & SDL_DOUBLEBUF)
    mode_info.mode_type
      |= GRUB_VIDEO_MODE_TYPE_DOUBLE_BUFFERED;
#endif
  if (surface->format->palette)
    mode_info.mode_type |= GRUB_VIDEO_MODE_TYPE_INDEX_COLOR;
  else
    mode_info.mode_type |= GRUB_VIDEO_MODE_TYPE_RGB;

  mode_info.bpp = surface->format->BitsPerPixel;
  mode_info.bytes_per_pixel = surface->format->BytesPerPixel;
  mode_info.pitch = surface->pitch;

  /* In index color mode, number of colors.  In RGB mode this is 256.  */
  if (surface->format->palette)
    mode_info.number_of_colors
      = 1 << surface->format->BitsPerPixel;
  else
    mode_info.number_of_colors = 256;

  if (! surface->format->palette)
    {
      mode_info.red_mask_size
	= get_mask_size (surface->format->Rmask >> surface->format->Rshift);
      mode_info.red_field_pos = surface->format->Rshift;
      mode_info.green_mask_size
	= get_mask_size (surface->format->Gmask >> surface->format->Gshift);
      mode_info.green_field_pos = surface->format->Gshift;
      mode_info.blue_mask_size
	= get_mask_size (surface->format->Bmask >> surface->format->Bshift);
      mode_info.blue_field_pos = surface->format->Bshift;
      mode_info.reserved_mask_size
	= get_mask_size (surface->format->Amask >> surface->format->Ashift);
      mode_info.reserved_field_pos = surface->format->Ashift;
      mode_info.blit_format
	= grub_video_get_blit_format (&mode_info);
    }

  err = grub_video_fb_create_render_target_from_pointer (&sdl_render_target,
							 &mode_info,
							 surface->pixels);
  if (err)
    return err;

  /* Copy default palette to initialize emulated palette.  */
  grub_video_sdl_set_palette (0, GRUB_VIDEO_FBSTD_NUMCOLORS,
			      grub_video_fbstd_colors);

  /* Reset render target to SDL one.  */
  return grub_video_fb_set_active_render_target (sdl_render_target);
}

static grub_err_t
grub_video_sdl_set_palette (unsigned int start, unsigned int count,
                            struct grub_video_palette_data *palette_data)
{
  unsigned i;
  if (surface->format->palette)
    {
      SDL_Color *tmp;
      if (start >= mode_info.number_of_colors)
	return GRUB_ERR_NONE;

      if (start + count > mode_info.number_of_colors)
	count = mode_info.number_of_colors - start;

      tmp = grub_calloc (count, sizeof (tmp[0]));
      for (i = 0; i < count; i++)
	{
	  tmp[i].r = palette_data[i].r;
	  tmp[i].g = palette_data[i].g;
	  tmp[i].b = palette_data[i].b;
#ifdef HAVE_SDL2
	  tmp[i].a = palette_data[i].a;
#else
	  tmp[i].unused = palette_data[i].a;
#endif
	}
#ifdef HAVE_SDL2
      SDL_SetPaletteColors (surface->format->palette, tmp, 0 /* firstcolor */, count);
#else
      SDL_SetColors (window, tmp, start, count);
#endif
      grub_free (tmp);
    }

  return grub_video_fb_set_palette (start, count, palette_data);
}

static grub_err_t
grub_video_sdl_swap_buffers (void)
{
#ifdef HAVE_SDL2
  if (SDL_UpdateTexture (texture, NULL, surface->pixels, surface->w * sizeof (Uint32)) < 0)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not update texture: %s",
		       SDL_GetError ());
  if (SDL_RenderClear (renderer) < 0)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not clear renderer: %s",
		       SDL_GetError ());
  if (SDL_RenderCopy (renderer, texture, NULL, NULL) < 0)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not copy texture to renderer: %s",
		       SDL_GetError ());
  SDL_RenderPresent (renderer);
#else
  if (SDL_Flip (window) < 0)
    return grub_error (GRUB_ERR_BAD_DEVICE, "could not swap buffers: %s",
		       SDL_GetError ());
#endif
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_video_sdl_set_active_render_target (struct grub_video_render_target *target)
{
  if (target == GRUB_VIDEO_RENDER_TARGET_DISPLAY)
    return grub_video_fb_set_active_render_target (sdl_render_target);

  return grub_video_fb_set_active_render_target (target);
}

static struct grub_video_adapter grub_video_sdl_adapter =
  {
    .name = "SDL Video Driver",
    .id = GRUB_VIDEO_DRIVER_SDL,

    .prio = GRUB_VIDEO_ADAPTER_PRIO_FIRMWARE,

    .init = grub_video_sdl_init,
    .fini = grub_video_sdl_fini,
    .setup = grub_video_sdl_setup,
    .get_info = grub_video_fb_get_info,
    .set_palette = grub_video_sdl_set_palette,
    .get_palette = grub_video_fb_get_palette,
    .set_viewport = grub_video_fb_set_viewport,
    .get_viewport = grub_video_fb_get_viewport,
    .set_region = grub_video_fb_set_region,
    .get_region = grub_video_fb_get_region,
    .set_area_status = grub_video_fb_set_area_status,
    .get_area_status = grub_video_fb_get_area_status,
    .map_color = grub_video_fb_map_color,
    .map_rgb = grub_video_fb_map_rgb,
    .map_rgba = grub_video_fb_map_rgba,
    .unmap_color = grub_video_fb_unmap_color,
    .fill_rect = grub_video_fb_fill_rect,
    .blit_bitmap = grub_video_fb_blit_bitmap,
    .blit_render_target = grub_video_fb_blit_render_target,
    .scroll = grub_video_fb_scroll,
    .swap_buffers = grub_video_sdl_swap_buffers,
    .create_render_target = grub_video_fb_create_render_target,
    .delete_render_target = grub_video_fb_delete_render_target,
    .set_active_render_target = grub_video_sdl_set_active_render_target,
    .get_active_render_target = grub_video_fb_get_active_render_target,

    .next = 0
  };

GRUB_MOD_INIT(sdl)
{
  grub_video_register (&grub_video_sdl_adapter);
}

GRUB_MOD_FINI(sdl)
{
  grub_video_unregister (&grub_video_sdl_adapter);
}
