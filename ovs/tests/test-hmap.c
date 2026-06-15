/*
 * Copyright (c) 2008, 2009, 2010, 2013, 2014 Nicira, Inc.
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

/* A non-exhaustive test for some of the functions and macros declared in
 * hmap.h. */

#include <config.h>
#undef NDEBUG
#include "openvswitch/hmap.h"
#include <assert.h>
#include <string.h>
#include "hash.h"
#include "ovstest.h"
#include "random.h"
#include "util.h"

/* Sample hmap element. */
struct element {
    int value;
    struct hmap_node node;
};

typedef size_t hash_func(int value);

static size_t macro_hmap_calls;
static size_t macro_shard_calls;
static size_t macro_shard_count_calls;

static int
compare_ints(const void *a_, const void *b_)
{
    const int *a = a_;
    const int *b = b_;
    return *a < *b ? -1 : *a > *b;
}

static void
check_hmap_positions(struct hmap *hmap, const int values[], size_t n)
{
    struct hmap_position pos = { 0, 0 };
    int *sort_values, *position_values;
    struct hmap_node *node;
    size_t i;

    sort_values = xmalloc(sizeof *sort_values * n);
    position_values = xmalloc(sizeof *position_values * n);

    i = 0;
    while ((node = hmap_at_position(hmap, &pos)) != NULL) {
        struct element *e = CONTAINER_OF(node, struct element, node);

        assert(i < n);
        position_values[i++] = e->value;
    }
    assert(i == n);
    assert(pos.bucket == 0);
    assert(pos.offset == 0);

    memcpy(sort_values, values, sizeof *sort_values * n);
    qsort(sort_values, n, sizeof *sort_values, compare_ints);
    qsort(position_values, n, sizeof *position_values, compare_ints);

    for (i = 0; i < n; i++) {
        assert(sort_values[i] == position_values[i]);
    }

    free(position_values);
    free(sort_values);
}

static struct hmap *
macro_hmap_arg(struct hmap *hmap)
{
    macro_hmap_calls++;
    return hmap;
}

static size_t
macro_shard_arg(size_t shard)
{
    macro_shard_calls++;
    return shard;
}

static size_t
macro_shard_count_arg(size_t shard_count)
{
    macro_shard_count_calls++;
    return shard_count;
}

/* Verifies that 'hmap' contains exactly the 'n' values in 'values'. */
static void
check_hmap(struct hmap *hmap, const int values[], size_t n,
           hash_func *hash)
{
    int *sort_values, *hmap_values;
    struct element *e;
    size_t i;

    /* Check that all the values are there in iteration. */
    sort_values = xmalloc(sizeof *sort_values * n);
    hmap_values = xmalloc(sizeof *sort_values * n);

    i = 0;
    HMAP_FOR_EACH (e, node, hmap) {
        assert(i < n);
        hmap_values[i++] = e->value;
    }
    assert(i == n);
    assert(e == NULL);

    memcpy(sort_values, values, sizeof *sort_values * n);
    qsort(sort_values, n, sizeof *sort_values, compare_ints);
    qsort(hmap_values, n, sizeof *hmap_values, compare_ints);

    for (i = 0; i < n; i++) {
        assert(sort_values[i] == hmap_values[i]);
    }

    free(hmap_values);
    free(sort_values);
    check_hmap_positions(hmap, values, n);

    /* Check that all the values are there in lookup. */
    for (i = 0; i < n; i++) {
        size_t count = 0;

        HMAP_FOR_EACH_WITH_HASH (e, node, hash(values[i]), hmap) {
            count += e->value == values[i];
        }
        assert(count == 1);
        assert(e == NULL);
    }

    /* Check counters. */
    assert(hmap_is_empty(hmap) == !n);
    assert(hmap_count(hmap) == n);
}

/* Verifies that shard iteration visits exactly the 'n' values in 'values'. */
static void
check_hmap_shards(struct hmap *hmap, const int values[], size_t n,
                  size_t shard_count)
{
    int *sort_values, *hmap_values;
    struct element *e;
    size_t i, shard;

    sort_values = xmalloc(sizeof *sort_values * n);
    hmap_values = xmalloc(sizeof *hmap_values * n);

    i = 0;
    for (shard = 0; shard < shard_count; shard++) {
        HMAP_FOR_EACH_SHARD (e, node, hmap, shard, shard_count) {
            assert(i < n);
            hmap_values[i++] = e->value;
        }
        assert(e == NULL);
    }
    assert(i == n);

    memcpy(sort_values, values, sizeof *sort_values * n);
    qsort(sort_values, n, sizeof *sort_values, compare_ints);
    qsort(hmap_values, n, sizeof *hmap_values, compare_ints);

    for (i = 0; i < n; i++) {
        assert(sort_values[i] == hmap_values[i]);
    }

    free(hmap_values);
    free(sort_values);
}

/* Puts the 'n' values in 'values' into 'elements', and then puts those
 * elements into 'hmap'. */
static void
make_hmap(struct hmap *hmap, struct element elements[],
          int values[], size_t n, hash_func *hash)
{
    size_t i;

    hmap_init(hmap);
    for (i = 0; i < n; i++) {
        elements[i].value = i;
        hmap_insert(hmap, &elements[i].node, hash(elements[i].value));
        values[i] = i;
    }
}

static void
shuffle(int *p, size_t n)
{
    for (; n > 1; n--, p++) {
        int *q = &p[random_range(n)];
        int tmp = *p;
        *p = *q;
        *q = tmp;
    }
}

#if 0
/* Prints the values in 'hmap', plus 'name' as a title. */
static void
print_hmap(const char *name, struct hmap *hmap)
{
    struct element *e;

    printf("%s:", name);
    HMAP_FOR_EACH (e, node, hmap) {
        printf(" %d(%"PRIuSIZE")", e->value, hmap_node_hash(&e->node));
    }
    printf("\n");
}

/* Prints the 'n' values in 'values', plus 'name' as a title. */
static void
print_ints(const char *name, const int *values, size_t n)
{
    size_t i;

    printf("%s:", name);
    for (i = 0; i < n; i++) {
        printf(" %d", values[i]);
    }
    printf("\n");
}
#endif

static size_t
identity_hash(int value)
{
    return value;
}

static size_t
good_hash(int value)
{
    return hash_int(value, 0x1234abcd);
}

static size_t
constant_hash(int value OVS_UNUSED)
{
    return 123;
}

/* Tests basic hmap insertion and deletion. */
static void
test_hmap_insert_delete(hash_func *hash)
{
    enum { N_ELEMS = 100 };

    struct element elements[N_ELEMS];
    int values[N_ELEMS];
    struct hmap hmap;
    size_t i;

    hmap_init(&hmap);
    for (i = 0; i < N_ELEMS; i++) {
        elements[i].value = i;
        hmap_insert(&hmap, &elements[i].node, hash(i));
        values[i] = i;
        check_hmap(&hmap, values, i + 1, hash);
    }
    shuffle(values, N_ELEMS);
    for (i = 0; i < N_ELEMS; i++) {
        hmap_remove(&hmap, &elements[values[i]].node);
        check_hmap(&hmap, values + (i + 1), N_ELEMS - (i + 1), hash);
    }
    hmap_destroy(&hmap);
}

/* Tests basic hmap_reserve() and hmap_shrink(). */
static void
test_hmap_reserve_shrink(hash_func *hash)
{
    enum { N_ELEMS = 32 };

    size_t i;

    for (i = 0; i < N_ELEMS; i++) {
        struct element elements[N_ELEMS];
        int values[N_ELEMS];
        struct hmap hmap;
        size_t j;

        hmap_init(&hmap);
        hmap_reserve(&hmap, i);
        for (j = 0; j < N_ELEMS; j++) {
            elements[j].value = j;
            hmap_insert(&hmap, &elements[j].node, hash(j));
            values[j] = j;
            check_hmap(&hmap, values, j + 1, hash);
        }
        shuffle(values, N_ELEMS);
        for (j = 0; j < N_ELEMS; j++) {
            hmap_remove(&hmap, &elements[values[j]].node);
            hmap_shrink(&hmap);
            check_hmap(&hmap, values + (j + 1), N_ELEMS - (j + 1), hash);
        }
        hmap_destroy(&hmap);
    }
}

/* Tests that HMAP_FOR_EACH_SAFE properly allows for deletion of the current
 * element of a hmap.  */
static void
test_hmap_for_each_safe(hash_func *hash)
{
    enum { MAX_ELEMS = 10 };
    size_t n;
    unsigned long int pattern;

    for (n = 0; n <= MAX_ELEMS; n++) {
        for (pattern = 0; pattern < 1ul << n; pattern++) {
            struct element elements[MAX_ELEMS];
            int values[MAX_ELEMS];
            struct hmap hmap;
            struct element *e, *next;
            size_t n_remaining;
            int i;

            make_hmap(&hmap, elements, values, n, hash);

            i = 0;
            n_remaining = n;
            HMAP_FOR_EACH_SAFE (e, next, node, &hmap) {
                if (hmap_next(&hmap, &e->node) == NULL) {
                    assert(next == NULL);
                } else {
                    assert(&next->node == hmap_next(&hmap, &e->node));
                }
                assert(i < n);
                if (pattern & (1ul << e->value)) {
                    size_t j;
                    hmap_remove(&hmap, &e->node);
                    for (j = 0; ; j++) {
                        assert(j < n_remaining);
                        if (values[j] == e->value) {
                            values[j] = values[--n_remaining];
                            break;
                        }
                    }
                }
                check_hmap(&hmap, values, n_remaining, hash);
                i++;
            }
            assert(i == n);
            assert(next == NULL);
            assert(e == NULL);

            for (i = 0; i < n; i++) {
                if (pattern & (1ul << i)) {
                    n_remaining++;
                }
            }
            assert(n == n_remaining);
            hmap_destroy(&hmap);

            /* Test short version (without next variable). */
            make_hmap(&hmap, elements, values, n, hash);

            i = 0;
            n_remaining = n;
            HMAP_FOR_EACH_SAFE (e, node, &hmap) {
                assert(i < n);
                if (pattern & (1ul << e->value)) {
                    size_t j;
                    hmap_remove(&hmap, &e->node);
                    for (j = 0; ; j++) {
                        assert(j < n_remaining);
                        if (values[j] == e->value) {
                            values[j] = values[--n_remaining];
                            break;
                        }
                    }
                }
                check_hmap(&hmap, values, n_remaining, hash);
                i++;
            }
            assert(i == n);
            assert(e == NULL);

            for (i = 0; i < n; i++) {
                if (pattern & (1ul << i)) {
                    n_remaining++;
                }
            }
            assert(n == n_remaining);

            hmap_destroy(&hmap);
        }
    }
}

/* Tests that HMAP_FOR_EACH_POP removes every element of a hmap. */
static void
test_hmap_for_each_pop(hash_func *hash)
{
    enum { MAX_ELEMS = 10 };
    size_t n;

    for (n = 0; n <= MAX_ELEMS; n++) {
        struct element elements[MAX_ELEMS];
        int values[MAX_ELEMS];
        struct hmap hmap;
        struct element *e;
        size_t n_remaining, i;

        make_hmap(&hmap, elements, values, n, hash);

        i = 0;
        n_remaining = n;
        HMAP_FOR_EACH_POP (e, node, &hmap) {
            size_t j;

            assert(i < n);

            for (j = 0; ; j++) {
                assert(j < n_remaining);
                if (values[j] == e->value) {
                    values[j] = values[--n_remaining];
                    break;
                }
            }
            /* Trash the element memory (including the hmap node) */
            memset(e, 0, sizeof *e);
            check_hmap(&hmap, values, n_remaining, hash);
            i++;
        }
        assert(i == n);
        assert(e == NULL);

        hmap_destroy(&hmap);
    }
}

/* Tests that HMAP_FOR_EACH_SHARD partitions read-only iteration. */
static void
test_hmap_for_each_shard(hash_func *hash)
{
    enum { N_ELEMS = 96 };

    struct element elements[N_ELEMS];
    int values[N_ELEMS];
    struct hmap hmap;
    struct element *e;
    size_t init_count, iter_count;
    size_t n_remaining;
    size_t i, j;

    make_hmap(&hmap, elements, values, N_ELEMS, hash);
    check_hmap_shards(&hmap, values, N_ELEMS, 1);
    check_hmap_shards(&hmap, values, N_ELEMS, 2);
    check_hmap_shards(&hmap, values, N_ELEMS, 5);
    check_hmap_shards(&hmap, values, N_ELEMS, N_ELEMS * 2);

    n_remaining = N_ELEMS;
    for (i = 0; i < N_ELEMS; i += 7) {
        hmap_remove(&hmap, &elements[i].node);
        for (j = 0; ; j++) {
            assert(j < n_remaining);
            if (values[j] == elements[i].value) {
                values[j] = values[--n_remaining];
                break;
            }
        }
    }
    check_hmap_shards(&hmap, values, n_remaining, 3);
    check_hmap_shards(&hmap, values, n_remaining, 17);

    init_count = 0;
    iter_count = 0;
    HMAP_FOR_EACH_SHARD_INIT (e, node, &hmap, 0, 1, init_count++) {
        iter_count++;
    }
    assert(e == NULL);
    assert(init_count == 1);
    assert(iter_count == n_remaining);

    macro_hmap_calls = 0;
    macro_shard_calls = 0;
    macro_shard_count_calls = 0;
    iter_count = 0;
    HMAP_FOR_EACH_SHARD (e, node, macro_hmap_arg(&hmap),
                         macro_shard_arg(0), macro_shard_count_arg(1)) {
        iter_count++;
    }
    assert(e == NULL);
    assert(iter_count == n_remaining);
    assert(macro_hmap_calls == 1);
    assert(macro_shard_calls == 1);
    assert(macro_shard_count_calls == 1);

    hmap_destroy(&hmap);

    hmap_init(&hmap);
    hmap_reserve(&hmap, HMAP_SWISS_THRESHOLD + N_ELEMS);
    for (i = 0; i < N_ELEMS; i++) {
        elements[i].value = i;
        hmap_insert(&hmap, &elements[i].node, hash(elements[i].value));
        values[i] = i;
    }
    check_hmap_shards(&hmap, values, N_ELEMS, 4);
    check_hmap_shards(&hmap, values, N_ELEMS, 19);
    hmap_destroy(&hmap);
}

static bool
test_hmap_is_swiss(const struct hmap *hmap)
{
    return hmap->mode == HMAP_MODE_SWISS;
}

/* Tests that the implementation mode knobs select the expected layout. */
static void
test_hmap_modes(hash_func *hash)
{
    enum { N_ELEMS = 32 };

    struct element elements[N_ELEMS];
    int values[N_ELEMS];
    struct hmap hmap;
    size_t i;

    hmap_init(&hmap);
#if HMAP_IMPL == HMAP_IMPL_SWISS
    assert(test_hmap_is_swiss(&hmap));
#else
    assert(!test_hmap_is_swiss(&hmap));
#endif

    hmap_reserve(&hmap, HMAP_SWISS_THRESHOLD + N_ELEMS);
#if HMAP_IMPL == HMAP_IMPL_CHAINED
    assert(!test_hmap_is_swiss(&hmap));
#else
    assert(test_hmap_is_swiss(&hmap));
#endif

    for (i = 0; i < N_ELEMS; i++) {
        elements[i].value = i;
        hmap_insert(&hmap, &elements[i].node, hash(elements[i].value));
        values[i] = i;
    }
    check_hmap(&hmap, values, N_ELEMS, hash);
    hmap_destroy(&hmap);

    struct hmap initialized = HMAP_INITIALIZER(&initialized);
    struct hmap_position pos = { 0, 0 };

    assert(hmap_at_position(&initialized, &pos) == NULL);
    assert(pos.bucket == 0);
    assert(pos.offset == 0);
#if HMAP_IMPL == HMAP_IMPL_SWISS
    assert(test_hmap_is_swiss(&initialized));
#else
    assert(!test_hmap_is_swiss(&initialized));
#endif
    hmap_insert(&initialized, &elements[0].node, hash(0));
    values[0] = 0;
    check_hmap(&initialized, values, 1, hash);
    hmap_destroy(&initialized);

    elements[0].value = 0;
    elements[1].value = 1;
    elements[0].node.hash = hash(0);
    elements[0].node.next = &elements[1].node;
    elements[1].node.hash = hash(1);
    elements[1].node.next = NULL;
    struct hmap const_hmap = HMAP_CONST(&const_hmap, 2, &elements[0].node);
    values[0] = 0;
    values[1] = 1;
    check_hmap(&const_hmap, values, 2, hash);

#if HMAP_SWISS_THRESHOLD <= 4096
    size_t n_promote = HMAP_SWISS_THRESHOLD + N_ELEMS + 1;
    struct element *many_elements = xmalloc(sizeof *many_elements * n_promote);
    int *many_values = xmalloc(sizeof *many_values * n_promote);

    hmap_init(&hmap);
    for (i = 0; i < n_promote; i++) {
        many_elements[i].value = i;
        hmap_insert(&hmap, &many_elements[i].node,
                    hash(many_elements[i].value));
        many_values[i] = i;

#if HMAP_IMPL == HMAP_IMPL_CHAINED
        assert(!test_hmap_is_swiss(&hmap));
#elif HMAP_IMPL == HMAP_IMPL_SWISS
        assert(test_hmap_is_swiss(&hmap));
#else
        assert(test_hmap_is_swiss(&hmap) == (i + 1 > HMAP_SWISS_THRESHOLD));
#endif
    }
    check_hmap(&hmap, many_values, n_promote, hash);
    hmap_destroy(&hmap);
    free(many_values);
    free(many_elements);
#endif

#if HMAP_IMPL != HMAP_IMPL_CHAINED
    enum { N_TOMBSTONES = HMAP_SWISS_GROUP_WIDTH };
    struct element tombstone_elements[N_TOMBSTONES];

    hmap_init(&hmap);
    hmap_reserve(&hmap, HMAP_SWISS_THRESHOLD + N_TOMBSTONES);
    assert(test_hmap_is_swiss(&hmap));
    for (i = 0; i < N_TOMBSTONES; i++) {
        tombstone_elements[i].value = i;
        hmap_insert(&hmap, &tombstone_elements[i].node, i);
    }
    for (i = 0; i < N_TOMBSTONES; i++) {
        hmap_remove(&hmap, &tombstone_elements[i].node);
    }
    assert(hmap_count(&hmap) == 0);
    assert(hmap.n_occupied > 0);
    hmap_clear(&hmap);
    assert(hmap.n_occupied == 0);
    hmap_destroy(&hmap);
#endif
}

static void
run_test(void (*function)(hash_func *))
{
    hash_func *hash_funcs[] = { identity_hash, good_hash, constant_hash };
    size_t i;

    for (i = 0; i < ARRAY_SIZE(hash_funcs); i++) {
        function(hash_funcs[i]);
        printf(".");
        fflush(stdout);
    }
}

static void
test_hmap_main(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
    run_test(test_hmap_insert_delete);
    run_test(test_hmap_for_each_safe);
    run_test(test_hmap_reserve_shrink);
    run_test(test_hmap_for_each_pop);
    run_test(test_hmap_for_each_shard);
    run_test(test_hmap_modes);
    printf("\n");
}

OVSTEST_REGISTER("test-hmap", test_hmap_main);
