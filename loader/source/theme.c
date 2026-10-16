/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/param.h>

#include "theme.h"
#include "loader_prefs.h"
#include "ff_utf8.h"
#include "font.h"
#include "global.h"

#define THEME_MAX 16

static NinTheme themes[THEME_MAX];
static int theme_count = 0;
static int theme_index = 0;
static GRRLIB_texImg *bg_tex = NULL;

static u32 lerp_u8(u32 a, u32 b, int t, int tmax)
{
	if (tmax <= 0)
		return a;
	return (u32)(a + (int)(b - a) * t / tmax);
}

static u32 lerp_color(u32 a, u32 b, int t, int tmax)
{
	return RGBA(
		lerp_u8(R(a), R(b), t, tmax),
		lerp_u8(G(a), G(b), t, tmax),
		lerp_u8(B(a), B(b), t, tmax),
		lerp_u8(A(a), A(b), t, tmax));
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static u32 parse_color(const char *s, u32 fallback)
{
	int i;
	int n;
	u32 v = 0;

	while (*s == ' ' || *s == '#')
		s++;
	n = (int)strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\r'))
		n--;
	if (n != 6 && n != 8)
		return fallback;
	for (i = 0; i < n; i++) {
		int d = hex_nibble(s[i]);
		if (d < 0)
			return fallback;
		v = (v << 4) | (u32)d;
	}
	if (n == 6)
		v = (v << 8) | 0xFF;
	return v;
}

static void trim_inplace(char *s)
{
	char *end;
	char *start = s;

	while (*start == ' ' || *start == '\t' || *start == '\r')
		start++;
	if (start != s)
		memmove(s, start, strlen(start) + 1);
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
		end--;
		*end = 0;
	}
}

static int key_eq(const char *a, const char *b)
{
	while (*a && *b) {
		char ca = *a;
		char cb = *b;
		if (ca >= 'A' && ca <= 'Z')
			ca = (char)(ca + 32);
		if (cb >= 'A' && cb <= 'Z')
			cb = (char)(cb + 32);
		if (ca != cb)
			return 0;
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

static bool flag_on(const char *v)
{
	return key_eq(v, "on") || key_eq(v, "yes") || key_eq(v, "1") || key_eq(v, "true");
}

static void fill_gamecube(NinTheme *t)
{
	memset(t, 0, sizeof(*t));
	strcpy(t->id, "gamecube");
	strcpy(t->name, "GameCube");
	t->bg_top = 0x24143EFF;
	t->bg_mid = 0x7A68AEFF;
	t->bg_bottom = 0x3A2870FF;
	t->header = 0x000000FF;
	t->text = 0xFFFFFFFF;
	t->text_dim = 0xD4CCE4FF;
	t->accent = 0xF0D060FF;
	t->panel = 0x14082088;
	t->frame = 0xFFFFFFFF;
	t->placeholder = 0x4C3C84FF;
	t->classic = false;
	t->header_bar = true;
	t->wordmark = true;
}

static void fill_classic(NinTheme *t)
{
	memset(t, 0, sizeof(*t));
	strcpy(t->id, "classic");
	strcpy(t->name, "Classic");
	t->bg_top = 0xDEDFE0FF;
	t->bg_mid = 0xDEDFE0FF;
	t->bg_bottom = 0xDEDFE0FF;
	t->header = 0xDEDFE0FF;
	t->text = 0x000000FF;
	t->text_dim = 0x333333FF;
	t->accent = 0x000000FF;
	t->panel = 0xFFFFFFC0;
	t->frame = 0x000000FF;
	t->placeholder = 0xC8C8D0FF;
	t->classic = true;
	t->header_bar = false;
	t->wordmark = false;
}

static GRRLIB_texImg *load_png(const char *path)
{
	FIL fp;
	u32 sz;
	u8 *buf = NULL;
	UINT rd = 0;
	GRRLIB_texImg *tex = NULL;

	if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return NULL;
	sz = (u32)f_size(&fp);
	if (sz < 32 || sz > 1024 * 1024) {
		f_close(&fp);
		return NULL;
	}
	buf = (u8 *)memalign(32, sz);
	if (!buf) {
		f_close(&fp);
		return NULL;
	}
	if (f_read(&fp, buf, sz, &rd) != FR_OK || rd != sz) {
		free(buf);
		f_close(&fp);
		return NULL;
	}
	f_close(&fp);
	tex = GRRLIB_LoadTexture(buf);
	free(buf);
	if (!tex || !tex->data || tex->w == 0 || tex->h == 0 || tex->w > 1024 || tex->h > 1024) {
		if (tex)
			GRRLIB_FreeTexture(tex);
		return NULL;
	}
	return tex;
}

static void load_background(void)
{
	if (bg_tex) {
		GRRLIB_FreeTexture(bg_tex);
		bg_tex = NULL;
	}
	if (themes[theme_index].background[0])
		bg_tex = load_png(themes[theme_index].background);
}

static void apply_ini_line(NinTheme *t, const char *folder, char *line)
{
	char *eq;
	char key[32];
	char val[256];

	if (line[0] == '#' || line[0] == ';' || line[0] == 0)
		return;
	eq = strchr(line, '=');
	if (!eq)
		return;
	*eq = 0;
	strncpy(key, line, sizeof(key) - 1);
	key[sizeof(key) - 1] = 0;
	strncpy(val, eq + 1, sizeof(val) - 1);
	val[sizeof(val) - 1] = 0;
	trim_inplace(key);
	trim_inplace(val);

	if (key_eq(key, "name")) {
		strncpy(t->name, val, sizeof(t->name) - 1);
		t->name[sizeof(t->name) - 1] = 0;
	} else if (key_eq(key, "bg_top"))
		t->bg_top = parse_color(val, t->bg_top);
	else if (key_eq(key, "bg_mid"))
		t->bg_mid = parse_color(val, t->bg_mid);
	else if (key_eq(key, "bg_bottom"))
		t->bg_bottom = parse_color(val, t->bg_bottom);
	else if (key_eq(key, "header"))
		t->header = parse_color(val, t->header);
	else if (key_eq(key, "text"))
		t->text = parse_color(val, t->text);
	else if (key_eq(key, "text_dim"))
		t->text_dim = parse_color(val, t->text_dim);
	else if (key_eq(key, "accent"))
		t->accent = parse_color(val, t->accent);
	else if (key_eq(key, "panel"))
		t->panel = parse_color(val, t->panel);
	else if (key_eq(key, "frame") || key_eq(key, "cover_frame"))
		t->frame = parse_color(val, t->frame);
	else if (key_eq(key, "placeholder"))
		t->placeholder = parse_color(val, t->placeholder);
	else if (key_eq(key, "header_bar"))
		t->header_bar = flag_on(val);
	else if (key_eq(key, "wordmark"))
		t->wordmark = flag_on(val);
	else if (key_eq(key, "background") && val[0] && folder) {
		snprintf(t->background, sizeof(t->background), "%s%s", folder, val);
	}
}

static void load_ini(NinTheme *t, const char *folder, const char *ini_path)
{
	FIL fp;
	char buf[2048];
	UINT rd = 0;
	char *line;
	char *nl;

	if (f_open_char(&fp, ini_path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return;
	if (f_read(&fp, buf, sizeof(buf) - 1, &rd) != FR_OK)
		rd = 0;
	f_close(&fp);
	buf[rd] = 0;
	line = buf;
	while (line && *line) {
		nl = strchr(line, '\n');
		if (nl)
			*nl = 0;
		apply_ini_line(t, folder, line);
		if (!nl)
			break;
		line = nl + 1;
	}
}

static void scan_folders(void)
{
	char themes_dir[MAXPATHLEN];
	DIR dir;
	FILINFO info;

	if (!Prefs_Path(themes_dir, sizeof(themes_dir), "themes"))
		return;
	if (f_opendir_char(&dir, themes_dir) != FR_OK)
		return;

	while (theme_count < THEME_MAX && f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
		const char *name;
		char folder[MAXPATHLEN];
		char ini_path[MAXPATHLEN];
		NinTheme *t;

		if (!(info.fattrib & AM_DIR))
			continue;
		name = wchar_to_char(info.fname);
		if (!name || name[0] == '.' || name[0] == 0)
			continue;
		if (!Prefs_Path(folder, sizeof(folder), "themes/"))
			continue;
		if (strlen(folder) + strlen(name) + 2 >= sizeof(folder))
			continue;
		strcat(folder, name);
		strcat(folder, "/");
		snprintf(ini_path, sizeof(ini_path), "%stheme.ini", folder);
		t = &themes[theme_count];
		fill_gamecube(t);
		t->wordmark = false;
		strncpy(t->id, name, sizeof(t->id) - 1);
		t->id[sizeof(t->id) - 1] = 0;
		strncpy(t->name, name, sizeof(t->name) - 1);
		t->name[sizeof(t->name) - 1] = 0;
		t->background[0] = 0;
		load_ini(t, folder, ini_path);
		t->classic = false;
		theme_count++;
	}
	f_closedir(&dir);
}

bool Theme_IsReady(void)
{
	return theme_count > 0;
}

void Theme_Init(void)
{
	fill_gamecube(&themes[0]);
	fill_classic(&themes[1]);
	theme_count = 2;
	theme_index = 0;
	scan_folders();
	if (!Theme_SetByName(Prefs_GetTheme()))
		Theme_SetIndex(0);
}

void Theme_Shutdown(void)
{
	if (bg_tex) {
		GRRLIB_FreeTexture(bg_tex);
		bg_tex = NULL;
	}
}

const NinTheme *Theme_Current(void)
{
	if (theme_count <= 0)
		Theme_Init();
	return &themes[theme_index];
}

bool Theme_IsClassic(void)
{
	return Theme_Current()->classic;
}

u32 Theme_ColorText(void)
{
	return Theme_Current()->text;
}

int Theme_Count(void)
{
	if (theme_count <= 0)
		Theme_Init();
	return theme_count;
}

const NinTheme *Theme_At(int index)
{
	if (theme_count <= 0)
		Theme_Init();
	if (index < 0 || index >= theme_count)
		return &themes[0];
	return &themes[index];
}

int Theme_Index(void)
{
	return theme_index;
}

bool Theme_SetIndex(int index)
{
	if (theme_count <= 0)
		Theme_Init();
	if (index < 0 || index >= theme_count)
		return false;
	theme_index = index;
	load_background();
	return true;
}

bool Theme_SetByName(const char *id)
{
	int i;

	if (!id || !id[0])
		return false;
	if (theme_count <= 0)
		Theme_Init();
	for (i = 0; i < theme_count; i++) {
		if (key_eq(themes[i].id, id)) {
			Theme_SetIndex(i);
			return true;
		}
	}
	return false;
}

static void draw_wordmark(void)
{
	const char *top = "NINTENDO";
	const char *bot = "GAMECUBE";
	unsigned int tw;
	unsigned int bw;
	int x;

	tw = GRRLIB_WidthTTF(myFont, top, 12);
	bw = GRRLIB_WidthTTF(myFont, bot, 22);
	x = 320 - (int)((bw > tw ? bw : tw) / 2);
	PrintFormat(12, 0xFFFFFFFF, x + (int)((bw > tw ? (bw - tw) : 0) / 2), 2, "%s", top);
	PrintFormat(22, 0xFFFFFFFF, x, 16, "%s", bot);
}

static void paint_theme(bool with_header)
{
	const NinTheme *t = Theme_Current();
	int y;

	if (bg_tex) {
		GRRLIB_DrawImg(0, 0, bg_tex, 0, 640.0f / (float)bg_tex->w, 480.0f / (float)bg_tex->h, 0xFFFFFFFF);
	} else {
		for (y = 0; y < 480; y += 8) {
			u32 c;
			if (y < 240)
				c = lerp_color(t->bg_top, t->bg_mid, y, 240);
			else
				c = lerp_color(t->bg_mid, t->bg_bottom, y - 240, 240);
			GRRLIB_Rectangle(0, (f32)y, 640, 8, c, true);
		}
	}

	if (with_header && t->header_bar) {
		GRRLIB_Rectangle(0, 0, 640, 46, t->header, true);
		if (t->wordmark)
			draw_wordmark();
		else
			PrintFormat(20, t->text, 230, 10, "%s", t->name);
	}
}

void Theme_Paint(void)
{
	paint_theme(false);
}

void Theme_DrawBackground(void)
{
	if (!Theme_IsReady() || Theme_IsClassic()) {
		ClearScreen();
		return;
	}
	paint_theme(true);
}
