/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 */
#include <stdio.h>
#include <string.h>

#include "flow.h"
#include "theme.h"
#include "covers.h"
#include "banner.h"
#include "font.h"
#include "FPad.h"
#include "global.h"

static u32 held_l = 0;
static u32 held_r = 0;
static u32 held_u = 0;
static u32 held_d = 0;

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

static int wrap_index(int selected, int count, int delta)
{
	if (count <= 0)
		return 0;
	selected += delta;
	while (selected < 0)
		selected += count;
	while (selected >= count)
		selected -= count;
	return selected;
}

FlowResult Flow_Update(const gameinfo *games, int count, int *selected)
{
	int idx;

	(void)games;
	if (!selected)
		return FLOW_NONE;
	idx = *selected;
	if (count <= 0) {
		*selected = 0;
		return FLOW_NONE;
	}
	if (idx < 0 || idx >= count)
		idx = 0;

	if (FPAD_OK(0)) {
		*selected = idx;
		return FLOW_LAUNCH;
	}
	if (FPAD_X(0)) {
		*selected = idx;
		return FLOW_INFO;
	}
	if (held_step(FPAD_Left(1), &held_l)) {
		*selected = wrap_index(idx, count, -1);
		return FLOW_MOVED;
	}
	if (held_step(FPAD_Right(1), &held_r)) {
		*selected = wrap_index(idx, count, 1);
		return FLOW_MOVED;
	}
	if (held_step(FPAD_Up(1), &held_u)) {
		idx -= 4;
		if (idx < 0)
			idx = 0;
		*selected = idx;
		return FLOW_MOVED;
	}
	if (held_step(FPAD_Down(1), &held_d)) {
		idx += 4;
		if (idx >= count)
			idx = count - 1;
		*selected = idx;
		return FLOW_MOVED;
	}
	*selected = idx;
	return FLOW_NONE;
}

static int draw_wrapped(const char *text, int x, int y, int max_w, u8 size, u32 color, int max_lines)
{
	const char *p;
	char line[96];
	int lines = 0;

	if (!text)
		return 0;
	p = text;
	while (*p && lines < max_lines) {
		int len = 0;
		while (*p == '\r')
			p++;
		if (*p == '\n') {
			p++;
			continue;
		}
		line[0] = 0;
		while (*p && *p != '\n' && *p != '\r') {
			const char *word;
			int wlen;
			char trial[96];
			while (*p == ' ')
				p++;
			if (!*p || *p == '\n' || *p == '\r')
				break;
			word = p;
			while (*p && *p != ' ' && *p != '\n' && *p != '\r')
				p++;
			wlen = (int)(p - word);
			if (wlen > 80)
				wlen = 80;
			if (len == 0) {
				if (wlen > (int)sizeof(line) - 1)
					wlen = (int)sizeof(line) - 1;
				memcpy(line, word, (size_t)wlen);
				line[wlen] = 0;
				len = wlen;
			} else {
				if (len + 1 + wlen >= (int)sizeof(trial))
					break;
				memcpy(trial, line, (size_t)len);
				trial[len] = ' ';
				memcpy(trial + len + 1, word, (size_t)wlen);
				trial[len + 1 + wlen] = 0;
				if ((int)GRRLIB_WidthTTF(myFont, trial, size) > max_w) {
					p = word;
					break;
				}
				strcpy(line, trial);
				len = (int)strlen(line);
			}
		}
		if (line[0]) {
			PrintFormat(size, color, x, y + lines * (size + 4), "%s", line);
			lines++;
		}
		if (*p == '\n')
			p++;
	}
	return lines;
}

static void draw_strip(const gameinfo *games, int count, int selected, int top)
{
	static const int offs[5] = { -2, -1, 0, 1, 2 };
	static const float widths[5] = { 64, 80, 108, 80, 64 };
	static const float heights[5] = { 90, 112, 150, 112, 90 };
	float gap = 40;
	float total = 0;
	float x;
	int i;

	for (i = 0; i < 5; i++)
		total += widths[i];
	total += gap * 4;
	x = (640.0f - total) / 2.0f;

	for (i = 0; i < 5; i++) {
		int gi = selected + offs[i];
		float y = (float)top + (150.0f - heights[i]);

		if (count <= 0 || gi < 0 || gi >= count) {
			x += widths[i] + gap;
			continue;
		}
		Covers_Draw(&games[gi], x, y, widths[i], heights[i], offs[i] == 0);
		x += widths[i] + gap;
	}
}

static void draw_gamecube(const gameinfo *games, int count, int selected, const char *status)
{
	const NinTheme *theme = Theme_Current();
	const GameBanner *ban = NULL;
	const gameinfo *game = NULL;
	char heading[80];
	u8 disc = 0;

	Theme_DrawBackground();
	if (count <= 0 || selected < 0 || selected >= count) {
		PrintFormat(18, theme->text, 180, 200, "No GameCube games found");
		PrintFormat(14, theme->text_dim, 150, 420, "B  Settings     Y  Options     Home  Back");
		return;
	}

	game = &games[selected];
	ban = Banner_Get(game);
	disc = (u8)((game->Flags & GIFLAG_DISCNUMBER_MASK) >> 5);
	if (ban && ban->title[0])
		strncpy(heading, ban->title, sizeof(heading) - 1);
	else if (game->Name)
		strncpy(heading, game->Name, sizeof(heading) - 1);
	else
		heading[0] = 0;
	heading[sizeof(heading) - 1] = 0;
	if (disc > 0) {
		char extra[16];
		snprintf(extra, sizeof(extra), " (%d)", disc + 1);
		if (strlen(heading) + strlen(extra) < sizeof(heading))
			strcat(heading, extra);
	}

	if (ban && ban->image) {
		float vis_w = ban->image->content_w ? (float)ban->image->content_w : (float)ban->image->w;
		float vis_h = ban->image->content_h ? (float)ban->image->content_h : (float)ban->image->h;
		float s;
		if (vis_w < 1.0f)
			vis_w = 1.0f;
		if (vis_h < 1.0f)
			vis_h = 1.0f;
		s = 220.0f / vis_w;
		GRRLIB_DrawImg(12.0f, 50.0f, ban->image, 0, s, s, 0xFFFFFFFF);
	}

	draw_strip(games, count, selected, 210);

	{
		unsigned int hw = GRRLIB_WidthTTF(myFont, heading, 14);
		int hx = 320 - (int)(hw / 2);
		if (hx < 8)
			hx = 8;
		PrintFormat(14, theme->text, hx, 378, "%s", heading);
	}
	if (ban && ban->description[0])
		draw_wrapped(ban->description, 40, 398, 560, 12, theme->text, 2);

	if (status && status[0] && strstr(status, ":/") == NULL) {
		PrintFormat(14, theme->accent, 250, 440, "%s", status);
	} else {
		GRRLIB_Circle(220, 436, 8, 0x3CB44AFF, true);
		PrintFormat(14, theme->text, 236, 428, "Press A to play");
	}
	PrintFormat(12, theme->text_dim, 70, 458, "Left/Right flip    B Settings    X Info    Y Options    Home Back");
}

static void draw_plain(const gameinfo *games, int count, int selected, const char *status)
{
	const NinTheme *theme = Theme_Current();
	const gameinfo *game;
	char heading[80];

	Theme_DrawBackground();
	if (count <= 0 || selected < 0 || selected >= count) {
		PrintFormat(18, theme->text, 170, 220, "No GameCube games found");
		return;
	}
	game = &games[selected];
	strncpy(heading, game->Name ? game->Name : "", sizeof(heading) - 1);
	heading[sizeof(heading) - 1] = 0;
	{
		unsigned int w = GRRLIB_WidthTTF(myFont, heading, 20);
		int x = 320 - (int)(w / 2);
		if (x < 8)
			x = 8;
		PrintFormat(20, theme->text, x, theme->classic ? 36 : 56, "%s", heading);
	}
	if (status && status[0])
		PrintFormat(16, theme->accent, 230, 64, "%s", status);
	draw_strip(games, count, selected, 120);
	PrintFormat(14, theme->text, 80, 430, "Left/Right flip    A Play    B Settings    X Info    Y Options");
}

void Flow_Draw(const gameinfo *games, int count, int selected, const char *status)
{
	if (Theme_IsClassic())
		draw_plain(games, count, selected, status);
	else
		draw_gamecube(games, count, selected, status);
}

void Flow_Free(void)
{
	Banner_Free();
}
