/* $Id: screen.c,v 1.26 2007-11-20 21:45:26 nicm Exp $ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicm@users.sourceforge.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>

#include <string.h>

#include "tmux.h"

/*
 * Virtual screen and basic terminal emulator.
 *
 * XXX Much of this file sucks.
 */

/* Colour to string. */
const char *
screen_colourstring(u_char c)
{
	switch (c) {
	case 0:
		return ("black");
	case 1:
		return ("red");
	case 2:
		return ("green");
	case 3:
		return ("yellow");
	case 4:
		return ("blue");
	case 5:
		return ("magenta");
	case 6:
		return ("cyan");
	case 7:
		return ("white");
	case 8:
		return ("default");
	}
	return (NULL);
}

/* String to colour. */
u_char
screen_stringcolour(const char *s)
{
	if (strcasecmp(s, "black") == 0 || (s[0] == '0' && s[1] == '\0'))
		return (0);
	if (strcasecmp(s, "red") == 0 || (s[0] == '1' && s[1] == '\0'))
		return (1);
	if (strcasecmp(s, "green") == 0 || (s[0] == '2' && s[1] == '\0'))
		return (2);
	if (strcasecmp(s, "yellow") == 0 || (s[0] == '3' && s[1] == '\0'))
		return (3);
	if (strcasecmp(s, "blue") == 0 || (s[0] == '4' && s[1] == '\0'))
		return (4);
	if (strcasecmp(s, "magenta") == 0 || (s[0] == '5' && s[1] == '\0'))
		return (5);
	if (strcasecmp(s, "cyan") == 0 || (s[0] == '6' && s[1] == '\0'))
		return (6);
	if (strcasecmp(s, "white") == 0 || (s[0] == '7' && s[1] == '\0'))
		return (7);
	if (strcasecmp(s, "default") == 0 || (s[0] == '8' && s[1] == '\0'))
		return (8);
	return (255);
}

/* Create a new screen. */
void
screen_create(struct screen *s, u_int dx, u_int dy)
{
	s->dx = dx;
	s->dy = dy;
	s->cx = 0;
	s->cy = 0;

	s->rupper = 0;
	s->rlower = s->dy - 1;

	s->ysize = dy;
	s->ylimit = SHRT_MAX;

	s->attr = SCREEN_DEFATTR;
	s->colr = SCREEN_DEFCOLR;

	s->mode = MODE_CURSOR;
	*s->title = '\0';

	s->grid_data = xmalloc(dy * (sizeof *s->grid_data));
	s->grid_attr = xmalloc(dy * (sizeof *s->grid_attr));
	s->grid_colr = xmalloc(dy * (sizeof *s->grid_colr));
	screen_make_lines(s, 0, dy);
}

/* Resize screen. */
void
screen_resize(struct screen *s, u_int sx, u_int sy)
{
	u_int	i, ox, oy, ny;

	if (sx == s->dx && sy == s->dy)
		return;

	if (sx < 1)
		sx = 1;
	if (sy < 1)
		sy = 1;

	ox = s->dx;
	oy = s->dy;

	log_debug("resizing screen (%u, %u) -> (%u, %u)", ox, oy, sx, sy);

	s->dx = sx;
	s->dy = sy;
       
	s->rupper = 0;
	s->rlower = s->dy - 1;

	s->ysize = dy;

	if (sy < oy) {
		ny = oy - sy;
		if (ny > s->cy)
			ny = s->cy;

		if (ny != 0) {
			log_debug("removing %u lines from top", ny);
			for (i = 0; i < ny; i++) {
				log_debug("freeing line %u", i);
				xfree(s->grid_data[i]);
				xfree(s->grid_attr[i]);
				xfree(s->grid_colr[i]);
			}
			memmove(s->grid_data, s->grid_data + ny,
			    (oy - ny) * (sizeof *s->grid_data));
			memmove(s->grid_attr, s->grid_attr + ny,
			    (oy - ny) * (sizeof *s->grid_attr));
			memmove(s->grid_colr, s->grid_colr + ny,
			    (oy - ny) * (sizeof *s->grid_colr));
			s->cy -= ny;
		}
		if (ny < oy - sy) {
			log_debug(
			    "removing %u lines from bottom", oy - sy - ny);
			for (i = sy; i < oy - ny; i++) {
				log_debug("freeing line %u", i);
				  xfree(s->grid_data[i]);
				  xfree(s->grid_attr[i]);
				  xfree(s->grid_colr[i]);
			}
			if (s->cy >= sy)
				s->cy = sy - 1;
		}
	}
	if (sy != oy) {
		s->grid_data = xrealloc(s->grid_data, sy, sizeof *s->grid_data);
		s->grid_attr = xrealloc(s->grid_attr, sy, sizeof *s->grid_attr);
		s->grid_colr = xrealloc(s->grid_colr, sy, sizeof *s->grid_colr);
	}
	if (sy > oy) {
		for (i = oy; i < sy; i++) {
			log_debug("allocating line %u", i);
			s->grid_data[i] = xmalloc(sx);
			s->grid_attr[i] = xmalloc(sx);
			s->grid_colr[i] = xmalloc(sx);
			screen_display_fill_line(s, i,
			    SCREEN_DEFDATA, SCREEN_DEFATTR, SCREEN_DEFCOLR);
		}
		sy = oy;
	}

	if (sx != ox) {
		for (i = 0; i < sy; i++) {
			log_debug("adjusting line %u to %u", i, sx);
			s->grid_data[i] = xrealloc(s->grid_data[i], sx, 1);
			s->grid_attr[i] = xrealloc(s->grid_attr[i], sx, 1);
			s->grid_colr[i] = xrealloc(s->grid_colr[i], sx, 1);
			if (sx > ox) {
				screen_display_fill_cells(s, ox, i, s->dx - ox,
				    SCREEN_DEFDATA, SCREEN_DEFATTR,
				    SCREEN_DEFCOLR);
			}
		}
		if (s->cx >= sx)
			s->cx = sx - 1;
	}
}

/* Destroy a screen. */
void
screen_destroy(struct screen *s)
{
	screen_free_lines(s, 0, s->dy);
	xfree(s->grid_data);
	xfree(s->grid_attr);
	xfree(s->grid_colr);
}

/* Draw a set of lines on the screen. */
void
screen_draw(struct screen *s, struct buffer *b, u_int uy, u_int ly)
{
	u_char		 attr, colr;
	u_int		 i, j;

	if (uy > s->dy - 1 || ly > s->dy - 1 || ly < uy)
		fatalx("bad range");

	/* XXX. This is naive and rough right now. */
	attr = 0;
	colr = SCREEN_DEFCOLR;

	input_store_two(b, CODE_SCROLLREGION, s->rupper + 1, s->rlower + 1);

	input_store_zero(b, CODE_CURSOROFF);
	input_store_two(b, CODE_ATTRIBUTES, attr, colr);

	for (j = uy; j <= ly; j++) {
		input_store_two(b, CODE_CURSORMOVE, j + 1, 1);

		for (i = 0; i <= screen_last_x(s); i++) {
			if (s->grid_attr[j][i] != attr ||
			    s->grid_colr[j][i] != colr) {
				input_store_two(b, CODE_ATTRIBUTES,
				    s->grid_attr[j][i], s->grid_colr[j][i]);
				attr = s->grid_attr[j][i];
				colr = s->grid_colr[j][i];
			}
			input_store8(b, s->grid_data[j][i]);
		}
	}
	input_store_two(b, CODE_CURSORMOVE, s->cy + 1, s->cx + 1);

	input_store_two(b, CODE_ATTRIBUTES, s->attr, s->colr);
	if (s->mode & MODE_CURSOR)
		input_store_zero(b, CODE_CURSORON);
}

/* Create a range of lines. */
void
screen_make_lines(struct screen *s, u_int py, u_int ny)
{
	u_int	i;

	for (i = py; i < py + ny; i++) {
		s->grid_data[i] = xmalloc(s->dx);
		s->grid_attr[i] = xmalloc(s->dx);
		s->grid_colr[i] = xmalloc(s->dx);
	}
	screen_fill_lines(
	    s, py, ny, SCREEN_DEFDATA, SCREEN_DEFATTR, SCREEN_DEFCOLR);
}


/* Free a range of ny lines at py. */
void
screen_free_lines(struct screen *s, u_int py, u_int ny)
{
	u_int	i;

	for (i = py; i < py + ny; i++) {
		xfree(s->grid_data[i]);
		xfree(s->grid_attr[i]);
		xfree(s->grid_colr[i]);
	}
}

/* Move a range of lines. */
void
screen_move_lines(struct screen *s, u_int dy, u_int py, u_int ny)
{
	memmove(
	    &s->grid_data[dy], &s->grid_data[py], ny * (sizeof *s->grid_data));
	memmove(
	    &s->grid_attr[dy], &s->grid_attr[py], ny * (sizeof *s->grid_attr));
	memmove(
	    &s->grid_colr[dy], &s->grid_colr[py], ny * (sizeof *s->grid_colr));
}

/* Fill a range of lines. */
void
screen_fill_lines(
    struct screen *s, u_int py, u_int ny, u_char data, u_char attr, u_char colr)
{
	u_int	i;

	for (i = py; i < py + ny; i++)
		screen_fill_cells(s, 0, i, s->dx, data, attr, colr);
}

/* Fill a range of cells. */
void
screen_fill_cells(struct screen *s,
    u_int px, u_int py, u_int nx, u_char data, u_char attr, u_char colr)
{
	memset(&s->grid_data[py][px], data, nx);
	memset(&s->grid_attr[py][px], attr, nx);
	memset(&s->grid_colr[py][px], colr, nx);
}
