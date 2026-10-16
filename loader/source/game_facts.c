/*
 * Nintendont loader UI.
 * GPL-2.0-only. The full license notice is in loader/source/menu.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <network.h>

#include "game_facts.h"
#include "http.h"
#include "ff_utf8.h"
#include "loader_prefs.h"

#define PAGE_MAX (96 * 1024)

static int net_ready = 0;

static const char *find_span(const char *hay, unsigned len, const char *needle)
{
	unsigned n = (unsigned)strlen(needle);
	unsigned i;

	if (n == 0 || len < n)
		return NULL;
	for (i = 0; i + n <= len; i++) {
		if (memcmp(hay + i, needle, n) == 0)
			return hay + i;
	}
	return NULL;
}

static int esrb_code(const char *s)
{
	static const char *codes[] = { "EC", "E10+", "AO", "RP", "E", "T", "M", NULL };
	int i;

	for (i = 0; codes[i]; i++) {
		if (strcmp(s, codes[i]) == 0)
			return 1;
	}
	return 0;
}

static void decode_copy(char *dst, int dstlen, const char *src, int srclen)
{
	int o = 0;
	int i = 0;

	while (i < srclen && o < dstlen - 1) {
		char c = src[i];
		if (c == '<')
			break;
		if (c == '&') {
			if (i + 4 < srclen && memcmp(src + i, "&amp;", 5) == 0) {
				c = '&';
				i += 5;
			} else if (i + 3 < srclen && memcmp(src + i, "&lt;", 4) == 0) {
				c = '<';
				i += 4;
			} else if (i + 3 < srclen && memcmp(src + i, "&gt;", 4) == 0) {
				c = '>';
				i += 4;
			} else if (i + 5 < srclen && memcmp(src + i, "&quot;", 6) == 0) {
				c = '"';
				i += 6;
			} else if (i + 4 < srclen && memcmp(src + i, "&#39;", 5) == 0) {
				c = '\'';
				i += 5;
			} else if (i + 5 < srclen && memcmp(src + i, "&nbsp;", 6) == 0) {
				c = ' ';
				i += 6;
			} else {
				i++;
				continue;
			}
		} else {
			i++;
		}
		if (c == '\r' || c == '\n' || c == '\t')
			c = ' ';
		if (c == ' ' && (o == 0 || dst[o - 1] == ' '))
			continue;
		if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E)
			continue;
		dst[o++] = c;
	}
	while (o > 0 && dst[o - 1] == ' ')
		o--;
	dst[o] = 0;
}

static void take_cell(const char *html, unsigned len, const char *mark, char *dst, int dstlen)
{
	const char *p;
	const char *td;
	const char *gt;
	const char *end;
	unsigned left;

	dst[0] = 0;
	p = find_span(html, len, mark);
	if (!p)
		return;
	left = len - (unsigned)(p - html);
	td = find_span(p, left, "<td");
	if (!td)
		return;
	left = len - (unsigned)(td - html);
	gt = find_span(td, left, ">");
	if (!gt)
		return;
	gt++;
	left = len - (unsigned)(gt - html);
	end = find_span(gt, left, "</td>");
	if (!end)
		end = gt + (left > 800 ? 800 : left);
	decode_copy(dst, dstlen, gt, (int)(end - gt));
}

static void ensure_net(void)
{
	if (net_ready != 0)
		return;
	if (net_init() < 0)
		net_ready = -1;
	else
		net_ready = 1;
}

static void copy_field(char *dst, int dstlen, const char *src)
{
	if (!src)
		src = "";
	strncpy(dst, src, (size_t)dstlen - 1);
	dst[dstlen - 1] = 0;
}

static void apply_cached_line(GameFacts *out, char *line)
{
	char *eq;
	char *key;
	char *val;

	eq = strchr(line, '=');
	if (!eq)
		return;
	*eq = 0;
	key = line;
	val = eq + 1;
	if (strcmp(key, "players") == 0)
		copy_field(out->players, (int)sizeof(out->players), val);
	else if (strcmp(key, "genre") == 0)
		copy_field(out->genre, (int)sizeof(out->genre), val);
	else if (strcmp(key, "developer") == 0)
		copy_field(out->developer, (int)sizeof(out->developer), val);
	else if (strcmp(key, "publisher") == 0)
		copy_field(out->publisher, (int)sizeof(out->publisher), val);
	else if (strcmp(key, "released") == 0)
		copy_field(out->released, (int)sizeof(out->released), val);
	else if (strcmp(key, "content") == 0)
		copy_field(out->content, (int)sizeof(out->content), val);
	else if (strcmp(key, "rating") == 0)
		copy_field(out->rating, (int)sizeof(out->rating), val);
	else if (strcmp(key, "synopsis") == 0)
		copy_field(out->synopsis, GAME_SYNOPSIS_MAX, val);
}

static bool load_cache(const char *id6, GameFacts *out)
{
	char rel[32];
	char path[256];
	FIL fp;
	FSIZE_t sz;
	UINT rd = 0;
	char *buf;
	char *p;

	snprintf(rel, sizeof(rel), "info/%s.txt", id6);
	if (!Prefs_Path(path, sizeof(path), rel))
		return false;
	if (f_open_char(&fp, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;
	sz = f_size(&fp);
	if (sz < 3 || sz > 4096) {
		f_close(&fp);
		return false;
	}
	buf = (char *)malloc((size_t)sz + 1);
	if (!buf) {
		f_close(&fp);
		return false;
	}
	if (f_read(&fp, buf, (UINT)sz, &rd) != FR_OK || rd == 0) {
		free(buf);
		f_close(&fp);
		return false;
	}
	f_close(&fp);
	buf[rd] = 0;
	p = buf;
	while (*p) {
		char *nl = strchr(p, '\n');
		if (nl)
			*nl = 0;
		if (p[0] && p[strlen(p) - 1] == '\r')
			p[strlen(p) - 1] = 0;
		apply_cached_line(out, p);
		if (!nl)
			break;
		p = nl + 1;
	}
	free(buf);
	return true;
}

static void put_line(char *buf, int *used, int cap, const char *key, const char *val)
{
	int n;

	if (!val || !val[0] || *used >= cap)
		return;
	n = snprintf(buf + *used, (size_t)(cap - *used), "%s=%s\n", key, val);
	if (n > 0 && *used + n < cap)
		*used += n;
}

static void save_cache(const char *id6, const GameFacts *in)
{
	char dir[256];
	char rel[32];
	char path[256];
	char *buf;
	int used = 0;
	FIL fp;
	UINT wrote = 0;
	int any;

	if (!Prefs_Path(dir, sizeof(dir), "info"))
		return;
	f_mkdir_char(dir);
	snprintf(rel, sizeof(rel), "info/%s.txt", id6);
	if (!Prefs_Path(path, sizeof(path), rel))
		return;
	buf = (char *)malloc(4096);
	if (!buf)
		return;
	any = in->players[0] || in->genre[0] || in->developer[0] || in->publisher[0]
		|| in->released[0] || in->content[0] || in->rating[0] || in->synopsis[0];
	if (!any)
		put_line(buf, &used, 4096, "missing", "1");
	put_line(buf, &used, 4096, "players", in->players);
	put_line(buf, &used, 4096, "genre", in->genre);
	put_line(buf, &used, 4096, "developer", in->developer);
	put_line(buf, &used, 4096, "publisher", in->publisher);
	put_line(buf, &used, 4096, "released", in->released);
	put_line(buf, &used, 4096, "content", in->content);
	put_line(buf, &used, 4096, "rating", in->rating);
	put_line(buf, &used, 4096, "synopsis", in->synopsis);
	if (f_open_char(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
		f_write(&fp, buf, (UINT)used, &wrote);
		f_close(&fp);
	}
	free(buf);
}

void GameFacts_Load(const gameinfo *game, GameFacts *out)
{
	char id6[8];
	char url[80];
	u8 *data = NULL;
	unsigned int len = 0;
	unsigned int status = 0;
	int i;

	memset(out, 0, sizeof(*out));
	if (!game)
		return;
	for (i = 0; i < 6; i++) {
		char c = game->ID[i];
		if (c >= 'a' && c <= 'z')
			c = (char)(c - 32);
		if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
			return;
		id6[i] = c;
	}
	id6[6] = 0;

	if (load_cache(id6, out))
		return;

	ensure_net();
	if (net_ready < 0)
		return;

	snprintf(url, sizeof(url), "http://www.gametdb.com/Wii/%s", id6);
	if (!http_request(url, PAGE_MAX))
		return;
	http_get_result(&status, &data, &len);
	if (!data || len < 32) {
		if (data)
			free(data);
		return;
	}

	{
		char raw[16];
		take_cell((char *)data, len, "synopsis (EN)", out->synopsis, GAME_SYNOPSIS_MAX);
		if (!out->synopsis[0])
			take_cell((char *)data, len, "synopsis (", out->synopsis, GAME_SYNOPSIS_MAX);
		take_cell((char *)data, len, ">players", out->players, (int)sizeof(out->players));
		take_cell((char *)data, len, ">genre", out->genre, (int)sizeof(out->genre));
		take_cell((char *)data, len, ">developer", out->developer, (int)sizeof(out->developer));
		take_cell((char *)data, len, ">publisher", out->publisher, (int)sizeof(out->publisher));
		take_cell((char *)data, len, "release date", out->released, (int)sizeof(out->released));
		take_cell((char *)data, len, ">content", out->content, (int)sizeof(out->content));
		take_cell((char *)data, len, ">rating", raw, (int)sizeof(raw));
		free(data);
		if (raw[0] && esrb_code(raw))
			snprintf(out->rating, sizeof(out->rating), "ESRB %s", raw);
		else if (raw[0])
			snprintf(out->rating, sizeof(out->rating), "Rating %s", raw);
		save_cache(id6, out);
	}
}
