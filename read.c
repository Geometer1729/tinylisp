#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "tinylisp.h"

#ifndef DEFAULT_SYM_LEN
/** The default size of a symbol buffer, in bytes.
 *
 * It's more time-efficient to keep this small, but allowing it to grow is more
 * space-efficient. The growth algorithm is exponential.
 */
#define DEFAULT_SYM_LEN 64
#endif

/** A helper macro to return a TL_SYM from a C string which was allocated using malloc(). */
#define return_sym_from_buf(in, s, sz) do { \
	tl_object *ret = tl_new_sym_data((in), (s), (sz)); \
	free((s)); \
	return ret; \
} while(0)

/** A helper macro to add another character to the buffer. */
#define add_to_cstr(in, buf, sz, idx, c) do { \
	buf[idx++] = c; \
	if(idx >= sz) { \
		sz <<= 1; \
		buf = tl_alloc_realloc(in, buf, sz); \
		assert(buf); \
	} \
} while(0)

/* FIXME: NULL and TL_EMPTY_LIST are the same; empty list can signal EOF */
/** Read a value.
 *
 * This invokes `tl_getc` to get the next character, and may also invoke
 * `tl_putback` to make `tl_getc` return a different character on the next
 * invocation.
 *
 * This function is heavily recursive, and reading a large data structure very
 * well could cause issues with the C stack size on constrained platforms. This
 * is a known bug.
 *
 * The returned value is the entire expression read using `tl_getc` (normally
 * from wherever `readf` is reading data, which is, e.g., stdin in the
 * `main.c` REPL). If `readf` returns `EOF` immediately, this returns NULL.
 * Note that there is presently no way to discriminate between an empty list
 * (`TL_EMPTY_LIST`) of the syntax `()` and a `NULL` return due to EOF.
 */
tl_object *tl_read(tl_interp *in, tl_object *args) {
	int c, d, q;
	long ival = 0;
	tl_object *list = TL_EMPTY_LIST;
	char *symbuf;
	size_t idx = 0, bufsz = DEFAULT_SYM_LEN;

	while(1) {
		switch(c = tl_getc(in)) {
			case EOF:
				return NULL;
				break;

			case ' ': case '\n': case '\t': case '\v': case '\r': case '\b':
				continue;
				break;

			case ';':
				while(tl_getc(in) != '\n');
				continue;
				break;

			case '(':
				while(1) {
					switch(d = tl_getc(in)) {
						case ' ': case '\n': case '\t': case '\v': case '\r': case '\b':
							continue;
							break;

						case ')':
							return tl_list_rvs(in, list);
							break;

						case '.':
							list = tl_new_pair(in, tl_read(in, TL_EMPTY_LIST), list);
							while((d = tl_getc(in))) {
								if(d != ' ' && d != '\n' && d != '\t' && d != '\v' && d != '\r' && d != 'b') break;
							}

							/* This definitely SHOULD be the case; the other path is errant. */
							if(d == ')') {
								return tl_list_rvs_improp(in, list);
							} else {
								tl_putback(in, d);
								list = tl_new_pair(in, tl_first(list), tl_new_pair(in, tl_new_sym(in, "."), tl_next(list)));
							}

							break;


						default:
							tl_putback(in, d);
							list = tl_new_pair(in, tl_read(in, TL_EMPTY_LIST), list);
							break;
					}
				}
				break;

			case '"':
				q = c;
				symbuf = tl_alloc_malloc(in, bufsz);
				assert(symbuf);
				while((d = in->readf(in)) != q) {
					if(d == EOF) break;  // TODO: reject
					add_to_cstr(in, symbuf, bufsz, idx, (char) d);
				}
				return_sym_from_buf(in, symbuf, idx);
				break;

			default:
				if(isdigit(c)) {
					ival = c - '0';
					while(isdigit((c = tl_getc(in)))) {
						ival *= 10;
						ival += c - '0';
					}
					tl_putback(in, c);
					return tl_new_int(in, ival);
				}
				for(tl_list_iter(in->prefixes, kv)) {
					tl_object *key = tl_first(kv);
					tl_object *val = tl_next(kv);
					if(key && val && tl_is_sym(key) && key->nm->here.len > 0 && key->nm->here.data[0] == c) {
						list = tl_read(in, TL_EMPTY_LIST);
						return tl_new_pair(in, val, tl_new_pair(in, list, TL_EMPTY_LIST));
					}
				}
				symbuf = tl_alloc_malloc(in, bufsz);
				add_to_cstr(in, symbuf, bufsz, idx, c);
				while(1) {
					switch(d = tl_getc(in)) {
						case ' ': case '\n': case '\t': case '\v': case '\r': case '\b': case EOF:
							// TODO: fix EOF case
							return_sym_from_buf(in, symbuf, idx);
							break;

						case '(': case ')':
							tl_putback(in, d);
							return_sym_from_buf(in, symbuf, idx);
							break;

						default:
							add_to_cstr(in, symbuf, bufsz, idx, (char) d);
							break;
					}
				}
				return_sym_from_buf(in, symbuf, idx);
				break;
		}
	}
}
