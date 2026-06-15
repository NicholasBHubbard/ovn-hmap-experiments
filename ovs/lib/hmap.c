/*
 * Copyright (c) 2008, 2009, 2010, 2012, 2013, 2015, 2019 Nicira, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>
#include "openvswitch/hmap.h"
#include <stdint.h>
#include <string.h>
#include "coverage.h"
#include "random.h"
#include "util.h"
#include "openvswitch/vlog.h"
#include "openvswitch/dshmap.h"

VLOG_DEFINE_THIS_MODULE(hmap);

COVERAGE_DEFINE(hmap_pathological);
COVERAGE_DEFINE(hmap_expand);
COVERAGE_DEFINE(hmap_shrink);
COVERAGE_DEFINE(hmap_reserve);

/* Initializes 'hmap' as an empty hash table. */
void
hmap_init(struct hmap *hmap)
{
    dshmap_init(&hmap->map, hmap_dshmap_hash);
}

/* Frees memory reserved by 'hmap'.  It is the client's responsibility to free
 * the nodes themselves, if necessary. */
void
hmap_destroy(struct hmap *hmap)
{
    if (hmap) {
        dshmap_destroy(&hmap->map);
    }
}

/* Removes all nodes from 'hmap', leaving it ready to accept more nodes.  Does
 * not free memory allocated for 'hmap'.
 *
 * This function is appropriate when 'hmap' will soon have about as many
 * elements as it did before.  If 'hmap' will likely have fewer elements than
 * before, use hmap_destroy() followed by hmap_init() to save memory and
 * iteration time. */
void
hmap_clear(struct hmap *hmap)
{
    dshmap_clear(&hmap->map);
}

/* Exchanges hash maps 'a' and 'b'. */
void
hmap_swap(struct hmap *a, struct hmap *b)
{
    struct hmap tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Adjusts 'hmap' to compensate for having moved position in memory (e.g. due
 * to realloc()). */
void
hmap_moved(struct hmap *hmap)
{
    (void)hmap;
}

/* Expands 'hmap', if necessary, to optimize the performance of searches.
 *
 * ('where' is used in debug logging.  Commonly one would use hmap_expand() to
 * automatically provide the caller's source file and line number for
 * 'where'.) */
void
hmap_expand_at(struct hmap *hmap, const char *where)
{
    (void) where;
    dshmap_reserve(&hmap->map, dshmap_size(&hmap->map));
}

void
hmap_shrink_at(struct hmap *hmap, const char *where)
{
    (void) where;
    dshmap_shrink(&hmap->map);
}

/* Expands 'hmap', if necessary, to optimize the performance of searches when
 * it has up to 'n' elements.  (But iteration will be slow in a hash map whose
 * allocated capacity is much higher than its current number of nodes.)
 *
 * ('where' is used in debug logging.  Commonly one would use hmap_reserve() to
 * automatically provide the caller's source file and line number for
 * 'where'.) */
void
hmap_reserve_at(struct hmap *hmap, size_t n, const char *where)
{
    (void) where;
    dshmap_reserve(&hmap->map, n);
}

/* Adjusts 'hmap' to compensate for 'old_node' having moved position in memory
 * to 'node' (e.g. due to realloc()). */
void
hmap_node_moved(struct hmap *hmap,
                struct hmap_node *old_node, struct hmap_node *node)
{
    node->hash = old_node->hash;
    node->next = NULL;
    dshmap_remove(&hmap->map, old_node, old_node->hash);
    dshmap_insert_reserved(&hmap->map, node, node->hash);
}

/* Chooses and returns a randomly selected node from 'hmap', which must not be
 * empty.
 *
 * I wouldn't depend on this algorithm to be fair, since I haven't analyzed it.
 * But it does at least ensure that any node in 'hmap' can be chosen. */
struct hmap_node *
hmap_random_node(const struct hmap *hmap)
{
    size_t i = random_range(hmap_count(hmap));
    dshmap_iter iter;

    dshmap_iter_init(&iter, &hmap->map);
    for (;;) {
        struct hmap_node *node =
            (struct hmap_node *) dshmap_iter_next(&hmap->map, &iter);
        if (i-- == 0) {
            return node;
        }
    }
}

/* Returns the next node in 'hmap' in arbitrary order, or NULL if no nodes remain in
 * 'hmap'.  Uses '*pos' to determine where to begin iteration, and updates
 * '*pos' to pass on the next iteration into them before returning.
 *
 * It's better to use plain HMAP_FOR_EACH and related functions, since they are
 * faster and better at dealing with hmaps that change during iteration.
 *
 * Before beginning iteration, set '*pos' to all zeros. */
struct hmap_node *
hmap_at_position(const struct hmap *hmap,
                 struct hmap_position *pos)
{
    size_t target = pos->offset;
    size_t i = 0;
    dshmap_iter iter;

    (void) pos->bucket;
    dshmap_iter_init(&iter, &hmap->map);
    for (;;) {
        struct hmap_node *node =
            (struct hmap_node *) dshmap_iter_next(&hmap->map, &iter);
        if (!node) {
            break;
        }
        if (i++ == target) {
            pos->bucket = 0;
            pos->offset = target + 1;
            return node;
        }
    }

    pos->bucket = 0;
    pos->offset = 0;
    return NULL;
}

/* Returns true if 'node' is in 'hmap', false otherwise. */
bool
hmap_contains(const struct hmap *hmap, const struct hmap_node *node)
{
    struct hmap_node *p;

    for (p = hmap_first_with_hash(hmap, node->hash); p;
         p = hmap_next_with_hash(hmap, node->hash, p)) {
        if (p == node) {
            return true;
        }
    }

    return false;
}
