/*

Nintendont (Loader) - Playing Gamecubes in Wii mode on a Wii U

Copyright (C) 2013  crediar

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>

#include "font.h"
#include "global.h"
#include "theme.h"

void PrintFormat(u8 size, const u32 color, int x, int y, const char *str, ... )
{
	char astr[2048];
	u32 draw = color;

	va_list ap;
	va_start( ap, str );

	vsnprintf(astr, sizeof(astr), str, ap);

	va_end( ap );
	/* List, settings, and the message screens pass BLACK. On a dark theme
	   that disappears, so map it to the theme text. */
	if (Theme_IsReady() && !Theme_IsClassic()) {
		if (draw == 0x000000FF)
			draw = Theme_ColorText();
		else if (draw == 0x666666FF)
			draw = Theme_Current()->text_dim;
	}
	GRRLIB_PrintfTTF(x, y, myFont, astr, size, draw);
}
