/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 *
 * opening.bnr is either a loose file (extracted FST layouts) or a file
 * inside the disc image. BNR1 has one language block. BNR2 has six, in
 * the same order as NIN_LAN_ENGLISH .. NIN_LAN_DUTCH.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#include "banner.h"
#include "ff_utf8.h"
#include "global.h"

static GameBanner cached;

static u32 rd32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u16 rd16(const u8 *p)
{
	return (u16)(((u16)p[0] << 8) | p[1]);
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

static void copy_text(char *dst, int dstlen, const u8 *src, int srclen)
{
	int i;
	int o = 0;

	for (i = 0; i < srclen && o < dstlen - 1; i++) {
		u8 c = src[i];
		if (c == 0)
			break;
		if (c == '\r')
			continue;
		if (c == '\n') {
			dst[o++] = '\n';
			continue;
		}
		if (c < 0x20 || c > 0x7E)
			c = ' ';
		dst[o++] = (char)c;
	}
	dst[o] = 0;
}

static GRRLIB_texImg *image_from_bnr(const u8 *bnr, u32 len)
{
	GRRLIB_texImg *tex;
	int x, y, i;

	if (len < 0x20 + 96 * 32 * 2)
		return NULL;
	/* 96 is not a power of two. The Wii draws those sizes as garbage,
	   so the texture is 128 wide and only the left 96 columns are shown. */
	tex = GRRLIB_CreateEmptyTexture(128, 32);
	if (!tex || !tex->data) {
		if (tex)
			GRRLIB_FreeTexture(tex);
		return NULL;
	}
	tex->content_w = 96;
	tex->content_h = 32;
	/* Pixels are 4x4 tiles, then the texture itself has to be a power of two. */
	i = 0x20;
	for (y = 0; y < 32; y += 4) {
		for (x = 0; x < 96; x += 4) {
			int ty;
			int tx;
			for (ty = 0; ty < 4; ty++) {
				for (tx = 0; tx < 4; tx++) {
					u16 px = rd16(bnr + i);
					i += 2;
					GRRLIB_SetPixelTotexImg(x + tx, y + ty, tex, rgb5a3(px));
				}
			}
		}
	}
	GRRLIB_FlushTex(tex);
	return tex;
}

static void text_from_bnr(GameBanner *out, const u8 *bnr, u32 len)
{
	u32 lang;
	u32 off;
	int blocks;
	const u8 *meta;

	out->title[0] = 0;
	out->description[0] = 0;
	if (len < 0x1820 + 0x140)
		return;
	blocks = 1;
	if (!memcmp(bnr, "BNR2", 4))
		blocks = 6;
	lang = ncfg->Language;
	if (lang >= (u32)blocks)
		lang = 0;
	off = 0x1820 + lang * 0x140;
	if (off + 0x140 > len)
		return;
	meta = bnr + off;
	copy_text(out->title, (int)sizeof(out->title), meta + 0x40, 0x40);
	if (out->title[0] == 0)
		copy_text(out->title, (int)sizeof(out->title), meta, 0x20);
	copy_text(out->description, (int)sizeof(out->description), meta + 0xC0, 0x80);
}

static bool take_bnr(GameBanner *out, const u8 *bnr, u32 len)
{
	if (len < 4)
		return false;
	if (memcmp(bnr, "BNR1", 4) != 0 && memcmp(bnr, "BNR2", 4) != 0)
		return false;
	out->image = image_from_bnr(bnr, len);
	text_from_bnr(out, bnr, len);
	out->valid = (out->image != NULL) || (out->title[0] != 0) || (out->description[0] != 0);
	return out->valid;
}

static bool read_at(FIL *fp, u32 pos, void *dst, u32 len)
{
	UINT rd = 0;
	if (f_lseek(fp, (FSIZE_t)pos) != FR_OK)
		return false;
	if (f_read(fp, dst, len, &rd) != FR_OK || rd != len)
		return false;
	return true;
}

static bool ciso_read(FIL *fp, const u8 *map, u32 map_len, u32 block_size, u32 disc_off, void *dst, u32 len)
{
	u8 *out = (u8 *)dst;

	while (len) {
		u32 block = disc_off / block_size;
		u32 intra = disc_off % block_size;
		u32 chunk = block_size - intra;
		u32 present = 0;
		u32 i;
		u32 file_off;

		if (chunk > len)
			chunk = len;
		if (block >= map_len || map[block] == 0)
			return false;
		for (i = 0; i < block; i++) {
			if (map[i])
				present++;
		}
		file_off = 0x8000 + present * block_size + intra;
		if (!read_at(fp, file_off, out, chunk))
			return false;
		out += chunk;
		disc_off += chunk;
		len -= chunk;
	}
	return true;
}

static bool disc_read(FIL *fp, bool ciso, const u8 *map, u32 map_len, u32 block_size, u32 off, void *dst, u32 len)
{
	if (ciso)
		return ciso_read(fp, map, map_len, block_size, off, dst, len);
	return read_at(fp, off, dst, len);
}

static bool fst_find(const u8 *fst, u32 fst_size, u32 *file_off, u32 *file_len)
{
	u32 count;
	u32 table;
	u32 i;

	if (fst_size < 12)
		return false;
	count = rd32(fst + 8);
	if (count < 2 || count > 200000)
		return false;
	table = count * 12;
	if (table >= fst_size)
		return false;
	for (i = 1; i < count; i++) {
		const u8 *e = fst + i * 12;
		u32 name_off;
		const char *name;
		u32 off;
		u32 flen;

		if (e[0] != 0)
			continue;
		name_off = ((u32)e[1] << 16) | ((u32)e[2] << 8) | (u32)e[3];
		if (table + name_off >= fst_size)
			continue;
		name = (const char *)(fst + table + name_off);
		if (strcmp(name, "opening.bnr") != 0)
			continue;
		off = rd32(e + 4);
		flen = rd32(e + 8);
		if (flen < 0x1820 || flen > 0x8000)
			continue;
		*file_off = off;
		*file_len = flen;
		return true;
	}
	return false;
}

static bool load_from_fst_dir(const char *dir, GameBanner *out)
{
	char path[256];
	FIL fp;
	u8 *buf = NULL;
	u32 sz;
	UINT rd = 0;
	bool ok = false;
	static const char *tails[] = { "files/opening.bnr", "opening.bnr" };
	int i;

	for (i = 0; i < 2; i++) {
		snprintf(path, sizeof(path), "%s%s", dir, tails[i]);
		if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
			continue;
		sz = (u32)f_size(&fp);
		if (sz < 0x1820 || sz > 0x8000) {
			f_close(&fp);
			continue;
		}
		buf = (u8 *)memalign(32, sz);
		if (!buf) {
			f_close(&fp);
			break;
		}
		if (f_read(&fp, buf, sz, &rd) == FR_OK && rd == sz)
			ok = take_bnr(out, buf, sz);
		free(buf);
		buf = NULL;
		f_close(&fp);
		if (ok)
			return true;
	}
	return false;
}

static bool load_from_image(const char *path, bool ciso, GameBanner *out)
{
	FIL fp;
	u8 header[0x440];
	u8 *map = NULL;
	u8 *fst = NULL;
	u8 *bnr = NULL;
	u32 block_size = 0x200000;
	u32 fst_off;
	u32 fst_size;
	u32 file_off = 0;
	u32 file_len = 0;
	u32 read_len;
	bool ok = false;

	if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;

	if (ciso) {
		map = (u8 *)memalign(32, 0x8000);
		if (!map)
			goto done;
		if (!read_at(&fp, 0, map, 0x8000))
			goto done;
		block_size = (u32)map[4] | ((u32)map[5] << 8) | ((u32)map[6] << 16) | ((u32)map[7] << 24);
		if (block_size < 0x8000 || block_size > 0x800000)
			goto done;
		if (!ciso_read(&fp, map + 8, 0x8000 - 8, block_size, 0, header, sizeof(header)))
			goto done;
	} else {
		if (!read_at(&fp, 0, header, sizeof(header)))
			goto done;
	}

	fst_off = rd32(header + 0x424);
	fst_size = rd32(header + 0x428);
	if (fst_off < 0x440 || fst_size < 12 || fst_size > 512 * 1024)
		goto done;
	fst = (u8 *)memalign(32, fst_size);
	if (!fst)
		goto done;
	if (!disc_read(&fp, ciso, map ? map + 8 : NULL, map ? 0x8000 - 8 : 0, block_size, fst_off, fst, fst_size))
		goto done;
	if (!fst_find(fst, fst_size, &file_off, &file_len))
		goto done;
	read_len = file_len;
	if (read_len > 0x4000)
		read_len = 0x4000;
	bnr = (u8 *)memalign(32, read_len);
	if (!bnr)
		goto done;
	if (!disc_read(&fp, ciso, map ? map + 8 : NULL, map ? 0x8000 - 8 : 0, block_size, file_off, bnr, read_len))
		goto done;
	ok = take_bnr(out, bnr, read_len);

done:
	if (bnr)
		free(bnr);
	if (fst)
		free(fst);
	if (map)
		free(map);
	f_close(&fp);
	return ok;
}

static void cache_clear(void)
{
	if (cached.image) {
		GRRLIB_FreeTexture(cached.image);
		cached.image = NULL;
	}
	cached.title[0] = 0;
	cached.description[0] = 0;
	cached.valid = false;
}

void Banner_Free(void)
{
	cache_clear();
}

const GameBanner *Banner_Get(const gameinfo *game)
{
	static char cached_id[8];
	char id6[8];
	u8 fmt;
	int i;
	bool ok = false;

	if (!game) {
		cache_clear();
		cached_id[0] = 0;
		return &cached;
	}
	for (i = 0; i < 6; i++)
		id6[i] = game->ID[i] ? game->ID[i] : ' ';
	id6[6] = 0;
	if (cached_id[0] && strcmp(cached_id, id6) == 0)
		return &cached;

	cache_clear();
	memcpy(cached_id, id6, 8);
	fmt = (u8)(game->Flags & GIFLAG_FORMAT_MASK);
	if (game->Path && strncmp(game->Path, "di:", 3) != 0) {
		if (fmt == GIFLAG_FORMAT_FST)
			ok = load_from_fst_dir(game->Path, &cached);
		else if (fmt != GIFLAG_FORMAT_MULTI)
			ok = load_from_image(game->Path, fmt == GIFLAG_FORMAT_CISO, &cached);
	}
	if (!ok) {
		if (game->Name) {
			strncpy(cached.title, game->Name, sizeof(cached.title) - 1);
			cached.title[sizeof(cached.title) - 1] = 0;
		}
	}
	cached.valid = true;
	return &cached;
}
