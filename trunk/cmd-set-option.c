/* $Id: cmd-set-option.c,v 1.1 2007-10-04 10:11:32 nicm Exp $ */

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

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Set an option.
 */

int		 cmd_set_option_parse(void **, int, char **, char **);
const char	*cmd_set_option_usage(void);
void		 cmd_set_option_exec(void *, struct cmd_ctx *);
void		 cmd_set_option_send(void *, struct buffer *);
void		 cmd_set_option_recv(void **, struct buffer *);
void		 cmd_set_option_free(void *);

struct cmd_set_option_data {
	char	*option;
	char	*value;
};

const struct cmd_entry cmd_set_option_entry = {
	CMD_SETOPTION, "set-option", "set", CMD_NOSESSION,
	cmd_set_option_parse,
	cmd_set_option_usage,
	cmd_set_option_exec, 
	cmd_set_option_send,
	cmd_set_option_recv,
	cmd_set_option_free
};

int
cmd_set_option_parse(void **ptr, int argc, char **argv, char **cause)
{
	struct cmd_set_option_data	*data;
	int				 opt;

	*ptr = data = xmalloc(sizeof *data);
	data->option = NULL;
	data->value = NULL;

	while ((opt = getopt(argc, argv, "")) != EOF) {
		switch (opt) {
		default:
			goto usage;
		}
	}	
	argc -= optind;
	argv += optind;
	if (argc != 1 && argc != 2)
		goto usage;

	data->option = xstrdup(argv[0]);
	if (argc == 2)
		data->value = xstrdup(argv[1]);

	return (0);

usage:
	usage(cause, "%s", cmd_set_option_usage());

	cmd_set_option_free(data);
	return (-1);
}

const char *
cmd_set_option_usage(void)
{
	return ("set-option option value");
}

void
cmd_set_option_exec(void *ptr, unused struct cmd_ctx *ctx)
{
	struct cmd_set_option_data	*data = ptr;
	struct client			*c = ctx->client;
	const char			*errstr;
	u_int				 number;
	int				 bool, key;

	if (data == NULL)
		return;

	if (*data->option == '\0') {
		ctx->error(ctx, "invalid option");
		return;
	}

	number =  strtonum(data->value, 0, UINT_MAX, &errstr);

	bool = -1;
	if (number == 1 ||
	    strcmp(data->value, "on") == 0 || strcmp(data->value, "yes") == 0)
		bool = 1;
	if (number == 0 ||
	    strcmp(data->value, "off") == 0 || strcmp(data->value, "no") == 0)
		bool = 0;
	
	if (strcmp(data->option, "prefix") == 0) {
		key = key_string_lookup_string(data->value);
		if (key == KEYC_NONE) {
			ctx->error(ctx, "unknown key: %s", data->value);
			return;
		}
		prefix_key = key;
	} else {
		ctx->error(ctx, "unknown option: %s", data->option);
		return;
	}

	if (!(ctx->flags & CMD_KEY))
		server_write_client(c, MSG_EXIT, NULL, 0);
}

void
cmd_set_option_send(void *ptr, struct buffer *b)
{
	struct cmd_set_option_data	*data = ptr;

	cmd_send_string(b, data->option);
	cmd_send_string(b, data->value);
}

void
cmd_set_option_recv(void **ptr, struct buffer *b)
{
	struct cmd_set_option_data	*data;

	*ptr = data = xmalloc(sizeof *data);
	data->option = cmd_recv_string(b);
	data->value = cmd_recv_string(b);
}

void
cmd_set_option_free(void *ptr)
{
	struct cmd_set_option_data	*data = ptr;

	if (data->option != NULL)
		xfree(data->option);
	if (data->value != NULL)
		xfree(data->value);
	xfree(data);
}
