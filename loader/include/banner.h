/*
 * opening.bnr from the disc. The GameCube IPL shows this 96x32 picture
 * and the description stored next to it. That is the left-hand image
 * in the system menu, not the box art.
 */
#ifndef __BANNER_H__
#define __BANNER_H__

#include "menu.h"
#include "grrlib.h"

typedef struct GameBanner {
	char title[64];
	char description[128];
	GRRLIB_texImg *image; /* 96x32, or NULL */
	bool valid;
} GameBanner;

/* Cached for the last game asked for. Do not free the result. */
const GameBanner *Banner_Get(const gameinfo *game);
void Banner_Free(void);

#endif
