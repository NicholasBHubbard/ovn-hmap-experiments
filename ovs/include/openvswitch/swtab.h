/*
 * Copyright (c) 2026 Nicholas B. Hubbard.
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

#ifndef OPENVSWITCH_SWTAB_H
#define OPENVSWITCH_SWTAB_H 1

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "openvswitch/util.h"

#ifdef  __cplusplus
extern "C" {
#endif

/* A Swiss hash table node, to be embedded inside the data structure being
 * mapped. */
struct swtab_node {
    size_t hash;                /* Hash value. */
    size_t slot;                /* Current slot while inserted. */
};

/* Returns the hash value embedded in 'node'. */
static inline size_t swtab_node_hash(const struct swtab_node *);

/* A Swiss hash table.
 *
 * The struct is exposed so it can be stack allocated.  Callers should treat
 * the fields as implementation details. */
struct swtab {
    int8_t *ctrl;
    struct swtab_node **slots;
    size_t mask;
    size_t n;
    size_t n_occupied;
};

#define SWTAB_INITIALIZER { NULL, NULL, 0, 0, 0 }

/* Iterator state.
 *
 * The struct is exposed so iterators can be stack allocated.  Callers should
 * treat the fields as implementation details. */
struct swtab_iter {
    size_t hash;
    size_t group;
    size_t match_group;
    size_t probe;
    uint64_t occupied;
};

#define SWTAB_GROUP_SIZE 8
#define SWTAB_EMPTY ((int8_t) 0x80)
#define SWTAB_DELETED ((int8_t) 0xfe)
#define SWTAB_BROADCAST_BYTE UINT64_C(0x0101010101010101)
#define SWTAB_HIGH_BITS UINT64_C(0x8080808080808080)

static inline uint64_t swtab_bswap64(uint64_t);
static inline unsigned int swtab_ctz64(uint64_t);
static inline size_t swtab_slot_count(const struct swtab *);
static inline size_t swtab_max_live(size_t);
static inline size_t swtab_h1(size_t);
static inline int8_t swtab_h2(size_t);
static inline size_t swtab_group_for_hash(const struct swtab *, size_t);
static inline size_t swtab_probe_next(size_t, size_t, size_t);
static inline uint64_t swtab_load_ctrl(const int8_t *);
static inline uint64_t swtab_match_byte(uint64_t, int8_t);
static inline uint64_t swtab_match_available(uint64_t);
static inline uint64_t swtab_match_occupied(uint64_t);
static inline bool swtab_group_has_empty(uint64_t);
static inline unsigned int swtab_match_first(uint64_t);
static inline size_t swtab_find_slot(const struct swtab *,
                                     const struct swtab_node *);
static inline void swtab_insert_into_available(struct swtab *,
                                               struct swtab_node *, size_t);
static inline struct swtab_node *swtab_first_with_hash_after(
    const struct swtab *, size_t, size_t);

/* Initialization. */
void swtab_init(struct swtab *);
void swtab_destroy(struct swtab *);
void swtab_clear(struct swtab *);
void swtab_swap(struct swtab *, struct swtab *);
static inline size_t swtab_count(const struct swtab *);
static inline bool swtab_is_empty(const struct swtab *);

/* Capacity. */
static inline size_t swtab_capacity(const struct swtab *);
void swtab_reserve_at(struct swtab *, size_t capacity, const char *where);
#define swtab_reserve(SWTAB, CAPACITY) \
    swtab_reserve_at(SWTAB, CAPACITY, OVS_SOURCE_LOCATOR)

/* Insertion and deletion. */
static inline void swtab_insert_at(struct swtab *, struct swtab_node *,
                                   size_t hash, const char *where);
#define swtab_insert(SWTAB, NODE, HASH) \
    swtab_insert_at(SWTAB, NODE, HASH, OVS_SOURCE_LOCATOR)
void swtab_insert_slow_at__(struct swtab *, struct swtab_node *, size_t hash,
                            const char *where);

static inline void swtab_insert_fast(struct swtab *, struct swtab_node *,
                                     size_t hash);
static inline void swtab_remove(struct swtab *, struct swtab_node *);
static inline bool swtab_contains(const struct swtab *,
                                  const struct swtab_node *);

/* Iterator API. */
static inline void swtab_iter_init(struct swtab_iter *, const struct swtab *);
static inline void swtab_iter_hash_init(struct swtab_iter *,
                                        const struct swtab *, size_t hash);
static inline struct swtab_node *swtab_iter_next(const struct swtab *,
                                                 struct swtab_iter *);
static inline struct swtab_node *swtab_iter_hash_next(const struct swtab *,
                                                      struct swtab_iter *);

/* Search.
 *
 * SWTAB_FOR_EACH_WITH_HASH iterates NODE over all of the nodes in SWTAB that
 * have hash value equal to HASH.  MEMBER must be the name of the
 * 'struct swtab_node' member within NODE.
 *
 * HASH is only evaluated once.
 *
 * When the loop terminates normally, meaning the iteration has completed
 * without using 'break', NODE will be NULL. */
#define SWTAB_FOR_EACH_WITH_HASH(NODE, MEMBER, HASH, SWTAB)                  \
    for (const struct swtab *NODE ##__swtab = (SWTAB);                       \
         NODE ##__swtab != NULL;                                             \
         NODE ##__swtab = NULL)                                              \
    for (size_t NODE ##__hash = (HASH), NODE ##__once = 1;                   \
         NODE ##__once;                                                      \
         NODE ##__once = 0)                                                  \
    for (struct swtab_iter NODE ##__iter, *NODE ##__iterp =                  \
             (swtab_iter_hash_init(&NODE ##__iter, NODE ##__swtab,           \
                                   NODE ##__hash),                           \
              &NODE ##__iter);                                               \
         NODE ##__iterp != NULL;                                             \
         NODE ##__iterp = NULL)                                              \
    for (INIT_MULTIVAR(NODE, MEMBER, swtab_iter_hash_next(NODE ##__swtab,    \
                                                          NODE ##__iterp),    \
                       struct swtab_node);                                   \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);           \
         UPDATE_MULTIVAR(NODE, swtab_iter_hash_next(NODE ##__swtab,          \
                                                    NODE ##__iterp)))

static inline struct swtab_node *swtab_first_with_hash(const struct swtab *,
                                                       size_t hash);
static inline struct swtab_node *swtab_next_with_hash(
    const struct swtab *, const struct swtab_node *);

/* Iteration. */
#define SWTAB_FOR_EACH(NODE, MEMBER, SWTAB)                                  \
    for (const struct swtab *NODE ##__swtab = (SWTAB);                       \
         NODE ##__swtab != NULL;                                             \
         NODE ##__swtab = NULL)                                              \
    for (struct swtab_iter NODE ##__iter, *NODE ##__iterp =                  \
             (swtab_iter_init(&NODE ##__iter, NODE ##__swtab),               \
              &NODE ##__iter);                                               \
         NODE ##__iterp != NULL;                                             \
         NODE ##__iterp = NULL)                                              \
    for (INIT_MULTIVAR(NODE, MEMBER, swtab_iter_next(NODE ##__swtab,         \
                                                     NODE ##__iterp),         \
                       struct swtab_node);                                   \
         CONDITION_MULTIVAR(NODE, MEMBER, ITER_VAR(NODE) != NULL);           \
         UPDATE_MULTIVAR(NODE, swtab_iter_next(NODE ##__swtab,               \
                                               NODE ##__iterp)))

#define SWTAB_FOR_EACH_SAFE_LONG(NODE, NEXT, MEMBER, SWTAB)                  \
    for (const struct swtab *NODE ##__swtab = (SWTAB);                       \
         NODE ##__swtab != NULL;                                             \
         NODE ##__swtab = NULL)                                              \
    for (struct swtab_iter NODE ##__iter, *NODE ##__iterp =                  \
             (swtab_iter_init(&NODE ##__iter, NODE ##__swtab),               \
              &NODE ##__iter);                                               \
         NODE ##__iterp != NULL;                                             \
         NODE ##__iterp = NULL)                                              \
    for (INIT_MULTIVAR_SAFE_LONG(NODE, NEXT, MEMBER,                         \
                                 swtab_iter_next(NODE ##__swtab,             \
                                                 NODE ##__iterp),             \
                                 struct swtab_node);                         \
         CONDITION_MULTIVAR_SAFE_LONG(NODE, NEXT, MEMBER,                    \
                                      ITER_VAR(NODE) != NULL,                \
                         ITER_VAR(NEXT) = swtab_iter_next(NODE ##__swtab,    \
                                                          NODE ##__iterp),    \
                                      ITER_VAR(NEXT) != NULL);               \
         UPDATE_MULTIVAR_SAFE_LONG(NODE, NEXT))

#define SWTAB_FOR_EACH_SAFE_SHORT(NODE, MEMBER, SWTAB)                       \
    for (const struct swtab *NODE ##__swtab = (SWTAB);                       \
         NODE ##__swtab != NULL;                                             \
         NODE ##__swtab = NULL)                                              \
    for (struct swtab_iter NODE ##__iter, *NODE ##__iterp =                  \
             (swtab_iter_init(&NODE ##__iter, NODE ##__swtab),               \
              &NODE ##__iter);                                               \
         NODE ##__iterp != NULL;                                             \
         NODE ##__iterp = NULL)                                              \
    for (INIT_MULTIVAR_SAFE_SHORT(NODE, MEMBER,                              \
                                  swtab_iter_next(NODE ##__swtab,            \
                                                  NODE ##__iterp),            \
                                  struct swtab_node);                        \
         CONDITION_MULTIVAR_SAFE_SHORT(NODE, MEMBER,                         \
                                       ITER_VAR(NODE) != NULL,               \
                    ITER_NEXT_VAR(NODE) = swtab_iter_next(NODE ##__swtab,    \
                                                          NODE ##__iterp));   \
         UPDATE_MULTIVAR_SAFE_SHORT(NODE))

#define SWTAB_FOR_EACH_SAFE(...)                                             \
    OVERLOAD_SAFE_MACRO(SWTAB_FOR_EACH_SAFE_LONG,                            \
                        SWTAB_FOR_EACH_SAFE_SHORT,                           \
                        4, __VA_ARGS__)

static inline struct swtab_node *swtab_first(const struct swtab *);
static inline struct swtab_node *swtab_next(const struct swtab *,
                                            const struct swtab_node *);

static inline uint64_t
swtab_bswap64(uint64_t x)
{
#if __GNUC__ >= 4 || defined(__clang__)
    return __builtin_bswap64(x);
#else
    return ((x & UINT64_C(0x00000000000000ff)) << 56
            | (x & UINT64_C(0x000000000000ff00)) << 40
            | (x & UINT64_C(0x0000000000ff0000)) << 24
            | (x & UINT64_C(0x00000000ff000000)) << 8
            | (x & UINT64_C(0x000000ff00000000)) >> 8
            | (x & UINT64_C(0x0000ff0000000000)) >> 24
            | (x & UINT64_C(0x00ff000000000000)) >> 40
            | (x & UINT64_C(0xff00000000000000)) >> 56);
#endif
}

static inline unsigned int
swtab_ctz64(uint64_t x)
{
#if __GNUC__ >= 4 || defined(__clang__)
    return __builtin_ctzll(x);
#else
    unsigned int n = 0;

    while (!(x & 1)) {
        n++;
        x >>= 1;
    }
    return n;
#endif
}

static inline size_t
swtab_slot_count(const struct swtab *swtab)
{
    return swtab->slots ? (swtab->mask + 1) * SWTAB_GROUP_SIZE : 0;
}

static inline size_t
swtab_max_live(size_t n_slots)
{
    return n_slots - n_slots / 8;
}

static inline size_t
swtab_h1(size_t hash)
{
    return hash >> 7;
}

static inline int8_t
swtab_h2(size_t hash)
{
    return hash & 0x7f;
}

static inline size_t
swtab_group_for_hash(const struct swtab *swtab, size_t hash)
{
    return swtab_h1(hash) & swtab->mask;
}

static inline size_t
swtab_probe_next(size_t group, size_t probe, size_t mask)
{
    return (group + probe + 1) & mask;
}

static inline uint64_t
swtab_load_ctrl(const int8_t *ctrl)
{
    uint64_t bits;

    memcpy(&bits, ctrl, sizeof bits);
#if defined(WORDS_BIGENDIAN) \
    || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    bits = swtab_bswap64(bits);
#endif
    return bits;
}

static inline uint64_t
swtab_match_byte(uint64_t ctrl, int8_t byte)
{
    uint64_t match = ctrl ^ (SWTAB_BROADCAST_BYTE * (uint8_t) byte);

    return (match - SWTAB_BROADCAST_BYTE) & ~match & SWTAB_HIGH_BITS;
}

static inline uint64_t
swtab_match_available(uint64_t ctrl)
{
    return ctrl & SWTAB_HIGH_BITS;
}

static inline uint64_t
swtab_match_occupied(uint64_t ctrl)
{
    return ~ctrl & SWTAB_HIGH_BITS;
}

static inline bool
swtab_group_has_empty(uint64_t ctrl)
{
    return swtab_match_byte(ctrl, SWTAB_EMPTY) != 0;
}

static inline unsigned int
swtab_match_first(uint64_t matches)
{
    return swtab_ctz64(matches) / CHAR_BIT;
}

static inline int8_t
swtab_ctrl_at(uint64_t ctrl, unsigned int slot)
{
    return ctrl >> (slot * CHAR_BIT);
}

static inline size_t
swtab_find_slot(const struct swtab *swtab, const struct swtab_node *node)
{
    size_t group;
    size_t probe;
    int8_t h2;

    if (!swtab->slots) {
        return SIZE_MAX;
    }

    if (node->slot < swtab_slot_count(swtab)
        && swtab->slots[node->slot] == node) {
        return node->slot;
    }

    group = swtab_group_for_hash(swtab, node->hash);
    probe = 0;
    h2 = swtab_h2(node->hash);

    for (;;) {
        size_t base = group * SWTAB_GROUP_SIZE;
        uint64_t ctrl = swtab_load_ctrl(&swtab->ctrl[base]);
        uint64_t matches = swtab_match_byte(ctrl, h2);

        while (matches) {
            unsigned int slot = swtab_match_first(matches);
            size_t i = base + slot;

            if (swtab->slots[i] == node) {
                return i;
            }
            matches &= matches - 1;
        }

        if (swtab_group_has_empty(ctrl)) {
            return SIZE_MAX;
        }

        group = swtab_probe_next(group, probe++, swtab->mask);
    }
}

static inline void
swtab_insert_into_available(struct swtab *swtab, struct swtab_node *node,
                            size_t hash)
{
    size_t group = swtab_group_for_hash(swtab, hash);
    size_t probe = 0;
    int8_t h2 = swtab_h2(hash);

    for (;;) {
        size_t base = group * SWTAB_GROUP_SIZE;
        uint64_t ctrl = swtab_load_ctrl(&swtab->ctrl[base]);
        uint64_t available = swtab_match_available(ctrl);

        if (available) {
            unsigned int slot = swtab_match_first(available);
            size_t i = base + slot;

            node->hash = hash;
            node->slot = i;
            swtab->ctrl[i] = h2;
            swtab->slots[i] = node;
            swtab->n++;
            if (swtab_ctrl_at(ctrl, slot) == SWTAB_EMPTY) {
                swtab->n_occupied++;
            }
            return;
        }

        group = swtab_probe_next(group, probe++, swtab->mask);
    }
}

static inline struct swtab_node *
swtab_first_with_hash_after(const struct swtab *swtab, size_t hash,
                            size_t after)
{
    size_t group;
    size_t probe;
    int8_t h2;
    bool past_after;

    if (!swtab->slots) {
        return NULL;
    }

    group = swtab_group_for_hash(swtab, hash);
    probe = 0;
    h2 = swtab_h2(hash);
    past_after = after == SIZE_MAX;

    for (;;) {
        size_t base = group * SWTAB_GROUP_SIZE;
        uint64_t ctrl = swtab_load_ctrl(&swtab->ctrl[base]);
        uint64_t matches = swtab_match_byte(ctrl, h2);

        while (matches) {
            unsigned int slot = swtab_match_first(matches);
            size_t i = base + slot;

            if (past_after && swtab->slots[i]->hash == hash) {
                return swtab->slots[i];
            }
            if (i == after) {
                past_after = true;
            }
            matches &= matches - 1;
        }

        if (swtab_group_has_empty(ctrl)) {
            return NULL;
        }

        group = swtab_probe_next(group, probe++, swtab->mask);
    }
}

static inline size_t
swtab_node_hash(const struct swtab_node *node)
{
    return node->hash;
}

static inline size_t
swtab_count(const struct swtab *swtab)
{
    return swtab->n;
}

static inline bool
swtab_is_empty(const struct swtab *swtab)
{
    return swtab->n == 0;
}

static inline size_t
swtab_capacity(const struct swtab *swtab)
{
    return swtab_max_live(swtab_slot_count(swtab));
}

static inline void
swtab_insert_fast(struct swtab *swtab, struct swtab_node *node, size_t hash)
{
    ovs_assert(swtab->n_occupied < swtab_capacity(swtab));
    swtab_insert_into_available(swtab, node, hash);
}

static inline void
swtab_insert_at(struct swtab *swtab, struct swtab_node *node, size_t hash,
                const char *where)
{
    size_t capacity = swtab_capacity(swtab);

    if (OVS_UNLIKELY(swtab->n_occupied + 1 > capacity)) {
        swtab_insert_slow_at__(swtab, node, hash, where);
    } else {
        swtab_insert_fast(swtab, node, hash);
    }
}

static inline void
swtab_remove(struct swtab *swtab, struct swtab_node *node)
{
    size_t i = node->slot;
    size_t base;
    uint64_t ctrl;

    ovs_assert(swtab->slots && i < swtab_slot_count(swtab));
    ovs_assert(swtab->slots[i] == node);

    base = i & ~(size_t) (SWTAB_GROUP_SIZE - 1);
    ctrl = swtab_load_ctrl(&swtab->ctrl[base]);
    if (swtab_group_has_empty(ctrl)) {
        swtab->ctrl[i] = SWTAB_EMPTY;
        swtab->n_occupied--;
    } else {
        swtab->ctrl[i] = SWTAB_DELETED;
    }
    swtab->slots[i] = NULL;
    swtab->n--;
}

static inline bool
swtab_contains(const struct swtab *swtab, const struct swtab_node *node)
{
    return swtab_find_slot(swtab, node) != SIZE_MAX;
}

static inline void
swtab_iter_init(struct swtab_iter *iter, const struct swtab *swtab OVS_UNUSED)
{
    iter->hash = 0;
    iter->group = 0;
    iter->match_group = 0;
    iter->probe = 0;
    iter->occupied = 0;
}

static inline void
swtab_iter_hash_init(struct swtab_iter *iter, const struct swtab *swtab,
                     size_t hash)
{
    iter->hash = hash;
    iter->group = swtab->slots ? swtab_group_for_hash(swtab, hash) : 0;
    iter->match_group = 0;
    iter->probe = 0;
    iter->occupied = 0;
}

static inline struct swtab_node *
swtab_iter_next(const struct swtab *swtab, struct swtab_iter *iter)
{
    if (!swtab->slots) {
        return NULL;
    }

    for (;;) {
        if (iter->occupied) {
            size_t group = iter->group - 1;
            unsigned int slot = swtab_match_first(iter->occupied);
            size_t i = group * SWTAB_GROUP_SIZE + slot;

            iter->occupied &= iter->occupied - 1;
            return swtab->slots[i];
        }

        if (iter->group > swtab->mask) {
            return NULL;
        }

        iter->occupied = swtab_match_occupied(
            swtab_load_ctrl(&swtab->ctrl[iter->group * SWTAB_GROUP_SIZE]));
        iter->group++;
    }
}

static inline struct swtab_node *
swtab_iter_hash_next(const struct swtab *swtab, struct swtab_iter *iter)
{
    int8_t h2;

    if (!swtab->slots) {
        return NULL;
    }

    h2 = swtab_h2(iter->hash);
    for (;;) {
        if (iter->occupied) {
            unsigned int slot = swtab_match_first(iter->occupied);
            size_t i = iter->match_group * SWTAB_GROUP_SIZE + slot;

            iter->occupied &= iter->occupied - 1;
            if (swtab->slots[i]->hash == iter->hash) {
                return swtab->slots[i];
            }
            continue;
        }

        if (iter->probe > swtab->mask) {
            return NULL;
        }

        size_t group = iter->group;
        uint64_t ctrl = swtab_load_ctrl(&swtab->ctrl[group * SWTAB_GROUP_SIZE]);

        iter->match_group = group;
        iter->occupied = swtab_match_byte(ctrl, h2);
        if (swtab_group_has_empty(ctrl)) {
            iter->probe = swtab->mask + 1;
        } else {
            iter->group = swtab_probe_next(group, iter->probe, swtab->mask);
            iter->probe++;
        }
    }
}

static inline struct swtab_node *
swtab_first_with_hash(const struct swtab *swtab, size_t hash)
{
    return swtab_first_with_hash_after(swtab, hash, SIZE_MAX);
}

static inline struct swtab_node *
swtab_next_with_hash(const struct swtab *swtab, const struct swtab_node *node)
{
    size_t i = swtab_find_slot(swtab, node);

    ovs_assert(i != SIZE_MAX);
    return swtab_first_with_hash_after(swtab, node->hash, i);
}

static inline struct swtab_node *
swtab_first(const struct swtab *swtab)
{
    size_t n_slots = swtab_slot_count(swtab);
    size_t i;

    for (i = 0; i < n_slots; i++) {
        if (swtab->ctrl[i] >= 0) {
            return swtab->slots[i];
        }
    }
    return NULL;
}

static inline struct swtab_node *
swtab_next(const struct swtab *swtab, const struct swtab_node *node)
{
    size_t n_slots = swtab_slot_count(swtab);
    size_t i = node->slot;

    ovs_assert(i < n_slots);
    for (++i; i < n_slots; i++) {
        if (swtab->ctrl[i] >= 0) {
            return swtab->slots[i];
        }
    }
    return NULL;
}

#ifdef  __cplusplus
}
#endif

#endif /* openvswitch/swtab.h */
