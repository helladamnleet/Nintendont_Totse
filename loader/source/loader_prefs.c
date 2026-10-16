/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 */
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "loader_prefs.h"
#include "ff_utf8.h"
#include "global.h"

extern char launch_dir[MAXPATHLEN];

static char base_dir[MAXPATHLEN];
static char theme_id[32];
static NinView view_mode = NIN_VIEW_FLOW;
static bool covers_on = true;
static NinArt art_style = NIN_ART_COVER;
static bool loaded = false;

static void copy_trim(char *dst, size_t dstlen, const char *src)
{
	size_t n;
	const char *end;

	while (*src == ' ' || *src == '\t' || *src == '\r')
		src++;
	end = src + strlen(src);
	while (end > src && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
		end--;
	n = (size_t)(end - src);
	if (n >= dstlen)
		n = dstlen - 1;
	memcpy(dst, src, n);
	dst[n] = 0;
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

static void set_base(void)
{
	size_t n;

	if (launch_dir[0] != 0)
		snprintf(base_dir, sizeof(base_dir), "%s", launch_dir);
	else
		snprintf(base_dir, sizeof(base_dir), "/apps/Nintendont/");

	n = strlen(base_dir);
	if (n > 0 && base_dir[n - 1] != '/' && base_dir[n - 1] != '\\') {
		if (n + 1 < sizeof(base_dir)) {
			base_dir[n] = '/';
			base_dir[n + 1] = 0;
		}
	}
}

const char *Prefs_BaseDir(void)
{
	if (!loaded)
		Prefs_Load();
	return base_dir;
}

bool Prefs_Path(char *out, size_t outlen, const char *rel)
{
	int n;

	if (!out || outlen == 0)
		return false;
	n = snprintf(out, outlen, "%s%s", Prefs_BaseDir(), rel ? rel : "");
	return n > 0 && (size_t)n < outlen;
}

static void apply_line(char *line)
{
	char *eq;
	char key[32];
	char val[64];

	if (line[0] == '#' || line[0] == ';' || line[0] == 0)
		return;
	eq = strchr(line, '=');
	if (!eq)
		return;
	*eq = 0;
	copy_trim(key, sizeof(key), line);
	copy_trim(val, sizeof(val), eq + 1);
	if (key_eq(key, "view")) {
		if (key_eq(val, "list"))
			view_mode = NIN_VIEW_LIST;
		else
			view_mode = NIN_VIEW_FLOW;
	} else if (key_eq(key, "theme")) {
		copy_trim(theme_id, sizeof(theme_id), val);
	} else if (key_eq(key, "covers")) {
		covers_on = key_eq(val, "on") || key_eq(val, "1") || key_eq(val, "yes");
	} else if (key_eq(key, "art")) {
		art_style = key_eq(val, "disc") ? NIN_ART_DISC : NIN_ART_COVER;
	}
}

void Prefs_Load(void)
{
	char path[MAXPATHLEN];
	FIL fp;
	char buf[1024];
	UINT rd = 0;
	char *line;
	char *nl;

	set_base();
	strcpy(theme_id, "gamecube");
	view_mode = NIN_VIEW_FLOW;
	covers_on = true;
	art_style = NIN_ART_COVER;
	loaded = true;

	if (!Prefs_Path(path, sizeof(path), "loader.ini"))
		return;
	if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
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
		apply_line(line);
		if (!nl)
			break;
		line = nl + 1;
	}
}

void Prefs_Save(void)
{
	char path[MAXPATHLEN];
	char body[256];
	FIL fp;
	UINT wrote = 0;
	int n;

	if (!loaded)
		Prefs_Load();
	n = snprintf(body, sizeof(body),
		"view=%s\r\ntheme=%s\r\ncovers=%s\r\nart=%s\r\n",
		view_mode == NIN_VIEW_LIST ? "list" : "flow",
		theme_id[0] ? theme_id : "gamecube",
		covers_on ? "on" : "off",
		art_style == NIN_ART_DISC ? "disc" : "cover");
	if (n <= 0)
		return;
	if (!Prefs_Path(path, sizeof(path), "loader.ini"))
		return;
	if (f_open_char(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
		return;
	f_write(&fp, body, (UINT)n, &wrote);
	f_close(&fp);
}

NinView Prefs_GetView(void)
{
	if (!loaded)
		Prefs_Load();
	return view_mode;
}

void Prefs_SetView(NinView view)
{
	if (!loaded)
		Prefs_Load();
	view_mode = view;
}

const char *Prefs_GetTheme(void)
{
	if (!loaded)
		Prefs_Load();
	return theme_id;
}

void Prefs_SetTheme(const char *id)
{
	if (!loaded)
		Prefs_Load();
	if (!id || !id[0])
		id = "gamecube";
	strncpy(theme_id, id, sizeof(theme_id) - 1);
	theme_id[sizeof(theme_id) - 1] = 0;
}

bool Prefs_GetCovers(void)
{
	if (!loaded)
		Prefs_Load();
	return covers_on;
}

void Prefs_SetCovers(bool on)
{
	if (!loaded)
		Prefs_Load();
	covers_on = on;
}

NinArt Prefs_GetArt(void)
{
	if (!loaded)
		Prefs_Load();
	return art_style;
}

void Prefs_SetArt(NinArt art)
{
	if (!loaded)
		Prefs_Load();
	art_style = art;
}
