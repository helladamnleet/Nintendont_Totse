/*
 * The small options page opened with Y: view, theme, cover download,
 * and the memory card browser.
 */
#ifndef __LOADER_MENU_H__
#define __LOADER_MENU_H__

#include "menu.h"

void LoaderMenu_Run(const gameinfo *selected);
void MenuUI_Shutdown(void);
bool MenuUI_TakeSettings(void);

#endif
