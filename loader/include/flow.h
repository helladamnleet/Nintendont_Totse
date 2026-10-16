/*
 * Cover browser. Left and right flip through the list. The gamecube
 * theme draws the IPL-style page (banner, blurb, cover strip). Any
 * other theme gets the same controls with that theme's colors.
 */
#ifndef __FLOW_H__
#define __FLOW_H__

#include "menu.h"

typedef enum {
	FLOW_NONE = 0,
	FLOW_MOVED,
	FLOW_LAUNCH,
	FLOW_INFO
} FlowResult;

FlowResult Flow_Update(const gameinfo *games, int count, int *selected);
void Flow_Draw(const gameinfo *games, int count, int selected, const char *status);
void Flow_Free(void);

#endif
