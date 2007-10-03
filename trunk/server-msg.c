/* $Id: server-msg.c,v 1.18 2007-10-03 12:34:16 nicm Exp $ */

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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tmux.h"

int	server_msg_fn_attach(struct hdr *, struct client *);
int	server_msg_fn_bindkey(struct hdr *, struct client *);
int	server_msg_fn_keys(struct hdr *, struct client *);
int	server_msg_fn_new(struct hdr *, struct client *);
int	server_msg_fn_rename(struct hdr *, struct client *);
int	server_msg_fn_sessions(struct hdr *, struct client *);
int	server_msg_fn_size(struct hdr *, struct client *);
int	server_msg_fn_unbindkey(struct hdr *, struct client *);
int	server_msg_fn_windowlist(struct hdr *, struct client *);
int	server_msg_fn_windows(struct hdr *, struct client *);

struct server_msg {
	enum hdrtype	type;
	
	int	        (*fn)(struct hdr *, struct client *);
};
const struct server_msg server_msg_table[] = {
	{ MSG_ATTACH, server_msg_fn_attach },
	{ MSG_BINDKEY, server_msg_fn_bindkey },
	{ MSG_KEYS, server_msg_fn_keys },
	{ MSG_NEW, server_msg_fn_new },
	{ MSG_RENAME, server_msg_fn_rename },
	{ MSG_SESSIONS, server_msg_fn_sessions },
	{ MSG_SIZE, server_msg_fn_size },
	{ MSG_UNBINDKEY, server_msg_fn_unbindkey },
	{ MSG_WINDOWLIST, server_msg_fn_windowlist },
	{ MSG_WINDOWS, server_msg_fn_windows },
};
#define NSERVERMSG (sizeof server_msg_table / sizeof server_msg_table[0])

int
server_msg_dispatch(struct client *c)
{
	struct hdr		 hdr;
	const struct server_msg	*msg;
	u_int		 	 i;
	int			 n;

	for (;;) {
		if (BUFFER_USED(c->in) < sizeof hdr)
			return (0);
		memcpy(&hdr, BUFFER_OUT(c->in), sizeof hdr);
		if (BUFFER_USED(c->in) < (sizeof hdr) + hdr.size)
			return (0);
		buffer_remove(c->in, sizeof hdr);
		
		for (i = 0; i < NSERVERMSG; i++) {
			msg = server_msg_table + i;
			if (msg->type == hdr.type) {
				if ((n = msg->fn(&hdr, c)) != 0)
					return (n);
				break;
			}
		}	
		if (i == NSERVERMSG)
			fatalx("unexpected message");
	}
}

/* New message from client. */
int
server_msg_fn_new(struct hdr *hdr, struct client *c)
{
	struct new_data	 data;
	char	         *msg;
	
	if (c->session != NULL)
		return (0);
	if (hdr->size != sizeof data)
		fatalx("bad MSG_NEW size");
	buffer_read(c->in, &data, sizeof data);

	c->sx = data.sx;
	if (c->sx == 0)
		c->sx = 80;
	c->sy = data.sy;
	if (c->sy == 0)
		c->sy = 25;

	if (c->sy >= status_lines)
		c->sy -= status_lines;

	data.name[(sizeof data.name) - 1] = '\0';
	if (*data.name != '\0' && session_find(data.name) != NULL) {
		xasprintf(&msg, "duplicate session: %s", data.name);
		server_write_client(c, MSG_ERROR, msg, strlen(msg));
		xfree(msg);
		return (0);
	}

	c->session = session_create(data.name, default_command, c->sx, c->sy);
	if (c->session == NULL)
		fatalx("session_create failed");

	server_write_client(c, MSG_OKAY, NULL, 0);
	server_draw_client(c, 0, c->sy - 1);

	return (0);
}

/* Attach message from client. */
int
server_msg_fn_attach(struct hdr *hdr, struct client *c)
{
	struct attach_data	 data;
	char			*cause;
	
	if (c->session != NULL)
		return (0);
	if (hdr->size != sizeof data)
		fatalx("bad MSG_ATTACH size");
	buffer_read(c->in, &data, sizeof data);

	c->sx = data.sx;
	if (c->sx == 0)
		c->sx = 80;
	c->sy = data.sy;
	if (c->sy == 0)
		c->sy = 25;

	if (c->sy >= status_lines)
		c->sy -= status_lines;

	if ((c->session = server_find_sessid(&data.sid, &cause)) == NULL) {
		server_write_error(c, "%s", cause);
		xfree(cause);
		return (0);
	}

	server_draw_client(c, 0, c->sy - 1);

	return (0);
}

/* Size message from client. */
int
server_msg_fn_size(struct hdr *hdr, struct client *c)
{
	struct size_data	data;

	if (c->session == NULL)
		return (0);
	if (hdr->size != sizeof data)
		fatalx("bad MSG_SIZE size");
	buffer_read(c->in, &data, sizeof data);

	c->sx = data.sx;
	if (c->sx == 0)
		c->sx = 80;
	c->sy = data.sy;
	if (c->sy == 0)
		c->sy = 25;

	if (c->sy >= status_lines)
		c->sy -= status_lines;

	if (window_resize(c->session->window, c->sx, c->sy) != 0)
		server_draw_client(c, 0, c->sy - 1);

	return (0);
}

/* Keys message from client. */
int
server_msg_fn_keys(struct hdr *hdr, struct client *c)
{
	int	key;
	size_t	size;

	if (c->session == NULL)
		return (0);
	if (hdr->size & 0x1)
		fatalx("bad MSG_KEYS size");

	size = hdr->size;
	while (size != 0) {
		key = (int16_t) input_extract16(c->in);
		size -= 2;

		if (c->prefix) {
			cmd_dispatch(c, key);
			c->prefix = 0;
			continue;
		}

		if (key == cmd_prefix)
			c->prefix = 1;
		else
			window_key(c->session->window, key);
	}

	return (0);
}

/* Sessions message from client. */
int
server_msg_fn_sessions(struct hdr *hdr, struct client *c)
{
	struct sessions_data	 data;
	struct sessions_entry	 entry;
	struct session		*s;
	u_int			 i, j;

	if (hdr->size != sizeof data)
		fatalx("bad MSG_SESSIONS size");
	buffer_read(c->in, &data, sizeof data);

	data.sessions = 0;
	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		if (ARRAY_ITEM(&sessions, i) != NULL)
			data.sessions++;
	}
	server_write_client2(c, MSG_SESSIONS,
	    &data, sizeof data, NULL, data.sessions * sizeof entry);

	for (i = 0; i < ARRAY_LENGTH(&sessions); i++) {
		s = ARRAY_ITEM(&sessions, i);
		if (s == NULL)
			continue;
		strlcpy(entry.name, s->name, sizeof entry.name);
		entry.tim = s->tim;
		entry.windows = 0;
		for (j = 0; j < ARRAY_LENGTH(&s->windows); j++) {
			if (ARRAY_ITEM(&s->windows, j) != NULL)
				entry.windows++;
		}
		buffer_write(c->out, &entry, sizeof entry);
	}

	return (0);
}

/* Windows message from client. */
int
server_msg_fn_windows(struct hdr *hdr, struct client *c)
{
	struct windows_data	 data;
	struct windows_entry	 entry;
	struct session		*s;
	struct window		*w;
	u_int			 i;
	char		 	*cause;

	if (hdr->size != sizeof data)
		fatalx("bad MSG_WINDOWS size");
	buffer_read(c->in, &data, sizeof data);

	if ((s = server_find_sessid(&data.sid, &cause)) == NULL) {
		server_write_error(c, "%s", cause);
		xfree(cause);
		return (0);
	}

	data.windows = 0;
	for (i = 0; i < ARRAY_LENGTH(&s->windows); i++) {
		if (ARRAY_ITEM(&s->windows, i) != NULL)
			data.windows++;
	}
	server_write_client2(c, MSG_WINDOWS,
	    &data, sizeof data, NULL, data.windows * sizeof entry);
	
	for (i = 0; i < ARRAY_LENGTH(&s->windows); i++) {
		w = ARRAY_ITEM(&s->windows, i);
		if (w == NULL)
			continue;
		entry.idx = i;
		strlcpy(entry.name, w->name, sizeof entry.name);
		strlcpy(entry.title, w->screen.title, sizeof entry.title);
		if (ttyname_r(w->fd, entry.tty, sizeof entry.tty) != 0)
			*entry.tty = '\0';
		buffer_write(c->out, &entry, sizeof entry);
	}

	return (0);
}

/* Rename message from client. */
int
server_msg_fn_rename(struct hdr *hdr, struct client *c)
{
	struct rename_data	data;
	char                   *cause;
	struct window	       *w;
	struct session	       *s;
	u_int			i;

	if (hdr->size != sizeof data)
		fatalx("bad MSG_RENAME size");
	buffer_read(c->in, &data, sizeof data);

 	data.newname[(sizeof data.newname) - 1] = '\0';
	if ((s = server_find_sessid(&data.sid, &cause)) == NULL) {
		server_write_error(c, "%s", cause);
		xfree(cause);
		return (0);
	}

	if (data.idx == -1)
		w = s->window;
	else {
		if (data.idx < 0)
			fatalx("bad window index");
		w = window_at(&s->windows, data.idx);
		if (w == NULL) { 
			server_write_error(c, "window not found: %d", data.idx);
			return (0);
		}
	}

	strlcpy(w->name, data.newname, sizeof w->name);

	server_write_client(c, MSG_OKAY, NULL, 0);
	for (i = 0; i < ARRAY_LENGTH(&clients); i++) {
		c = ARRAY_ITEM(&clients, i);
		if (c != NULL && c->session != NULL) {
			if (session_has(c->session, w))
				server_draw_status(c);
		}
	}

	return (0);
}

/* Window list message from client */
int
server_msg_fn_windowlist(struct hdr *hdr, struct client *c)
{
	struct window	*w;
	char 		*buf;
	size_t		 len, off;
	u_int 		 i;

	if (c->session == NULL)
		return (0);
	if (hdr->size != 0)
		fatalx("bad MSG_WINDOWLIST size");

	len = c->sx + 1;
	buf = xmalloc(len);
	off = 0;

	*buf = '\0';
	for (i = 0; i < ARRAY_LENGTH(&c->session->windows); i++) {
		w = ARRAY_ITEM(&c->session->windows, i);
		if (w == NULL)
			continue;
		off += xsnprintf(buf + off, len - off, "%u:%s%s ", i, w->name, 
		    w == c->session->window ? "*" : "");
		if (off >= len)
			break;
	}

	server_write_message(c, "%s", buf);
	xfree(buf);

	return (0);
}

/* Bind key message from client */
int
server_msg_fn_bindkey(struct hdr *hdr, struct client *c)
{
	struct bind_data	data;
	const struct bind      *bind;
	char		       *str;

	if (hdr->size < sizeof data)
		fatalx("bad MSG_BINDKEY size");
	buffer_read(c->in, &data, sizeof data);

	str = NULL;
	if (data.flags & BIND_STRING) {
		hdr->size -= sizeof data;

		if (hdr->size != 0) {
			str = xmalloc(hdr->size + 1);
			buffer_read(c->in, str, hdr->size);
			str[hdr->size] = '\0';
		}
		if (*str == '\0') {
			xfree(str);
			str = NULL;
		}
	}

 	data.cmd[(sizeof data.cmd) - 1] = '\0';	
	if ((bind = cmd_lookup_bind(data.cmd)) == NULL)
		fatalx("unknown command");
	if (!(bind->flags & BIND_USER) &&
	    (data.flags & (BIND_NUMBER|BIND_STRING)) != 0)
		fatalx("argument missing");
	if ((bind->flags & BIND_USER) &&
	    (data.flags & (BIND_NUMBER|BIND_STRING)) == 0)
		fatalx("argument required");
	
	cmd_add_bind(data.key, data.num, str, bind);
	if (str != NULL)
		xfree(str);

	server_write_client(c, MSG_OKAY, NULL, 0);

	return (0);
}

/* Unbind key message from client */
int
server_msg_fn_unbindkey(struct hdr *hdr, struct client *c)
{
	struct bind_data	data;

	if (hdr->size != sizeof data)
		fatalx("bad MSG_UNBINDKEY size");

	buffer_read(c->in, &data, hdr->size);

	cmd_remove_bind(data.key);

	server_write_client(c, MSG_OKAY, NULL, 0);

	return (0);
}
