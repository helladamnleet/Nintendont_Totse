/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 *
 * Add a row to kCoverPatterns to search another folder. Tokens:
 *   %I  6-character game id (GMSE01)
 *   %i  4-character game id (GMSE)
 *   %D  directory that holds the disc image, with a trailing slash
 *   %d  "sd" or "usb" (tried for both devices)
 * Patterns with %d are tried on the game device, then the other one.
 * Patterns with %D are skipped when the game has no directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <network.h>

#include "covers.h"
#include "theme.h"
#include "loader_prefs.h"
#include "ff_utf8.h"
#include "font.h"
#include "http.h"
#include "ssl.h"
#include "global.h"

#define COVER_SLOTS 8
#define COVER_MAX_EDGE 512
#define COVER_MAX_FILE (512 * 1024)

/*
 * Folders people already fill from USB Loader GX, WiiFlow, and CFG.
 * GameTDB is the download fallback, not a row in this table.
 */
static const char *const kCoverPatterns[] = {
	"%Dcover.png",
	"%D%I.png",
	"%D%i.png",
	"%d:/apps/Nintendont/covers/%I.png",
	"%d:/apps/nintendont/covers/%I.png",
	"%d:/wiiflow/boxcovers/%I.png",
	"%d:/wiiflow/covers/%I.png",
	"%d:/wiiflow/cart_disk/%I.png",
	"%d:/apps/usbloader_gx/images/%I.png",
	"%d:/apps/usbloader_gx/images/%i.png",
	"%d:/covers/%I.png",
	"%d:/usb-loader/covers/%I.png",
	NULL
};

typedef struct CoverSlot {
	char key[12];
	GRRLIB_texImg *tex;
	bool known_missing;
} CoverSlot;

static CoverSlot slots[COVER_SLOTS];
static int slot_clock = 0;
static char attempted[48][8];
static int attempted_count = 0;
static int net_state = 0; /* 0 unknown, 1 up, -1 failed */
static bool ssl_done = false;
static char cover_note[128];

const char *Covers_Note(void)
{
	return cover_note;
}

static uint next_pot(uint v)
{
	uint p = 1;
	if (v < 1)
		return 1;
	while (p < v)
		p <<= 1;
	return p;
}

/* The Wii only displays power-of-two textures. Pad and remember the real size. */
static GRRLIB_texImg *pad_pot(GRRLIB_texImg *src)
{
	uint nw;
	uint nh;
	uint x;
	uint y;
	GRRLIB_texImg *dst;

	if (!src)
		return NULL;
	nw = next_pot(src->w);
	nh = next_pot(src->h);
	if (nw == src->w && nh == src->h) {
		src->content_w = src->w;
		src->content_h = src->h;
		return src;
	}
	dst = GRRLIB_CreateEmptyTexture(nw, nh);
	if (!dst)
		return src;
	for (y = 0; y < src->h; y++) {
		for (x = 0; x < src->w; x++)
			GRRLIB_SetPixelTotexImg((int)x, (int)y, dst, GRRLIB_GetPixelFromtexImg((int)x, (int)y, src));
	}
	dst->content_w = src->w;
	dst->content_h = src->h;
	GRRLIB_FlushTex(dst);
	GRRLIB_FreeTexture(src);
	return dst;
}

static void id_strings(const gameinfo *game, char *id6, char *id4)
{
	int i;

	id6[0] = 0;
	id4[0] = 0;
	if (!game)
		return;
	for (i = 0; i < 6; i++) {
		char c = game->ID[i];
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 32);
		id6[i] = c;
	}
	id6[6] = 0;
	memcpy(id4, id6, 4);
	id4[4] = 0;
}

static bool id_fetchable(const char *id6)
{
	int i;

	if (!id6 || strlen(id6) < 6)
		return false;
	if (strncmp(id6, "DISC", 4) == 0)
		return false;
	for (i = 0; i < 6; i++) {
		char c = id6[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
			return false;
	}
	return true;
}

static void game_dir(const gameinfo *game, char *out, size_t outlen)
{
	const char *path;
	char tmp[256];
	char *slash;
	size_t n;
	u8 fmt;

	out[0] = 0;
	if (!game || !game->Path)
		return;
	path = game->Path;
	if (strncmp(path, "di:", 3) == 0)
		return;
	n = strlen(path);
	if (n == 0 || n >= sizeof(tmp))
		return;
	memcpy(tmp, path, n + 1);
	fmt = (u8)(game->Flags & GIFLAG_FORMAT_MASK);
	if (fmt == GIFLAG_FORMAT_FST) {
		if (tmp[n - 1] != '/') {
			if (n + 1 >= sizeof(tmp))
				return;
			tmp[n] = '/';
			tmp[n + 1] = 0;
		}
		strncpy(out, tmp, outlen - 1);
		out[outlen - 1] = 0;
		return;
	}
	if (n > 0 && (tmp[n - 1] == '/' || tmp[n - 1] == '\\'))
		tmp[--n] = 0;
	slash = strrchr(tmp, '/');
	if (!slash)
		return;
	slash[1] = 0;
	strncpy(out, tmp, outlen - 1);
	out[outlen - 1] = 0;
}

static bool expand_pattern(char *out, size_t outlen, const char *pat,
	const char *id6, const char *id4, const char *dir, const char *dev)
{
	size_t o = 0;
	const char *p;

	for (p = pat; *p; p++) {
		const char *ins = NULL;
		size_t n;
		if (*p == '%' && p[1]) {
			p++;
			if (*p == 'I')
				ins = id6;
			else if (*p == 'i')
				ins = id4;
			else if (*p == 'D')
				ins = dir;
			else if (*p == 'd')
				ins = dev;
			else
				continue;
			if (!ins)
				return false;
			if ((*p == 'D') && ins[0] == 0)
				return false;
			n = strlen(ins);
			if (o + n >= outlen)
				return false;
			memcpy(out + o, ins, n);
			o += n;
		} else {
			if (o + 1 >= outlen)
				return false;
			out[o++] = *p;
		}
	}
	out[o] = 0;
	return o > 0;
}

static bool file_exists(const char *path)
{
	FIL fp;
	if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;
	f_close(&fp);
	return true;
}

static GRRLIB_texImg *load_image(const char *path)
{
	FIL fp;
	u32 sz;
	u8 *buf;
	UINT rd = 0;
	GRRLIB_texImg *tex;

	if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return NULL;
	sz = (u32)f_size(&fp);
	if (sz < 32 || sz > COVER_MAX_FILE) {
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
	if (!tex || !tex->data || tex->w == 0 || tex->h == 0 ||
	    tex->w > COVER_MAX_EDGE || tex->h > COVER_MAX_EDGE) {
		if (tex)
			GRRLIB_FreeTexture(tex);
		return NULL;
	}
	return pad_pot(tex);
}

static void style_key(const char *id6, char *key, int keylen)
{
	snprintf(key, (size_t)keylen, "%s%c", id6, Prefs_GetArt() == NIN_ART_DISC ? 'd' : 'c');
}

static int find_slot(const char *key)
{
	int i;
	for (i = 0; i < COVER_SLOTS; i++) {
		if (slots[i].key[0] && strcmp(slots[i].key, key) == 0)
			return i;
	}
	return -1;
}

static int alloc_slot(const char *key)
{
	int i;
	int idx = -1;

	idx = find_slot(key);
	if (idx >= 0)
		return idx;
	for (i = 0; i < COVER_SLOTS; i++) {
		if (!slots[i].key[0]) {
			idx = i;
			break;
		}
	}
	if (idx < 0) {
		idx = slot_clock % COVER_SLOTS;
		slot_clock++;
		if (slots[idx].tex) {
			GRRLIB_FreeTexture(slots[idx].tex);
			slots[idx].tex = NULL;
		}
	}
	memset(&slots[idx], 0, sizeof(slots[idx]));
	strncpy(slots[idx].key, key, sizeof(slots[idx].key) - 1);
	slots[idx].key[sizeof(slots[idx].key) - 1] = 0;
	return idx;
}

static bool find_local_path(const gameinfo *game, char *out, size_t outlen)
{
	char id6[8];
	char id4[8];
	char dir[256];
	const char *devs[2];
	int d;
	int p;

	id_strings(game, id6, id4);
	if (!id6[0])
		return false;
	game_dir(game, dir, sizeof(dir));
	devs[0] = GetRootDevice();
	devs[1] = (UseSD ? "usb" : "sd");

	for (p = 0; kCoverPatterns[p] != NULL; p++) {
		int dev_tries = (strchr(kCoverPatterns[p], '%') && strstr(kCoverPatterns[p], "%d")) ? 2 : 1;
		int t;
		if (strchr(kCoverPatterns[p], 'D') && strstr(kCoverPatterns[p], "%D") && dir[0] == 0)
			continue;
		for (t = 0; t < dev_tries; t++) {
			d = t;
			if (!expand_pattern(out, outlen, kCoverPatterns[p], id6, id4, dir, devs[d]))
				continue;
			if (file_exists(out))
				return true;
		}
	}

	/* Cache written by Covers_Fetch, which may not match the patterns above
	   when Nintendont was launched from a path other than /apps/Nintendont. */
	if (Prefs_Path(out, outlen, "covers/")) {
		size_t n = strlen(out);
		if (n + 10 < outlen) {
			strcat(out, id6);
			strcat(out, ".png");
			if (file_exists(out))
				return true;
		}
	}
	return false;
}

void Covers_Shutdown(void)
{
	Covers_Invalidate();
}

void Covers_Invalidate(void)
{
	int i;
	for (i = 0; i < COVER_SLOTS; i++) {
		if (slots[i].tex)
			GRRLIB_FreeTexture(slots[i].tex);
		memset(&slots[i], 0, sizeof(slots[i]));
	}
}

static bool cached_art(const char *folder, const char *id6, char *out, size_t outlen)
{
	char rel[40];

	snprintf(rel, sizeof(rel), "%s%s.png", folder, id6);
	if (!Prefs_Path(out, outlen, rel))
		return false;
	return file_exists(out);
}

static bool device_art(const char *id6, NinArt art, char *out, size_t outlen)
{
	const char *folder = (art == NIN_ART_DISC) ? "discs" : "covers";
	const char *other;

	snprintf(out, outlen, "%s:/%s/%s.png", GetRootDevice(), folder, id6);
	if (file_exists(out))
		return true;
	other = UseSD ? "usb" : "sd";
	snprintf(out, outlen, "%s:/%s/%s.png", other, folder, id6);
	return file_exists(out);
}

static bool find_art_file(const gameinfo *game, const char *id6, NinArt art, char *out, size_t outlen)
{
	if (device_art(id6, art, out, outlen))
		return true;
	if (art == NIN_ART_DISC)
		return cached_art("discs/", id6, out, outlen);
	if (find_local_path(game, out, outlen))
		return true;
	return cached_art("covers/", id6, out, outlen);
}

GRRLIB_texImg *Covers_Get(const gameinfo *game)
{
	char id6[8];
	char id4[8];
	char key[12];
	char path[256];
	int idx;
	GRRLIB_texImg *tex;
	NinArt art;
	NinArt other;

	id_strings(game, id6, id4);
	if (!id6[0])
		return NULL;
	art = Prefs_GetArt();
	other = (art == NIN_ART_DISC) ? NIN_ART_COVER : NIN_ART_DISC;
	style_key(id6, key, (int)sizeof(key));
	idx = find_slot(key);
	if (idx >= 0) {
		if (slots[idx].tex)
			return slots[idx].tex;
		if (slots[idx].known_missing)
			return NULL;
	}
	if (!find_art_file(game, id6, art, path, sizeof(path)) &&
	    !find_art_file(game, id6, other, path, sizeof(path))) {
		idx = alloc_slot(key);
		slots[idx].known_missing = true;
		return NULL;
	}
	tex = load_image(path);
	idx = alloc_slot(key);
	if (!tex) {
		slots[idx].known_missing = true;
		return NULL;
	}
	slots[idx].tex = tex;
	slots[idx].known_missing = false;
	return tex;
}

static bool was_attempted(const char *id6)
{
	int i;
	for (i = 0; i < attempted_count; i++) {
		if (strcmp(attempted[i], id6) == 0)
			return true;
	}
	return false;
}

static void mark_attempted(const char *id6)
{
	if (was_attempted(id6))
		return;
	if (attempted_count >= 48)
		return;
	strncpy(attempted[attempted_count], id6, 7);
	attempted[attempted_count][7] = 0;
	attempted_count++;
}

bool Covers_ShouldFetch(const gameinfo *game)
{
	char id6[8];
	char id4[8];
	char path[256];

	if (!Prefs_GetCovers())
		return false;
	id_strings(game, id6, id4);
	if (!id_fetchable(id6))
		return false;
	if (was_attempted(id6))
		return false;
	/* Fetch when the picture the user asked for is not on disk yet.
	   A box cover on screen should not block the disc download. */
	if (find_art_file(game, id6, Prefs_GetArt(), path, sizeof(path)))
		return false;
	return true;
}

static void region_list(char region, const char **out, int *count)
{
	static const char *us[] = { "US", "EN", "JA" };
	static const char *ja[] = { "JA", "US", "EN" };
	static const char *pal[] = { "EN", "FR", "DE", "ES", "IT", "NL", "AU", "US" };
	static const char *de[] = { "DE", "EN", "US" };
	static const char *fr[] = { "FR", "EN", "US" };
	static const char *es[] = { "ES", "EN", "US" };
	static const char *it[] = { "IT", "EN", "US" };
	static const char *ko[] = { "KO", "JA", "US" };
	const char **src = us;
	int n = 3;
	int i;

	if (region == 'J') {
		src = ja;
		n = 3;
	} else if (region == 'P') {
		src = pal;
		n = 8;
	} else if (region == 'D') {
		src = de;
	} else if (region == 'F') {
		src = fr;
	} else if (region == 'S') {
		src = es;
	} else if (region == 'I') {
		src = it;
	} else if (region == 'K') {
		src = ko;
	}
	for (i = 0; i < n; i++)
		out[i] = src[i];
	*count = n;
}

static bool net_up(void)
{
	s32 rc;

	if (net_state == 1)
		return true;
	if (net_state < 0)
		return false;
	rc = net_init();
	if (rc < 0) {
		net_state = -1;
		return false;
	}
	net_state = 1;
	if (!ssl_done) {
		ssl_init();
		ssl_done = true;
	}
	return true;
}

static bool download_url(const char *url, u8 **data, unsigned int *len)
{
	unsigned int status = 0;
	bool ok;

	*data = NULL;
	*len = 0;
	ok = http_request(url, COVER_MAX_FILE);
	http_get_result(&status, data, len);
	if (!ok || !(*data) || *len < 8) {
		if (*data)
			free(*data);
		*data = NULL;
		*len = 0;
		return false;
	}
	if ((*data)[0] != 0x89 || (*data)[1] != 'P' || (*data)[2] != 'N' || (*data)[3] != 'G') {
		free(*data);
		*data = NULL;
		*len = 0;
		return false;
	}
	return true;
}

static bool save_cache(const char *folder, const char *id6, const u8 *data, unsigned int len)
{
	char dir[64];
	char path[96];
	FIL fp;
	UINT wrote = 0;

	snprintf(dir, sizeof(dir), "%s:/%s", GetRootDevice(), folder);
	f_mkdir_char(dir);
	snprintf(path, sizeof(path), "%s:/%s/%s.png", GetRootDevice(), folder, id6);
	if (f_open_char(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		snprintf(cover_note, sizeof(cover_note), "Couldn't save %s", path);
		return false;
	}
	f_write(&fp, data, len, &wrote);
	f_close(&fp);
	if (wrote != len) {
		snprintf(cover_note, sizeof(cover_note), "Short write %s", path);
		return false;
	}
	snprintf(cover_note, sizeof(cover_note), "Saved %s", path);
	return true;
}

static bool fetch_kind(const char *id6, const char *kind, const char *folder)
{
	const char *regions[8];
	int nregions = 0;
	int i;
	char url[160];
	u8 *data = NULL;
	unsigned int len = 0;

	region_list(id6[3], regions, &nregions);
	for (i = 0; i < nregions; i++) {
		int scheme;
		/* GameTDB keeps GameCube box and disc art on the wii path.
		   Plain HTTP works. The loader's HTTPS handshake does not. */
		static const char *schemes[] = { "http", "https" };
		for (scheme = 0; scheme < 2; scheme++) {
			snprintf(url, sizeof(url),
				"%s://art.gametdb.com/wii/%s/%s/%s.png",
				schemes[scheme], kind, regions[i], id6);
			if (!download_url(url, &data, &len))
				continue;
			if (!save_cache(folder, id6, data, len)) {
				free(data);
				return false;
			}
			free(data);
			return true;
		}
	}
	return false;
}

bool Covers_Fetch(const gameinfo *game)
{
	char id6[8];
	char id4[8];

	id_strings(game, id6, id4);
	if (!id_fetchable(id6))
		return false;
	if (!net_up()) {
		snprintf(cover_note, sizeof(cover_note), "No network. Art goes in %s:/covers", GetRootDevice());
		return false;
	}
	{
		char dir[32];
		snprintf(dir, sizeof(dir), "%s:/covers", GetRootDevice());
		f_mkdir_char(dir);
		snprintf(dir, sizeof(dir), "%s:/discs", GetRootDevice());
		f_mkdir_char(dir);
	}
	mark_attempted(id6);

	/* Box and disc land on the game device, in /covers and /discs. */
	{
		bool got_cover = fetch_kind(id6, "cover", "covers");
		bool got_disc = fetch_kind(id6, "disc", "discs");
		if (!got_cover && !got_disc) {
			const char *regions[8];
			int nregions = 0;
			const char *kind = (Prefs_GetArt() == NIN_ART_DISC) ? "disc" : "cover";
			region_list(id6[3], regions, &nregions);
			snprintf(cover_note, sizeof(cover_note),
				"No art: http://art.gametdb.com/wii/%s/%s/%s.png",
				kind, nregions ? regions[0] : "US", id6);
		}
	}
	Covers_Invalidate();
	return Covers_Get(game) != NULL;
}

static void fit_label(char *dst, int dstlen, const char *src, int max_px)
{
	int n;

	if (!src)
		src = "";
	strncpy(dst, src, (size_t)dstlen - 1);
	dst[dstlen - 1] = 0;
	n = (int)strlen(dst);
	while (n > 1 && (int)GRRLIB_WidthTTF(myFont, dst, 12) > max_px) {
		dst[n - 1] = 0;
		n--;
	}
}

void Covers_Draw(const gameinfo *game, float x, float y, float w, float h, bool selected)
{
	const NinTheme *theme = Theme_Current();
	GRRLIB_texImg *tex;
	char label[48];
	const char *name;

	(void)selected;
	tex = Covers_Get(game);
	if (tex && tex->w > 0 && tex->h > 0) {
		uint cw = tex->content_w ? tex->content_w : tex->w;
		uint ch = tex->content_h ? tex->content_h : tex->h;
		float sx = w / (float)cw;
		float sy = h / (float)ch;
		float s = (sx < sy) ? sx : sy;
		float dw = (float)cw * s;
		float dh = (float)ch * s;
		GRRLIB_DrawImg(x + (w - dw) / 2.0f, y + (h - dh) / 2.0f, tex, 0, s, s, 0xFFFFFFFF);
	} else {
		GRRLIB_Rectangle(x, y, w, h, theme->placeholder, true);
		name = (game && game->Name) ? game->Name : "???";
		fit_label(label, (int)sizeof(label), name, (int)w - 8);
		PrintFormat(12, theme->text, (int)x + 4, (int)y + (int)h / 2 - 8, "%s", label);
	}
}
