/* $Id$ */

/*
 * Copyright (c) 2012 Thomas Adam <thomas@xteddy.org>
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

#include <ctype.h>

#include <string.h>

#include "tmux.h"

/*
 * Enter choice mode to choose a session and/or window.
 */

int	cmd_choose_tree_exec(struct cmd *, struct cmd_ctx *);

void	cmd_choose_tree_callback(struct window_choose_data *);
void	cmd_choose_tree_free(struct window_choose_data *);

const struct cmd_entry cmd_choose_tree_entry = {
	"choose-tree", NULL,
	"SWs:w:b:c:t:", 0, 1,
	"[-S] [-W] [-s format] [-w format ] [-b session template] " \
		"[-c window template] " CMD_TARGET_WINDOW_USAGE,
	0,
	NULL,
	NULL,
	cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_session_entry = {
	"choose-session", NULL,
	"Ss:b:t:", 0, 1,
	CMD_TARGET_WINDOW_USAGE " [-s format] [-b session template]",
	0,
	NULL,
	NULL,
	cmd_choose_tree_exec
};

const struct cmd_entry cmd_choose_window_entry = {
	"choose-window", NULL,
	"Ww:c:t:", 0, 1,
	CMD_TARGET_WINDOW_USAGE "[-W] [-w format] [-c window template]",
	0,
	NULL,
	NULL,
	cmd_choose_tree_exec
};

int
cmd_choose_tree_exec(struct cmd *self, struct cmd_ctx *ctx)
{
	struct args			*args = self->args;
	struct winlink			*wl, *wm;
	struct session			*s, *s2;
	struct tty			*tty;
	struct window_choose_data	*wcd = NULL;
	const char			*ses_template, *win_template;
	char				*final_win_action, *final_win_template;
	const char			*ses_action, *win_action;
	u_int				 cur_win, idx_ses, win_ses;
	u_int				 wflag, sflag;

	if (ctx->curclient == NULL) {
		ctx->error(ctx, "must be run interactively");
		return (-1);
	}

	sflag = self->entry == &cmd_choose_session_entry;
	wflag = self->entry == &cmd_choose_window_entry;

	s = ctx->curclient->session;
	tty = &ctx->curclient->tty;

	if ((wl = cmd_find_window(ctx, args_get(args, 't'), NULL)) == NULL)
		return (-1);

	if (window_pane_set_mode(wl->window->active, &window_choose_mode) != 0)
		return (0);

	if ((ses_action = args_get(args, 'b')) == NULL)
		ses_action = "switch-client -t '%%'";

	if ((win_action = args_get(args, 'c')) == NULL)
		win_action = "select-window -t '%%'";

	if ((ses_template = args_get(args, 's')) == NULL)
		ses_template = DEFAULT_SESSION_TEMPLATE;

	if ((win_template = args_get(args, 'w')) == NULL)
		win_template = DEFAULT_WINDOW_TEMPLATE " \"#{pane_title}\"";

	if (self->entry == &cmd_choose_tree_entry) {
		wflag = args_has(args, 'W');
		sflag = args_has(args, 'S');
	}

	if (!wflag && !sflag) {
		ctx->error(ctx, "Nothing to display, no flags given.");
		window_pane_reset_mode(wl->window->active);
		return (-1);
	}

	/* If we're drawing in tree mode, including sessions, then pad the
	 * window template with ACS drawing characters, otherwise just render
	 * the windows as a flat list, without any padding.
	 */
	if (wflag && sflag)
		xasprintf(&final_win_template, "   %s%s> %s",
			tty_acs_get(tty, 't'), tty_acs_get(tty, 'q'),
			win_template);
	else
		final_win_template = xstrdup(win_template);

	idx_ses = cur_win = -1;
	RB_FOREACH(s2, sessions, &sessions) {
		idx_ses++;

		/* If we're just choosing windows, jump straight there.  Note
		 * that this implies the current session, so only choose
		 * windows when the session matches this one.
		 */
		if (wflag && !sflag) {
			if (s != s2)
				continue;
			goto windows_only;
		}

		wcd = window_choose_add_session(wl->window->active,
			ctx, s2, ses_template, (char *)ses_action, idx_ses);

		/* If we're just choosing sessions, skip choosing windows. */
		if (sflag && !wflag) {
			if (s == s2)
				cur_win = idx_ses;
			continue;
		}
windows_only:
		win_ses = -1;
		RB_FOREACH(wm, winlinks, &s2->windows) {
			win_ses++;
			if (sflag && wflag)
				idx_ses++;

			if (wm == s2->curw && s == s2) {
				if (wflag && !sflag)
					/* Then we're only counting windows.
					 * So remember which is the current
					 * window in the list.
					 */
					cur_win = win_ses;
				else
					cur_win = idx_ses;
			}

			xasprintf(&final_win_action, "%s ; %s", win_action,
				wcd ? wcd->command : "");

			window_choose_add_window(wl->window->active,
				ctx, s2, wm, final_win_template,
				final_win_action, idx_ses);

			xfree(final_win_action);
		}
		/* If we're just drawing windows, don't consider moving on to
		 * other sessions as we only list windows in this session.
		 */
		if (wflag && !sflag)
			break;
	}
	xfree(final_win_template);

	window_choose_ready(wl->window->active, cur_win,
		cmd_choose_tree_callback, cmd_choose_tree_free);

	return (0);
}

void
cmd_choose_tree_callback(struct window_choose_data *cdata)
{
	if (cdata == NULL)
		return;

	if (cdata->client->flags & CLIENT_DEAD)
		return;

	window_choose_ctx(cdata);
}

void
cmd_choose_tree_free(struct window_choose_data *cdata)
{
	cdata->session->references--;
	cdata->client->references--;

	xfree(cdata->ft_template);
	xfree(cdata->command);
	format_free(cdata->ft);
	xfree(cdata);

}

