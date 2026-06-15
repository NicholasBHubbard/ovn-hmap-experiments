/*
 * Copyright (c) 2008, 2009, 2010, 2012, 2013, 2015, 2016 Nicira, Inc.
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

#ifndef HMAP_H
#define HMAP_H 1

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "openvswitch/util.h"

#ifdef  __cplusplus
extern "C" {
#endif

#define HMAP_IMPL_CHAINED 0
#define HMAP_IMPL_SWISS 1
#define HMAP_IMPL_HYBRID 2

#ifndef HMAP_IMPL
#define HMAP_IMPL HMAP_IMPL_HYBRID
#endif

#ifndef HMAP_SWISS_THRESHOLD
#define HMAP_SWISS_THRESHOLD 2048
#endif

#ifndef HMAP_SWISS_LOAD_FACTOR_NUM
#define HMAP_SWISS_LOAD_FACTOR_NUM 7
#endif

#ifndef HMAP_SWISS_LOAD_FACTOR_DEN
#define HMAP_SWISS_LOAD_FACTOR_DEN 8
#endif

#if HMAP_IMPL != HMAP_IMPL_CHAINED && HMAP_IMPL != HMAP_IMPL_SWISS && \
    HMAP_IMPL != HMAP_IMPL_HYBRID
#error "HMAP_IMPL must be HMAP_IMPL_CHAINED, HMAP_IMPL_SWISS, or HMAP_IMPL_HYBRID"
#endif
#if HMAP_SWISS_THRESHOLD < 0
#error "HMAP_SWISS_THRESHOLD must be nonnegative"
#endif
#if HMAP_SWISS_LOAD_FACTOR_NUM < 1
#error "HMAP_SWISS_LOAD_FACTOR_NUM must be at least 1"
#endif
#if HMAP_SWISS_LOAD_FACTOR_DEN < 1
#error "HMAP_SWISS_LOAD_FACTOR_DEN must be at least 1"
#endif
#if HMAP_SWISS_LOAD_FACTOR_NUM > HMAP_SWISS_LOAD_FACTOR_DEN
#error "HMAP_SWISS_LOAD_FACTOR_NUM must be <= HMAP_SWISS_LOAD_FACTOR_DEN"
#endif
#if HMAP_SWISS_LOAD_FACTOR_NUM * 8 < HMAP_SWISS_LOAD_FACTOR_DEN
#error "HMAP_SWISS_LOAD_FACTOR_NUM / HMAP_SWISS_LOAD_FACTOR_DEN must be at least 1/8"
#endif

enum hmap_mode {
    HMAP_MODE_CHAINED,
    HMAP_MODE_SWISS,
};

/* A hash map node, to be embedded inside the data structure being mapped. */
struct hmap_node {
    size_t hash;                /* Hash value. */
    struct hmap_node *next;     /* Next in chain, or compatibility state. */
};

/* Returns the hash value embedded in 'node'. */
static inline size_t hmap_node_hash(const struct hmap_node *node)
{
    return node->hash;
}

#define HMAP_NODE_NULL ((struct hmap_node *) 1)
#define HMAP_NODE_NULL_INITIALIZER { 0, HMAP_NODE_NULL }

/* Returns true if 'node' has been set to null by hmap_node_nullify() and has
 * not been un-nullified by being inserted into an hmap. */
static inline bool
hmap_node_is_null(const struct hmap_node *node)
{
    return node->next == HMAP_NODE_NULL;
}

/* Marks 'node' with a distinctive value that can be tested with
 * hmap_node_is_null().  */
static inline void
hmap_node_nullify(struct hmap_node *node)
{
    node->next = HMAP_NODE_NULL;
}

/* A hash map.
 *
 * The implementation starts in chained mode unless HMAP_IMPL forces Swiss.
 * Hybrid builds promote to Swiss mode once the configured threshold is crossed.
 * The fields are exposed so hmaps can be stack allocated; callers should treat
 * them as implementation details. */
struct hmap {
    struct hmap_node **buckets; /* Chained buckets, or Swiss slots. */
    struct hmap_node *one;      /* Single chained bucket for mask == 0. */
    int8_t *ctrl;               /* Swiss control bytes, or NULL. */
    size_t mask;                /* Chained bucket mask, or Swiss group mask. */
    size_t n;                   /* Number of live nodes. */
    size_t n_occupied;          /* Swiss live + deleted slots. */
    uint8_t mode;               /* enum hmap_mode. */
};

#if HMAP_IMPL == HMAP_IMPL_SWISS
#define HMAP_INITIALIZER(HMAP) { NULL, NULL, NULL, 0, 0, 0, HMAP_MODE_SWISS }
#else
#define HMAP_INITIALIZER(HMAP) { NULL, NULL, NULL, 0, 0, 0, HMAP_MODE_CHAINED }
#endif

/* Initializer for an immutable struct hmap 'HMAP' that contains 'N' nodes
 * linked together starting at 'NODE'.  The hmap only has a single chain of
 * hmap_nodes, so 'N' should be small. */
#define HMAP_CONST(HMAP, N, NODE) {                                 \
        CONST_CAST(struct hmap_node **, &(HMAP)->one), NODE, NULL, 0, N, 0, \
        HMAP_MODE_CHAINED }

struct hmap_iter {
    size_t index;
    size_t match_index;
    size_t probe;
    size_t step;
    size_t limit;
    size_t hash;
    uint64_t matches;
    struct hmap_node *node;
};

/* Initialization. */
void hmap_init(struct hmap *);
void hmap_destroy(struct hmap *);
void hmap_clear(struct hmap *);
void hmap_swap(struct hmap *a, struct hmap *b);
void hmap_moved(struct hmap *hmap);
static inline size_t hmap_count(const struct hmap *);
static inline bool hmap_is_empty(const struct hmap *);

/* Adjusting capacity. */
void hmap_expand_at(struct hmap *, const char *where);
#define hmap_expand(HMAP) hmap_expand_at(HMAP, OVS_SOURCE_LOCATOR)

void hmap_shrink_at(struct hmap *, const char *where);
#define hmap_shrink(HMAP) hmap_shrink_at(HMAP, OVS_SOURCE_LOCATOR)

void hmap_reserve_at(struct hmap *, size_t capacity, const char *where);
#define hmap_reserve(HMAP, CAPACITY) \
    hmap_reserve_at(HMAP, CAPACITY, OVS_SOURCE_LOCATOR)

/* Internal slow paths used by inline operations below. */
void hmap_insert_at__(struct hmap *, struct hmap_node *, size_t hash,
                      const char *where);
void hmap_insert_fast__(struct hmap *, struct hmap_node *, size_t hash);
void hmap_remove__(struct hmap *, struct hmap_node *);
void hmap_replace__(struct hmap *, const struct hmap_node *old,
                    struct hmap_node *new_node);

/* Insertion and deletion. */
static inline void hmap_insert_at(struct hmap *, struct hmap_node *,
                                  size_t hash, const char *where);
#define hmap_insert(HMAP, NODE, HASH) \
    hmap_insert_at(HMAP, NODE, HASH, OVS_SOURCE_LOCATOR)

static inline void hmap_insert_fast(struct hmap *,
                                    struct hmap_node *, size_t hash);
static inline void hmap_remove(struct hmap *, struct hmap_node *);

void hmap_node_moved(struct hmap *, struct hmap_node *, struct hmap_node *);
static inline void hmap_replace(struct hmap *, const struct hmap_node *old,
                                struct hmap_node *new_node);

struct hmap_node *hmap_random_node(const struct hmap *);

#define HMAP_SWISS_GROUP_WIDTH 8
#define HMAP_SWISS_EMPTY ((int8_t) -128)
#define HMAP_SWISS_DELETED ((int8_t) -2)
#define HMAP_SWISS_BYTE_MASK 0x0101010101010101ULL
#define HMAP_SWISS_HIGH_BITS 0x8080808080808080ULL

static inline bool
hmap_is_swiss__(const struct hmap *hmap)
{
    return hmap->mode == HMAP_MODE_SWISS;
}

static inline bool
hmap_should_use_swiss__(size_t count)
{
#if HMAP_IMPL == HMAP_IMPL_HYBRID
    return count > HMAP_SWISS_THRESHOLD;
#elif HMAP_IMPL == HMAP_IMPL_SWISS
    (void) count;
    return true;
#else
    (void) count;
    return false;
#endif
}

static inline size_t
hmap_swiss_capacity__(const struct hmap *hmap)
{
    return hmap->ctrl ? (hmap->mask + 1) * HMAP_SWISS_GROUP_WIDTH : 0;
}

static inline size_t
hmap_swiss_live_capacity_for_slots__(size_t slots)
{
    return slots / HMAP_SWISS_LOAD_FACTOR_DEN * HMAP_SWISS_LOAD_FACTOR_NUM
           + slots % HMAP_SWISS_LOAD_FACTOR_DEN * HMAP_SWISS_LOAD_FACTOR_NUM
             / HMAP_SWISS_LOAD_FACTOR_DEN;
}

static inline size_t
hmap_swiss_live_capacity__(const struct hmap *hmap)
{
    return hmap_swiss_live_capacity_for_slots__(hmap_swiss_capacity__(hmap));
}

static inline size_t
hmap_swiss_group_index__(const struct hmap *hmap, size_t hash)
{
    return (hash >> 7) & hmap->mask;
}

static inline uint8_t
hmap_swiss_h2__(size_t hash)
{
    return hash & 0x7f;
}

static inline size_t
hmap_swiss_slot_pos__(size_t group, size_t slot)
{
    return group * HMAP_SWISS_GROUP_WIDTH + slot;
}

static inline uint64_t
hmap_swiss_load_ctrl__(const struct hmap *hmap, size_t group)
{
    uint64_t ctrl;

    memcpy(&ctrl, &hmap->ctrl[group * HMAP_SWISS_GROUP_WIDTH], sizeof ctrl);
    return ctrl;
}

static inline uint64_t
hmap_swiss_match8__(uint64_t ctrl, uint8_t byte)
{
    uint64_t match = ctrl ^ (HMAP_SWISS_BYTE_MASK * byte);
    return (match - HMAP_SWISS_BYTE_MASK) & ~match & HMAP_SWISS_HIGH_BITS;
}

static inline uint64_t
hmap_swiss_occupied__(uint64_t ctrl)
{
    return ~ctrl & HMAP_SWISS_HIGH_BITS;
}

static inline uint64_t
hmap_swiss_available__(uint64_t ctrl)
{
    return ctrl & HMAP_SWISS_HIGH_BITS;
}

static inline uint64_t
hmap_swiss_match__(uint64_t ctrl, uint8_t h2)
{
    return hmap_swiss_match8__(ctrl, h2);
}

static inline uint64_t
hmap_swiss_has_empty__(uint64_t ctrl)
{
    return (ctrl & ~(ctrl << 1)) & HMAP_SWISS_HIGH_BITS;
}

static inline size_t
hmap_swiss_match_slot__(uint64_t matches)
{
    return (size_t) __builtin_ctzll(matches) / 8;
}

static inline size_t
hmap_swiss_next_match__(uint64_t *matches)
{
    size_t slot = hmap_swiss_match_slot__(*matches);
    *matches &= *matches - 1;
    return slot;
}

static inline size_t
hmap_swiss_next_group__(const struct hmap *hmap, size_t group, size_t probe)
{
    return (group + probe + 1) & hmap->mask;
}

static inline struct hmap_node *
hmap_next_with_hash__(const struct hmap_node *node, size_t hash)
{
    while (node != NULL && node->hash != hash) {
        node = node->next;
    }
    return CONST_CAST(struct hmap_node *, node);
}

static inline struct hmap_node *
hmap_swiss_first_with_hash__(const struct hmap *hmap, size_t hash)
{
    uint8_t h2;
    size_t group;

    if (!hmap->ctrl) {
        return NULL;
    }

    h2 = hmap_swiss_h2__(hash);
    group = hmap_swiss_group_index__(hmap, hash);
    for (size_t probe = 0; probe <= hmap->mask; probe++) {
        uint64_t ctrl = hmap_swiss_load_ctrl__(hmap, group);
        uint64_t matches = hmap_swiss_match__(ctrl, h2);

        while (matches) {
            size_t slot = hmap_swiss_next_match__(&matches);
            size_t pos = hmap_swiss_slot_pos__(group, slot);
            struct hmap_node *node = hmap->buckets[pos];

            if (node->hash == hash) {
                return node;
            }
        }
        if (hmap_swiss_has_empty__(ctrl)) {
            return NULL;
        }
        group = hmap_swiss_next_group__(hmap, group, probe);
    }
    return NULL;
}

static inline struct hmap_node *
hmap_swiss_next_with_hash__(const struct hmap *hmap, size_t hash,
                            const struct hmap_node *prev)
{
    uint8_t h2;
    size_t group;
    bool found_prev = false;

    if (!hmap->ctrl || !prev) {
        return NULL;
    }

    h2 = hmap_swiss_h2__(hash);
    group = hmap_swiss_group_index__(hmap, hash);
    for (size_t probe = 0; probe <= hmap->mask; probe++) {
        uint64_t ctrl = hmap_swiss_load_ctrl__(hmap, group);
        uint64_t matches = hmap_swiss_match__(ctrl, h2);

        while (matches) {
            size_t slot = hmap_swiss_next_match__(&matches);
            size_t pos = hmap_swiss_slot_pos__(group, slot);
            struct hmap_node *node = hmap->buckets[pos];

            if (!found_prev) {
                if (node == prev) {
                    found_prev = true;
                }
            } else if (node->hash == hash) {
                return node;
            }
        }
        if (hmap_swiss_has_empty__(ctrl)) {
            return NULL;
        }
        group = hmap_swiss_next_group__(hmap, group, probe);
    }
    return NULL;
}

static inline struct hmap_node *
hmap_swiss_next_from__(const struct hmap *hmap, size_t group)
{
    if (!hmap->ctrl) {
        return NULL;
    }

    for (; group <= hmap->mask; group++) {
        uint64_t occupied = hmap_swiss_occupied__(
            hmap_swiss_load_ctrl__(hmap, group));
        if (occupied) {
            return hmap->buckets[hmap_swiss_slot_pos__(
                group, hmap_swiss_next_match__(&occupied))];
        }
    }
    return NULL;
}

static inline struct hmap_node *
hmap_swiss_next_after__(const struct hmap *hmap, const struct hmap_node *node)
{
    size_t pos = 0;

    if (!hmap->ctrl || !node) {
        return NULL;
    }

    size_t group = hmap_swiss_group_index__(hmap, node->hash);
    uint8_t h2 = hmap_swiss_h2__(node->hash);

    for (size_t probe = 0; probe <= hmap->mask; probe++) {
        uint64_t ctrl = hmap_swiss_load_ctrl__(hmap, group);
        uint64_t matches = hmap_swiss_match__(ctrl, h2);

        while (matches) {
            size_t slot = hmap_swiss_next_match__(&matches);
            size_t candidate = hmap_swiss_slot_pos__(group, slot);

            if (hmap->buckets[candidate] == node) {
                pos = candidate;
                goto found_node;
            }
        }
        if (hmap_swiss_has_empty__(ctrl)) {
            return NULL;
        }
        group = hmap_swiss_next_group__(hmap, group, probe);
    }

    return NULL;

found_node:
    for (size_t i = pos + 1; i < hmap_swiss_capacity__(hmap); i++) {
        if (hmap->ctrl[i] >= 0) {
            return hmap->buckets[i];
        }
    }

    return NULL;
}

static inline void
hmap_iter_init(struct hmap_iter *iter, const struct hmap *hmap)
{
    iter->index = 0;
    iter->match_index = 0;
    iter->probe = 0;
    iter->step = 1;
    iter->limit = hmap_is_swiss__(hmap) ? (hmap->ctrl ? hmap->mask + 1 : 0)
                                        : (hmap->buckets ? hmap->mask + 1 : 0);
    iter->hash = 0;
    iter->matches = 0;
    iter->node = NULL;
}

static inline struct hmap_node *
hmap_iter_next(const struct hmap *hmap, struct hmap_iter *iter)
{
    if (!hmap_is_swiss__(hmap)) {
        for (;;) {
            if (iter->node) {
                struct hmap_node *node = iter->node;
                iter->node = node->next;
                return node;
            }
            if (!hmap->buckets || iter->index > hmap->mask) {
                return NULL;
            }
            iter->node = hmap->buckets[iter->index++];
        }
    }

    if (!hmap->ctrl) {
        return NULL;
    }

    for (;;) {
        if (iter->matches) {
            size_t pos = hmap_swiss_slot_pos__(
                iter->match_index, hmap_swiss_next_match__(&iter->matches));
            return hmap->buckets[pos];
        }
        if (iter->index > hmap->mask) {
            return NULL;
        }
        iter->match_index = iter->index;
        iter->matches = hmap_swiss_occupied__(
            hmap_swiss_load_ctrl__(hmap, iter->index));
        iter->index++;
    }
}

static inline void
hmap_iter_hash_init(struct hmap_iter *iter, const struct hmap *hmap,
                    size_t hash)
{
    iter->index = 0;
    iter->match_index = 0;
    iter->probe = 0;
    iter->step = 1;
    iter->limit = 0;
    iter->hash = hash;
    iter->matches = 0;
    iter->node = NULL;

    if (!hmap_is_swiss__(hmap)) {
        if (!hmap->buckets) {
            return;
        }
        iter->node = hmap_next_with_hash__(hmap->buckets[hash & hmap->mask],
                                           hash);
    } else if (hmap->ctrl) {
        iter->index = hmap_swiss_group_index__(hmap, hash);
    }
}

static inline struct hmap_node *
hmap_iter_hash_next(const struct hmap *hmap, struct hmap_iter *iter)
{
    if (!hmap_is_swiss__(hmap)) {
        struct hmap_node *node = iter->node;

        if (node) {
            iter->node = hmap_next_with_hash__(node->next, iter->hash);
        }
        return node;
    }

    if (!hmap->ctrl) {
        return NULL;
    }

    for (;;) {
        if (iter->matches) {
            size_t pos = hmap_swiss_slot_pos__(
                iter->match_index, hmap_swiss_next_match__(&iter->matches));
            struct hmap_node *node = hmap->buckets[pos];

            if (node->hash == iter->hash) {
                return node;
            }
            continue;
        }

        if (iter->probe > hmap->mask) {
            return NULL;
        }

        size_t group = iter->index;
        uint64_t ctrl = hmap_swiss_load_ctrl__(hmap, group);
        iter->match_index = group;
        iter->matches = hmap_swiss_match__(ctrl, hmap_swiss_h2__(iter->hash));
        if (hmap_swiss_has_empty__(ctrl)) {
            iter->probe = hmap->mask + 1;
        } else {
            iter->index = hmap_swiss_next_group__(hmap, group, iter->probe);
            iter->probe++;
        }
    }
}

static inline void
hmap_iter_shard_init(struct hmap_iter *iter, const struct hmap *hmap,
                     size_t shard, size_t shard_count)
{
    iter->index = 0;
    iter->match_index = 0;
    iter->probe = 0;
    iter->step = shard_count;
    iter->limit = 0;
    iter->hash = 0;
    iter->matches = 0;
    iter->node = NULL;

    if (!shard_count || shard >= shard_count) {
        return;
    }

    iter->index = shard;
    iter->limit = hmap_is_swiss__(hmap) ? (hmap->ctrl ? hmap->mask + 1 : 0)
                                        : (hmap->buckets ? hmap->mask + 1 : 0);
}

static inline struct hmap_node *
hmap_iter_shard_next(const struct hmap *hmap, struct hmap_iter *iter)
{
    if (!iter->step) {
        return NULL;
    }

    if (!hmap_is_swiss__(hmap)) {
        for (;;) {
            if (iter->node) {
                struct hmap_node *node = iter->node;
                iter->node = node->next;
                return node;
            }
            if (iter->index >= iter->limit) {
                return NULL;
            }
            iter->node = hmap->buckets[iter->index];
            iter->index = iter->step > iter->limit - iter->index
                          ? iter->limit
                          : iter->index + iter->step;
        }
    }

    if (!hmap->ctrl) {
        return NULL;
    }

    for (;;) {
        if (iter->matches) {
            size_t pos = hmap_swiss_slot_pos__(
                iter->match_index, hmap_swiss_next_match__(&iter->matches));
            return hmap->buckets[pos];
        }
        if (iter->index >= iter->limit) {
            return NULL;
        }
        iter->match_index = iter->index;
        iter->matches = hmap_swiss_occupied__(
            hmap_swiss_load_ctrl__(hmap, iter->index));
        iter->index = iter->step > iter->limit - iter->index
                      ? iter->limit
                      : iter->index + iter->step;
    }
}

/* Search.
 *
 * HMAP_FOR_EACH_WITH_HASH iterates NODE over all of the nodes in HMAP that
 * have hash value equal to HASH.  HMAP_FOR_EACH_IN_BUCKET is kept as a
 * compatibility alias for the same exact-hash iteration.  MEMBER must be the
 * name of the 'struct hmap_node' member within NODE.
 *
 * HASH is only evaluated once.
 *
 * When the loop terminates normally, meaning the iteration has completed
 * without using 'break', NODE will be NULL.  This is true for all of the
 * HMAP_FOR_EACH_*() macros.
 */
#define HMAP_FOR_EACH_WITH_HASH(NODE, MEMBER, HASH, HMAP)                    \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;           \
         NODE##_hmap__ = NULL)                                               \
    for (size_t NODE##_hash__ = (HASH), NODE##_once__ = 1;                   \
         NODE##_once__; NODE##_once__ = 0)                                   \
    for (struct hmap_iter NODE##_iter__, *NODE##_iterp__ =                   \
             (hmap_iter_hash_init(&NODE##_iter__, NODE##_hmap__,             \
                                  NODE##_hash__),                            \
              &NODE##_iter__);                                               \
         NODE##_iterp__; NODE##_iterp__ = NULL)                              \
    for (INIT_MULTIVAR(NODE, MEMBER,                                         \
                       hmap_iter_hash_next(NODE##_hmap__, NODE##_iterp__),   \
                       struct hmap_node);                                    \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);           \
         UPDATE_MULTIVAR(NODE,                                               \
                          hmap_iter_hash_next(NODE##_hmap__, NODE##_iterp__)))

#define HMAP_FOR_EACH_IN_BUCKET(NODE, MEMBER, HASH, HMAP)                    \
    HMAP_FOR_EACH_WITH_HASH(NODE, MEMBER, HASH, HMAP)

static inline struct hmap_node *hmap_first_with_hash(const struct hmap *,
                                                     size_t hash);
static inline struct hmap_node *hmap_next_with_hash(const struct hmap *,
                                                    size_t hash,
                                                    const struct hmap_node *);

bool hmap_contains(const struct hmap *, const struct hmap_node *);

/* Iteration.
 *
 * The *_INIT variants of these macros additionally evaluate the expressions
 * supplied following the HMAP argument once during the loop initialization. */

/* Iterates through every node in HMAP. */
#define HMAP_FOR_EACH(NODE, MEMBER, HMAP) \
    HMAP_FOR_EACH_INIT(NODE, MEMBER, HMAP, (void) 0)
#define HMAP_FOR_EACH_INIT(NODE, MEMBER, HMAP, ...)                          \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;           \
         NODE##_hmap__ = NULL)                                               \
    for (struct hmap_iter NODE##_iter__, *NODE##_iterp__ =                   \
             (hmap_iter_init(&NODE##_iter__, NODE##_hmap__),                 \
              &NODE##_iter__);                                               \
         NODE##_iterp__; NODE##_iterp__ = NULL)                              \
    for (INIT_MULTIVAR_EXP(NODE, MEMBER,                                     \
                           hmap_iter_next(NODE##_hmap__, NODE##_iterp__),    \
                           struct hmap_node,                                 \
                           __VA_ARGS__);                                     \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);           \
         UPDATE_MULTIVAR(NODE,                                               \
                          hmap_iter_next(NODE##_hmap__, NODE##_iterp__)))

/* Iterates through every node in one read-only shard of HMAP.  Run SHARD from
 * 0 to SHARD_COUNT - 1 to visit every node exactly once across all shards.
 * SHARD_COUNT must be nonzero, and SHARD must be less than SHARD_COUNT.  Do
 * not insert, remove, clear, reserve, shrink, or destroy HMAP while shard
 * iteration is active. */
#define HMAP_FOR_EACH_SHARD(NODE, MEMBER, HMAP, SHARD, SHARD_COUNT)           \
    HMAP_FOR_EACH_SHARD_INIT(NODE, MEMBER, HMAP, SHARD, SHARD_COUNT, (void) 0)
#define HMAP_FOR_EACH_SHARD_INIT(NODE, MEMBER, HMAP, SHARD, SHARD_COUNT, ...) \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;            \
         NODE##_hmap__ = NULL)                                                \
    for (size_t NODE##_shard__ = (SHARD),                                     \
                NODE##_shard_count__ = (SHARD_COUNT),                         \
                NODE##_once__ = 1;                                            \
         NODE##_once__;                                                       \
         NODE##_once__ = 0)                                                   \
    for (struct hmap_iter NODE##_iter__, *NODE##_iterp__ =                    \
             (hmap_iter_shard_init(&NODE##_iter__, NODE##_hmap__,             \
                                   NODE##_shard__, NODE##_shard_count__),     \
              &NODE##_iter__);                                                \
         NODE##_iterp__; NODE##_iterp__ = NULL)                               \
    for (INIT_MULTIVAR_EXP(NODE, MEMBER,                                      \
                           hmap_iter_shard_next(NODE##_hmap__,               \
                                                NODE##_iterp__),              \
                           struct hmap_node,                                  \
                           __VA_ARGS__);                                      \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);            \
         UPDATE_MULTIVAR(NODE,                                                \
                          hmap_iter_shard_next(NODE##_hmap__,                \
                                               NODE##_iterp__)))

/* Safe when NODE may be freed (not needed when NODE may be removed from the
 * hash map but its members remain accessible and intact). */
#define HMAP_FOR_EACH_SAFE_LONG(NODE, NEXT, MEMBER, HMAP) \
    HMAP_FOR_EACH_SAFE_LONG_INIT (NODE, NEXT, MEMBER, HMAP, (void) NEXT)

#define HMAP_FOR_EACH_SAFE_LONG_INIT(NODE, NEXT, MEMBER, HMAP, ...)          \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;           \
         NODE##_hmap__ = NULL)                                               \
    for (struct hmap_iter NODE##_iter__, *NODE##_iterp__ =                   \
             (hmap_iter_init(&NODE##_iter__, NODE##_hmap__),                 \
              &NODE##_iter__);                                               \
         NODE##_iterp__; NODE##_iterp__ = NULL)                              \
    for (INIT_MULTIVAR_SAFE_LONG_EXP(                                        \
             NODE, NEXT, MEMBER,                                             \
             hmap_iter_next(NODE##_hmap__, NODE##_iterp__),                  \
             struct hmap_node, __VA_ARGS__);                                 \
         CONDITION_MULTIVAR_SAFE_LONG(                                       \
             NODE, NEXT, MEMBER,                                             \
             ITER_VAR(NODE) != NULL,                                         \
             ITER_VAR(NEXT) = hmap_iter_next(NODE##_hmap__, NODE##_iterp__), \
             ITER_VAR(NEXT) != NULL);                                        \
         UPDATE_MULTIVAR_SAFE_LONG(NODE, NEXT))

/* Short versions of HMAP_FOR_EACH_SAFE. */
#define HMAP_FOR_EACH_SAFE_SHORT(NODE, MEMBER, HMAP)                         \
    HMAP_FOR_EACH_SAFE_SHORT_INIT (NODE, MEMBER, HMAP, (void) 0)

#define HMAP_FOR_EACH_SAFE_SHORT_INIT(NODE, MEMBER, HMAP, ...)               \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;           \
         NODE##_hmap__ = NULL)                                               \
    for (struct hmap_iter NODE##_iter__, *NODE##_iterp__ =                   \
             (hmap_iter_init(&NODE##_iter__, NODE##_hmap__),                 \
              &NODE##_iter__);                                               \
         NODE##_iterp__; NODE##_iterp__ = NULL)                              \
    for (INIT_MULTIVAR_SAFE_SHORT_EXP(                                       \
             NODE, MEMBER,                                                   \
             hmap_iter_next(NODE##_hmap__, NODE##_iterp__),                  \
             struct hmap_node, __VA_ARGS__);                                 \
         CONDITION_MULTIVAR_SAFE_SHORT(                                      \
             NODE, MEMBER,                                                   \
             ITER_VAR(NODE) != NULL,                                         \
             ITER_NEXT_VAR(NODE) = hmap_iter_next(NODE##_hmap__,             \
                                                  NODE##_iterp__));           \
         UPDATE_MULTIVAR_SAFE_SHORT(NODE))

#define HMAP_FOR_EACH_SAFE(...)                                              \
    OVERLOAD_SAFE_MACRO(HMAP_FOR_EACH_SAFE_LONG,                             \
                        HMAP_FOR_EACH_SAFE_SHORT,                            \
                        4, __VA_ARGS__)

/* Continues an iteration from just after NODE. */
#define HMAP_FOR_EACH_CONTINUE(NODE, MEMBER, HMAP) \
    HMAP_FOR_EACH_CONTINUE_INIT(NODE, MEMBER, HMAP, (void) 0)
#define HMAP_FOR_EACH_CONTINUE_INIT(NODE, MEMBER, HMAP, ...)                 \
    for (INIT_MULTIVAR_EXP(NODE, MEMBER, hmap_next(HMAP, &(NODE)->MEMBER),   \
                           struct hmap_node, __VA_ARGS__);                   \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);           \
         UPDATE_MULTIVAR(NODE, hmap_next(HMAP, ITER_VAR(NODE))))

static inline struct hmap_node *hmap_first(const struct hmap *);
static inline struct hmap_node *hmap_next(const struct hmap *,
                                          const struct hmap_node *);

struct hmap_pop_helper_iter__ {
    struct hmap_iter iter;
    struct hmap_node *node;
    bool init;
};

static inline void
hmap_pop_helper__(struct hmap *hmap, struct hmap_pop_helper_iter__ *iter)
{
    if (!iter->init) {
        hmap_iter_init(&iter->iter, hmap);
        iter->init = true;
    }
    iter->node = hmap_iter_next(hmap, &iter->iter);
    if (iter->node) {
        hmap_remove(hmap, iter->node);
    }
}

#define HMAP_FOR_EACH_POP(NODE, MEMBER, HMAP)                                \
    for (struct hmap_pop_helper_iter__ ITER_VAR(NODE) = { { 0 }, NULL, false }; \
         hmap_pop_helper__(HMAP, &ITER_VAR(NODE)),                           \
         (ITER_VAR(NODE).node != NULL) ?                                     \
            (((NODE) = OBJECT_CONTAINING(ITER_VAR(NODE).node,                \
                                         NODE, MEMBER)),1):                  \
            (((NODE) = NULL), 0);)

struct hmap_position {
    unsigned int bucket;
    unsigned int offset;
};

struct hmap_node *hmap_at_position(const struct hmap *,
                                   struct hmap_position *);

/* Returns the number of nodes currently in 'hmap'. */
static inline size_t
hmap_count(const struct hmap *hmap)
{
    return hmap->n;
}

/* Returns the maximum number of nodes that 'hmap' may hold before it should be
 * rehashed. */
static inline size_t
hmap_capacity(const struct hmap *hmap)
{
    size_t capacity = hmap_is_swiss__(hmap) ? hmap_swiss_live_capacity__(hmap)
                                            : hmap->mask * 2 + 1;

    return capacity ? capacity : 1;
}

/* Returns true if 'hmap' currently contains no nodes,
 * false otherwise.
 * Note: While hmap in general is not thread-safe without additional locking,
 * hmap_is_empty() is. */
static inline bool
hmap_is_empty(const struct hmap *hmap)
{
    return hmap->n == 0;
}

static inline void
hmap_chain_insert_fast__(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    if (!hmap->buckets) {
        hmap->buckets = &hmap->one;
    }

    struct hmap_node **bucket = &hmap->buckets[hash & hmap->mask];
    node->hash = hash;
    node->next = *bucket;
    *bucket = node;
    hmap->n++;
}

/* Inserts 'node', with the given 'hash', into 'hmap'.  'hmap' is not
 * necessarily expanded automatically in forced chained mode. */
static inline void
hmap_insert_fast(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    if (!hmap_is_swiss__(hmap) && !hmap_should_use_swiss__(hmap->n + 1)) {
        hmap_chain_insert_fast__(hmap, node, hash);
    } else {
        hmap_insert_fast__(hmap, node, hash);
    }
}

/* Inserts 'node', with the given 'hash', into 'hmap', and expands 'hmap' if
 * necessary to optimize search performance.
 *
 * ('where' is used in debug logging.  Commonly one would use hmap_insert() to
 * automatically provide the caller's source file and line number for
 * 'where'.) */
static inline void
hmap_insert_at(struct hmap *hmap, struct hmap_node *node, size_t hash,
               const char *where)
{
    if (!hmap_is_swiss__(hmap) && !hmap_should_use_swiss__(hmap->n + 1)) {
        hmap_chain_insert_fast__(hmap, node, hash);
        if (hmap->n / 2 > hmap->mask) {
            hmap_expand_at(hmap, where);
        }
    } else {
        hmap_insert_at__(hmap, node, hash, where);
    }
}

/* Removes 'node' from 'hmap'.  Does not shrink the hash table; call
 * hmap_shrink() directly if desired. */
static inline void
hmap_remove(struct hmap *hmap, struct hmap_node *node)
{
    if (!hmap_is_swiss__(hmap)) {
        ovs_assert(hmap->buckets);
        struct hmap_node **bucket = &hmap->buckets[node->hash & hmap->mask];
        while (*bucket != node) {
            bucket = &(*bucket)->next;
        }
        *bucket = node->next;
        hmap->n--;
    } else {
        hmap_remove__(hmap, node);
    }
}

/* Puts 'new_node' in the position in 'hmap' currently occupied by 'old_node'.
 * The 'new_node' must hash to the same value as 'old_node'.  The client is
 * responsible for ensuring that the replacement does not violate any
 * client-imposed invariants (e.g. uniqueness of keys within a map).
 *
 * Afterward, 'old_node' is not part of 'hmap', and the client is responsible
 * for freeing it (if this is desirable). */
static inline void
hmap_replace(struct hmap *hmap,
             const struct hmap_node *old_node, struct hmap_node *new_node)
{
    if (!hmap_is_swiss__(hmap)) {
        ovs_assert(hmap->buckets);
        struct hmap_node **bucket = &hmap->buckets[old_node->hash & hmap->mask];
        while (*bucket != old_node) {
            bucket = &(*bucket)->next;
        }
        *bucket = new_node;
        new_node->hash = old_node->hash;
        new_node->next = old_node->next;
    } else {
        hmap_replace__(hmap, old_node, new_node);
    }
}

/* Returns the first node in 'hmap' with the given 'hash', or a null pointer if
 * no nodes have that hash value. */
static inline struct hmap_node *
hmap_first_with_hash(const struct hmap *hmap, size_t hash)
{
    return hmap_is_swiss__(hmap)
           ? hmap_swiss_first_with_hash__(hmap, hash)
           : (hmap->buckets
              ? hmap_next_with_hash__(hmap->buckets[hash & hmap->mask], hash)
              : NULL);
}

/* Returns the next node in the same hash map as 'node' with the same hash
 * value, or a null pointer if no more nodes have that hash value. */
static inline struct hmap_node *
hmap_next_with_hash(const struct hmap *hmap, size_t hash,
                    const struct hmap_node *node)
{
    return hmap_is_swiss__(hmap)
           ? hmap_swiss_next_with_hash__(hmap, hash, node)
           : hmap_next_with_hash__(node->next, hash);
}

static inline struct hmap_node *
hmap_next__(const struct hmap *hmap, size_t start)
{
    if (!hmap->buckets) {
        return NULL;
    }

    size_t i;
    for (i = start; i <= hmap->mask; i++) {
        struct hmap_node *node = hmap->buckets[i];
        if (node) {
            return node;
        }
    }
    return NULL;
}

/* Returns the first node in 'hmap', in arbitrary order, or a null pointer if
 * 'hmap' is empty. */
static inline struct hmap_node *
hmap_first(const struct hmap *hmap)
{
    return hmap_is_swiss__(hmap) ? hmap_swiss_next_from__(hmap, 0)
                                 : hmap_next__(hmap, 0);
}

/* Returns the next node in 'hmap' following 'node', in arbitrary order, or a
 * null pointer if 'node' is the last node in 'hmap'.
 *
 * If the hash map has been reallocated since 'node' was visited, some nodes
 * may be skipped or visited twice. */
static inline struct hmap_node *
hmap_next(const struct hmap *hmap, const struct hmap_node *node)
{
    return hmap_is_swiss__(hmap)
           ? hmap_swiss_next_after__(hmap, node)
           : (node->next
              ? node->next
              : hmap_next__(hmap, (node->hash & hmap->mask) + 1));
}

#ifdef  __cplusplus
}
#endif

#endif /* hmap.h */
