/* $Id: buffer.c,v 1.1.1.1 2007-07-09 19:03:33 nicm Exp $ */

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

/* Create a buffer. */
struct buffer *
buffer_create(size_t size)
{
	struct buffer	*b;

	if (size == 0)
		log_fatalx("buffer_create: zero size");

	b = xcalloc(1, sizeof *b);

	b->base = xmalloc(size);
	b->space = size;

	return (b);
}

/* Destroy a buffer. */
void
buffer_destroy(struct buffer *b)
{
	xfree(b->base);
	xfree(b);
}

/* Empty a buffer. */
void
buffer_clear(struct buffer *b)
{
	b->size = 0;
	b->off = 0;
}

/* Ensure free space for size in buffer. */
void
buffer_ensure(struct buffer *b, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_ensure: zero size");

	if (BUFFER_FREE(b) >= size)
		return;

	if (b->off > 0) {
		if (b->size > 0)
			memmove(b->base, b->base + b->off, b->size);
		b->off = 0;
	}

	ENSURE_FOR(b->base, b->space, b->size, size);
}

/* Adjust buffer after data appended. */
void
buffer_add(struct buffer *b, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_add: zero size");
	if (size > b->space - b->size)
		log_fatalx("buffer_add: overflow");

	b->size += size;
}

/* Reverse buffer add. */
void
buffer_reverse_add(struct buffer *b, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_reverse_add: zero size");
	if (size > b->size)
		log_fatalx("buffer_reverse_add: underflow");

	b->size -= size;
}

/* Adjust buffer after data removed. */
void
buffer_remove(struct buffer *b, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_remove: zero size");
	if (size > b->size)
		log_fatalx("buffer_remove: underflow");

	b->size -= size;
	b->off += size;
}

/* Reverse buffer remove. */
void
buffer_reverse_remove(struct buffer *b, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_reverse_remove: zero size");
	if (size > b->off)
		log_fatalx("buffer_reverse_remove: overflow");

	b->size += size;
	b->off -= size;
}

/* Insert a section into the buffer. */
void
buffer_insert_range(struct buffer *b, size_t base, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_insert_range: zero size");
	if (base > b->size)
		log_fatalx("buffer_insert_range: range overflows buffer");

	buffer_ensure(b, size);
	memmove(b->base + b->off + base + size,
	    b->base + b->off + base, b->size - base);
	b->size += size;
}

/* Delete a section from the buffer. */
void
buffer_delete_range(struct buffer *b, size_t base, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_delete_range: zero size");
	if (size > b->size)
		log_fatalx("buffer_delete_range: size too big");
	if (base + size > b->size)
		log_fatalx("buffer_delete_range: range overflows buffer");

	memmove(b->base + b->off + base,
	    b->base + b->off + base + size, b->size - base - size);
	b->size -= size;
}

/* Copy data into a buffer. */
void
buffer_write(struct buffer *b, const void *data, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_write: zero size");

	buffer_ensure(b, size);
	memcpy(BUFFER_IN(b), data, size);
	buffer_add(b, size);
}

/* Copy data out of a buffer. */
void
buffer_read(struct buffer *b, void *data, size_t size)
{
	if (size == 0)
		log_fatalx("buffer_read: zero size");
	if (size > b->size)
		log_fatalx("buffer_read: underflow");

	memcpy(data, BUFFER_OUT(b), size);
	buffer_remove(b, size);
}
