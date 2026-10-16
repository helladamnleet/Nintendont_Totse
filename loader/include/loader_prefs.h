/*
 * Loader-only preferences. These do not belong in nincfg.bin: the kernel
 * shares that struct, and adding fields there changes the boot protocol.
 *
 * The file is loader.ini next to boot.dol:
 *   view=flow|list
 *   theme=gamecube|classic|<folder in themes/>
 *   covers=on|off
 *   art=cover|disc
 */
#ifndef __LOADER_PREFS_H__
#define __LOADER_PREFS_H__

#include <gctypes.h>
#include <stddef.h>

typedef enum {
	NIN_VIEW_FLOW = 0,
	NIN_VIEW_LIST = 1
} NinView;

typedef enum {
	NIN_ART_COVER = 0,
	NIN_ART_DISC = 1
} NinArt;

void Prefs_Load(void);
void Prefs_Save(void);

/* Directory the loader was started from, with a trailing slash. */
const char *Prefs_BaseDir(void);

/* Join BaseDir and a relative path. Returns false if it does not fit. */
bool Prefs_Path(char *out, size_t outlen, const char *rel);

NinView Prefs_GetView(void);
void Prefs_SetView(NinView view);

const char *Prefs_GetTheme(void);
void Prefs_SetTheme(const char *id);

bool Prefs_GetCovers(void);
void Prefs_SetCovers(bool on);

NinArt Prefs_GetArt(void);
void Prefs_SetArt(NinArt art);

#endif
