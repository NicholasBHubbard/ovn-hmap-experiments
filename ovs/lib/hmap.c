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
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "coverage.h"
#include "ovs-thread.h"
#include "random.h"
#include "util.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(hmap);

COVERAGE_DEFINE(hmap_pathological);
COVERAGE_DEFINE(hmap_expand);
COVERAGE_DEFINE(hmap_shrink);
COVERAGE_DEFINE(hmap_reserve);

#ifdef HMAP_STATS
struct hmap_stats {
    struct hmap_stats *next;
    unsigned long long id;
    const struct hmap *map;
    bool active;
    const char *init_where;
    const char *first_insert_where;
    const char *first_insert_fast_where;
    const char *first_remove_where;
    const char *first_search_hash_where;
    const char *first_search_bucket_where;
    const char *first_iter_where;
    const char *first_clear_where;
    const char *first_destroy_where;
    const char *first_resize_where;
    const char *first_reserve_where;
    uint64_t inserts;
    uint64_t insert_fasts;
    uint64_t removes;
    uint64_t replaces;
    uint64_t clears;
    uint64_t clear_nodes;
    uint64_t destroys;
    uint64_t swaps;
    uint64_t moved;
    uint64_t resizes;
    uint64_t reserves;
    uint64_t reserve_capacity;
    uint64_t search_hashes;
    uint64_t search_hash_hits;
    uint64_t search_hash_candidates;
    uint64_t search_buckets;
    uint64_t search_bucket_hits;
    uint64_t search_bucket_candidates;
    uint64_t iter_firsts;
    uint64_t iter_nonempty_firsts;
    uint64_t iter_nexts;
    uint64_t iter_next_hits;
    size_t peak_n;
    size_t peak_capacity;
    size_t final_n;
    size_t final_capacity;
};

static struct ovs_mutex hmap_stats_mutex = OVS_MUTEX_INITIALIZER;
static struct hmap_stats *hmap_stats_list;
static unsigned long long hmap_stats_next_id = 1;
static bool hmap_stats_checked_env;
static bool hmap_stats_enabled_;
static bool hmap_stats_atexit_registered;
static char *hmap_stats_dir;

static void hmap_stats_dump(void);

static bool
hmap_stats_enabled(void)
{
    if (!hmap_stats_checked_env) {
        const char *dir = getenv("OVS_HMAP_STATS_DIR");
        hmap_stats_enabled_ = dir && dir[0];
        if (hmap_stats_enabled_) {
            hmap_stats_dir = xstrdup(dir);
            if (!hmap_stats_atexit_registered) {
                atexit(hmap_stats_dump);
                hmap_stats_atexit_registered = true;
            }
        }
        hmap_stats_checked_env = true;
    }
    return hmap_stats_enabled_;
}

static void
hmap_stats_update_size(struct hmap_stats *stats, const struct hmap *hmap)
{
    size_t capacity = hmap->mask ? hmap->mask + 1 : 1;

    stats->final_n = hmap->n;
    stats->final_capacity = capacity;
    if (hmap->n > stats->peak_n) {
        stats->peak_n = hmap->n;
    }
    if (capacity > stats->peak_capacity) {
        stats->peak_capacity = capacity;
    }
}

static struct hmap_stats *
hmap_stats_attach_locked(struct hmap *hmap, const char *where)
{
    struct hmap_stats *stats = hmap->stats;

    if (!stats) {
        stats = xzalloc(sizeof *stats);
        stats->id = hmap_stats_next_id++;
        stats->map = hmap;
        stats->active = true;
        stats->init_where = where;
        hmap_stats_update_size(stats, hmap);
        stats->next = hmap_stats_list;
        hmap_stats_list = stats;
        hmap->stats = stats;
    }
    return stats;
}

static struct hmap_stats *
hmap_stats_get(struct hmap *hmap, const char *where)
{
    struct hmap_stats *stats;

    if (!hmap_stats_enabled()) {
        return NULL;
    }

    stats = hmap->stats;
    if (OVS_UNLIKELY(!stats)) {
        ovs_mutex_lock(&hmap_stats_mutex);
        stats = hmap_stats_attach_locked(hmap, where);
        ovs_mutex_unlock(&hmap_stats_mutex);
    }
    return stats;
}

static struct hmap_stats *
hmap_stats_get_const(const struct hmap *hmap)
{
    return hmap_stats_enabled() ? CONST_CAST(struct hmap *, hmap)->stats : NULL;
}

static void
hmap_stats_add_u64(uint64_t *value, uint64_t add)
{
    __atomic_fetch_add(value, add, __ATOMIC_RELAXED);
}

static void
hmap_stats_max_size(size_t *value, size_t candidate)
{
    size_t old = __atomic_load_n(value, __ATOMIC_RELAXED);

    while (candidate > old
           && !__atomic_compare_exchange_n(value, &old, candidate, false,
                                           __ATOMIC_RELAXED,
                                           __ATOMIC_RELAXED)) {
        continue;
    }
}

static size_t
hmap_stats_capacity(const struct hmap *hmap)
{
    return hmap->mask ? hmap->mask + 1 : 1;
}

static void
hmap_stats_note_size(struct hmap_stats *stats, const struct hmap *hmap)
{
    if (stats) {
        __atomic_store_n(&stats->final_n, hmap->n, __ATOMIC_RELAXED);
        __atomic_store_n(&stats->final_capacity, hmap_stats_capacity(hmap),
                         __ATOMIC_RELAXED);
        hmap_stats_max_size(&stats->peak_n, hmap->n);
        hmap_stats_max_size(&stats->peak_capacity, hmap_stats_capacity(hmap));
    }
}

void
hmap_stats_init(struct hmap *hmap, const char *where)
{
    hmap->stats = NULL;
    if (hmap_stats_enabled()) {
        ovs_mutex_lock(&hmap_stats_mutex);
        hmap_stats_attach_locked(hmap, where);
        ovs_mutex_unlock(&hmap_stats_mutex);
    }
}

void
hmap_stats_destroy(struct hmap *hmap, const char *where)
{
    struct hmap_stats *stats;

    if (!hmap) {
        return;
    }
    stats = hmap_stats_get(hmap, where);
    if (stats) {
        if (!stats->first_destroy_where) {
            stats->first_destroy_where = where;
        }
        hmap_stats_add_u64(&stats->destroys, 1);
        hmap_stats_note_size(stats, hmap);
        stats->active = false;
        hmap->stats = NULL;
    }
}

void
hmap_stats_clear(struct hmap *hmap, size_t n, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get(hmap, where);

    if (stats) {
        if (!stats->first_clear_where) {
            stats->first_clear_where = where;
        }
        hmap_stats_add_u64(&stats->clears, 1);
        hmap_stats_add_u64(&stats->clear_nodes, n);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_swap(struct hmap *a, struct hmap *b, const char *where)
{
    struct hmap_stats *a_stats = hmap_stats_get(a, where);
    struct hmap_stats *b_stats = hmap_stats_get(b, where);

    if (a_stats) {
        hmap_stats_add_u64(&a_stats->swaps, 1);
        hmap_stats_note_size(a_stats, a);
    }
    if (b_stats) {
        hmap_stats_add_u64(&b_stats->swaps, 1);
        hmap_stats_note_size(b_stats, b);
    }
}

void
hmap_stats_moved(struct hmap *hmap, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get(hmap, where);

    if (stats) {
        stats->map = hmap;
        hmap_stats_add_u64(&stats->moved, 1);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_resize(struct hmap *hmap, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get(hmap, where);

    if (stats) {
        if (!stats->first_resize_where) {
            stats->first_resize_where = where;
        }
        hmap_stats_add_u64(&stats->resizes, 1);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_reserve(struct hmap *hmap, size_t capacity, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get(hmap, where);

    if (stats) {
        if (!stats->first_reserve_where) {
            stats->first_reserve_where = where;
        }
        hmap_stats_add_u64(&stats->reserves, 1);
        hmap_stats_add_u64(&stats->reserve_capacity, capacity);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_insert(struct hmap *hmap, bool fast, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get(hmap, where);

    if (stats) {
        if (fast) {
            if (!stats->first_insert_fast_where) {
                stats->first_insert_fast_where = where;
            }
            hmap_stats_add_u64(&stats->insert_fasts, 1);
        } else {
            if (!stats->first_insert_where) {
                stats->first_insert_where = where;
            }
            hmap_stats_add_u64(&stats->inserts, 1);
        }
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_remove(struct hmap *hmap, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get(hmap, where);

    if (stats) {
        if (!stats->first_remove_where) {
            stats->first_remove_where = where;
        }
        hmap_stats_add_u64(&stats->removes, 1);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_replace(struct hmap *hmap, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get(hmap, where);

    if (stats) {
        hmap_stats_add_u64(&stats->replaces, 1);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_search_hash(const struct hmap *hmap, size_t hash, bool found,
                       const char *where)
{
    struct hmap_stats *stats = hmap_stats_get_const(hmap);
    uint64_t candidates = 0;

    if (stats) {
        const struct hmap_node *node;

        for (node = hmap->buckets[hash & hmap->mask]; node;
             node = node->next) {
            if (node->hash == hash) {
                candidates++;
            }
        }
        if (!stats->first_search_hash_where) {
            stats->first_search_hash_where = where;
        }
        hmap_stats_add_u64(&stats->search_hashes, 1);
        hmap_stats_add_u64(&stats->search_hash_hits, found ? 1 : 0);
        hmap_stats_add_u64(&stats->search_hash_candidates, candidates);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_search_bucket(const struct hmap *hmap, size_t hash, bool found,
                         const char *where)
{
    struct hmap_stats *stats = hmap_stats_get_const(hmap);
    uint64_t candidates = 0;

    if (stats) {
        const struct hmap_node *node;

        for (node = hmap->buckets[hash & hmap->mask]; node;
             node = node->next) {
            candidates++;
        }
        if (!stats->first_search_bucket_where) {
            stats->first_search_bucket_where = where;
        }
        hmap_stats_add_u64(&stats->search_buckets, 1);
        hmap_stats_add_u64(&stats->search_bucket_hits, found ? 1 : 0);
        hmap_stats_add_u64(&stats->search_bucket_candidates, candidates);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_iter_first(const struct hmap *hmap, bool found, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get_const(hmap);

    if (stats) {
        if (!stats->first_iter_where) {
            stats->first_iter_where = where;
        }
        hmap_stats_add_u64(&stats->iter_firsts, 1);
        hmap_stats_add_u64(&stats->iter_nonempty_firsts, found ? 1 : 0);
        hmap_stats_note_size(stats, hmap);
    }
}

void
hmap_stats_iter_next(const struct hmap *hmap, bool found, const char *where)
{
    struct hmap_stats *stats = hmap_stats_get_const(hmap);

    if (stats) {
        if (!stats->first_iter_where) {
            stats->first_iter_where = where;
        }
        hmap_stats_add_u64(&stats->iter_nexts, 1);
        hmap_stats_add_u64(&stats->iter_next_hits, found ? 1 : 0);
        hmap_stats_note_size(stats, hmap);
    }
}

static const char *
hmap_stats_str(const char *s)
{
    return s ? s : "";
}

static void
hmap_stats_dump(void)
{
    char *path;
    FILE *file;
    struct hmap_stats *stats;

    if (!hmap_stats_enabled_ || !hmap_stats_dir) {
        return;
    }

    path = xasprintf("%s/hmap-stats.%s.%ld.csv", hmap_stats_dir,
                     ovs_get_program_name() ? ovs_get_program_name() : "unknown",
                     (long int) getpid());
    file = fopen(path, "w");
    if (!file) {
        free(path);
        return;
    }

    fprintf(file, "pid,program,id,map,active,init_where,"
            "first_insert_where,first_insert_fast_where,first_remove_where,"
            "first_search_hash_where,first_search_bucket_where,"
            "first_iter_where,first_clear_where,first_destroy_where,"
            "first_resize_where,first_reserve_where,"
            "peak_n,peak_capacity,final_n,final_capacity,"
            "inserts,insert_fasts,removes,replaces,clears,clear_nodes,"
            "destroys,swaps,moved,resizes,reserves,reserve_capacity,"
            "search_hashes,search_hash_hits,search_hash_candidates,"
            "search_buckets,search_bucket_hits,search_bucket_candidates,"
            "iter_firsts,iter_nonempty_firsts,iter_nexts,iter_next_hits\n");

    ovs_mutex_lock(&hmap_stats_mutex);
    for (stats = hmap_stats_list; stats; stats = stats->next) {
        fprintf(file,
                "%ld,%s,%llu,%p,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,"
                "%"PRIuSIZE",%"PRIuSIZE",%"PRIuSIZE",%"PRIuSIZE","
                "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
                "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
                "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
                "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
                "%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64","
                "%"PRIu64",%"PRIu64"\n",
                (long int) getpid(),
                ovs_get_program_name() ? ovs_get_program_name() : "unknown",
                stats->id, stats->map, stats->active,
                hmap_stats_str(stats->init_where),
                hmap_stats_str(stats->first_insert_where),
                hmap_stats_str(stats->first_insert_fast_where),
                hmap_stats_str(stats->first_remove_where),
                hmap_stats_str(stats->first_search_hash_where),
                hmap_stats_str(stats->first_search_bucket_where),
                hmap_stats_str(stats->first_iter_where),
                hmap_stats_str(stats->first_clear_where),
                hmap_stats_str(stats->first_destroy_where),
                hmap_stats_str(stats->first_resize_where),
                hmap_stats_str(stats->first_reserve_where),
                stats->peak_n, stats->peak_capacity,
                stats->final_n, stats->final_capacity,
                stats->inserts, stats->insert_fasts, stats->removes,
                stats->replaces, stats->clears, stats->clear_nodes,
                stats->destroys, stats->swaps, stats->moved, stats->resizes,
                stats->reserves, stats->reserve_capacity,
                stats->search_hashes, stats->search_hash_hits,
                stats->search_hash_candidates, stats->search_buckets,
                stats->search_bucket_hits, stats->search_bucket_candidates,
                stats->iter_firsts, stats->iter_nonempty_firsts,
                stats->iter_nexts, stats->iter_next_hits);
    }
    ovs_mutex_unlock(&hmap_stats_mutex);

    fclose(file);
    free(path);
}
#endif

/* Initializes 'hmap' as an empty hash table. */
void
hmap_init(struct hmap *hmap)
{
    hmap->buckets = &hmap->one;
    hmap->one = NULL;
    hmap->mask = 0;
    hmap->n = 0;
    hmap_stats_init(hmap, OVS_SOURCE_LOCATOR);
}

/* Frees memory reserved by 'hmap'.  It is the client's responsibility to free
 * the nodes themselves, if necessary. */
void
hmap_destroy(struct hmap *hmap)
{
    hmap_stats_destroy(hmap, OVS_SOURCE_LOCATOR);
    if (hmap && hmap->buckets != &hmap->one) {
        free(hmap->buckets);
    }
}

/* Removes all node from 'hmap', leaving it ready to accept more nodes.  Does
 * not free memory allocated for 'hmap'.
 *
 * This function is appropriate when 'hmap' will soon have about as many
 * elements as it did before.  If 'hmap' will likely have fewer elements than
 * before, use hmap_destroy() followed by hmap_init() to save memory and
 * iteration time. */
void
hmap_clear(struct hmap *hmap)
{
    size_t n = hmap->n;

    if (hmap->n > 0) {
        hmap->n = 0;
        memset(hmap->buckets, 0, (hmap->mask + 1) * sizeof *hmap->buckets);
    }
    hmap_stats_clear(hmap, n, OVS_SOURCE_LOCATOR);
}

/* Exchanges hash maps 'a' and 'b'. */
void
hmap_swap(struct hmap *a, struct hmap *b)
{
#ifdef HMAP_STATS
    struct hmap_stats *a_stats = a->stats;
    struct hmap_stats *b_stats = b->stats;
#endif
    struct hmap tmp = *a;
    *a = *b;
    *b = tmp;
#ifdef HMAP_STATS
    a->stats = a_stats;
    b->stats = b_stats;
#endif
    hmap_moved(a);
    hmap_moved(b);
    hmap_stats_swap(a, b, OVS_SOURCE_LOCATOR);
}

/* Adjusts 'hmap' to compensate for having moved position in memory (e.g. due
 * to realloc()). */
void
hmap_moved(struct hmap *hmap)
{
    if (!hmap->mask) {
        hmap->buckets = &hmap->one;
    }
    hmap_stats_moved(hmap, OVS_SOURCE_LOCATOR);
}

static void
resize(struct hmap *hmap, size_t new_mask, const char *where)
{
    struct hmap tmp;
    size_t i;

    ovs_assert(is_pow2(new_mask + 1));

    hmap_init(&tmp);
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
        for (node = hmap->buckets[i]; node; node = next) {
            next = node->next;
            hmap_insert_fast_raw__(&tmp, node, node->hash);
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
    hmap_stats_resize(hmap, where);
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

/* Expands 'hmap', if necessary, to optimize the performance of searches.
 *
 * ('where' is used in debug logging.  Commonly one would use hmap_expand() to
 * automatically provide the caller's source file and line number for
 * 'where'.) */
void
hmap_expand_at(struct hmap *hmap, const char *where)
{
    size_t new_mask = calc_mask(hmap->n);
    if (new_mask > hmap->mask) {
        COVERAGE_INC(hmap_expand);
        resize(hmap, new_mask, where);
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
    size_t new_mask = calc_mask(hmap->n);
    if (new_mask < hmap->mask) {
        COVERAGE_INC(hmap_shrink);
        resize(hmap, new_mask, where);
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
    size_t new_mask = calc_mask(n);
    if (new_mask > hmap->mask) {
        COVERAGE_INC(hmap_reserve);
        resize(hmap, new_mask, where);
        hmap_stats_reserve(hmap, n, where);
    }
}

/* Adjusts 'hmap' to compensate for 'old_node' having moved position in memory
 * to 'node' (e.g. due to realloc()). */
void
hmap_node_moved(struct hmap *hmap,
                struct hmap_node *old_node, struct hmap_node *node)
{
    struct hmap_node **bucket = &hmap->buckets[node->hash & hmap->mask];
    while (*bucket != old_node) {
        bucket = &(*bucket)->next;
    }
    *bucket = node;
}

/* Chooses and returns a randomly selected node from 'hmap', which must not be
 * empty.
 *
 * I wouldn't depend on this algorithm to be fair, since I haven't analyzed it.
 * But it does at least ensure that any node in 'hmap' can be chosen. */
struct hmap_node *
hmap_random_node(const struct hmap *hmap)
{
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
    size_t offset;
    size_t b_idx;

    offset = pos->offset;
    for (b_idx = pos->bucket; b_idx <= hmap->mask; b_idx++) {
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
    struct hmap_node *p;

    for (p = hmap_first_in_bucket(hmap, node->hash); p; p = p->next) {
        if (p == node) {
            return true;
        }
    }

    return false;
}
