/* $Id$ */

#define _POSIX_C_SOURCE 2

#include <unistd.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fsm/graph.h> /* XXX */
#include <re/re.h>

#include "parser.h"
#include "internal.h"

#include "libre/internal.h" /* XXX */
#include "libfsm/internal.h" /* XXX */
#include "libfsm/set.h" /* XXX */

#include "ast.h"

static
void usage(void)
{
	printf("usage: lx [-h] [-l <language>] <input>\n");
}

/* TODO: centralise */
static FILE *
xopen(int argc, char * const argv[], int i, FILE *f, const char *mode)
{
	if (argc <= i || 0 == strcmp("-", argv[i])) {
		return f;
	}

	f = fopen(argv[i], mode);
	if (f == NULL) {
		perror(argv[i]);
		exit(EXIT_FAILURE);
	}

	return f;
}

static void
carryopaque(struct state_set *set, struct fsm *fsm, struct fsm_state *state)
{
	struct state_set *s;

	assert(set != NULL); /* TODO: right? */
	assert(fsm != NULL);
	assert(state != NULL);
	assert(state->opaque == NULL);

	if (!fsm_isend(fsm, state)) {
		return;
	}

	/*
	 * Here we mark newly-created DFA states with the same AST mapping
	 * as from their corresponding source NFA states. These are the mappings
	 * which indicate which lexical token (and zone transition) is produced
	 * from each accepting state in a particular regexp.
	 *
	 * Because all the accepting states belong to the same regexp, they
	 * should all have the same mapping. So we nominate one to use for the
	 * opaque value and check all other accepting states are the same.
	 */

	for (s = set; s != NULL; s = s->next) {
		if (fsm_isend(fsm, s->state)) {
			state->opaque = s->state->opaque;
			break;
		}
	}

	assert(state->opaque != NULL);

	for (s = set; s != NULL; s = s->next) {
		if (!fsm_isend(fsm, s->state)) {
			continue;
		}

		if (s->state->opaque != state->opaque) {
			goto error;
		}
	}

	return;

error:

	state->opaque = NULL;

	return;
}

int
main(int argc, char *argv[])
{
	struct ast *ast;
	enum lx_out format = LX_OUT_C;
	FILE *in;

	{
		int c;

		while ((c = getopt(argc, argv, "hvl:")) != -1) {
			switch (c) {
			case 'h':
				usage();
				exit(EXIT_SUCCESS);

			case 'l':
				if (0 == strcmp(optarg, "dot")) {
					format = LX_OUT_DOT;
				} else if (0 == strcmp(optarg, "c")) {
					format = LX_OUT_C;
				} else if (0 == strcmp(optarg, "h")) {
					format = LX_OUT_H;
				} else {
					fprintf(stderr, "unrecognised output language; valid languages are: c, h, dot\n");
					exit(EXIT_FAILURE);
				}
				break;

			case '?':
			default:
				usage();
				exit(EXIT_FAILURE);
			}
		}

		argc -= optind;
		argv += optind;

		if (argc > 1) {
			usage();
			exit(EXIT_FAILURE);
		}

		in = xopen(argc, argv, 1, stdin, "r");
	}

	ast = lx_parse(in);
	if (ast == NULL) {
		return EXIT_FAILURE;
	}

	/*
	 * What happens here is a transformation of the AST. This is equivalent to
	 * compiling to produce an IR, however the IR in lx's case is practically
	 * identical to the AST, so the same data structure is kept for simplicity.
	 *
	 * The important steps are:
	 *  1. Set FSM end state to point to each ast_mapping;
	 *  2. Minimize each mapping's regex;
	 *  3. Union all regexps within one mapping to produce one regexp per zone;
	 *  4. NFA->DFA on each zone's regexp.
	 *
	 * This produces as minimal state machines as possible, without losing track
	 * of which end state is associated with each mapping. In other words,
	 * without losing track of which regexp maps to which token.
	 */
	{
		struct ast_zone    *z;
		struct ast_mapping *m;

		for (z = ast->zl; z != NULL; z = z->next) {
			assert(z->re == NULL);

			z->re = re_new_empty();
			if (z->re == NULL) {
				return EXIT_FAILURE;
			}

			for (m = z->ml; m != NULL; m = m->next) {
				struct fsm_state *s;

				assert(m->re != NULL);
				assert(m->re->fsm != NULL);

				/* XXX: abstraction */
				if (!fsm_minimize(m->re->fsm)) {
					return EXIT_FAILURE;
				}

				/* potentially invalidated by fsm_minimize */
				/* TODO: maybe it would be convenient to re-find this, if neccessary */
				m->re->end = NULL;

				/* Attach this mapping to each end state for this regexp */
				for (s = m->re->fsm->sl; s != NULL; s = s->next) {
					if (fsm_isend(m->re->fsm, s)) {
						assert(s->opaque == NULL);
						s->opaque = m;
					}
				}

				if (!re_union(z->re, m->re)) {
					return EXIT_FAILURE;
				}

				m->re = NULL;   /* TODO: free properly somehow? or clone first? */
			}

			/* TODO: note this makes re->end invalid. that's what i get for breaking abstraction */
			if (!fsm_todfa_opaque(z->re->fsm, carryopaque)) {
				return EXIT_FAILURE;
			}
		}
	}

	/*
	 * Semantic checks.
	 */
	{
		struct ast_zone  *z;
		struct fsm_state *s;
		int e;

		assert(ast->zl != NULL);

		/* TODO: check for: no end states (same as no tokens?) */
		/* TODO: check for reserved token names (ERROR, EOF etc) */
		/* TODO: don't forget to indicate zone in error messages */

		e = 0;

		for (z = ast->zl; z != NULL; z = z->next) {
			assert(z->re != NULL);
			assert(z->ml != NULL);

			if (fsm_isend(z->re->fsm, z->re->fsm->start)) {
				fprintf(stderr, "start state accepts\n"); /* TODO */
				e = 1;
			}

			/* pick up conflicts flagged by carryopaque() */
			for (s = z->re->fsm->sl; s != NULL; s = s->next) {
				if (!fsm_isend(z->re->fsm, s)) {
					continue;
				}

				if (s->opaque == NULL) {
					fprintf(stderr, "opaque conflict\n"); /* TODO */
					e = 1;
				}
			}
		}

		if (e) {
			return EXIT_FAILURE;
		}
	}

	/* TODO: free ast */

	lx_print(ast, stdout, format);

	return EXIT_SUCCESS;
}

