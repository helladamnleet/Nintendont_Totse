/*
 * Memory card browser modeled on the GameCube BIOS card screen.
 * Card images are the .raw files in /saves on the game device.
 */
#ifndef __MEMCARD_MGR_H__
#define __MEMCARD_MGR_H__

#include "menu.h"

void MemCard_Run(const gameinfo *selected_game);

/* True once after the browser changes Memory Card Multi. */
bool MemCard_TakeSettings(void);

#endif
