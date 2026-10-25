/*
 * Visual themes for the game browser.
 *
 * Built in: "gamecube" (the purple IPL-style menu), "classic" (the
 * original text list on the stock backdrop), "halloween", "thanksgiving",
 * and "christmas".
 *
 * Drop-in themes live in themes/<folder>/theme.ini next to boot.dol.
 * See LOADER_UI.md for the keys. To add a built-in theme, add one
 * NinTheme to kBuiltin in theme.c.
 */
#ifndef __THEME_H__
#define __THEME_H__

#include <gctypes.h>

typedef struct NinTheme {
	char id[32];
	char name[48];
	u32 bg_top;
	u32 bg_mid;
	u32 bg_bottom;
	u32 header;
	u32 text;
	u32 text_dim;
	u32 accent;
	u32 panel;
	u32 frame;
	u32 placeholder;
	u32 highlight; /* pill behind a line, 0 if unused */
	char background[256]; /* optional PNG, empty if unused */
	bool classic;         /* stock backdrop, dark text */
	bool header_bar;
	bool wordmark;        /* "NINTENDO GAMECUBE" header */
} NinTheme;

void Theme_Init(void);
void Theme_Shutdown(void);
bool Theme_IsReady(void);

/* Paint the current theme. Does not call ClearScreen. */
void Theme_Paint(void);

const NinTheme *Theme_Current(void);
bool Theme_IsClassic(void);
u32 Theme_ColorText(void);

int Theme_Count(void);
const NinTheme *Theme_At(int index);
int Theme_Index(void);
bool Theme_SetIndex(int index);
bool Theme_SetByName(const char *id);

void Theme_DrawBackground(void);

#endif
