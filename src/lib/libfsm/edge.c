/* $Id$ */

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <fsm/fsm.h>

#include "internal.h"
#include "set.h"
#include "colour.h"
#include "xalloc.h"

static int fsm_addedge(struct fsm *fsm, struct fsm_state *from, struct fsm_state *to, struct fsm_edge *edge) {
	struct colour_set *c;

	assert(from != NULL);
	assert(to != NULL);
	assert(edge != NULL);

	(void) fsm;

	if (!set_addstate(&edge->sl, to)) {
		return 0;
	}

	for (c = from->cl; c != NULL; c = c->next) {
		if (!set_addcolour(fsm, &edge->cl, c)) {
			/* XXX */
			return 0;
		}
	}

	return 1;
}

int
fsm_addedge_epsilon(struct fsm *fsm, struct fsm_state *from, struct fsm_state *to)
{
	assert(fsm != NULL);
	assert(from != NULL);
	assert(to != NULL);

	return fsm_addedge(fsm, from, to, &from->edges[FSM_EDGE_EPSILON]);
}

int
fsm_addedge_any(struct fsm *fsm, struct fsm_state *from, struct fsm_state *to)
{
	int i;

	assert(fsm != NULL);
	assert(from != NULL);
	assert(to != NULL);

	for (i = 0; i <= UCHAR_MAX; i++) {
		if (!fsm_addedge(fsm, from, to, &from->edges[i])) {
			return 0;
		}
	}

	return 1;
}

int
fsm_addedge_literal(struct fsm *fsm, struct fsm_state *from, struct fsm_state *to,
	char c)
{
	assert(fsm != NULL);
	assert(from != NULL);
	assert(to != NULL);

	return fsm_addedge(fsm, from, to, &from->edges[(unsigned char) c]);
}

