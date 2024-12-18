#include "tinylisp.h"

TL_DECLARE_INIT_ENTS;

/** An internal macro for creating a new binding inside of a frame. */
#define _tl_frm_set(sm, obj, fm) tl_new_pair(in, tl_new_pair(in, tl_new_sym(in, sm), obj), fm)

#include <stdio.h>
#include <stdlib.h>
static int _readf(tl_interp *in) { return getchar(); }
static void _writef(tl_interp *in, const char c) { putchar(c); }
static int _modloadf(tl_interp *in, const char *fn) { return 0; }
static void *_reallocf(tl_interp *in, void *ptr, size_t s) {
	/* Valgrind is unhappy unless this actually uses free, so we'll force this
	 * to occur.
	 */
	if(!s) {
		free(ptr);
		return NULL;
	}
	return realloc(ptr, s);
}

/** Initialize a TinyLISP interpreter.
 *
 * This function properly initializes the fields of a `tl_interp`, after which
 * the interpreter may be considered valid, and can run evaluations. (It is
 * undefined behavior to use an interpreter before it is initialized.)
 *
 * The source for this function is a logical place to add more language
 * builtins if a module would not suffice.
 *
 * Other initialization may need to follow calling this function; for example,
 * if the implementation wants to override IO, it may set the interpreter's
 * `readf` and `writef` functions. If they are to be functionally used, the
 * host environment probably wants to set the `modloadf` function. The ones
 * declared here use the simplest implementation from stdio.h (which may be
 * minilibc's stdio).
 *
 * This function calls tl_interp_init_alloc() with the system default
 * allocator, as provided through malloc and free.
 */

void tl_interp_init(tl_interp *in) {
	tl_interp_init_alloc(in, _reallocf);
}

/** Initialize a TinyLISP interpreter with a custom allocator.
 *
 * This function is the core of tl_interp_init() and does all of the tasks it
 * does, but receives two function pointers as arguments corresponding to
 * tl_interp::mallocf and tl_interp::freef which can be used to adjust the
 * allocator used for the initializing allocations done by the interpreter (and
 * thereafter).
 * 
 * See tl_interp_init() for other details.
 */

void tl_interp_init_alloc(tl_interp *in, void *(*reallocf)(tl_interp *, void *, size_t)) {
	in->reallocf = reallocf;
	in->readf = _readf;
	in->writef = _writef;
#ifdef CONFIG_MODULES
	in->modloadf = _modloadf;
#endif

	tl_ns_init(in, &in->ns);
	in->top_alloc = in->free_alloc = NULL;
	in->oballoc_batch = TL_DEFAULT_OBALLOC_BATCH;

	in->true_ = tl_new_sym(in, "tl-#t");
	in->false_ = tl_new_sym(in, "tl-#f");
	in->error = NULL;
	in->prefixes = TL_EMPTY_LIST;
	in->current = TL_EMPTY_LIST;
	in->conts = TL_EMPTY_LIST;
	in->values = TL_EMPTY_LIST;
	in->rescue = TL_EMPTY_LIST;
	in->gc_events = TL_DEFAULT_GC_EVENTS;
	in->ctr_events = 0;
	in->putback = 0;
	in->is_putback = 0;
	in->read_buffer = NULL;
	in->disp_sep = '\t';
	in->disp_indent = '\0';
	in->next_tag = 1;

	in->top_env = TL_EMPTY_LIST;

	tl_object *top_frm = TL_EMPTY_LIST;
	top_frm = _tl_frm_set("tl-#t", in->true_, top_frm);
	top_frm = _tl_frm_set("tl-#f", in->false_, top_frm);

	top_frm = tl_interp_load_funcs(in, top_frm,
			TL_START_INIT_ENTS,
			TL_STOP_INIT_ENTS
	);

	in->top_env = tl_new_pair(in, top_frm, in->top_env);
	in->env = in->top_env;
}

/** Load functions from ::tl_init_ent entries.
 *
 * This is most often done from a linker-defined section. (See the definition
 * of ::TL_CF_FLAGS , which is invoked by ::TL_CF and ::TL_CFBV .)
 */
tl_object *tl_interp_load_funcs(tl_interp *in, tl_object *frame, tl_init_ent *start, tl_init_ent *stop) {
#ifdef LOAD_DEBUG
	fprintf(stderr, "Load on %p starts from %p to %p:\n", in, start, stop);
#endif
	while(start != stop) {
#ifdef LOAD_DEBUG
		fprintf(stderr, "Loading %s %s declared in %s:%u from %p...\n", start->flags & TL_EF_BYVAL ? "cfunc_byval" : "cfunc", start->name, start->file, start->line, start->fn);
#endif
		frame = _tl_frm_set(
				start->name,
				start->flags & TL_EF_BYVAL ? _tl_new_cfunc_byval(in, start->fn, start->name) : _tl_new_cfunc(in, start->fn, start->name),
				frame
		);
		start++;
	}
#ifdef LOAD_DEBUG
	fprintf(stderr, "Load complete.\n");
#endif
	return frame;
}

/** Finalizes a module.
 *
 * For the most part, this frees all memory allocated by the interpreter,
 * leaving many of its pointers dangling. It is undefined behavior to use an
 * interpreter after it has been finalized.
 */
void tl_interp_cleanup(tl_interp *in) {
	while(in->top_alloc) {
		tl_free(in, in->top_alloc);
	}
	tl_ns_free(in, &in->ns);
}
