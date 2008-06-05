/* $Id: cmd.c,v 1.42 2008-06-05 16:35:32 nicm Exp $ */

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
#include <sys/time.h>

#include <fnmatch.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

const struct cmd_entry *cmd_table[] = {
	&cmd_attach_session_entry,
	&cmd_bind_key_entry,
	&cmd_copy_mode_entry,
	&cmd_detach_client_entry,
	&cmd_has_session_entry,
	&cmd_kill_server_entry,
	&cmd_kill_session_entry,
	&cmd_kill_window_entry,
	&cmd_last_window_entry,
	&cmd_link_window_entry,
	&cmd_list_clients_entry,
	&cmd_list_keys_entry,
	&cmd_list_sessions_entry,
	&cmd_list_windows_entry,
	&cmd_new_session_entry,
	&cmd_new_window_entry,
	&cmd_next_window_entry,
	&cmd_paste_buffer_entry,
	&cmd_previous_window_entry,
	&cmd_refresh_client_entry,
	&cmd_rename_session_entry,
	&cmd_rename_window_entry,
	&cmd_scroll_mode_entry,
	&cmd_select_window_entry,
	&cmd_set_window_option_entry,
	&cmd_send_keys_entry,
	&cmd_send_prefix_entry,
	&cmd_set_option_entry,
	&cmd_start_server_entry,
	&cmd_swap_window_entry,
	&cmd_switch_client_entry,
	&cmd_unbind_key_entry,
	&cmd_unlink_window_entry,
	NULL
};

struct client	*cmd_lookup_client(const char *);
struct session	*cmd_lookup_session(const char *);

struct cmd *
cmd_parse(int argc, char **argv, char **cause)
{
	const struct cmd_entry **entryp, *entry;
	struct cmd	        *cmd;
	char			 s[BUFSIZ];
	int			 opt;

	*cause = NULL;
	if (argc == 0)
		return (NULL);

	entry = NULL;
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if ((*entryp)->alias != NULL &&
		    strcmp((*entryp)->alias, argv[0]) == 0) {
			entry = *entryp;
			break;
		}

		if (strncmp((*entryp)->name, argv[0], strlen(argv[0])) != 0)
			continue;
		if (entry != NULL)
			goto ambiguous;
		entry = *entryp;
	}
	if (entry == NULL) {
		xasprintf(cause, "unknown command: %s", argv[0]);
		return (NULL);
	}

	optind = 1;
	if (entry->parse == NULL) {
		while ((opt = getopt(argc, argv, "")) != EOF) {
			switch (opt) {
			default:
				goto usage;
			}
		}
		argc -= optind;
		argv += optind;
		if (argc != 0)
			goto usage;
	}

	cmd = xmalloc(sizeof *cmd);
	cmd->entry = entry;
	if (entry->parse != NULL) {
		if (entry->parse(cmd, argc, argv, cause) != 0) {
			xfree(cmd);
			return (NULL);
		}
	}
	return (cmd);

ambiguous:
	*s = '\0';
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if (strncmp((*entryp)->name, argv[0], strlen(argv[0])) != 0)
			continue;
		if (strlcat(s, (*entryp)->name, sizeof s) >= sizeof s)
			break;
		if (strlcat(s, ", ", sizeof s) >= sizeof s)
			break;
	}
	s[strlen(s) - 2] = '\0';
	xasprintf(cause, "ambiguous command: %s, could be: %s", argv[0], s);
	return (NULL);

usage:
	xasprintf(cause, "usage: %s %s", entry->name, entry->usage);
	return (NULL);
}

void
cmd_exec(struct cmd *cmd, struct cmd_ctx *ctx)
{
	cmd->entry->exec(cmd, ctx);
}

void
cmd_send(struct cmd *cmd, struct buffer *b)
{
	const struct cmd_entry **entryp;
	u_int			 n;

	n = 0;
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if (*entryp == cmd->entry)
			break;
		n++;
	}
	if (*entryp == NULL)
		fatalx("command not found");

	buffer_write(b, &n, sizeof n);

	if (cmd->entry->send != NULL)
		cmd->entry->send(cmd, b);
}

struct cmd *
cmd_recv(struct buffer *b)
{
	const struct cmd_entry **entryp;
	struct cmd   	        *cmd;
	u_int			 m, n;

	buffer_read(b, &m, sizeof m);

	n = 0;
	for (entryp = cmd_table; *entryp != NULL; entryp++) {
		if (n == m)
			break;
		n++;
	}
	if (*entryp == NULL)
		fatalx("command not found");

	cmd = xmalloc(sizeof *cmd);
	cmd->entry = *entryp;

	if (cmd->entry->recv != NULL)
		cmd->entry->recv(cmd, b);
	return (cmd);
}

void
cmd_free(struct cmd *cmd)
{
	if (cmd->data != NULL && cmd->entry->free != NULL)
		cmd->entry->free(cmd);
	xfree(cmd);
}

void
cmd_send_string(struct buffer *b, const char *s)
{
	size_t	n;

	if (s == NULL) {
		n = 0;
		buffer_write(b, &n, sizeof n);
		return;
	}

	n = strlen(s) + 1;
	buffer_write(b, &n, sizeof n);

	buffer_write(b, s, n);
}

char *
cmd_recv_string(struct buffer *b)
{
	char   *s;
	size_t	n;

	buffer_read(b, &n, sizeof n);

	if (n == 0)
		return (NULL);

	s = xmalloc(n);
	buffer_read(b, s, n);
	s[n - 1] = '\0';

	return (s);
}

struct client *
cmd_lookup_client(const char *cname)
{
	struct client	*c;
	u_int		 i;

	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c != NULL && strcmp(cname, c->tty.path) == 0)
			return (c);
	}

	return (NULL);
}

struct session *
cmd_lookup_session(const char *sname)
{
	struct session	*s, *newest = NULL;
	struct timespec	*ts;
	u_int		 i;

	ts = NULL;
	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i); 
		if (s == NULL || fnmatch(sname, s->name, 0) != 0)
			continue;

		if (ts == NULL || timespeccmp(&s->ts, ts, >)) {
			newest = s;
			ts = &s->ts;
		}
	}

	return (newest);
}

/* 
 * Figure out the client. Try the given client name first, then the current
 * client.
 */
struct client *
cmd_find_client(unused struct cmd_ctx *ctx, const char *cname)
{
	struct client	*c;

	if (cname != NULL) {
		if ((c = cmd_lookup_client(cname)) == NULL)
			ctx->error(ctx, "client not found: %s", cname);
		return (c);
	}

	if (ctx->curclient != NULL)
		return (ctx->curclient);

	ctx->error(ctx, "must specify a client");
	return (NULL);
}

/*
 * Attempt to establish session. This looks first at the given arguments,
 * if any, then sees if there is a session in the context, then finally tries
 * the session data passed up from the client $TMUX variable.
 */
struct session *
cmd_find_session(struct cmd_ctx *ctx, const char *cname, const char *sname)
{
	struct session		*s, *newest = NULL;
	struct client		*c;
	struct msg_command_data	*data = ctx->msgdata;
	u_int			 i;
	struct timespec		*ts;

	if (cname != NULL) {
		if ((c = cmd_lookup_client(cname)) == NULL) {
			ctx->error(ctx, "client not found: %s", cname);
			return (NULL);
		}
		if (c->session == NULL)
			ctx->error(ctx, "client has no session: %s", cname);
		return (c->session);
	}

	if (sname != NULL) {
		if ((s = cmd_lookup_session(sname)) == NULL)
			ctx->error(ctx, "session not found: %s", sname);
		return (s);
	}

	if (ctx->cursession != NULL)
		return (ctx->cursession);

	if (data != NULL && data->pid != -1) {
		if (data->pid != getpid()) {
			ctx->error(ctx, "wrong server: %lld", data->pid);
			return (NULL);
		}
		if (data->idx > ARRAY_LENGTH(&sessions)) {
			ctx->error(ctx, "index out of range: %d", data->idx);
			return (NULL);
		}
		if ((s = ARRAY_ITEM(&sessions, data->idx)) == NULL) {
			ctx->error(ctx, "session doesn't exist: %u", data->idx);
			return (NULL);
		}
		return (s);
	}

	ts = NULL;
	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i); 
		if (s != NULL && (ts == NULL || timespeccmp(&s->ts, ts, >))) {
			newest = ARRAY_ITEM(&sessions, i);
			ts = &s->ts;
		}
	}
	if (newest == NULL)
		ctx->error(ctx, "no sessions found");
	return (newest);
}

struct winlink *
cmd_find_window(struct cmd_ctx *ctx, 
    const char *cname, const char *sname, int idx, struct session **sp)
{
	struct session			*s;
	struct winlink			*wl;

	if ((s = cmd_find_session(ctx, cname, sname)) == NULL)
		return (NULL);
	if (sp != NULL)
		*sp = s;

	if (idx == -1)
		return (s->curw);
	if ((wl = winlink_find_by_index(&s->windows, idx)) == NULL)
		ctx->error(ctx, "no window %d", idx);
	return (wl);
}
