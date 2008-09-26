/* $Id: screen.c,v 1.73 2008-09-26 06:45:26 nicm Exp $ */

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

void	screen_resize_x(struct screen *, u_int);
void	screen_resize_y(struct screen *, u_int);

/* Create a new screen. */
void
screen_init(struct screen *s, u_int sx, u_int sy, u_int hlimit)
{
	s->grid = grid_create(sx, sy, hlimit);

	s->title = xstrdup("");

	screen_reinit(s);
}

/* Reinitialise screen. */
void
screen_reinit(struct screen *s)
{
	s->cx = 0;
	s->cy = 0;

	s->rupper = 0;
	s->rlower = screen_size_y(s) - 1;

	s->mode = MODE_CURSOR;

	/* XXX */
	grid_clear_lines(
	    s->grid, s->grid->hsize, s->grid->hsize + s->grid->sy - 1);

	screen_clear_selection(s);
}

/* Destroy a screen. */
void
screen_free(struct screen *s)
{
	xfree(s->title);
	grid_destroy(s->grid);
}

/* Set screen title. */
void
screen_set_title(struct screen *s, const char *title)
{
	xfree(s->title);
	s->title = xstrdup(title);
}

/* Resize screen. */
void
screen_resize(struct screen *s, u_int sx, u_int sy)
{
	if (sx < 1)
		sx = 1;
	if (sy < 1)
		sy = 1;

	if (sx != screen_size_x(s))
		screen_resize_x(s, sx);
	if (sy != screen_size_y(s))
		screen_resize_y(s, sy);
}

void
screen_resize_x(struct screen *s, u_int sx)
{
	struct grid_data	*gd = s->grid;
	const struct grid_cell	*gc;
	u_int			 xx, yy;

	/* If getting larger, not much to do. */
	if (sx > screen_size_x(s)) {
		gd->sx = sx;
		return;
	}

	/* If getting smaller, nuke any data in lines over the new size. */
	for (yy = gd->hsize; yy < gd->hsize + screen_size_y(s); yy++) {
		/*
		 * If the character after the last is wide or padding, remove
		 * it and any leading padding.
		 */
		for (xx = sx; xx > 0; xx--) {
			gc = grid_peek_cell(gd, xx - 1, yy);
			if (!(gc->flags & GRID_FLAG_PADDING))
				break;
			grid_set_cell(gd, xx - 1, yy, &grid_default_cell);
		}
		if (xx > 0 && xx != sx && utf8_width(gc->data) != 1)
			grid_set_cell(gd, xx - 1, yy, &grid_default_cell);

		/* Reduce the line size. */
		grid_reduce_line(gd, yy, sx);
	}

	if (s->cx >= sx)
		s->cx = sx - 1;
	gd->sx = sx;
}

void
screen_resize_y(struct screen *s, u_int sy)
{
	struct grid_data	*gd = s->grid;
	u_int			 oy, yy, ny;

	/* Size decreasing. */
	if (sy < screen_size_y(s)) {
		oy = screen_size_y(s);

		if (s->cy != 0) {
			/*
			 * The cursor is not at the start. Try to remove as
			 * many lines as possible from the top. (Up to the
			 * cursor line.)
			 */
			ny = s->cy;
			if (ny > oy - sy)
				ny = oy - sy;

			grid_view_delete_lines(gd, 0, ny);

 			s->cy -= ny;
			oy -= ny;
		}

		if (sy < oy) {
			/* Remove any remaining lines from the bottom. */
			grid_view_delete_lines(gd, sy, oy - sy);
			if (s->cy >= sy)
				s->cy = sy - 1;
		}
	}

	/* Resize line arrays. */
	gd->size = xrealloc(gd->size, gd->hsize + sy, sizeof *gd->size);
	gd->data = xrealloc(gd->data, gd->hsize + sy, sizeof *gd->data);

	/* Size increasing. */
	if (sy > screen_size_y(s)) {
		oy = screen_size_y(s);
		for (yy = gd->hsize + oy; yy < gd->hsize + sy; yy++) {
			gd->size[yy] = 0;
			gd->data[yy] = NULL;
		}
	}

	gd->sy = sy;

	s->rupper = 0;
	s->rlower = screen_size_y(s) - 1;
}

/* Set selection. */
void
screen_set_selection(struct screen *s, u_int sx, u_int sy, u_int ex, u_int ey)
{
	struct screen_sel	*sel = &s->sel;

	sel->flag = 1;
	if (ey < sy || (sy == ey && ex < sx)) {
		sel->sx = ex; sel->sy = ey;
		sel->ex = sx; sel->ey = sy;
	} else {
		sel->sx = sx; sel->sy = sy;
		sel->ex = ex; sel->ey = ey;
	}
}

/* Clear selection. */
void
screen_clear_selection(struct screen *s)
{
	struct screen_sel	*sel = &s->sel;

	sel->flag = 0;
}

/* Check if cell in selection. */
int
screen_check_selection(struct screen *s, u_int px, u_int py)
{
	struct screen_sel	*sel = &s->sel;

	if (!sel->flag || py < sel->sy || py > sel->ey)
		return (0);

	if (py == sel->sy && py == sel->ey) {
		if (px < sel->sx || px > sel->ex)
			return (0);
		return (1);
	}

	if ((py == sel->sy && px < sel->sx) || (py == sel->ey && px > sel->ex))
		return (0);
	return (1);
}
