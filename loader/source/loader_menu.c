/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 */
#include <stdio.h>
#include <string.h>

#include "loader_menu.h"
#include "loader_prefs.h"
#include "theme.h"
#include "memcard_mgr.h"
#include "covers.h"
#include "flow.h"
#include "font.h"
#include "FPad.h"
#include "global.h"

static int held_step(bool down, u32 *held)
{
	if (down) {
		int fire = (*held == 0 || *held > 10);
		(*held)++;
		return fire;
	}
	*held = 0;
	return 0;
}

void MenuUI_Shutdown(void)
{
	Covers_Shutdown();
	Flow_Free();
	Theme_Shutdown();
}

bool MenuUI_TakeSettings(void)
{
	return MemCard_TakeSettings();
}

void LoaderMenu_Run(const gameinfo *selected)
{
	int cursor = 0;
	bool redraw = true;
	u32 held_u = 0;
	u32 held_d = 0;
	u32 tap = 0;
	const int rows = 5;

	while (1) {
		const NinTheme *theme;
		u32 color;
		int i;
		char line[80];

		VIDEO_WaitVSync();
		FPAD_Update();
		if (Shutdown)
			LoaderShutdown();
		Screenshot();

		if (held_step(FPAD_Up(1), &held_u)) {
			cursor--;
			if (cursor < 0)
				cursor = rows - 1;
			redraw = true;
		} else if (held_step(FPAD_Down(1), &held_d)) {
			cursor++;
			if (cursor >= rows)
				cursor = 0;
			redraw = true;
		} else if (tap) {
			tap--;
		} else if (FPAD_Cancel(1) || FPAD_Start(1)) {
			break;
		} else {
			bool left = FPAD_Left(1);
			bool right = FPAD_Right(1);
			bool ok = FPAD_OK(1);
			if (left || right || ok) {
				int dir = right ? 1 : -1;
				tap = 12;
				if (cursor == 0) {
					Prefs_SetView(Prefs_GetView() == NIN_VIEW_FLOW ? NIN_VIEW_LIST : NIN_VIEW_FLOW);
					Prefs_Save();
				} else if (cursor == 1 && (left || right)) {
					int idx = Theme_Index();
					int n = Theme_Count();
					if (n > 0) {
						idx = (idx + dir + n) % n;
						Theme_SetIndex(idx);
						Prefs_SetTheme(Theme_Current()->id);
						Prefs_Save();
					}
				} else if (cursor == 2) {
					Prefs_SetCovers(!Prefs_GetCovers());
					Prefs_Save();
				} else if (cursor == 3 && (left || right || ok)) {
					Prefs_SetArt(Prefs_GetArt() == NIN_ART_DISC ? NIN_ART_COVER : NIN_ART_DISC);
					Prefs_Save();
					Covers_Invalidate();
				} else if (cursor == 4 && ok) {
					MemCard_Run(selected);
				}
				redraw = true;
			}
		}

		if (!redraw)
			continue;

		theme = Theme_Current();
		color = theme->text;
		Theme_DrawBackground();
		PrintFormat(20, color, 36, theme->header_bar ? 56 : 36, "Options");
		for (i = 0; i < rows; i++) {
			int y = (theme->header_bar ? 100 : 80) + i * 28;
			u32 c = (i == cursor) ? theme->accent : color;
			line[0] = 0;
			if (i == 0) {
				snprintf(line, sizeof(line), "View            %s",
					Prefs_GetView() == NIN_VIEW_FLOW ? "Cover flow" : "List");
			} else if (i == 1) {
				snprintf(line, sizeof(line), "Theme           %s", Theme_Current()->name);
			} else if (i == 2) {
				snprintf(line, sizeof(line), "Download covers %s", Prefs_GetCovers() ? "On" : "Off");
			} else if (i == 3) {
				snprintf(line, sizeof(line), "Artwork         %s", Prefs_GetArt() == NIN_ART_DISC ? "Disc" : "Cover");
			} else {
				snprintf(line, sizeof(line), "Memory cards");
			}
			PrintFormat(18, c, 48, y, "%s%s", i == cursor ? "> " : "  ", line);
		}
		PrintFormat(14, theme->text_dim, 36, 420, "Left/Right change    A open cards    B back");
		PrintFormat(14, theme->text_dim, 36, 444, "Themes in themes/<name>/theme.ini    Covers cached in covers/");
		GRRLIB_Render();
		ClearScreen();
		redraw = false;
	}
}
