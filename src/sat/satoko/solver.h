//===--- solver.h -----------------------------------------------------------===
//
//                     satoko: Satisfiability solver
//
// This file is distributed under the BSD 2-Clause License.
// See LICENSE for details.
//
//===------------------------------------------------------------------------===
#ifndef satoko__solver_h
#define satoko__solver_h

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "clause.h"
#include "cdb.h"
#include "satoko.h"
#include "types.h"
#include "watch_list.h"
#include "utils/b_queue.h"
#include "utils/heap.h"
#include "utils/mem.h"
#include "utils/misc.h"
#include "utils/vec/vec_char.h"
#include "utils/vec/vec_dble.h"
#include "utils/vec/vec_uint.h"

#include "misc/util/abc_global.h"
ABC_NAMESPACE_HEADER_START

enum {
	LIT_FALSE = 1,
	LIT_TRUE = 0,
	VAR_UNASSING = 3
};

#define UNDEF 0xFFFFFFFF

struct satoko_stats {
	unsigned n_starts;
	unsigned n_reduce_db;

	long n_decisions;
	long n_propagations;
	long n_inspects;
	long n_conflicts;

	long n_original_lits;
	long n_learnt_lits;
};

typedef struct solver_t_ solver_t;
struct solver_t_ {
	/* User data */
	vec_uint_t *assumptions;
	vec_uint_t *final_conflict;

	/* Clauses Database */
	struct cdb *all_clauses;
	vec_uint_t *learnts;
	vec_uint_t *originals;
	vec_wl_t *watches;
	vec_wl_t *bin_watches;

	/* Activity heuristic */
	act_t var_act_inc; /* Amount to bump next variable with. */
	clause_act_t clause_act_inc; /* Amount to bump next clause with. */

	/* Variable Information */
	vec_act_t *activity; /* A heuristic measurement of the activity of a variable. */
	heap_t *var_order;
	vec_uint_t *levels;  /* Decision level of the current assignment */
	vec_uint_t *reasons; /* Reason (clause) of the current assignment */
	vec_char_t *assigns;
	vec_char_t *polarity;

	/* Assignments */
	vec_uint_t *trail;
	vec_uint_t *trail_lim; /* Separator indices for different decision levels in 'trail'. */
	unsigned i_qhead; /* Head of propagation queue (as index into the trail). */
	unsigned n_assigns_simplify; /* Number of top-level assignments since
					last execution of 'simplify()'. */
	long n_props_simplify;  /* Remaining number of propagations that
				   must be made before next execution of
				   'simplify()'. */

	/* Temporary data used by Analyze */
	vec_uint_t *temp_lits;
	vec_char_t *seen;
	vec_uint_t *tagged; /* Stack */
	vec_uint_t *stack;
	vec_uint_t *last_dlevel;

	/* Temporary data used by Search method */
	b_queue_t *bq_trail;
	b_queue_t *bq_lbd;
	long RC1;
	long RC2;
	long n_confl_bfr_reduce;
	float sum_lbd;

	/* Misc temporary */
	unsigned cur_stamp; /* Used for marking literals and levels of interest */
	vec_uint_t *stamps; /* Multipurpose stamp used to calculate LBD and
			     * clauses minimization with binary resolution */

	struct satoko_stats stats;
	struct satoko_opts opts;
};

//===------------------------------------------------------------------------===
extern unsigned solver_clause_create(solver_t *, vec_uint_t *, unsigned);
extern char solver_search(solver_t *);
extern void solver_cancel_until(solver_t *, unsigned);
extern unsigned solver_propagate(solver_t *);
extern void solver_debug_check(solver_t *, int);

//===------------------------------------------------------------------------===
// Inline var/lit functions
//===------------------------------------------------------------------------===
static inline unsigned var2lit(unsigned var, char polarity)
{
	return var + var + ((unsigned) polarity != 0);
}

static inline unsigned lit2var(unsigned lit)
{
	return lit >> 1;
}
//===------------------------------------------------------------------------===
// Inline var functions
//===------------------------------------------------------------------------===
static inline char var_value(solver_t *s, unsigned var)
{
	return vec_char_at(s->assigns, var);
}

static inline unsigned var_dlevel(solver_t *s, unsigned var)
{
	return vec_uint_at(s->levels, var);
}

static inline unsigned var_reason(solver_t *s, unsigned var)
{
	return vec_uint_at(s->reasons, var);
}
//===------------------------------------------------------------------------===
// Inline lit functions
//===------------------------------------------------------------------------===
static inline unsigned lit_neg(unsigned lit)
{
	return lit ^ 1;
}

static inline char lit_polarity(unsigned lit)
{
	return (char)(lit & 1);
}

static inline char lit_value(solver_t *s, unsigned lit)
{
	return lit_polarity(lit) ^ vec_char_at(s->assigns, lit2var(lit));
}

static inline unsigned lit_dlevel(solver_t *s, unsigned lit)
{
	return vec_uint_at(s->levels, lit2var(lit));
}

static inline unsigned lit_reason(solver_t *s, unsigned lit)
{
	return vec_uint_at(s->reasons, lit2var(lit));
}
//===------------------------------------------------------------------------===
// Inline solver minor functions
//===------------------------------------------------------------------------===
static inline unsigned solver_check_limits(solver_t *s)
{
	return (s->opts.conf_limit == 0 || s->opts.conf_limit >= s->stats.n_conflicts) &&
	       (s->opts.prop_limit == 0 || s->opts.prop_limit >= s->stats.n_propagations);
}

/** Returns current decision level */
static inline unsigned solver_dlevel(solver_t *s)
{
	return vec_uint_size(s->trail_lim);
}

static inline int solver_enqueue(solver_t *s, unsigned lit, unsigned reason)
{
	unsigned var = lit2var(lit);

	vec_char_assign(s->assigns, var, lit_polarity(lit));
	vec_uint_assign(s->levels, var, solver_dlevel(s));
	vec_uint_assign(s->reasons, var, reason);
	vec_uint_push_back(s->trail, lit);
	return SATOKO_OK;
}

//===------------------------------------------------------------------------===
// Inline clause functions
//===------------------------------------------------------------------------===
static inline struct clause *clause_read(solver_t *s, unsigned cref)
{
	return cdb_handler(s->all_clauses, cref);
}

static inline void clause_watch(solver_t *s, unsigned cref)
{
	struct clause *clause = cdb_handler(s->all_clauses, cref);
	struct watcher w1;
	struct watcher w2;

	w1.cref = cref;
	w2.cref = cref;
	w1.blocker = clause->data[1].lit;
	w2.blocker = clause->data[0].lit;
	if (clause->size == 2) {
		watch_list_push(vec_wl_at(s->bin_watches, lit_neg(clause->data[0].lit)), w1);
		watch_list_push(vec_wl_at(s->bin_watches, lit_neg(clause->data[1].lit)), w2);
	} else {
		watch_list_push(vec_wl_at(s->watches, lit_neg(clause->data[0].lit)), w1);
		watch_list_push(vec_wl_at(s->watches, lit_neg(clause->data[1].lit)), w2);
	}
}

static inline void clause_unwatch(solver_t *s, unsigned cref)
{
	struct clause *clause = cdb_handler(s->all_clauses, cref);
	if (clause->size == 2) {
		watch_list_remove(vec_wl_at(s->bin_watches, lit_neg(clause->data[0].lit)), cref);
		watch_list_remove(vec_wl_at(s->bin_watches, lit_neg(clause->data[1].lit)), cref);
	} else {
		watch_list_remove(vec_wl_at(s->watches, lit_neg(clause->data[0].lit)), cref);
		watch_list_remove(vec_wl_at(s->watches, lit_neg(clause->data[1].lit)), cref);
	}
}

ABC_NAMESPACE_HEADER_END
#endif /* satoko__solver_h */
