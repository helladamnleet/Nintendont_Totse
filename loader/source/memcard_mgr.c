/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 *
 * Layout matches the GameCube BIOS memory card page: dark grid, slot
 * letter, "Open" plus the free-block count, a 3x4 tray of save icons,
 * a panel on the right, and the save comment on the bar at the bottom.
 * Move the MC_* coordinates to restyle it. The file format code below
 * the drawing does not depend on them.
 *
 * Files in /saves: ninmem.raw (shared USA/PAL), ninmemj.raw (shared
 * Japan), or a 4-letter game id for a per-game card.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "memcard_mgr.h"
#include "MemCard.h"
#include "font.h"
#include "FPad.h"
#include "ff_utf8.h"
#include "global.h"
#include <ogc/card.h>

#define MC_COLS 3
#define MC_ROWS 4
#define MC_PAGE (MC_COLS * MC_ROWS)
#define MC_MAX_CARDS 48
#define MC_MAX_SAVES 127
#define MC_BLOCK 0x2000

#define MC_GRID_X 36
#define MC_GRID_Y 96
#define MC_CELL_W 74
#define MC_CELL_H 60
#define MC_GAP_X 8
#define MC_GAP_Y 8
#define MC_PANEL_X 300
#define MC_PANEL_Y 92
#define MC_PANEL_W 308
#define MC_PANEL_H 246
#define MC_BAR_Y 356
#define MC_BAR_H 72

enum {
	MC_BROWSE = 0,
	MC_CONFIRM_SAVE,
	MC_TASKS,
	MC_CONFIRM_CARD,
	MC_CREATE
};

enum {
	MC_KIND_FILE = 0,
	MC_KIND_REAL = 1
};

typedef struct McCard {
	char name[64];
	char path[160];
	u32 bytes;
	u16 total;
	u16 freeb;
	u8 kind;
	s8 slot;
	bool parsed;
} McCard;

typedef struct McSave {
	char name[33];
	char rawname[33];
	char game[5];
	u16 blocks;
	u16 index;
	u16 first;
	u16 icon_fmt;
	u8 banner_fmt;
	u32 icon_addr;
	u32 comment_addr;
} McSave;

static McCard cards[MC_MAX_CARDS];
static int card_count = 0;
static McSave saves[MC_MAX_SAVES];
static int save_count = 0;
static GRRLIB_texImg *icons[MC_PAGE];
static GRRLIB_texImg *banner_tex = NULL;
static char comment_text[80];
static char status_line[80];
static const gameinfo *vmc_game = NULL;
static char vmc_slot_a[32];
static char vmc_slot_b[32];
static bool vmc_dirty = false;

static bool game_has_id(const gameinfo *game)
{
	if (!game || !game->ID[0])
		return false;
	if (game->Path && strncmp(game->Path, "di:", 3) == 0)
		return false;
	return true;
}

/* Japan and Korea share ninmemj.raw. USA and PAL share ninmem.raw. */
static bool game_uses_japan_card(const gameinfo *game)
{
	u32 region;

	if (!game)
		return false;
	region = (game->Flags & GIFLAG_REGION_MASK) >> 3;
	return region == BI2_REGION_JAPAN;
}

static void refresh_vmc_names(void)
{
	char id4[5];

	vmc_slot_a[0] = 0;
	vmc_slot_b[0] = 0;
	if (!game_has_id(vmc_game))
		return;
	memcpy(id4, vmc_game->ID, 4);
	id4[4] = 0;
	if (ncfg->Config & NIN_CFG_MC_MULTI) {
		if (game_uses_japan_card(vmc_game)) {
			strcpy(vmc_slot_a, "ninmemj.raw");
			strcpy(vmc_slot_b, "ninmemjb.raw");
		} else {
			strcpy(vmc_slot_a, "ninmem.raw");
			strcpy(vmc_slot_b, "ninmemb.raw");
		}
	} else {
		snprintf(vmc_slot_a, sizeof(vmc_slot_a), "%s.raw", id4);
		snprintf(vmc_slot_b, sizeof(vmc_slot_b), "%s_B.raw", id4);
	}
}

static bool real_slot_present(void)
{
	int i;

	for (i = 0; i < card_count; i++) {
		if (cards[i].kind == MC_KIND_REAL)
			return true;
	}
	return false;
}

static const char *vmc_scope_text(void)
{
	if (!(ncfg->Config & NIN_CFG_MEMCARDEMU) && real_slot_present())
		return "the card in the slot";
	if (ncfg->Config & NIN_CFG_MC_MULTI) {
		if (game_uses_japan_card(vmc_game))
			return "every Japan game";
		return "every USA/PAL game";
	}
	return "only this game";
}

static int task_count(void)
{
	return game_has_id(vmc_game) ? 4 : 3;
}

bool MemCard_TakeSettings(void)
{
	bool dirty = vmc_dirty;
	vmc_dirty = false;
	return dirty;
}

static u16 rd16(const u8 *p)
{
	return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32 rd32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static void wr16(u8 *p, u16 v)
{
	p[0] = (u8)(v >> 8);
	p[1] = (u8)v;
}

static u32 rgb5a3(u16 p)
{
	u8 r, g, b, a;

	if (p & 0x8000) {
		r = (u8)(((p >> 10) & 0x1F) * 255 / 31);
		g = (u8)(((p >> 5) & 0x1F) * 255 / 31);
		b = (u8)((p & 0x1F) * 255 / 31);
		a = 255;
	} else {
		a = (u8)(((p >> 12) & 7) * 255 / 7);
		r = (u8)(((p >> 8) & 0xF) * 255 / 15);
		g = (u8)(((p >> 4) & 0xF) * 255 / 15);
		b = (u8)((p & 0xF) * 255 / 15);
	}
	return RGBA(r, g, b, a);
}

/* Same sum the card formatter in MemCard.c uses. */
static void do_checksum(const u16 *buffer, u32 size, u16 *c1, u16 *c2)
{
	u32 i;

	*c1 = 0;
	*c2 = 0;
	for (i = size / sizeof(u16); i > 0; i--, buffer++) {
		*c1 = (u16)(*c1 + *buffer);
		*c2 = (u16)(*c2 + (*buffer ^ 0xFFFF));
	}
	if (*c1 == 0xFFFF)
		*c1 = 0;
	if (*c2 == 0xFFFF)
		*c2 = 0;
}

static bool checksum_ok(const u8 *block, u32 sum_off, u32 sum_len, u32 csum_at)
{
	u16 c1, c2, s1, s2;

	do_checksum((const u16 *)(block + sum_off), sum_len, &c1, &c2);
	s1 = rd16(block + csum_at);
	s2 = rd16(block + csum_at + 2);
	return s1 == c1 && s2 == c2;
}

static void write_checksum(u8 *block, u32 sum_off, u32 sum_len, u32 csum_at)
{
	u16 c1, c2;

	do_checksum((const u16 *)(block + sum_off), sum_len, &c1, &c2);
	wr16(block + csum_at, c1);
	wr16(block + csum_at + 2, c2);
}

static int pick_copy(const u8 *a, const u8 *b, u32 counter_off, u32 sum_off, u32 sum_len, u32 csum_at)
{
	bool ok0 = checksum_ok(a, sum_off, sum_len, csum_at);
	bool ok1 = checksum_ok(b, sum_off, sum_len, csum_at);
	u16 c0, c1, d;

	if (ok0 && !ok1)
		return 0;
	if (!ok0 && ok1)
		return 1;
	if (!ok0 && !ok1)
		return -1;
	c0 = rd16(a + counter_off);
	c1 = rd16(b + counter_off);
	d = (u16)(c0 - c1);
	if (d == 0 || d < 0x8000)
		return 0;
	return 1;
}

static void copy_printable(char *dst, int dstlen, const u8 *src, int srclen)
{
	int i;
	int o = 0;

	for (i = 0; i < srclen && o < dstlen - 1; i++) {
		u8 c = src[i];
		if (c == 0)
			break;
		if (c < 0x20 || c > 0x7E)
			continue;
		dst[o++] = (char)c;
	}
	dst[o] = 0;
}

static bool ends_with_raw(const char *name)
{
	size_t n;

	n = strlen(name);
	if (n < 4)
		return false;
	return name[n - 4] == '.' &&
		(name[n - 3] == 'r' || name[n - 3] == 'R') &&
		(name[n - 2] == 'a' || name[n - 2] == 'A') &&
		(name[n - 1] == 'w' || name[n - 1] == 'W');
}

static int name_eq(const char *a, const char *b)
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

static void free_icons(void)
{
	int i;

	for (i = 0; i < MC_PAGE; i++) {
		if (icons[i]) {
			GRRLIB_FreeTexture(icons[i]);
			icons[i] = NULL;
		}
	}
	if (banner_tex) {
		GRRLIB_FreeTexture(banner_tex);
		banner_tex = NULL;
	}
}

static GRRLIB_texImg *tex_rgb5a3(const u8 *src, u32 avail, int w, int h)
{
	GRRLIB_texImg *tex;
	int x, y, i;

	if ((u32)(w * h * 2) > avail || (w & 3) || (h & 3))
		return NULL;
	tex = GRRLIB_CreateEmptyTexture((uint)w, (uint)h);
	if (!tex || !tex->data) {
		if (tex)
			GRRLIB_FreeTexture(tex);
		return NULL;
	}
	i = 0;
	for (y = 0; y < h; y += 4) {
		for (x = 0; x < w; x += 4) {
			int ty;
			int tx;
			for (ty = 0; ty < 4; ty++) {
				for (tx = 0; tx < 4; tx++) {
					GRRLIB_SetPixelTotexImg(x + tx, y + ty, tex, rgb5a3(rd16(src + i)));
					i += 2;
				}
			}
		}
	}
	GRRLIB_FlushTex(tex);
	return tex;
}

static GRRLIB_texImg *tex_ci8(const u8 *idx, const u8 *pal, u32 avail, int w, int h)
{
	GRRLIB_texImg *tex;
	int x, y, i;

	if ((u32)(w * h) + 512 > avail || (w & 7) || (h & 3))
		return NULL;
	tex = GRRLIB_CreateEmptyTexture((uint)w, (uint)h);
	if (!tex || !tex->data) {
		if (tex)
			GRRLIB_FreeTexture(tex);
		return NULL;
	}
	/* CI8 indices are 8x4 tiles. The palette after them is linear RGB5A3. */
	i = 0;
	for (y = 0; y < h; y += 4) {
		for (x = 0; x < w; x += 8) {
			int ty;
			int tx;
			for (ty = 0; ty < 4; ty++) {
				for (tx = 0; tx < 8; tx++) {
					u8 pi = idx[i++];
					GRRLIB_SetPixelTotexImg(x + tx, y + ty, tex, rgb5a3(rd16(pal + pi * 2)));
				}
			}
		}
	}
	GRRLIB_FlushTex(tex);
	return tex;
}

static bool read_block(FIL *fp, u16 block, u8 *dst)
{
	UINT rd = 0;

	if (f_lseek(fp, (FSIZE_t)block * MC_BLOCK) != FR_OK)
		return false;
	if (f_read(fp, dst, MC_BLOCK, &rd) != FR_OK || rd != MC_BLOCK)
		return false;
	return true;
}

static bool write_block(FIL *fp, u16 block, const u8 *src)
{
	UINT wrote = 0;

	if (f_lseek(fp, (FSIZE_t)block * MC_BLOCK) != FR_OK)
		return false;
	if (f_write(fp, src, MC_BLOCK, &wrote) != FR_OK || wrote != MC_BLOCK)
		return false;
	return true;
}

/*
 * Physical slots are optional. Slot A/B are listed only when a card is
 * actually inserted. With both slots empty, the browser is just the
 * virtual cards in /saves (ninmem.raw, ninmemj.raw, or a per-game raw).
 */
static u8 *real_work = NULL;
static s32 real_mounted = -1;
static bool real_inited = false;

static void real_unmount(void)
{
	if (real_mounted >= 0) {
		CARD_Unmount(real_mounted);
		real_mounted = -1;
	}
}

static void show_wait(const char *msg)
{
	GRRLIB_Rectangle(0, 0, 640, 480, 0x07070CFF, true);
	PrintFormat(18, 0xFFFFFFFF, 150, 220, "%s", msg);
	GRRLIB_Render();
	ClearScreen();
}

static bool real_mount(s32 chn)
{
	s32 res;

	if (IsWiiU())
		return false;
	if (!real_inited) {
		CARD_Init(NULL, NULL);
		real_inited = true;
	}
	if (!real_work) {
		real_work = (u8 *)memalign(32, CARD_WORKAREA);
		if (!real_work)
			return false;
	}
	if (real_mounted == chn)
		return true;
	real_unmount();
	res = CARD_Mount(chn, real_work, NULL);
	if (res < 0)
		return false;
	real_mounted = chn;
	return true;
}

static bool slot_has_card(s32 chn)
{
	s32 res;

	if (IsWiiU())
		return false;
	if (!real_inited) {
		CARD_Init(NULL, NULL);
		real_inited = true;
	}
	res = CARD_ProbeEx(chn, NULL, NULL);
	return res == CARD_ERROR_READY || res == CARD_ERROR_UNLOCKED;
}

static void fill_save_from_stat(McSave *sv, const card_dir *dir, const card_stat *st, bool have_stat)
{
	memset(sv, 0, sizeof(*sv));
	memcpy(sv->rawname, dir->filename, 32);
	sv->rawname[32] = 0;
	copy_printable(sv->name, 33, dir->filename, 32);
	copy_printable(sv->game, 5, dir->gamecode, 4);
	sv->blocks = (u16)(dir->filelen / MC_BLOCK);
	if (sv->blocks == 0 && dir->filelen)
		sv->blocks = 1;
	sv->index = (u16)dir->fileno;
	if (have_stat) {
		sv->banner_fmt = st->banner_fmt;
		sv->icon_addr = st->icon_addr;
		sv->icon_fmt = st->icon_fmt;
		sv->comment_addr = st->comment_addr;
	}
	if (sv->name[0] == 0)
		snprintf(sv->name, sizeof(sv->name), "%s", sv->game[0] ? sv->game : "save");
}

static bool load_real_card(McCard *card)
{
	card_dir dir;
	s32 res;
	u32 total = 0;
	u32 used = 0;
	char wait[32];

	save_count = 0;
	comment_text[0] = 0;
	card->parsed = false;
	card->freeb = 0;
	card->total = 0;
	snprintf(wait, sizeof(wait), "Reading Slot %c...", 'A' + card->slot);
	show_wait(wait);
	if (!slot_has_card(card->slot)) {
		snprintf(status_line, sizeof(status_line), "No card in Slot %c", 'A' + card->slot);
		real_unmount();
		return false;
	}
	if (!real_mount(card->slot)) {
		snprintf(status_line, sizeof(status_line), "Couldn't mount Slot %c", 'A' + card->slot);
		return false;
	}
	CARD_GetBlockCount(card->slot, &total);
	res = CARD_FindFirst(card->slot, &dir, true);
	while (res == CARD_ERROR_READY && save_count < MC_MAX_SAVES) {
		card_stat st;
		bool have = false;

		memset(&st, 0, sizeof(st));
		if (CARD_GetStatus(card->slot, (s32)dir.fileno, &st) == CARD_ERROR_READY)
			have = true;
		fill_save_from_stat(&saves[save_count], &dir, &st, have);
		used += saves[save_count].blocks;
		save_count++;
		res = CARD_FindNext(&dir);
	}
	if (total == 64 || total == 128 || total == 256 || total == 512 ||
	    total == 1024 || total == 2048) {
		if (total > 5)
			total -= 5;
	}
	if (total > 65535)
		total = 65535;
	card->total = (u16)total;
	card->freeb = (total >= used) ? (u16)(total - used) : 0;
	card->parsed = true;
	return true;
}

static void decode_block(const u8 *block, const McSave *sv, int icon_slot, bool want_banner)
{
	u8 fmt;

	if (icon_slot >= 0 && icon_slot < MC_PAGE) {
		fmt = (u8)(sv->icon_fmt & 3);
		if (fmt == 2 && sv->icon_addr + 0x800 <= MC_BLOCK)
			icons[icon_slot] = tex_rgb5a3(block + sv->icon_addr, MC_BLOCK - sv->icon_addr, 32, 32);
		else if (fmt == 1 && sv->icon_addr + 0x600 <= MC_BLOCK)
			icons[icon_slot] = tex_ci8(block + sv->icon_addr, block + sv->icon_addr + 0x400,
				MC_BLOCK - sv->icon_addr, 32, 32);
	}
	if (!want_banner)
		return;
	fmt = (u8)(sv->banner_fmt & 3);
	if (fmt == 2)
		banner_tex = tex_rgb5a3(block, MC_BLOCK, 96, 32);
	else if (fmt == 1)
		banner_tex = tex_ci8(block, block + 0xC00, MC_BLOCK, 96, 32);
	if (sv->comment_addr + 64 <= MC_BLOCK) {
		char line_a[33];
		char line_b[33];
		copy_printable(line_a, 33, block + sv->comment_addr, 32);
		copy_printable(line_b, 33, block + sv->comment_addr + 32, 32);
		if (line_a[0] && line_b[0])
			snprintf(comment_text, sizeof(comment_text), "%s  %s", line_a, line_b);
		else
			snprintf(comment_text, sizeof(comment_text), "%s%s", line_a, line_b);
	}
}

static bool read_real_file(s32 chn, const char *filename, u8 *block)
{
	card_file file;
	s32 res;

	if (!filename || !filename[0])
		return false;
	res = CARD_Open(chn, filename, &file);
	if (res != CARD_ERROR_READY)
		return false;
	res = CARD_Read(&file, block, MC_BLOCK, 0);
	CARD_Close(&file);
	return res == CARD_ERROR_READY;
}

static void load_visuals_real(const McCard *card, int page, int cursor)
{
	u8 *block;
	int i;
	int selected;

	free_icons();
	comment_text[0] = 0;
	if (!card || !card->parsed || real_mounted != card->slot)
		return;
	block = (u8 *)memalign(32, MC_BLOCK);
	if (!block)
		return;
	for (i = 0; i < MC_PAGE; i++) {
		int si = page * MC_PAGE + i;
		if (si >= save_count)
			break;
		if (saves[si].blocks == 0)
			continue;
		if (!read_real_file(card->slot, saves[si].rawname, block))
			continue;
		decode_block(block, &saves[si], i, false);
	}
	selected = page * MC_PAGE + cursor;
	if (selected >= 0 && selected < save_count &&
	    read_real_file(card->slot, saves[selected].rawname, block))
		decode_block(block, &saves[selected], -1, true);
	free(block);
}

static bool delete_real_save(const McCard *card, int save_index)
{
	s32 res;

	if (!card || save_index < 0 || save_index >= save_count)
		return false;
	if (!saves[save_index].rawname[0])
		return false;
	if (!real_mount(card->slot))
		return false;
	res = CARD_Delete(card->slot, saves[save_index].rawname);
	return res == CARD_ERROR_READY;
}

static void add_real_slots(void)
{
	McCard found[2];
	int n = 0;
	int i;

	if (IsWiiU())
		return;
	memset(found, 0, sizeof(found));
	for (i = 0; i < 2; i++) {
		if (!slot_has_card((s32)i))
			continue;
		found[n].kind = MC_KIND_REAL;
		found[n].slot = (s8)i;
		snprintf(found[n].name, sizeof(found[n].name), "Slot %c", 'A' + i);
		n++;
	}
	if (n == 0 || card_count + n > MC_MAX_CARDS)
		return;
	memmove(&cards[n], &cards[0], (size_t)card_count * sizeof(McCard));
	memcpy(&cards[0], found, (size_t)n * sizeof(McCard));
	card_count += n;
}

static void load_visuals(const McCard *card, int page, int cursor)
{
	FIL fp;
	u8 *block = NULL;
	int i;
	int selected;

	free_icons();
	comment_text[0] = 0;
	if (!card || !card->parsed)
		return;
	if (card->kind == MC_KIND_REAL) {
		load_visuals_real(card, page, cursor);
		return;
	}
	if (f_open_char(&fp, card->path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return;
	block = (u8 *)memalign(32, MC_BLOCK);
	if (!block) {
		f_close(&fp);
		return;
	}

	for (i = 0; i < MC_PAGE; i++) {
		int si = page * MC_PAGE + i;
		McSave *sv;
		u8 fmt;

		if (si >= save_count)
			break;
		sv = &saves[si];
		if (sv->first < 5 || !read_block(&fp, sv->first, block))
			continue;
		fmt = (u8)(sv->icon_fmt & 3);
		if (fmt == 2 && sv->icon_addr + 0x800 <= MC_BLOCK)
			icons[i] = tex_rgb5a3(block + sv->icon_addr, MC_BLOCK - sv->icon_addr, 32, 32);
		else if (fmt == 1 && sv->icon_addr + 0x600 <= MC_BLOCK)
			icons[i] = tex_ci8(block + sv->icon_addr, block + sv->icon_addr + 0x400,
				MC_BLOCK - sv->icon_addr, 32, 32);
	}

	selected = page * MC_PAGE + cursor;
	if (selected >= 0 && selected < save_count && saves[selected].first >= 5 &&
	    read_block(&fp, saves[selected].first, block)) {
		McSave *sv = &saves[selected];
		u8 bfmt = (u8)(sv->banner_fmt & 3);
		char line_a[33];
		char line_b[33];

		if (bfmt == 2)
			banner_tex = tex_rgb5a3(block, MC_BLOCK, 96, 32);
		else if (bfmt == 1 && 0xE00 <= MC_BLOCK)
			banner_tex = tex_ci8(block, block + 0xC00, MC_BLOCK, 96, 32);
		if (sv->comment_addr + 64 <= MC_BLOCK) {
			copy_printable(line_a, 33, block + sv->comment_addr, 32);
			copy_printable(line_b, 33, block + sv->comment_addr + 32, 32);
			if (line_a[0] && line_b[0])
				snprintf(comment_text, sizeof(comment_text), "%s  %s", line_a, line_b);
			else
				snprintf(comment_text, sizeof(comment_text), "%s%s", line_a, line_b);
		}
	}
	free(block);
	f_close(&fp);
}

static void parse_dir(const u8 *dir)
{
	int i;

	save_count = 0;
	for (i = 0; i < MC_MAX_SAVES; i++) {
		const u8 *e = dir + i * 0x40;
		McSave *sv;

		if (e[0] == 0xFF)
			continue;
		sv = &saves[save_count];
		memset(sv, 0, sizeof(*sv));
		copy_printable(sv->name, 33, e + 0x08, 32);
		copy_printable(sv->game, 5, e, 4);
		sv->banner_fmt = e[0x07];
		sv->icon_addr = rd32(e + 0x2C);
		sv->icon_fmt = rd16(e + 0x30);
		sv->first = rd16(e + 0x36);
		sv->blocks = rd16(e + 0x38);
		sv->comment_addr = rd32(e + 0x3C);
		sv->index = (u16)i;
		if (sv->name[0] == 0)
			snprintf(sv->name, sizeof(sv->name), "%s", sv->game[0] ? sv->game : "save");
		save_count++;
	}
}

static bool load_card(int index)
{
	McCard *card;
	FIL fp;
	u8 *dir0 = NULL;
	u8 *dir1 = NULL;
	u8 *bat0 = NULL;
	u8 *bat1 = NULL;
	int which;
	bool ok = false;
	u32 blocks;

	save_count = 0;
	comment_text[0] = 0;
	if (index < 0 || index >= card_count)
		return false;
	card = &cards[index];
	card->parsed = false;
	card->freeb = 0;
	card->total = 0;
	if (card->kind == MC_KIND_REAL)
		return load_real_card(card);
	real_unmount();
	if (f_open_char(&fp, card->path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;
	card->bytes = (u32)f_size(&fp);
	blocks = card->bytes / MC_BLOCK;
	if (blocks < 6)
		goto done;
	card->total = (u16)(blocks - 5);

	dir0 = (u8 *)memalign(32, MC_BLOCK);
	dir1 = (u8 *)memalign(32, MC_BLOCK);
	bat0 = (u8 *)memalign(32, MC_BLOCK);
	bat1 = (u8 *)memalign(32, MC_BLOCK);
	if (!dir0 || !dir1 || !bat0 || !bat1)
		goto done;
	if (!read_block(&fp, 1, dir0) || !read_block(&fp, 2, dir1))
		goto done;
	if (!read_block(&fp, 3, bat0) || !read_block(&fp, 4, bat1))
		goto done;

	which = pick_copy(dir0, dir1, 0x1FFA, 0, 0x1FFC, 0x1FFC);
	if (which >= 0)
		parse_dir(which == 0 ? dir0 : dir1);
	which = pick_copy(bat0, bat1, 4, 4, 0x1FFC, 0);
	if (which >= 0)
		card->freeb = rd16((which == 0 ? bat0 : bat1) + 6);
	card->parsed = true;
	ok = true;

done:
	if (dir0)
		free(dir0);
	if (dir1)
		free(dir1);
	if (bat0)
		free(bat0);
	if (bat1)
		free(bat1);
	f_close(&fp);
	return ok;
}

static void scan_cards(void)
{
	char dirpath[32];
	DIR dir;
	FILINFO info;

	card_count = 0;
	snprintf(dirpath, sizeof(dirpath), "%s:/saves", GetRootDevice());
	f_mkdir_char(dirpath);
	if (f_opendir_char(&dir, dirpath) == FR_OK) {
	while (card_count < MC_MAX_CARDS && f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
		const char *name;
		McCard *card;

		if (info.fattrib & AM_DIR)
			continue;
		name = wchar_to_char(info.fname);
		if (!name || name[0] == '.' || !ends_with_raw(name))
			continue;
		if (strlen(name) >= sizeof(cards[0].name))
			continue;
		card = &cards[card_count];
		memset(card, 0, sizeof(*card));
		strncpy(card->name, name, sizeof(card->name) - 1);
		snprintf(card->path, sizeof(card->path), "%s/%s", dirpath, card->name);
		card->bytes = (u32)info.fsize;
		card->kind = MC_KIND_FILE;
		card_count++;
	}
	f_closedir(&dir);
	}
	/* Slots with no card are left out, so the list stays the virtual cards. */
	add_real_slots();
}

static int find_card(const char *name)
{
	int i;

	for (i = 0; i < card_count; i++) {
		if (name_eq(cards[i].name, name))
			return i;
	}
	return 0;
}

static bool file_exists(const char *path)
{
	FIL fp;

	if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;
	f_close(&fp);
	return true;
}

static bool delete_save(const McCard *card, int save_index)
{
	FIL fp;
	u8 *dirs[2];
	u8 *bats[2];
	int active_dir;
	int active_bat;
	int dest_dir;
	int dest_bat;
	McSave *sv;
	u16 block;
	int freed = 0;
	int guard = 0;
	u32 nblocks;
	u16 freeb;
	u16 ctr;
	bool ok = false;

	dirs[0] = dirs[1] = NULL;
	bats[0] = bats[1] = NULL;
	if (!card || save_index < 0 || save_index >= save_count)
		return false;
	if (card->kind == MC_KIND_REAL)
		return delete_real_save(card, save_index);
	sv = &saves[save_index];
	dirs[0] = (u8 *)memalign(32, MC_BLOCK);
	dirs[1] = (u8 *)memalign(32, MC_BLOCK);
	bats[0] = (u8 *)memalign(32, MC_BLOCK);
	bats[1] = (u8 *)memalign(32, MC_BLOCK);
	if (!dirs[0] || !dirs[1] || !bats[0] || !bats[1])
		goto done;
	if (f_open_char(&fp, card->path, FA_READ | FA_WRITE) != FR_OK)
		goto done;
	nblocks = (u32)(f_size(&fp) / MC_BLOCK);
	if (!read_block(&fp, 1, dirs[0]) || !read_block(&fp, 2, dirs[1]) ||
	    !read_block(&fp, 3, bats[0]) || !read_block(&fp, 4, bats[1])) {
		f_close(&fp);
		goto done;
	}

	active_dir = pick_copy(dirs[0], dirs[1], 0x1FFA, 0, 0x1FFC, 0x1FFC);
	active_bat = pick_copy(bats[0], bats[1], 4, 4, 0x1FFC, 0);
	if (active_dir < 0 || active_bat < 0) {
		f_close(&fp);
		goto done;
	}
	dest_dir = active_dir ^ 1;
	dest_bat = active_bat ^ 1;
	memcpy(dirs[dest_dir], dirs[active_dir], MC_BLOCK);
	memcpy(bats[dest_bat], bats[active_bat], MC_BLOCK);

	block = sv->first;
	while (block != 0 && block != 0xFFFF && guard < 4096) {
		u16 next;

		guard++;
		if (block < 5 || block >= nblocks)
			break;
		next = rd16(bats[dest_bat] + 0x0A + block * 2);
		wr16(bats[dest_bat] + 0x0A + block * 2, 0);
		freed++;
		if (next == 0 || next == 0xFFFF)
			break;
		block = next;
	}
	freeb = rd16(bats[dest_bat] + 6);
	if ((u32)freeb + (u32)freed > 65535)
		freeb = 65535;
	else
		freeb = (u16)(freeb + freed);
	wr16(bats[dest_bat] + 6, freeb);
	memset(dirs[dest_dir] + sv->index * 0x40, 0xFF, 0x40);

	ctr = rd16(dirs[active_dir] + 0x1FFA);
	wr16(dirs[dest_dir] + 0x1FFA, (u16)(ctr + 1));
	ctr = rd16(bats[active_bat] + 4);
	wr16(bats[dest_bat] + 4, (u16)(ctr + 1));
	write_checksum(dirs[dest_dir], 0, 0x1FFC, 0x1FFC);
	write_checksum(bats[dest_bat], 4, 0x1FFC, 0);

	ok = write_block(&fp, (u16)(1 + dest_dir), dirs[dest_dir]) &&
		write_block(&fp, (u16)(3 + dest_bat), bats[dest_bat]);
	f_close(&fp);

done:
	if (dirs[0])
		free(dirs[0]);
	if (dirs[1])
		free(dirs[1]);
	if (bats[0])
		free(bats[0]);
	if (bats[1])
		free(bats[1]);
	return ok;
}

static bool copy_card_file(const McCard *src)
{
	char base[64];
	char dest_name[64];
	char dest_path[160];
	FIL in;
	FIL out;
	u8 *chunk;
	u32 left;
	int n;
	UINT rd;
	UINT wrote;
	bool ok = false;

	if (!src)
		return false;
	strncpy(base, src->name, sizeof(base) - 1);
	base[sizeof(base) - 1] = 0;
	n = (int)strlen(base);
	if (n > 4 && base[n - 4] == '.')
		base[n - 4] = 0;
	snprintf(dest_name, sizeof(dest_name), "%s-copy.raw", base);
	snprintf(dest_path, sizeof(dest_path), "%s:/saves/%s", GetRootDevice(), dest_name);
	if (file_exists(dest_path)) {
		snprintf(dest_name, sizeof(dest_name), "%s-copy2.raw", base);
		snprintf(dest_path, sizeof(dest_path), "%s:/saves/%s", GetRootDevice(), dest_name);
		if (file_exists(dest_path))
			return false;
	}
	chunk = (u8 *)memalign(32, 32768);
	if (!chunk)
		return false;
	if (f_open_char(&in, src->path, FA_READ | FA_OPEN_EXISTING) != FR_OK) {
		free(chunk);
		return false;
	}
	if (f_open_char(&out, dest_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
		f_close(&in);
		free(chunk);
		return false;
	}
	left = (u32)f_size(&in);
	ok = true;
	while (left && ok) {
		u32 want = left > 32768 ? 32768 : left;
		rd = 0;
		wrote = 0;
		if (f_read(&in, chunk, want, &rd) != FR_OK || rd != want)
			ok = false;
		else if (f_write(&out, chunk, want, &wrote) != FR_OK || wrote != want)
			ok = false;
		else
			left -= want;
	}
	f_close(&out);
	f_close(&in);
	free(chunk);
	if (!ok)
		f_unlink_char(dest_path);
	return ok;
}

static void draw_background(void)
{
	int x;
	int y;

	GRRLIB_Rectangle(0, 0, 640, 480, 0x07070CFF, true);
	for (x = 0; x < 640; x += 20)
		GRRLIB_Line((f32)x, 0, (f32)x, 480, 0x24242EFF);
	for (y = 0; y < 480; y += 20)
		GRRLIB_Line(0, (f32)y, 640, (f32)y, 0x24242EFF);
}

static void draw_blank_card(int x, int y, bool hot)
{
	u32 edge = hot ? 0x9AF8C8FF : 0x081028FF;
	u32 body = hot ? 0x3A6AD8FF : 0x1A3C9CFF;

	GRRLIB_Rectangle((f32)x - 2, (f32)y - 2, MC_CELL_W + 4, MC_CELL_H + 4, edge, false);
	GRRLIB_Rectangle((f32)x, (f32)y, MC_CELL_W, MC_CELL_H, 0x081028FF, true);
	GRRLIB_Rectangle((f32)x + 4, (f32)y + 4, MC_CELL_W - 8, MC_CELL_H - 8, body, true);
	GRRLIB_Rectangle((f32)x + 18, (f32)y + 8, 36, 8, 0xE4E8F0FF, true);
}

static void draw_header(const McCard *card, int index)
{
	char letter[4];
	char count[12];

	if (card && card->kind == MC_KIND_REAL)
		snprintf(letter, sizeof(letter), "%c", 'A' + card->slot);
	else if (index >= 0 && index < 26)
		snprintf(letter, sizeof(letter), "%c", 'A' + index);
	else
		snprintf(letter, sizeof(letter), "%d", index + 1);
	PrintFormat(36, 0xFFFFFFFF, 28, 18, "%s", letter);
	PrintFormat(18, 0xFFFFFFFF, 78, 28, "Open");
	if (card && card->parsed)
		snprintf(count, sizeof(count), "%u", (unsigned)card->freeb);
	else
		snprintf(count, sizeof(count), "----");
	GRRLIB_Rectangle(150, 26, 88, 28, 0xF4F4F4FF, true);
	PrintFormat(18, 0x101010FF, 162, 28, "%s", count);
	if (card) {
		const char *kind = "Card";
		if (card->kind == MC_KIND_REAL)
			kind = "Slot";
		else if (vmc_slot_a[0] && name_eq(card->name, vmc_slot_a))
			kind = "In use";
		else if (vmc_slot_b[0] && name_eq(card->name, vmc_slot_b))
			kind = "Slot B";
		else if (name_eq(card->name, "ninmem.raw") || name_eq(card->name, "ninmemb.raw"))
			kind = "USA/PAL shared";
		else if (name_eq(card->name, "ninmemj.raw") || name_eq(card->name, "ninmemjb.raw"))
			kind = "Japan shared";
		PrintFormat(14, 0xD0D4E0FF, 250, 32, "%s   %s", kind, card->name);
	}
	if (!(ncfg->Config & NIN_CFG_MEMCARDEMU) && real_slot_present())
		PrintFormat(14, 0xFFFFFFFF, 28, 62, "Uses the card in the slot");
	else if (vmc_slot_a[0])
		PrintFormat(14, 0xFFFFFFFF, 28, 62, "Uses %s (%s)", vmc_slot_a, vmc_scope_text());
	PrintFormat(14, 0xC8C8D0FF, 430, 18, "X  Next card");
	PrintFormat(14, 0xC8C8D0FF, 430, 38, "Y  Card tasks");
}

static void draw_grid(int page, int cursor)
{
	int i;

	for (i = 0; i < MC_PAGE; i++) {
		int col = i % MC_COLS;
		int row = i / MC_COLS;
		int x = MC_GRID_X + col * (MC_CELL_W + MC_GAP_X);
		int y = MC_GRID_Y + row * (MC_CELL_H + MC_GAP_Y);
		int si = page * MC_PAGE + i;
		bool hot = (i == cursor);

		draw_blank_card(x, y, hot && (si >= save_count));
		if (si < save_count && icons[i]) {
			float s = (MC_CELL_W - 16) / 32.0f;
			GRRLIB_DrawImg((f32)x + 8, (f32)y + 14, icons[i], 0, s, s, 0xFFFFFFFF);
		}
		if (hot)
			GRRLIB_Rectangle((f32)x - 3, (f32)y - 3, MC_CELL_W + 6, MC_CELL_H + 6, 0x39FF4AFF, false);
	}
}

static void draw_panel_box(void)
{
	int y;

	for (y = 0; y < MC_PANEL_H; y += 6) {
		u32 c = 0x6A2848FF;
		int t = y * 255 / MC_PANEL_H;
		u32 top = 0x9A4060FF;
		u32 bot = 0x3A1028FF;
		u8 r = (u8)(R(top) + (int)(R(bot) - R(top)) * t / 255);
		u8 g = (u8)(G(top) + (int)(G(bot) - G(top)) * t / 255);
		u8 b = (u8)(B(top) + (int)(B(bot) - B(top)) * t / 255);
		c = RGBA(r, g, b, 255);
		GRRLIB_Rectangle(MC_PANEL_X, (f32)(MC_PANEL_Y + y), MC_PANEL_W, 6, c, true);
	}
	GRRLIB_Rectangle(MC_PANEL_X, MC_PANEL_Y, MC_PANEL_W, MC_PANEL_H, 0x000000FF, false);
}

static void draw_choice(int x, int y, const char *label, bool on)
{
	if (on)
		GRRLIB_Rectangle((f32)x - 6, (f32)y - 4, 230, 26, 0x39FF4AFF, false);
	PrintFormat(16, 0xFFFFFFFF, x, y, "%s", label);
}

static void draw_panel(int mode, int page, int cursor, int choice, const gameinfo *game)
{
	int si = page * MC_PAGE + cursor;
	int y = MC_PANEL_Y + 16;

	draw_panel_box();
	if (mode == MC_CONFIRM_SAVE) {
		PrintFormat(16, 0xFFFFFFFF, MC_PANEL_X + 24, y, "Delete this save?");
		draw_choice(MC_PANEL_X + 90, y + 70, "Yes", choice == 0);
		draw_choice(MC_PANEL_X + 90, y + 100, "No", choice == 1);
		return;
	}
	if (mode == MC_CONFIRM_CARD) {
		PrintFormat(16, 0xFFFFFFFF, MC_PANEL_X + 24, y, "Delete this card");
		PrintFormat(16, 0xFFFFFFFF, MC_PANEL_X + 24, y + 22, "and every save on it?");
		draw_choice(MC_PANEL_X + 90, y + 80, "Yes", choice == 0);
		draw_choice(MC_PANEL_X + 90, y + 110, "No", choice == 1);
		return;
	}
	if (mode == MC_TASKS) {
		int row = 0;
		PrintFormat(16, 0xFFFFFFFF, MC_PANEL_X + 24, y, "Card tasks");
		if (game_has_id(vmc_game)) {
			const char *mode_label = (ncfg->Config & NIN_CFG_MC_MULTI) ? "Use this game's card" : "Use the shared card";
			draw_choice(MC_PANEL_X + 24, y + 36, mode_label, choice == row);
			row++;
		}
		draw_choice(MC_PANEL_X + 24, y + 36 + row * 32, "Create card", choice == row);
		row++;
		draw_choice(MC_PANEL_X + 24, y + 36 + row * 32, "Copy card", choice == row);
		row++;
		draw_choice(MC_PANEL_X + 24, y + 36 + row * 32, "Delete card", choice == row);
		return;
	}
	if (mode == MC_CREATE) {
		char id4[8];
		int row = 0;

		PrintFormat(16, 0xFFFFFFFF, MC_PANEL_X + 24, y, "Create a blank card");
		id4[0] = 0;
		if (game && game->Path && strncmp(game->Path, "di:", 3) != 0) {
			memcpy(id4, game->ID, 4);
			id4[4] = 0;
		}
		if (id4[0]) {
			char line[40];
			snprintf(line, sizeof(line), "This game (%s)", id4);
			draw_choice(MC_PANEL_X + 28, y + 40 + row * 32, line, choice == row);
			row++;
		}
		draw_choice(MC_PANEL_X + 28, y + 40 + row * 32, "Shared USA/PAL", choice == row);
		row++;
		draw_choice(MC_PANEL_X + 28, y + 40 + row * 32, "Shared Japan", choice == row);
		return;
	}

	if (si < 0 || si >= save_count) {
		PrintFormat(16, 0xFFFFFFFF, MC_PANEL_X + 24, y + 40, "Empty");
		return;
	}
	if (banner_tex)
		GRRLIB_DrawImg(MC_PANEL_X + 24, (f32)y, banner_tex, 0, 2.2f, 2.2f, 0xFFFFFFFF);
	y += banner_tex ? 78 : 8;
	PrintFormat(16, 0xFFFFFFFF, MC_PANEL_X + 16, y, "%s", saves[si].name);
	PrintFormat(14, 0xF0E0E8FF, MC_PANEL_X + 16, y + 22, "%s    %u blocks", saves[si].game, (unsigned)saves[si].blocks);
	if (status_line[0])
		PrintFormat(14, 0xFFE080FF, MC_PANEL_X + 16, y + 48, "%s", status_line);
	else
		PrintFormat(14, 0xF0E0E8FF, MC_PANEL_X + 16, y + 52, "A  Delete save");
}

static void draw_bar(void)
{
	int x;

	for (x = 24; x < 616; x += 8) {
		int t = (x - 24) * 255 / 592;
		u8 r = (u8)(0x10 + (0x40 - 0x10) * t / 255);
		u8 g = (u8)(0x18 + (0x10 - 0x18) * t / 255);
		u8 b = (u8)(0x90 + (0x70 - 0x90) * t / 255);
		GRRLIB_Rectangle((f32)x, MC_BAR_Y, 8, MC_BAR_H, RGBA(r, g, b, 255), true);
	}
	if (comment_text[0])
		PrintFormat(16, 0xFFFFFFFF, 40, MC_BAR_Y + 24, "%s", comment_text);
}

static void draw_footer(void)
{
	PrintFormat(15, 0xFFFFFFFF, 36, 442, "Move  Select");
	PrintFormat(15, 0xFF4040FF, 250, 442, "B");
	PrintFormat(15, 0xFFFFFFFF, 270, 442, "Cancel");
	PrintFormat(15, 0x40FF60FF, 400, 442, "A");
	PrintFormat(15, 0xFFFFFFFF, 420, 442, "Confirm");
}

static void refresh(int card_index, int page, int cursor)
{
	if (card_index >= 0 && card_index < card_count)
		load_card(card_index);
	load_visuals((card_index >= 0 && card_index < card_count) ? &cards[card_index] : NULL, page, cursor);
}

static const char *create_name(int choice, const gameinfo *game, int *region)
{
	char id4[8];
	static char per_game[16];
	int row = 0;

	id4[0] = 0;
	*region = BI2_REGION_USA;
	if (game && game->Path && strncmp(game->Path, "di:", 3) != 0) {
		memcpy(id4, game->ID, 4);
		id4[4] = 0;
		if (id4[0]) {
			if (choice == row) {
				*region = (int)((game->Flags & GIFLAG_REGION_MASK) >> 3);
				snprintf(per_game, sizeof(per_game), "%s.raw", id4);
				return per_game;
			}
			row++;
		}
	}
	if (choice == row)
		return "ninmem.raw";
	row++;
	if (choice == row) {
		*region = BI2_REGION_JAPAN;
		return "ninmemj.raw";
	}
	return NULL;
}

static void focus_game_card(int *index)
{
	int i;

	refresh_vmc_names();
	if (!(ncfg->Config & NIN_CFG_MEMCARDEMU)) {
		for (i = 0; i < card_count; i++) {
			if (cards[i].kind == MC_KIND_REAL && cards[i].slot == CARD_SLOTA) {
				*index = i;
				return;
			}
		}
		for (i = 0; i < card_count; i++) {
			if (cards[i].kind == MC_KIND_REAL) {
				*index = i;
				return;
			}
		}
	}
	if (vmc_slot_a[0]) {
		i = find_card(vmc_slot_a);
		if (i >= 0)
			*index = i;
	}
}

static int create_rows(const gameinfo *game)
{
	int rows = 2;
	if (game && game->Path && strncmp(game->Path, "di:", 3) != 0 && game->ID[0])
		rows++;
	return rows;
}

void MemCard_Run(const gameinfo *selected_game)
{
	int card_index = 0;
	int cursor = 0;
	int page = 0;
	int mode = MC_BROWSE;
	int choice = 1;
	int pages;
	u32 held_l = 0, held_r = 0, held_u = 0, held_d = 0;
	bool redraw = true;
	bool reload = true;

	memset(icons, 0, sizeof(icons));
	banner_tex = NULL;
	status_line[0] = 0;
	comment_text[0] = 0;
	vmc_game = selected_game;
	scan_cards();
	focus_game_card(&card_index);
	if (card_count == 0)
		mode = MC_CREATE;

	while (1) {
		int si;

		VIDEO_WaitVSync();
		FPAD_Update();
		if (Shutdown)
			LoaderShutdown();
		Screenshot();

		pages = (save_count + MC_PAGE - 1) / MC_PAGE;
		if (pages < 1)
			pages = 1;
		if (page >= pages)
			page = pages - 1;
		si = page * MC_PAGE + cursor;

		if (mode == MC_BROWSE) {
			int col = cursor % MC_COLS;
			int row = cursor / MC_COLS;
			bool moved = false;

			if (held_step(FPAD_Left(1), &held_l)) {
				col = (col + MC_COLS - 1) % MC_COLS;
				moved = true;
			} else if (held_step(FPAD_Right(1), &held_r)) {
				col = (col + 1) % MC_COLS;
				moved = true;
			} else if (held_step(FPAD_Up(1), &held_u)) {
				if (row == 0 && page > 0) {
					page--;
					row = MC_ROWS - 1;
					reload = true;
				} else if (row > 0) {
					row--;
				}
				moved = true;
			} else if (held_step(FPAD_Down(1), &held_d)) {
				if (row == MC_ROWS - 1 && page + 1 < pages) {
					page++;
					row = 0;
					reload = true;
				} else if (row < MC_ROWS - 1) {
					row++;
				}
				moved = true;
			}
			if (moved) {
				cursor = row * MC_COLS + col;
				redraw = true;
				status_line[0] = 0;
			}
			if (FPAD_OK(0) && si >= 0 && si < save_count) {
				mode = MC_CONFIRM_SAVE;
				choice = 1;
				redraw = true;
			} else if (FPAD_X(0) && card_count > 1) {
				card_index = (card_index + 1) % card_count;
				cursor = 0;
				page = 0;
				reload = true;
				redraw = true;
				status_line[0] = 0;
			} else if (FPAD_Y(0)) {
				mode = MC_TASKS;
				choice = 0;
				redraw = true;
			} else if (FPAD_Cancel(0) || FPAD_Start(0)) {
				break;
			}
		} else if (mode == MC_CONFIRM_SAVE || mode == MC_CONFIRM_CARD) {
			if (FPAD_Up(0) || FPAD_Down(0)) {
				choice ^= 1;
				redraw = true;
			} else if (FPAD_Cancel(0)) {
				mode = MC_BROWSE;
				redraw = true;
			} else if (FPAD_OK(0)) {
				if (choice == 0 && mode == MC_CONFIRM_SAVE && card_index < card_count) {
					if (delete_save(&cards[card_index], si))
						snprintf(status_line, sizeof(status_line), "Save deleted");
					else
						snprintf(status_line, sizeof(status_line), "Couldn't delete that save");
					reload = true;
				} else if (choice == 0 && mode == MC_CONFIRM_CARD && card_index < card_count) {
					char gone[64];
					strncpy(gone, cards[card_index].name, sizeof(gone) - 1);
					gone[sizeof(gone) - 1] = 0;
					if (f_unlink_char(cards[card_index].path) == FR_OK) {
						scan_cards();
						card_index = 0;
						cursor = 0;
						page = 0;
						snprintf(status_line, sizeof(status_line), "Deleted %s", gone);
						if (card_count == 0)
							mode = MC_CREATE;
					} else {
						snprintf(status_line, sizeof(status_line), "Couldn't delete that card");
					}
					reload = true;
				}
				if (mode != MC_CREATE)
					mode = MC_BROWSE;
				redraw = true;
			}
		} else if (mode == MC_TASKS) {
			int ntasks = task_count();
			int mode_row = game_has_id(vmc_game) ? 1 : 0;
			if (FPAD_Up(0) && choice > 0) {
				choice--;
				redraw = true;
			} else if (FPAD_Down(0) && choice + 1 < ntasks) {
				choice++;
				redraw = true;
			} else if (FPAD_Cancel(0)) {
				mode = MC_BROWSE;
				redraw = true;
			} else if (FPAD_OK(0)) {
				int act = choice - mode_row;
				if (mode_row && choice == 0) {
					ncfg->Config ^= NIN_CFG_MC_MULTI;
					vmc_dirty = true;
					cursor = 0;
					page = 0;
					focus_game_card(&card_index);
					reload = true;
					snprintf(status_line, sizeof(status_line), "Now uses %s (%s)",
						vmc_slot_a[0] ? vmc_slot_a : "Slot A", vmc_scope_text());
					mode = MC_BROWSE;
				} else if (act == 0) {
					mode = MC_CREATE;
					choice = 0;
				} else if (act == 1 && card_index < card_count) {
					if (cards[card_index].kind == MC_KIND_REAL) {
						snprintf(status_line, sizeof(status_line), "Use the virtual cards to copy");
						mode = MC_BROWSE;
						redraw = true;
					} else if (copy_card_file(&cards[card_index])) {
						char keep[64];
						strncpy(keep, cards[card_index].name, sizeof(keep) - 1);
						keep[sizeof(keep) - 1] = 0;
						scan_cards();
						card_index = find_card(keep);
						snprintf(status_line, sizeof(status_line), "Card copied");
						reload = true;
					} else {
						snprintf(status_line, sizeof(status_line), "Couldn't copy that card");
					}
					mode = MC_BROWSE;
				} else if (act == 2 && card_index < card_count) {
					if (cards[card_index].kind == MC_KIND_REAL) {
						snprintf(status_line, sizeof(status_line), "Unplug the card, or delete one save");
						mode = MC_BROWSE;
					} else {
						mode = MC_CONFIRM_CARD;
						choice = 1;
					}
				}
				redraw = true;
			}
		} else if (mode == MC_CREATE) {
			int rows = create_rows(selected_game);
			if (FPAD_Up(0) && choice > 0) {
				choice--;
				redraw = true;
			} else if (FPAD_Down(0) && choice + 1 < rows) {
				choice++;
				redraw = true;
			} else if (FPAD_Cancel(0)) {
				if (card_count == 0)
					break;
				mode = MC_BROWSE;
				redraw = true;
			} else if (FPAD_OK(0)) {
				int region = BI2_REGION_USA;
				const char *fname = create_name(choice, selected_game, &region);
				char path[160];
				if (fname) {
					snprintf(path, sizeof(path), "%s:/saves/%s", GetRootDevice(), fname);
					if (file_exists(path)) {
						snprintf(status_line, sizeof(status_line), "That card already exists");
					} else if (GenerateMemCard(path, (u32)region)) {
						scan_cards();
						card_index = find_card(fname);
						cursor = 0;
						page = 0;
						snprintf(status_line, sizeof(status_line), "Created %s", fname);
						reload = true;
					} else {
						snprintf(status_line, sizeof(status_line), "Couldn't create the card");
					}
				}
				mode = MC_BROWSE;
				redraw = true;
			}
		}

		if (reload) {
			refresh(card_count ? card_index : -1, page, cursor);
			reload = false;
			redraw = true;
		}

		if (redraw) {
			draw_background();
			draw_header(card_count ? &cards[card_index] : NULL, card_index);
			draw_grid(page, cursor);
			draw_panel(mode, page, cursor, choice, selected_game);
			draw_bar();
			draw_footer();
			GRRLIB_Render();
			ClearScreen();
			redraw = false;
		}
	}

	real_unmount();
	if (real_work) {
		free(real_work);
		real_work = NULL;
	}
	free_icons();
}
