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

VLOG_DEFINE_THIS_MODULE(hmap);

COVERAGE_DEFINE(hmap_pathological);
COVERAGE_DEFINE(hmap_expand);
COVERAGE_DEFINE(hmap_shrink);
COVERAGE_DEFINE(hmap_reserve);

static void
hmap_init_chained__(struct hmap *hmap)
{
    hmap->buckets = &hmap->one;
    hmap->one = NULL;
    hmap->ctrl = NULL;
    hmap->mask = 0;
    hmap->n = 0;
    hmap->n_occupied = 0;
    hmap->mode = HMAP_MODE_CHAINED;
}

static void
hmap_init_swiss__(struct hmap *hmap)
{
    hmap->buckets = NULL;
    hmap->one = NULL;
    hmap->ctrl = NULL;
    hmap->mask = 0;
    hmap->n = 0;
    hmap->n_occupied = 0;
    hmap->mode = HMAP_MODE_SWISS;
}

/* Initializes 'hmap' as an empty hash table. */
void
hmap_init(struct hmap *hmap)
{
#if HMAP_IMPL == HMAP_IMPL_SWISS
    hmap_init_swiss__(hmap);
#else
    hmap_init_chained__(hmap);
#endif
}

static void
hmap_free_storage__(struct hmap *hmap)
{
    if (hmap_is_swiss__(hmap)) {
        free(hmap->ctrl);
        free(hmap->buckets);
    } else if (hmap->buckets != &hmap->one) {
        free(hmap->buckets);
    }
}

/* Frees memory reserved by 'hmap'.  It is the client's responsibility to free
 * the nodes themselves, if necessary. */
void
hmap_destroy(struct hmap *hmap)
{
    if (hmap) {
        hmap_free_storage__(hmap);
        hmap_init(hmap);
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
    if (hmap_is_swiss__(hmap)) {
        if (!hmap->n && !hmap->n_occupied) {
            return;
        }
        if (hmap->ctrl) {
            memset(hmap->ctrl, HMAP_SWISS_EMPTY, hmap_swiss_capacity__(hmap));
        }
        hmap->n = 0;
        hmap->n_occupied = 0;
        return;
    }

    if (!hmap->n) {
        return;
    }
    hmap->n = 0;
    memset(hmap->buckets, 0, (hmap->mask + 1) * sizeof *hmap->buckets);
}

/* Exchanges hash maps 'a' and 'b'. */
void
hmap_swap(struct hmap *a, struct hmap *b)
{
    struct hmap tmp = *a;
    *a = *b;
    *b = tmp;
    hmap_moved(a);
    hmap_moved(b);
}

/* Adjusts 'hmap' to compensate for having moved position in memory (e.g. due
 * to realloc()). */
void
hmap_moved(struct hmap *hmap)
{
    if (!hmap_is_swiss__(hmap) && !hmap->mask && hmap->buckets) {
        hmap->buckets = &hmap->one;
    }
}

static size_t
calc_mask(size_t capacity)
{
    size_t mask = capacity / 2;
    mask |= mask >> 1;
    mask |= mask >> 2;
    mask |= mask >> 4;
    mask |= mask >> 8;
    mask |= mask >> 16;
#if SIZE_MAX > UINT32_MAX
    mask |= mask >> 32;
#endif

    /* If we need to dynamically allocate buckets we might as well allocate at
     * least 4 of them. */
    mask |= (mask & 1) << 1;

    return mask;
}

static void
resize_chained(struct hmap *hmap, size_t new_mask, const char *where)
{
    struct hmap tmp;
    size_t i;

    ovs_assert(!hmap_is_swiss__(hmap));
    ovs_assert(is_pow2(new_mask + 1));

    hmap_init_chained__(&tmp);
    if (new_mask) {
        tmp.buckets = xmalloc(sizeof *tmp.buckets * (new_mask + 1));
        tmp.mask = new_mask;
        for (i = 0; i <= tmp.mask; i++) {
            tmp.buckets[i] = NULL;
        }
    }

    int n_big_buckets = 0;
    int biggest_count = 0;
    int n_biggest_buckets = 0;
    for (i = 0; i <= hmap->mask; i++) {
        struct hmap_node *node, *next;
        int count = 0;
        for (node = hmap->buckets ? hmap->buckets[i] : NULL;
             node; node = next) {
            next = node->next;
            hmap_chain_insert_fast__(&tmp, node, node->hash);
            count++;
        }
        if (count > 5) {
            n_big_buckets++;
            if (count > biggest_count) {
                biggest_count = count;
                n_biggest_buckets = 1;
            } else if (count == biggest_count) {
                n_biggest_buckets++;
            }
        }
    }
    hmap_swap(hmap, &tmp);
    hmap_destroy(&tmp);

    if (n_big_buckets) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(10, 10);
        COVERAGE_INC(hmap_pathological);
        VLOG_DBG_RL(&rl, "%s: %d bucket%s with 6+ nodes, "
                    "including %d bucket%s with %d nodes "
                    "(%"PRIuSIZE" nodes total across %"PRIuSIZE" buckets)",
                    where,
                    n_big_buckets, n_big_buckets > 1 ? "s" : "",
                    n_biggest_buckets, n_biggest_buckets > 1 ? "s" : "",
                    biggest_count,
                    hmap->n, hmap->mask + 1);
    }
}

static size_t
swiss_groups_for_count(size_t count)
{
    size_t groups = 1;

    while (hmap_swiss_live_capacity_for_slots__(
               groups * HMAP_SWISS_GROUP_WIDTH) < count) {
        groups *= 2;
    }
    return groups;
}

static void
swiss_alloc(struct hmap *hmap, size_t groups)
{
    size_t slots = groups * HMAP_SWISS_GROUP_WIDTH;

    hmap->buckets = xmalloc(slots * sizeof *hmap->buckets);
    hmap->ctrl = xmalloc(slots);
    memset(hmap->ctrl, HMAP_SWISS_EMPTY, slots);
    hmap->one = NULL;
    hmap->mask = groups - 1;
    hmap->n = 0;
    hmap->n_occupied = 0;
    hmap->mode = HMAP_MODE_SWISS;
}

static void
swiss_insert_no_grow(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    uint8_t h2 = hmap_swiss_h2__(hash);
    size_t group = hmap_swiss_group_index__(hmap, hash);

    for (size_t probe = 0; probe <= hmap->mask; probe++) {
        uint64_t ctrl = hmap_swiss_load_ctrl__(hmap, group);
        uint64_t available = hmap_swiss_available__(ctrl);

        if (available) {
            size_t slot = hmap_swiss_next_match__(&available);
            size_t pos = hmap_swiss_slot_pos__(group, slot);
            bool was_empty = hmap->ctrl[pos] == HMAP_SWISS_EMPTY;

            hmap->ctrl[pos] = (int8_t) h2;
            hmap->buckets[pos] = node;
            node->hash = hash;
            node->next = NULL;
            hmap->n++;
            if (was_empty) {
                hmap->n_occupied++;
            }
            return;
        }
        group = hmap_swiss_next_group__(hmap, group, probe);
    }
    OVS_NOT_REACHED();
}

static void
swiss_rebuild(struct hmap *hmap, size_t groups)
{
    struct hmap tmp;

    hmap_init_swiss__(&tmp);
    swiss_alloc(&tmp, groups);

    if (hmap_is_swiss__(hmap)) {
        if (hmap->ctrl) {
            for (size_t group = 0; group <= hmap->mask; group++) {
                uint64_t occupied = hmap_swiss_occupied__(
                    hmap_swiss_load_ctrl__(hmap, group));

                while (occupied) {
                    size_t pos = hmap_swiss_slot_pos__(
                        group, hmap_swiss_next_match__(&occupied));
                    struct hmap_node *node = hmap->buckets[pos];
                    swiss_insert_no_grow(&tmp, node, node->hash);
                }
            }
        }
    } else {
        for (size_t i = 0; i <= hmap->mask; i++) {
            struct hmap_node *node = hmap->buckets ? hmap->buckets[i] : NULL;

            while (node) {
                struct hmap_node *next = node->next;
                swiss_insert_no_grow(&tmp, node, node->hash);
                node = next;
            }
        }
    }

    hmap_free_storage__(hmap);
    *hmap = tmp;
}

static void
swiss_reserve(struct hmap *hmap, size_t count)
{
    size_t groups = swiss_groups_for_count(count ? count : 1);

    if (hmap_is_swiss__(hmap)) {
        if (hmap->ctrl) {
            size_t old_groups = hmap->mask + 1;
            if (groups <= old_groups &&
                count <= hmap_swiss_live_capacity__(hmap)) {
                return;
            }
        }
        swiss_rebuild(hmap, groups);
    } else {
        swiss_rebuild(hmap, groups);
    }
}

static void
swiss_ensure_insert_capacity(struct hmap *hmap)
{
    size_t cap = hmap_swiss_capacity__(hmap);

    if (!cap) {
        swiss_reserve(hmap, 1);
        return;
    }

    if (hmap->n_occupied + 1 >
        hmap_swiss_live_capacity_for_slots__(cap)) {
        size_t groups = swiss_groups_for_count(hmap->n + 1);
        if (groups < hmap->mask + 1) {
            groups = hmap->mask + 1;
        }
        swiss_rebuild(hmap, groups);
    }
}

static bool
swiss_find_slot(const struct hmap *hmap, const struct hmap_node *node,
                size_t *posp)
{
    size_t hash = node->hash;
    uint8_t h2 = hmap_swiss_h2__(hash);
    size_t group;

    if (!hmap->ctrl) {
        return false;
    }

    group = hmap_swiss_group_index__(hmap, hash);
    for (size_t probe = 0; probe <= hmap->mask; probe++) {
        uint64_t ctrl = hmap_swiss_load_ctrl__(hmap, group);
        uint64_t matches = hmap_swiss_match__(ctrl, h2);

        while (matches) {
            size_t pos = hmap_swiss_slot_pos__(
                group, hmap_swiss_next_match__(&matches));
            if (hmap->buckets[pos] == node) {
                *posp = pos;
                return true;
            }
        }
        if (hmap_swiss_has_empty__(ctrl)) {
            return false;
        }
        group = hmap_swiss_next_group__(hmap, group, probe);
    }
    return false;
}

void
hmap_insert_fast__(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    if (!hmap_is_swiss__(hmap)) {
        if (!hmap_should_use_swiss__(hmap->n + 1)) {
            hmap_chain_insert_fast__(hmap, node, hash);
            return;
        }
        swiss_reserve(hmap, hmap->n + 1);
    }

    swiss_ensure_insert_capacity(hmap);
    swiss_insert_no_grow(hmap, node, hash);
}

void
hmap_insert_at__(struct hmap *hmap, struct hmap_node *node, size_t hash,
                 const char *where)
{
    (void) where;

    hmap_insert_fast__(hmap, node, hash);
}

void
hmap_remove__(struct hmap *hmap, struct hmap_node *node)
{
    size_t pos;

    ovs_assert(hmap_is_swiss__(hmap));
    if (!swiss_find_slot(hmap, node, &pos)) {
        OVS_NOT_REACHED();
    }

    size_t group = pos / HMAP_SWISS_GROUP_WIDTH;
    uint64_t ctrl = hmap_swiss_load_ctrl__(hmap, group);
    if (hmap_swiss_has_empty__(ctrl)) {
        hmap->ctrl[pos] = HMAP_SWISS_EMPTY;
        hmap->n_occupied--;
    } else {
        hmap->ctrl[pos] = HMAP_SWISS_DELETED;
    }
    hmap->buckets[pos] = NULL;
    node->next = NULL;
    hmap->n--;
}

void
hmap_replace__(struct hmap *hmap, const struct hmap_node *old_node,
               struct hmap_node *new_node)
{
    size_t pos;

    ovs_assert(hmap_is_swiss__(hmap));
    if (!swiss_find_slot(hmap, old_node, &pos)) {
        OVS_NOT_REACHED();
    }

    new_node->hash = old_node->hash;
    new_node->next = NULL;
    hmap->buckets[pos] = new_node;
}

/* Expands 'hmap', if necessary, to optimize the performance of searches.
 *
 * ('where' is used in debug logging.  Commonly one would use hmap_expand() to
 * automatically provide the caller's source file and line number for
 * 'where'.) */
void
hmap_expand_at(struct hmap *hmap, const char *where)
{
    if (hmap_is_swiss__(hmap) || hmap_should_use_swiss__(hmap->n)) {
        size_t old_capacity = hmap_capacity(hmap);
        swiss_reserve(hmap, hmap->n);
        if (hmap_capacity(hmap) > old_capacity) {
            COVERAGE_INC(hmap_expand);
        }
        return;
    }

    size_t new_mask = calc_mask(hmap->n);
    if (new_mask > hmap->mask) {
        COVERAGE_INC(hmap_expand);
        resize_chained(hmap, new_mask, where);
    }
}

/* Shrinks 'hmap', if necessary, to optimize the performance of iteration.
 *
 * ('where' is used in debug logging.  Commonly one would use hmap_shrink() to
 * automatically provide the caller's source file and line number for
 * 'where'.) */
void
hmap_shrink_at(struct hmap *hmap, const char *where)
{
    if (hmap_is_swiss__(hmap)) {
        if (!hmap->n) {
            COVERAGE_INC(hmap_shrink);
            hmap_destroy(hmap);
            return;
        }

        size_t groups = swiss_groups_for_count(hmap->n);
        if (hmap->ctrl && groups < hmap->mask + 1) {
            COVERAGE_INC(hmap_shrink);
            swiss_rebuild(hmap, groups);
        }
        return;
    }

    size_t new_mask = calc_mask(hmap->n);
    if (new_mask < hmap->mask) {
        COVERAGE_INC(hmap_shrink);
        resize_chained(hmap, new_mask, where);
    }
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
    if (hmap_is_swiss__(hmap) || hmap_should_use_swiss__(n)) {
        size_t old_capacity = hmap_capacity(hmap);
        swiss_reserve(hmap, n);
        if (hmap_capacity(hmap) > old_capacity) {
            COVERAGE_INC(hmap_reserve);
        }
        return;
    }

    size_t new_mask = calc_mask(n);
    if (new_mask > hmap->mask) {
        COVERAGE_INC(hmap_reserve);
        resize_chained(hmap, new_mask, where);
    }
}

/* Adjusts 'hmap' to compensate for 'old_node' having moved position in memory
 * to 'node' (e.g. due to realloc()). */
void
hmap_node_moved(struct hmap *hmap,
                struct hmap_node *old_node, struct hmap_node *node)
{
    node->hash = old_node->hash;

    if (hmap_is_swiss__(hmap)) {
        size_t pos;

        if (!swiss_find_slot(hmap, old_node, &pos)) {
            OVS_NOT_REACHED();
        }
        node->next = NULL;
        hmap->buckets[pos] = node;
    } else {
        ovs_assert(hmap->buckets);
        struct hmap_node **bucket = &hmap->buckets[node->hash & hmap->mask];
        while (*bucket != old_node) {
            bucket = &(*bucket)->next;
        }
        node->next = old_node->next;
        *bucket = node;
    }
}

/* Chooses and returns a randomly selected node from 'hmap', which must not be
 * empty.
 *
 * I wouldn't depend on this algorithm to be fair, since I haven't analyzed it.
 * But it does at least ensure that any node in 'hmap' can be chosen. */
struct hmap_node *
hmap_random_node(const struct hmap *hmap)
{
    if (hmap_is_swiss__(hmap)) {
        size_t cap = hmap_swiss_capacity__(hmap);

        for (;;) {
            size_t pos = random_range(cap);
            if (hmap->ctrl[pos] >= 0) {
                return hmap->buckets[pos];
            }
        }
    }

    struct hmap_node *bucket, *node;
    size_t n, i;

    /* Choose a random non-empty bucket. */
    for (;;) {
        bucket = hmap->buckets[random_uint32() & hmap->mask];
        if (bucket) {
            break;
        }
    }

    /* Count nodes in bucket. */
    n = 0;
    for (node = bucket; node; node = node->next) {
        n++;
    }

    /* Choose random node from bucket. */
    i = random_range(n);
    for (node = bucket; i-- > 0; node = node->next) {
        continue;
    }
    return node;
}

/* Returns the next node in 'hmap' in hash order, or NULL if no nodes remain in
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
    if (hmap_is_swiss__(hmap)) {
        if (!hmap->ctrl) {
            pos->bucket = 0;
            pos->offset = 0;
            return NULL;
        }

        for (size_t group = pos->bucket; group <= hmap->mask; group++) {
            size_t slot = group == pos->bucket ? pos->offset : 0;

            for (; slot < HMAP_SWISS_GROUP_WIDTH; slot++) {
                size_t p = hmap_swiss_slot_pos__(group, slot);
                if (hmap->ctrl[p] >= 0) {
                    if (slot + 1 < HMAP_SWISS_GROUP_WIDTH) {
                        pos->bucket = group;
                        pos->offset = slot + 1;
                    } else {
                        pos->bucket = group + 1;
                        pos->offset = 0;
                    }
                    return hmap->buckets[p];
                }
            }
        }

        pos->bucket = 0;
        pos->offset = 0;
        return NULL;
    }

    if (!hmap->buckets) {
        pos->bucket = 0;
        pos->offset = 0;
        return NULL;
    }

    size_t offset = pos->offset;
    for (size_t b_idx = pos->bucket; b_idx <= hmap->mask; b_idx++) {
        struct hmap_node *node;
        size_t n_idx;

        for (n_idx = 0, node = hmap->buckets[b_idx]; node != NULL;
             n_idx++, node = node->next) {
            if (n_idx == offset) {
                if (node->next) {
                    pos->bucket = node->hash & hmap->mask;
                    pos->offset = offset + 1;
                } else {
                    pos->bucket = (node->hash & hmap->mask) + 1;
                    pos->offset = 0;
                }
                return node;
            }
        }
        offset = 0;
    }

    pos->bucket = 0;
    pos->offset = 0;
    return NULL;
}

/* Returns true if 'node' is in 'hmap', false otherwise. */
bool
hmap_contains(const struct hmap *hmap, const struct hmap_node *node)
{
    if (hmap_is_swiss__(hmap)) {
        size_t pos;
        return swiss_find_slot(hmap, node, &pos);
    }

    struct hmap_node *p;

    if (!hmap->buckets) {
        return false;
    }

    for (p = hmap_first_with_hash(hmap, node->hash); p;
         p = hmap_next_with_hash(hmap, node->hash, p)) {
        if (p == node) {
            return true;
        }
    }

    return false;
}
