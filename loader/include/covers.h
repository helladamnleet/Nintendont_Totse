/*
 * Box art for the cover browser.
 *
 * Lookup order is the table kCoverPatterns in covers.c. That is the
 * list to edit when a new loader's cover folder should be recognized.
 * Anything found is shown as-is. If nothing local exists and cover
 * download is on, the selected game is fetched from GameTDB and saved
 * onto the game device as /covers/<ID6>.png and /discs/<ID6>.png.
 *
 * Images wider or taller than 512 pixels are skipped. GameTDB's flat
 * "cover" images are about 160x224 and are the ones we ask for.
 */
#ifndef __COVERS_H__
#define __COVERS_H__

#include "menu.h"
#include "grrlib.h"

void Covers_Shutdown(void);
void Covers_Invalidate(void);
const char *Covers_Note(void);

/* Local art only. NULL if this game has nothing cached or on disk. */
GRRLIB_texImg *Covers_Get(const gameinfo *game);

/* True when a download should be attempted for the selected game. */
bool Covers_ShouldFetch(const gameinfo *game);

/* Download one cover into the local cache. Safe to call once per game. */
bool Covers_Fetch(const gameinfo *game);

/* Draw art, or a titled stand-in, into the rectangle. */
void Covers_Draw(const gameinfo *game, float x, float y, float w, float h, bool selected);

#endif
