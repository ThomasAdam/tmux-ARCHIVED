/* $Id$ */

/*
 * Copyright (c) 2008 Nicholas Marriott <nicm@users.sourceforge.net>
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

struct utf8_width_entry {
	u_int	first;
	u_int	last;

	int	width;

	struct utf8_width_entry	*left;
	struct utf8_width_entry	*right;
};

/* Random order. Not optimal but it'll do for now... */
struct utf8_width_entry utf8_width_table[] = {
	{ 0x00951, 0x00954, 0, NULL, NULL },
	{ 0x00ccc, 0x00ccd, 0, NULL, NULL },
	{ 0x0fff9, 0x0fffb, 0, NULL, NULL },
	{ 0x20000, 0x2fffd, 2, NULL, NULL },
	{ 0x00ebb, 0x00ebc, 0, NULL, NULL },
	{ 0x01932, 0x01932, 0, NULL, NULL },
	{ 0x0070f, 0x0070f, 0, NULL, NULL },
	{ 0x00a70, 0x00a71, 0, NULL, NULL },
	{ 0x02329, 0x02329, 2, NULL, NULL },
	{ 0x00acd, 0x00acd, 0, NULL, NULL },
	{ 0x00ac7, 0x00ac8, 0, NULL, NULL },
	{ 0x00a3c, 0x00a3c, 0, NULL, NULL },
	{ 0x009cd, 0x009cd, 0, NULL, NULL },
	{ 0x00591, 0x005bd, 0, NULL, NULL },
	{ 0x01058, 0x01059, 0, NULL, NULL },
	{ 0x0ffe0, 0x0ffe6, 2, NULL, NULL },
	{ 0x01100, 0x0115f, 2, NULL, NULL },
	{ 0x0fe20, 0x0fe23, 0, NULL, NULL },
	{ 0x0302a, 0x0302f, 0, NULL, NULL },
	{ 0x01772, 0x01773, 0, NULL, NULL },
	{ 0x005bf, 0x005bf, 0, NULL, NULL },
	{ 0x006ea, 0x006ed, 0, NULL, NULL },
	{ 0x00bc0, 0x00bc0, 0, NULL, NULL },
	{ 0x00962, 0x00963, 0, NULL, NULL },
	{ 0x01732, 0x01734, 0, NULL, NULL },
	{ 0x00d41, 0x00d43, 0, NULL, NULL },
	{ 0x01b42, 0x01b42, 0, NULL, NULL },
	{ 0x00a41, 0x00a42, 0, NULL, NULL },
	{ 0x00eb4, 0x00eb9, 0, NULL, NULL },
	{ 0x00b01, 0x00b01, 0, NULL, NULL },
	{ 0x00e34, 0x00e3a, 0, NULL, NULL },
	{ 0x03040, 0x03098, 2, NULL, NULL },
	{ 0x0093c, 0x0093c, 0, NULL, NULL },
	{ 0x00c4a, 0x00c4d, 0, NULL, NULL },
	{ 0x01032, 0x01032, 0, NULL, NULL },
	{ 0x00f37, 0x00f37, 0, NULL, NULL },
	{ 0x00901, 0x00902, 0, NULL, NULL },
	{ 0x00cbf, 0x00cbf, 0, NULL, NULL },
	{ 0x0a806, 0x0a806, 0, NULL, NULL },
	{ 0x00dd2, 0x00dd4, 0, NULL, NULL },
	{ 0x00f71, 0x00f7e, 0, NULL, NULL },
	{ 0x01752, 0x01753, 0, NULL, NULL },
	{ 0x1d242, 0x1d244, 0, NULL, NULL },
	{ 0x005c1, 0x005c2, 0, NULL, NULL },
	{ 0x0309b, 0x0a4cf, 2, NULL, NULL },
	{ 0xe0100, 0xe01ef, 0, NULL, NULL },
	{ 0x017dd, 0x017dd, 0, NULL, NULL },
	{ 0x00600, 0x00603, 0, NULL, NULL },
	{ 0x009e2, 0x009e3, 0, NULL, NULL },
	{ 0x00cc6, 0x00cc6, 0, NULL, NULL },
	{ 0x0a80b, 0x0a80b, 0, NULL, NULL },
	{ 0x01712, 0x01714, 0, NULL, NULL },
	{ 0x00b3c, 0x00b3c, 0, NULL, NULL },
	{ 0x01b00, 0x01b03, 0, NULL, NULL },
	{ 0x007eb, 0x007f3, 0, NULL, NULL },
	{ 0xe0001, 0xe0001, 0, NULL, NULL },
	{ 0x1d185, 0x1d18b, 0, NULL, NULL },
	{ 0x0feff, 0x0feff, 0, NULL, NULL },
	{ 0x01b36, 0x01b3a, 0, NULL, NULL },
	{ 0x01920, 0x01922, 0, NULL, NULL },
	{ 0x00670, 0x00670, 0, NULL, NULL },
	{ 0x00f90, 0x00f97, 0, NULL, NULL },
	{ 0x01927, 0x01928, 0, NULL, NULL },
	{ 0x0200b, 0x0200f, 0, NULL, NULL },
	{ 0x0ff00, 0x0ff60, 2, NULL, NULL },
	{ 0x0f900, 0x0faff, 2, NULL, NULL },
	{ 0x0fb1e, 0x0fb1e, 0, NULL, NULL },
	{ 0x00cbc, 0x00cbc, 0, NULL, NULL },
	{ 0x00eb1, 0x00eb1, 0, NULL, NULL },
	{ 0x10a38, 0x10a3a, 0, NULL, NULL },
	{ 0x007a6, 0x007b0, 0, NULL, NULL },
	{ 0x00f80, 0x00f84, 0, NULL, NULL },
	{ 0x005c4, 0x005c5, 0, NULL, NULL },
	{ 0x0ac00, 0x0d7a3, 2, NULL, NULL },
	{ 0x017c9, 0x017d3, 0, NULL, NULL },
	{ 0x00d4d, 0x00d4d, 0, NULL, NULL },
	{ 0x1d167, 0x1d169, 0, NULL, NULL },
	{ 0x01036, 0x01037, 0, NULL, NULL },
	{ 0xe0020, 0xe007f, 0, NULL, NULL },
	{ 0x00f35, 0x00f35, 0, NULL, NULL },
	{ 0x017b4, 0x017b5, 0, NULL, NULL },
	{ 0x0206a, 0x0206f, 0, NULL, NULL },
	{ 0x00c46, 0x00c48, 0, NULL, NULL },
	{ 0x01939, 0x0193b, 0, NULL, NULL },
	{ 0x01dc0, 0x01dca, 0, NULL, NULL },
	{ 0x10a0c, 0x10a0f, 0, NULL, NULL },
	{ 0x0102d, 0x01030, 0, NULL, NULL },
	{ 0x017c6, 0x017c6, 0, NULL, NULL },
	{ 0x00ec8, 0x00ecd, 0, NULL, NULL },
	{ 0x00b41, 0x00b43, 0, NULL, NULL },
	{ 0x017b7, 0x017bd, 0, NULL, NULL },
	{ 0x1d173, 0x1d182, 0, NULL, NULL },
	{ 0x00a47, 0x00a48, 0, NULL, NULL },
	{ 0x0232a, 0x0232a, 2, NULL, NULL },
	{ 0x01b3c, 0x01b3c, 0, NULL, NULL },
	{ 0x10a01, 0x10a03, 0, NULL, NULL },
	{ 0x00ae2, 0x00ae3, 0, NULL, NULL },
	{ 0x00483, 0x00486, 0, NULL, NULL },
	{ 0x0135f, 0x0135f, 0, NULL, NULL },
	{ 0x01a17, 0x01a18, 0, NULL, NULL },
	{ 0x006e7, 0x006e8, 0, NULL, NULL },
	{ 0x03099, 0x0309a, 0, NULL, NULL },
	{ 0x00b4d, 0x00b4d, 0, NULL, NULL },
	{ 0x00ce2, 0x00ce3, 0, NULL, NULL },
	{ 0x00bcd, 0x00bcd, 0, NULL, NULL },
	{ 0x00610, 0x00615, 0, NULL, NULL },
	{ 0x00f99, 0x00fbc, 0, NULL, NULL },
	{ 0x009c1, 0x009c4, 0, NULL, NULL },
	{ 0x00730, 0x0074a, 0, NULL, NULL },
	{ 0x00300, 0x0036f, 0, NULL, NULL },
	{ 0x03030, 0x0303e, 2, NULL, NULL },
	{ 0x01b34, 0x01b34, 0, NULL, NULL },
	{ 0x1d1aa, 0x1d1ad, 0, NULL, NULL },
	{ 0x00dca, 0x00dca, 0, NULL, NULL },
	{ 0x006d6, 0x006e4, 0, NULL, NULL },
	{ 0x00f86, 0x00f87, 0, NULL, NULL },
	{ 0x00b3f, 0x00b3f, 0, NULL, NULL },
	{ 0x0fe30, 0x0fe6f, 2, NULL, NULL },
	{ 0x01039, 0x01039, 0, NULL, NULL },
	{ 0x0094d, 0x0094d, 0, NULL, NULL },
	{ 0x00c55, 0x00c56, 0, NULL, NULL },
	{ 0x00488, 0x00489, 0, NULL, NULL },
	{ 0x00e47, 0x00e4e, 0, NULL, NULL },
	{ 0x00a81, 0x00a82, 0, NULL, NULL },
	{ 0x00ac1, 0x00ac5, 0, NULL, NULL },
	{ 0x0202a, 0x0202e, 0, NULL, NULL },
	{ 0x00dd6, 0x00dd6, 0, NULL, NULL },
	{ 0x018a9, 0x018a9, 0, NULL, NULL },
	{ 0x0064b, 0x0065e, 0, NULL, NULL },
	{ 0x00abc, 0x00abc, 0, NULL, NULL },
	{ 0x00b82, 0x00b82, 0, NULL, NULL },
	{ 0x00f39, 0x00f39, 0, NULL, NULL },
	{ 0x020d0, 0x020ef, 0, NULL, NULL },
	{ 0x01dfe, 0x01dff, 0, NULL, NULL },
	{ 0x30000, 0x3fffd, 2, NULL, NULL },
	{ 0x00711, 0x00711, 0, NULL, NULL },
	{ 0x0fe00, 0x0fe0f, 0, NULL, NULL },
	{ 0x01160, 0x011ff, 0, NULL, NULL },
	{ 0x0180b, 0x0180d, 0, NULL, NULL },
	{ 0x10a3f, 0x10a3f, 0, NULL, NULL },
	{ 0x00981, 0x00981, 0, NULL, NULL },
	{ 0x0a825, 0x0a826, 0, NULL, NULL },
	{ 0x00941, 0x00948, 0, NULL, NULL },
	{ 0x01b6b, 0x01b73, 0, NULL, NULL },
	{ 0x00e31, 0x00e31, 0, NULL, NULL },
	{ 0x0fe10, 0x0fe19, 2, NULL, NULL },
	{ 0x00a01, 0x00a02, 0, NULL, NULL },
	{ 0x00a4b, 0x00a4d, 0, NULL, NULL },
	{ 0x00f18, 0x00f19, 0, NULL, NULL },
	{ 0x00fc6, 0x00fc6, 0, NULL, NULL },
	{ 0x02e80, 0x03029, 2, NULL, NULL },
	{ 0x00b56, 0x00b56, 0, NULL, NULL },
	{ 0x009bc, 0x009bc, 0, NULL, NULL },
	{ 0x005c7, 0x005c7, 0, NULL, NULL },
	{ 0x02060, 0x02063, 0, NULL, NULL },
	{ 0x00c3e, 0x00c40, 0, NULL, NULL },
	{ 0x10a05, 0x10a06, 0, NULL, NULL },
};

struct utf8_width_entry	*utf8_width_root = NULL;

int	utf8_overlap(struct utf8_width_entry *, struct utf8_width_entry *);
void	utf8_print(struct utf8_width_entry *, int);
u_int	utf8_combine(const u_char *);
void	utf8_split(u_int, u_char *);

int
utf8_overlap(
    struct utf8_width_entry *item1, struct utf8_width_entry *item2)
{
	if (item1->first >= item2->first && item1->first <= item2->last)
		return (1);
	if (item1->last >= item2->first && item1->last <= item2->last)
		return (1);
	if (item2->first >= item1->first && item2->first <= item1->last)
		return (1);
	if (item2->last >= item1->first && item2->last <= item1->last)
		return (1);
	return (0);
}

void
utf8_build(void)
{
	struct utf8_width_entry	**ptr, *item, *node;
	u_int			  i, j;

	for (i = 0; i < nitems(utf8_width_table); i++) {
		item = &utf8_width_table[i];

		for (j = 0; j < nitems(utf8_width_table); j++) {
			if (i != j && utf8_overlap(item, &utf8_width_table[j]))
				log_fatalx("utf8 overlap: %u %u", i, j);
		}

		ptr = &utf8_width_root;
		while (*ptr != NULL) {
			node = *ptr;			
			if (item->last < node->first)
				ptr = &(node->left);
			else if (item->first > node->last)
				ptr = &(node->right);
		}
		*ptr = item;
	}
}

void
utf8_print(struct utf8_width_entry *node, int n)
{
	log_debug("%*s%04x -> %04x", n, " ",  node->first, node->last);
	if (node->left != NULL)
		utf8_print(node->left, n + 1);
	if (node->right != NULL)
		utf8_print(node->right, n + 1);
}

u_int
utf8_combine(const u_char *data)
{
	u_int	uvalue;

	if (data[1] == 0xff)
		uvalue = data[0];
	else if (data[2] == 0xff) {
		uvalue = data[1] & 0x3f;
		uvalue |= (data[0] & 0x1f) << 6;
	} else if (data[3] == 0xff) {
		uvalue = data[2] & 0x3f;
		uvalue |= (data[1] & 0x3f) << 6;
		uvalue |= (data[0] & 0x0f) << 12;
	} else {
		uvalue = data[3] & 0x3f;
		uvalue |= (data[2] & 0x3f) << 6;
		uvalue |= (data[1] & 0x3f) << 12;
		uvalue |= (data[0] & 0x3f) << 18;
	}
	return (uvalue);
}

void
utf8_split(u_int uvalue, u_char *data)
{
	memset(data, 0xff, 4);

	if (uvalue <= 0x7f)
		data[0] = uvalue;
	else if (uvalue > 0x7f && uvalue <= 0x7ff) {
		data[0] = (uvalue >> 6) | 0xc0;
		data[1] = (uvalue & 0x3f) | 0x80;
	} else if (uvalue > 0x7ff && uvalue <= 0xffff) {
		data[0] = (uvalue >> 12) | 0xe0;
		data[1] = ((uvalue >> 6) & 0x3f) | 0x80;
		data[2] = (uvalue & 0x3f) | 0x80;
	} else if (uvalue > 0xffff && uvalue <= 0x10ffff) {
		data[0] = (uvalue >> 18) | 0xf0;
		data[1] = ((uvalue >> 12) & 0x3f) | 0x80;
		data[2] = ((uvalue >> 6) & 0x3f) | 0x80;
		data[3] = (uvalue & 0x3f) | 0x80;
	}
}

int
utf8_width(u_char *udata)
{
	struct utf8_width_entry	*item;
	u_int			 uvalue;

	uvalue = utf8_combine(udata);

	item = utf8_width_root;
	while (item != NULL) {
		if (uvalue < item->first)
			item = item->left;
		else if (uvalue > item->last)
			item = item->right;
		else
			return (item->width);
	}
	return (1);
}
