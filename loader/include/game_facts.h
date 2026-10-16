/*
 * Synopsis and rating from the GameTDB game page. The disc banner only
 * stores a short line. The info screen uses this when the page has more.
 * A copy is kept in info/<ID6>.txt next to boot.dol so the page is not
 * downloaded again.
 */
#ifndef __GAME_FACTS_H__
#define __GAME_FACTS_H__

#include "menu.h"

#define GAME_SYNOPSIS_MAX 1536

typedef struct GameFacts {
	char rating[32];
	char players[24];
	char genre[48];
	char developer[48];
	char publisher[48];
	char released[24];
	char content[80];
	char synopsis[GAME_SYNOPSIS_MAX];
} GameFacts;

/* Fills what it can. Missing fields are empty strings. */
void GameFacts_Load(const gameinfo *game, GameFacts *out);

#endif
