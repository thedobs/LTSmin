/*
 * \file spg-solve.c
 *
 *  Created on: 23 Jan 2012
 *      Author: kant
 */
#include <limits.h>
#if HAVE_PROFILER
#include <gperftools/profiler.h>
#endif

#include <hre/config.h>
#include <hre/user.h>
#include <bignum/bignum.h>
#include <spg-lib/spg-solve.h>


static inline void
get_vset_size(vset_t set, long *node_count,
                  char *elem_str, int str_len)
{
    {
        bn_int_t elem_count;
        int      len;
        vset_count(set, node_count, &elem_count);
        len = bn_int2string(elem_str, str_len, &elem_count);
        if (len >= str_len)
            Abort("Error converting number to string");
        bn_clear(&elem_count);
    }
}


static void report_game(const parity_game* g, const spg_attr_options* options, int indent)
{
    if (log_active(infoLong))
    {
        RTstopTimer(options->timer);
        long   total_count;
        long   n_count;
        char   elem_str[1024];
        get_vset_size(g->v, &n_count, elem_str, sizeof(elem_str));
        total_count = n_count;
        Print(infoLong, "[%7.3f] " "%*s" "parity_game: g->v has %s elements (%ld nodes)",
              RTrealTime(options->timer), indent, "", elem_str, n_count);
        //Print(infoLong, "parity_game: min_priority = %d, max_priority = %d", g->min_priority, g->max_priority);
        for(int i=g->min_priority; i<= g->max_priority; i++)
        {
            get_vset_size(g->v_priority[i], &n_count, elem_str, sizeof(elem_str));
            total_count += n_count;
            Print(infoLong, "[%7.3f] " "%*s" "parity_game: g->v_priority[%d] has %s elements (%ld nodes)",
                  RTrealTime(options->timer), indent, "", i, elem_str, n_count);
        }
        for(int i=0; i < 2; i++)
        {
            get_vset_size(g->v_player[i], &n_count, elem_str, sizeof(elem_str));
            total_count += n_count;
            Print(infoLong, "[%7.3f] " "%*s" "parity_game: g->v_player[%d] has %s elements (%ld nodes)",
                  RTrealTime(options->timer), indent, "", i, elem_str, n_count);
        }
        Print(infoLong, "[%7.3f] " "%*s" "parity_game: %ld nodes total",
              RTrealTime(options->timer), indent, "", total_count);
        RTstartTimer(options->timer);
    }
}


/**
 * \brief Determines if player 0 is the winner of the game.
 */
bool spg_solve(parity_game* g, spgsolver_options* options)
{
    spgsolver_options* opts;
    if (options==NULL || options==0) {
        opts = spg_get_solver_options();
    } else {
        opts = options;
    }
    int src[g->state_length];
    for(int i=0; i<g->state_length; i++)
    {
        src[i] = g->src[i];
    }
#if HAVE_PROFILER
    Print(infoLong, "Start profiling now.");
    ProfilerStart("spgsolver.perf");
#endif
    RTstartTimer(opts->attr_options->timer);
    recursive_result result = spg_solve_recursive(g, opts, 0);  // Note: g will be destroyed
    RTstopTimer(opts->attr_options->timer);
#if HAVE_PROFILER
    ProfilerStop();
    Print(infoLong, "Done profiling.");
#endif
    return vset_member(result.win[0], src);
}


/**
 * \brief Recursively computes the winning sets for both players.
 * As a side-effect destroys the game g.
 */
recursive_result spg_solve_recursive(parity_game* g,  const spgsolver_options* options, int depth)
{
    // Output game information
    int indent = 2 * depth;
    RTstopTimer(options->attr_options->timer);
    if (log_active(infoLong))
    {
        long   n_count;
        VSET_COUNT_NODES(g->v, n_count)
        Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: game has %ld nodes",
              RTrealTime(options->attr_options->timer), indent, "", n_count);
        report_game(g, options->attr_options, indent);
    }
    else
    {
        Print(info, "[%7.3f] " "%*s" "solve_recursive",
              RTrealTime(options->attr_options->timer), indent, "");
    }
    RTstartTimer(options->attr_options->timer);

    SPG_OUTPUT_DOT(options->attr_options->dot,g->v,"spg_set_%05zu_v.dot",options->attr_options->dot_count++)

    recursive_result result;
    // Check if game is empty
    if (vset_is_empty(g->v)) {
        RTstopTimer(options->attr_options->timer);
        Print(info, "[%7.3f] " "%*s" "solve_recursive: game is empty.", RTrealTime(options->attr_options->timer), indent, "");
        RTstartTimer(options->attr_options->timer);
        result.win[0] = vset_create(g->domain, -1, NULL);
        result.win[1] = vset_create(g->domain, -1, NULL);
        return result;
    }

    // Find deadlock states, determine least priority and player
    int player;
    vset_t u = vset_create(g->domain, -1, NULL);
    {
        bool have_deadlock_states[2];
        vset_t deadlock_states[2];
        for(int p=0; p<2; p++)
        {
            have_deadlock_states[p] = false;
            deadlock_states[p] = vset_create(g->domain, -1, NULL);
            vset_copy(deadlock_states[p], g->v_player[p]);
        }
        {
            vset_t t = vset_create(g->domain, -1, NULL);
            for(int group=0; group<g->num_groups; group++) {
                vset_clear(t);
                vset_prev(t, g->v, g->e[group], g->v);
                for(int p=0; p<2; p++)
                {
                    vset_minus(deadlock_states[p], t);
                }
            }
            vset_destroy(t);
        }
        for(int p=0; p<2; p++)
        {
            if (!vset_is_empty(deadlock_states[p]))
            {
                SPG_REPORT_DEADLOCK_STATES(options->attr_options, indent, p);
                have_deadlock_states[p] = true;
            }
        }

        // compute U <- {v \in V | p(v) = m}
        int m = g->min_priority;
        vset_copy(u, g->v_priority[m]);
        while(vset_is_empty(u)) {
            m++;
            if (m > g->max_priority) {
                Abort("no min priority found!");
            }
            vset_clear(u);
            vset_copy(u, g->v_priority[m]);
        }
        if (m > 0 && have_deadlock_states[1]) // deadlock states of player 1 (and) result in true: nu X0 = X0
        {
            m = 0;
        }
        else if (m > 1 && have_deadlock_states[0]) // deadlock states of player 0 (or) result in false: mu X1 = X1
        {
            m = 1;
        }

        player = m % 2;

        RTstopTimer(options->attr_options->timer);
        Print(info, "[%7.3f] " "%*s" "solve_recursive: min=%d, max=%d, m=%d, player=%d",
              RTrealTime(options->attr_options->timer), indent, "", g->min_priority, g->max_priority, m, player);
        RTstartTimer(options->attr_options->timer);

        // Add deadlock states
        if (m < 2)
        {
            RTstopTimer(options->attr_options->timer);
            Print(infoLong, "[%7.3f] " "%*s" "solve_recursive: adding deadlock states (m=%d).",
                  RTrealTime(options->attr_options->timer), indent, "", m);
            RTstartTimer(options->attr_options->timer);
            if (m >= g->min_priority)
            {
                vset_copy(u, g->v_priority[m]);
            }
            vset_union(u, deadlock_states[1-player]);
        }
        for(int p=0; p<2; p++)
        {
            vset_destroy(deadlock_states[p]);
        }
        SPG_REPORT_MIN_PRIORITY(options->attr_options, indent, m, u);
    }

    // First attractor computating (for player)
    // U = attr_player(G, U)
    options->attr(player, g, u, options->attr_options, depth);

    recursive_result x;
    vset_t empty = vset_create(g->domain, -1, NULL);
    {
        parity_game* g_minus_u = spg_copy(g);

        spg_game_restrict(g_minus_u, u, options);

        // First recursion step
        x = spg_solve_recursive(g_minus_u, options, depth+1);
        //spg_destroy(g_minus_u); // has already been destroyed in spg_solve_recursive
    }

    if (vset_is_empty(x.win[1-player])) {
        RTstopTimer(options->attr_options->timer);
        Print(info, "[%7.3f] " "%*s" "solve_recursive: x.win[%d] empty.", RTrealTime(options->attr_options->timer), indent, "", 1-player);
        RTstartTimer(options->attr_options->timer);

        vset_union(u, x.win[player]);
        vset_destroy(x.win[player]);
        vset_destroy(x.win[1-player]);
        result.win[player] = u;
        result.win[1-player] = empty;
    } else {
        RTstopTimer(options->attr_options->timer);
        Print(info, "[%7.3f] " "%*s" "solve_recursive: x.win[%d] not empty.", RTrealTime(options->attr_options->timer), indent, "", 1-player);
        RTstartTimer(options->attr_options->timer);

        vset_destroy(empty);
        vset_destroy(u);
        vset_destroy(x.win[player]);

        vset_t b = vset_create(g->domain, -1, NULL);
        vset_copy(b, x.win[1-player]);
        vset_destroy(x.win[1-player]);

        // Second attractor computating (for 1-player)
        options->attr(1-player, g, b, options->attr_options, depth);

        {
            parity_game* g_minus_b = spg_copy(g);
            spg_destroy(g);
            spg_game_restrict(g_minus_b, b, options);

            // Second recursion step
            recursive_result y = spg_solve_recursive(g_minus_b, options, depth+1);

            //spg_destroy(g_minus_b); // has already been destroyed in spg_solve_recursive
            result.win[player] = y.win[player];
            vset_union(b, y.win[1-player]);
            vset_destroy(y.win[1-player]);
            result.win[1-player] = b;
        }
    }
    SPG_REPORT_RECURSIVE_RESULT(options->attr_options,indent,result);

    SPG_OUTPUT_DOT(options->attr_options->dot,result.win[0],"spg_set_%05zu_win0.dot",options->attr_options->dot_count++);
    SPG_OUTPUT_DOT(options->attr_options->dot,result.win[1],"spg_set_%05zu_win1.dot",options->attr_options->dot_count++);
    return result;
}


/**
 * \brief Computes and returns G = G - A.
 */
void spg_game_restrict(parity_game *g, vset_t a, const spgsolver_options* options)
{
    vset_minus(g->v, a); // compute V - A:
    for(int i=0; i<2; i++) {
        vset_minus(g->v_player[i], a); // compute V - A:
    }
    for(int i=g->min_priority; i<=g->max_priority; i++) {
        vset_minus(g->v_priority[i], a); // compute V - A:
        if (i==g->min_priority && i < g->max_priority && vset_is_empty(g->v_priority[i])) {
            g->min_priority++;
        }
    }
    while(g->max_priority > g->min_priority
            && vset_is_empty(g->v_priority[g->max_priority])) {
        g->max_priority--;
    }
    // TODO: compute E \intersects (V-A x V-A)? -- needs extension of the vset interface
    (void)options;
}

