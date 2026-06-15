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
#include <stdlib.h>
#include "openvswitch/util.h"

#include "openvswitch/dshmap.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* A hash map node, to be embedded inside the data structure being mapped. */
struct hmap_node {
    size_t hash;                /* Hash value. */
    struct hmap_node *next;     /* Used for null markers and compatibility. */
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

/* A hash map. */
struct hmap {
    dshmap map;
};

static inline dshmap_hash_t
hmap_dshmap_hash(const void *node_)
{
    const struct hmap_node *node = (const struct hmap_node *) node_;
    return node->hash;
}

/* Initializer for an empty hash map. */
#define HMAP_INITIALIZER(HMAP) \
    { DSHMAP_INITIALIZER(hmap_dshmap_hash) }

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

/* Search.
 *
 * HMAP_FOR_EACH_WITH_HASH iterates NODE over all of the nodes in HMAP that
 * have hash value equal to HASH.  HMAP_FOR_EACH_IN_BUCKET is kept as a
 * compatibility alias for the same exact-hash iteration.  MEMBER must be the
 * name of the 'struct hmap_node' member within NODE.
 *
 * These macros may be used interchangeably to search for a particular value in
 * an hmap, see, e.g. shash_find() for an example.  Usually, using
 * HMAP_FOR_EACH_WITH_HASH provides an optimization, because comparing a hash
 * value is usually cheaper than comparing an entire hash map key.
 *
 * The loop should not change NODE to point to a different node or insert or
 * delete nodes in HMAP (unless it "break"s out of the loop to terminate
 * iteration).
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
    for (dshmap_iter NODE##_iter__, *NODE##_iterp__ =                        \
             (dshmap_iter_hash_init(&NODE##_iter__, &NODE##_hmap__->map,     \
                                    NODE##_hash__),                          \
              &NODE##_iter__);                                               \
         NODE##_iterp__; NODE##_iterp__ = NULL)                              \
    for (INIT_MULTIVAR(NODE, MEMBER,                                        \
                       (struct hmap_node *)                                  \
                       dshmap_iter_hash_next_with_hash_fn(                   \
                           &NODE##_hmap__->map, NODE##_iterp__,              \
                           hmap_dshmap_hash),                                \
                       struct hmap_node);                                    \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);           \
         UPDATE_MULTIVAR(NODE,                                               \
                          (struct hmap_node *)                               \
                          dshmap_iter_hash_next_with_hash_fn(                \
                              &NODE##_hmap__->map, NODE##_iterp__,           \
                              hmap_dshmap_hash)))

#define HMAP_FOR_EACH_IN_BUCKET(NODE, MEMBER, HASH, HMAP)                     \
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
 * supplied following the HMAP argument once during the loop initialization.
 * This makes it possible for data structures that wrap around hmaps to insert
 * additional initialization into their iteration macros without having to
 * completely rewrite them.  In particular, it can be a good idea to insert
 * BUILD_ASSERT_TYPE checks for map and node types that wrap hmap, since
 * otherwise it is possible for clients to accidentally confuse two derived
 * data structures that happen to use the same member names for struct hmap and
 * struct hmap_node. */

/* Iterates through every node in HMAP. */
#define HMAP_FOR_EACH(NODE, MEMBER, HMAP) \
    HMAP_FOR_EACH_INIT(NODE, MEMBER, HMAP, (void) 0)
#define HMAP_FOR_EACH_INIT(NODE, MEMBER, HMAP, ...)                           \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;            \
         NODE##_hmap__ = NULL)                                                \
    for (dshmap_iter NODE##_iter__, *NODE##_iterp__ =                         \
             (dshmap_iter_init(&NODE##_iter__, &NODE##_hmap__->map),          \
              &NODE##_iter__);                                                \
         NODE##_iterp__; NODE##_iterp__ = NULL)                               \
    for (INIT_MULTIVAR_EXP(NODE, MEMBER,                                      \
                           dshmap_iter_next(&NODE##_hmap__->map,              \
                                            NODE##_iterp__),                  \
                           struct hmap_node,                                  \
                           __VA_ARGS__);                                      \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);            \
         UPDATE_MULTIVAR(NODE,                                               \
                          dshmap_iter_next(&NODE##_hmap__->map,               \
                                           NODE##_iterp__)))

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
    for (dshmap_iter NODE##_iter__, *NODE##_iterp__ =                         \
             (dshmap_iter_shard_init(&NODE##_iter__, &NODE##_hmap__->map,     \
                                     NODE##_shard__, NODE##_shard_count__),   \
              &NODE##_iter__);                                                \
         NODE##_iterp__; NODE##_iterp__ = NULL)                               \
    for (INIT_MULTIVAR_EXP(NODE, MEMBER,                                      \
                           dshmap_iter_shard_next(&NODE##_hmap__->map,        \
                                                  NODE##_iterp__),            \
                           struct hmap_node,                                  \
                           __VA_ARGS__);                                      \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);            \
         UPDATE_MULTIVAR(NODE,                                               \
                          dshmap_iter_shard_next(&NODE##_hmap__->map,         \
                                                 NODE##_iterp__)))

/* Safe when NODE may be freed (not needed when NODE may be removed from the
 * hash map but its members remain accessible and intact). */
#define HMAP_FOR_EACH_SAFE_LONG(NODE, NEXT, MEMBER, HMAP) \
    HMAP_FOR_EACH_SAFE_LONG_INIT (NODE, NEXT, MEMBER, HMAP, (void) NEXT)

#define HMAP_FOR_EACH_SAFE_LONG_INIT(NODE, NEXT, MEMBER, HMAP, ...)           \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;            \
         NODE##_hmap__ = NULL)                                                \
    for (dshmap_iter NODE##_iter__, *NODE##_iterp__ =                         \
             (dshmap_iter_init(&NODE##_iter__, &NODE##_hmap__->map),          \
              &NODE##_iter__);                                                \
         NODE##_iterp__; NODE##_iterp__ = NULL)                               \
    for (INIT_MULTIVAR_SAFE_LONG_EXP(                                         \
             NODE, NEXT, MEMBER,                                              \
             dshmap_iter_next(&NODE##_hmap__->map, NODE##_iterp__),           \
             struct hmap_node, __VA_ARGS__);                                  \
         CONDITION_MULTIVAR_SAFE_LONG(NODE, NEXT, MEMBER,                     \
                                      ITER_VAR(NODE) != NULL,                 \
                                      ITER_VAR(NEXT) =                        \
                                          dshmap__iter_next_safe(              \
                                              &NODE##_hmap__->map,            \
                                              NODE##_iterp__,                 \
                                              ITER_VAR(NODE)),                \
                                      ITER_VAR(NEXT) != NULL);                \
         UPDATE_MULTIVAR_SAFE_LONG(NODE, NEXT))

/* Short versions of HMAP_FOR_EACH_SAFE. */
#define HMAP_FOR_EACH_SAFE_SHORT(NODE, MEMBER, HMAP)                          \
    HMAP_FOR_EACH_SAFE_SHORT_INIT (NODE, MEMBER, HMAP, (void) 0)

#define HMAP_FOR_EACH_SAFE_SHORT_INIT(NODE, MEMBER, HMAP, ...)                \
    for (const struct hmap *NODE##_hmap__ = (HMAP); NODE##_hmap__;            \
         NODE##_hmap__ = NULL)                                                \
    for (dshmap_iter NODE##_iter__, *NODE##_iterp__ =                         \
             (dshmap_iter_init(&NODE##_iter__, &NODE##_hmap__->map),          \
              &NODE##_iter__);                                                \
         NODE##_iterp__; NODE##_iterp__ = NULL)                               \
    for (INIT_MULTIVAR_SAFE_SHORT_EXP(                                        \
             NODE, MEMBER,                                                    \
             dshmap_iter_next(&NODE##_hmap__->map, NODE##_iterp__),           \
             struct hmap_node, __VA_ARGS__);                                  \
         CONDITION_MULTIVAR_SAFE_SHORT(NODE, MEMBER,                          \
                                       ITER_VAR(NODE) != NULL,                \
                                       ITER_NEXT_VAR(NODE) =                  \
                                           dshmap__iter_next_safe(             \
                                               &NODE##_hmap__->map,           \
                                               NODE##_iterp__,                \
                                               ITER_VAR(NODE)));              \
         UPDATE_MULTIVAR_SAFE_SHORT(NODE))

#define HMAP_FOR_EACH_SAFE(...)                                               \
    OVERLOAD_SAFE_MACRO(HMAP_FOR_EACH_SAFE_LONG,                              \
                        HMAP_FOR_EACH_SAFE_SHORT,                             \
                        4, __VA_ARGS__)


/* Continues an iteration from just after NODE. */
#define HMAP_FOR_EACH_CONTINUE(NODE, MEMBER, HMAP) \
    HMAP_FOR_EACH_CONTINUE_INIT(NODE, MEMBER, HMAP, (void) 0)
#define HMAP_FOR_EACH_CONTINUE_INIT(NODE, MEMBER, HMAP, ...)                  \
    for (INIT_MULTIVAR_EXP(NODE, MEMBER, hmap_next(HMAP, &(NODE)->MEMBER),    \
                           struct hmap_node, __VA_ARGS__);                    \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);            \
         UPDATE_MULTIVAR(NODE, hmap_next(HMAP, ITER_VAR(NODE))))

static inline struct hmap_node *hmap_first(const struct hmap *);
static inline struct hmap_node *hmap_next(const struct hmap *,
                                          const struct hmap_node *);

struct hmap_pop_helper_iter__ {
    struct hmap_node *node;
};

static inline void
hmap_pop_helper__(struct hmap *hmap, struct hmap_pop_helper_iter__ *iter) {
    struct hmap_node *node = hmap_first(hmap);

    if (node) {
        hmap_remove(hmap, node);
    }
    iter->node = node;
}

#define HMAP_FOR_EACH_POP(NODE, MEMBER, HMAP)                                 \
    for (struct hmap_pop_helper_iter__ ITER_VAR(NODE) = { NULL };             \
         hmap_pop_helper__(HMAP, &ITER_VAR(NODE)),                            \
         (ITER_VAR(NODE).node != NULL) ?                                      \
            (((NODE) = OBJECT_CONTAINING(ITER_VAR(NODE).node,                 \
                                         NODE, MEMBER)),1):                   \
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
    return dshmap_size(&hmap->map);
}

/* Returns the maximum number of nodes that 'hmap' may hold before it should be
 * rehashed. */
static inline size_t
hmap_capacity(const struct hmap *hmap)
{
    size_t capacity = dshmap_capacity(&hmap->map);
    return capacity ? capacity : 1;
}

/* Returns true if 'hmap' currently contains no nodes,
 * false otherwise.
 * Note: While hmap in general is not thread-safe without additional locking,
 * hmap_is_empty() is. */
static inline bool
hmap_is_empty(const struct hmap *hmap)
{
    return dshmap_is_empty(&hmap->map);
}

/* Inserts 'node', with the given 'hash', into 'hmap'.  The dshmap backend may
 * grow to preserve hmap_insert_fast() callers that relied on chained hmap
 * insertion not being limited by reserved slot capacity. */
static inline void
hmap_insert_fast(struct hmap *hmap, struct hmap_node *node, size_t hash)
{
    node->hash = hash;
    node->next = NULL;
    dshmap_insert(&hmap->map, node, hash);
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
    (void)where;
    node->hash = hash;
    node->next = NULL;
    dshmap_insert(&hmap->map, node, hash);
}

/* Removes 'node' from 'hmap'.  Does not shrink the hash table; call
 * hmap_shrink() directly if desired. */
static inline void
hmap_remove(struct hmap *hmap, struct hmap_node *node)
{
    dshmap_remove(&hmap->map, node, node->hash);
    node->next = NULL;
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
    new_node->hash = old_node->hash;
    new_node->next = NULL;
    dshmap_remove(&hmap->map, old_node, old_node->hash);
    dshmap_insert_reserved(&hmap->map, new_node, new_node->hash);
}

/* Returns the first node in 'hmap' with the given 'hash', or a null pointer if
 * no nodes have that hash value. */
static inline struct hmap_node *
hmap_first_with_hash(const struct hmap *hmap, size_t hash)
{
    return (struct hmap_node *) dshmap_find_with_hash_fn(
        &hmap->map, hash, hmap_dshmap_hash);
}

/* Returns the next node in the same hash map as 'node' with the same hash
 * value, or a null pointer if no more nodes have that hash value.
 */
static inline struct hmap_node *
hmap_next_with_hash(const struct hmap *hmap, size_t hash,
                    const struct hmap_node *node)
{
    return (struct hmap_node *) dshmap_find_next_with_hash_fn(
        &hmap->map, hash, node, hmap_dshmap_hash);
}

/* Returns the first node in 'hmap', in arbitrary order, or a null pointer if
 * 'hmap' is empty. */
static inline struct hmap_node *
hmap_first(const struct hmap *hmap)
{
    dshmap_iter iter;

    dshmap_iter_init(&iter, &hmap->map);
    return (struct hmap_node *) dshmap_iter_next(&hmap->map, &iter);
}

/* Returns the next node in 'hmap' following 'node', in arbitrary order, or a
 * null pointer if 'node' is the last node in 'hmap'.
 *
 * If the hash map has been reallocated since 'node' was visited, some nodes
 * may be skipped or visited twice. */
static inline struct hmap_node *
hmap_next(const struct hmap *hmap, const struct hmap_node *node)
{
    return (struct hmap_node *) dshmap_iter_next_after_hash(
        &hmap->map, node, node->hash);
}

#ifdef  __cplusplus
}
#endif

#endif /* hmap.h */
