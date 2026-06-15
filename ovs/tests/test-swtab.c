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

#include <config.h>
#undef NDEBUG
#include "openvswitch/swtab.h"
#include <assert.h>
#include <string.h>
#include "hash.h"
#include "ovstest.h"
#include "util.h"

struct element {
    int value;
    struct swtab_node node;
};

typedef size_t hash_func(int value);

static int
compare_ints(const void *a_, const void *b_)
{
    const int *a = a_;
    const int *b = b_;

    return *a < *b ? -1 : *a > *b;
}

static void
shuffle(int *p, size_t n)
{
    unsigned int state = 0x12345678;

    for (; n > 1; n--, p++) {
        int *q;
        int tmp;

        state = state * 1103515245 + 12345;
        q = &p[state % n];
        tmp = *p;
        *p = *q;
        *q = tmp;
    }
}

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

static void
check_swtab(struct swtab *swtab, const int values[], size_t n,
            hash_func *hash)
{
    int *sort_values, *swtab_values;
    struct element *e;
    size_t i;

    sort_values = xmalloc(sizeof *sort_values * n);
    swtab_values = xmalloc(sizeof *swtab_values * n);

    i = 0;
    SWTAB_FOR_EACH (e, node, swtab) {
        assert(i < n);
        swtab_values[i++] = e->value;
    }
    assert(i == n);
    assert(e == NULL);

    memcpy(sort_values, values, sizeof *sort_values * n);
    qsort(sort_values, n, sizeof *sort_values, compare_ints);
    qsort(swtab_values, n, sizeof *swtab_values, compare_ints);

    for (i = 0; i < n; i++) {
        assert(sort_values[i] == swtab_values[i]);
    }

    free(swtab_values);
    free(sort_values);

    for (i = 0; i < n; i++) {
        size_t count = 0;

        SWTAB_FOR_EACH_WITH_HASH (e, node, hash(values[i]), swtab) {
            count += e->value == values[i];
        }
        assert(count == 1);
        assert(e == NULL);
    }

    assert(swtab_is_empty(swtab) == !n);
    assert(swtab_count(swtab) == n);
}

static void
make_swtab(struct swtab *swtab, struct element elements[],
           int values[], size_t n, hash_func *hash)
{
    size_t i;

    swtab_init(swtab);
    for (i = 0; i < n; i++) {
        elements[i].value = i;
        swtab_insert(swtab, &elements[i].node, hash(elements[i].value));
        values[i] = i;
    }
}

static void
test_swtab_initializer(void)
{
    struct swtab swtab = SWTAB_INITIALIZER;

    assert(swtab_is_empty(&swtab));
    assert(swtab_count(&swtab) == 0);
    swtab_destroy(&swtab);
}

static void
test_swtab_insert_delete(hash_func *hash)
{
    enum { N_ELEMS = 100 };

    struct element elements[N_ELEMS];
    int values[N_ELEMS];
    struct swtab swtab;
    size_t i;

    swtab_init(&swtab);
    for (i = 0; i < N_ELEMS; i++) {
        elements[i].value = i;
        swtab_insert(&swtab, &elements[i].node, hash(i));
        values[i] = i;
        check_swtab(&swtab, values, i + 1, hash);
    }

    shuffle(values, N_ELEMS);
    for (i = 0; i < N_ELEMS; i++) {
        swtab_remove(&swtab, &elements[values[i]].node);
        check_swtab(&swtab, values + i + 1, N_ELEMS - i - 1, hash);
    }
    swtab_destroy(&swtab);
}

static void
test_swtab_reserve(hash_func *hash)
{
    enum { N_ELEMS = 64 };

    size_t n_reserved;

    for (n_reserved = 0; n_reserved <= N_ELEMS; n_reserved++) {
        struct element elements[N_ELEMS];
        int values[N_ELEMS];
        struct swtab swtab;
        size_t i;

        swtab_init(&swtab);
        swtab_reserve(&swtab, n_reserved);

        for (i = 0; i < N_ELEMS; i++) {
            elements[i].value = i;
            swtab_insert(&swtab, &elements[i].node, hash(i));
            values[i] = i;
        }
        check_swtab(&swtab, values, N_ELEMS, hash);
        swtab_destroy(&swtab);
    }
}

static void
test_swtab_for_each_safe(hash_func *hash)
{
    enum { MAX_ELEMS = 10 };
    size_t n;
    unsigned long int pattern;

    for (n = 0; n <= MAX_ELEMS; n++) {
        for (pattern = 0; pattern < 1ul << n; pattern++) {
            struct element elements[MAX_ELEMS];
            int values[MAX_ELEMS];
            struct swtab swtab;
            struct element *e, *next;
            size_t n_remaining;
            int i;

            make_swtab(&swtab, elements, values, n, hash);

            i = 0;
            n_remaining = n;
            SWTAB_FOR_EACH_SAFE (e, next, node, &swtab) {
                assert(i < n);
                if (pattern & (1ul << e->value)) {
                    size_t j;

                    swtab_remove(&swtab, &e->node);
                    for (j = 0; ; j++) {
                        assert(j < n_remaining);
                        if (values[j] == e->value) {
                            values[j] = values[--n_remaining];
                            break;
                        }
                    }
                }
                check_swtab(&swtab, values, n_remaining, hash);
                i++;
            }
            assert(i == n);
            assert(e == NULL);
            assert(next == NULL);

            swtab_destroy(&swtab);

            make_swtab(&swtab, elements, values, n, hash);

            i = 0;
            n_remaining = n;
            SWTAB_FOR_EACH_SAFE (e, node, &swtab) {
                assert(i < n);
                if (pattern & (1ul << e->value)) {
                    size_t j;

                    swtab_remove(&swtab, &e->node);
                    for (j = 0; ; j++) {
                        assert(j < n_remaining);
                        if (values[j] == e->value) {
                            values[j] = values[--n_remaining];
                            break;
                        }
                    }
                }
                check_swtab(&swtab, values, n_remaining, hash);
                i++;
            }
            assert(i == n);
            assert(e == NULL);

            swtab_destroy(&swtab);
        }
    }
}

static void
test_swtab_helpers(void)
{
    struct element elements[4];
    struct swtab a;
    struct swtab b;
    size_t i;

    swtab_init(&a);
    swtab_reserve(&a, ARRAY_SIZE(elements));
    assert(swtab_capacity(&a) >= ARRAY_SIZE(elements));

    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        elements[i].value = i;
        swtab_insert_fast(&a, &elements[i].node, good_hash(i));
        assert(swtab_contains(&a, &elements[i].node));
    }
    assert(swtab_count(&a) == ARRAY_SIZE(elements));

    swtab_clear(&a);
    assert(swtab_is_empty(&a));
    for (i = 0; i < ARRAY_SIZE(elements); i++) {
        assert(!swtab_contains(&a, &elements[i].node));
    }

    swtab_init(&b);
    swtab_insert(&a, &elements[0].node, good_hash(0));
    swtab_insert(&b, &elements[1].node, good_hash(1));
    swtab_swap(&a, &b);
    assert(swtab_contains(&a, &elements[1].node));
    assert(!swtab_contains(&a, &elements[0].node));
    assert(swtab_contains(&b, &elements[0].node));
    assert(!swtab_contains(&b, &elements[1].node));

    swtab_destroy(&a);
    swtab_destroy(&b);
}

static void
test_swtab_same_h2(void)
{
    enum { N_ELEMS = 40 };

    struct element elements[N_ELEMS];
    struct swtab swtab;
    size_t i;

    swtab_init(&swtab);
    for (i = 0; i < N_ELEMS; i++) {
        elements[i].value = i;
        swtab_insert(&swtab, &elements[i].node, (i << 7) | 42);
    }

    for (i = 0; i < N_ELEMS; i++) {
        struct element *e;
        size_t count = 0;

        SWTAB_FOR_EACH_WITH_HASH (e, node, (i << 7) | 42, &swtab) {
            assert(e->value == i);
            count++;
        }
        assert(count == 1);
        assert(e == NULL);
    }
    assert(swtab_first_with_hash(&swtab, (N_ELEMS << 7) | 42) == NULL);

    swtab_destroy(&swtab);
}

static void
test_swtab_iterators(void)
{
    enum { N_ELEMS = 64 };

    struct element elements[N_ELEMS];
    bool seen[N_ELEMS];
    struct swtab_iter iter;
    struct swtab_node *node;
    struct swtab swtab;
    size_t i;

    swtab_init(&swtab);
    swtab_iter_init(&iter, &swtab);
    assert(swtab_iter_next(&swtab, &iter) == NULL);

    for (i = 0; i < N_ELEMS; i++) {
        elements[i].value = i;
        swtab_insert(&swtab, &elements[i].node, (i << 7) | 42);
    }

    memset(seen, 0, sizeof seen);
    swtab_iter_init(&iter, &swtab);
    while ((node = swtab_iter_next(&swtab, &iter)) != NULL) {
        struct element *e = CONTAINER_OF(node, struct element, node);

        assert(e->value >= 0 && e->value < N_ELEMS);
        assert(!seen[e->value]);
        seen[e->value] = true;
    }
    for (i = 0; i < N_ELEMS; i++) {
        assert(seen[i]);
    }

    for (i = 0; i < N_ELEMS; i++) {
        size_t count = 0;

        swtab_iter_hash_init(&iter, &swtab, (i << 7) | 42);
        while ((node = swtab_iter_hash_next(&swtab, &iter)) != NULL) {
            struct element *e = CONTAINER_OF(node, struct element, node);

            assert(e->value == (int) i);
            count++;
        }
        assert(count == 1);
    }

    swtab_iter_hash_init(&iter, &swtab, (N_ELEMS << 7) | 42);
    assert(swtab_iter_hash_next(&swtab, &iter) == NULL);

    swtab_destroy(&swtab);
}

static void
test_swtab_reinsert_after_deletes(void)
{
    enum { N_ELEMS = 96 };

    struct element elements[N_ELEMS];
    int values[N_ELEMS];
    struct swtab swtab;
    size_t n_values = 0;
    size_t i;

    swtab_init(&swtab);
    for (i = 0; i < N_ELEMS / 2; i++) {
        elements[i].value = i;
        swtab_insert(&swtab, &elements[i].node, good_hash(i));
        values[n_values++] = i;
    }
    for (i = 0; i < N_ELEMS / 2; i += 2) {
        size_t j;

        swtab_remove(&swtab, &elements[i].node);
        for (j = 0; j < n_values; j++) {
            if (values[j] == (int) i) {
                values[j] = values[--n_values];
                break;
            }
        }
    }
    for (i = N_ELEMS / 2; i < N_ELEMS; i++) {
        elements[i].value = i;
        swtab_insert(&swtab, &elements[i].node, good_hash(i));
        values[n_values++] = i;
    }

    check_swtab(&swtab, values, n_values, good_hash);
    swtab_destroy(&swtab);
}

static void
test_swtab_next_after_remove(void)
{
    enum { N_ELEMS = 64 };

    struct element elements[N_ELEMS];
    bool seen[N_ELEMS];
    struct swtab_node *node;
    struct swtab swtab;
    size_t i;

    memset(seen, 0, sizeof seen);
    swtab_init(&swtab);

    for (i = 0; i < N_ELEMS; i++) {
        elements[i].value = i;
        swtab_insert(&swtab, &elements[i].node, good_hash(i));
    }

    for (node = swtab_first(&swtab); node; node = swtab_next(&swtab, node)) {
        struct element *e = CONTAINER_OF(node, struct element, node);

        assert(e->value >= 0 && e->value < N_ELEMS);
        assert(!seen[e->value]);
        seen[e->value] = true;
        swtab_remove(&swtab, node);
    }

    for (i = 0; i < N_ELEMS; i++) {
        assert(seen[i]);
    }
    assert(swtab_is_empty(&swtab));

    swtab_destroy(&swtab);
}

static void
test_swtab_randomized(void)
{
    enum { N_ELEMS = 128, N_STEPS = 3000 };

    struct element elements[N_ELEMS];
    bool present[N_ELEMS];
    struct swtab swtab;
    unsigned int state = 0xfeed1234;
    size_t step;

    memset(present, 0, sizeof present);
    swtab_init(&swtab);

    for (step = 0; step < N_STEPS; step++) {
        int value;

        state = state * 1103515245 + 12345;
        value = state % N_ELEMS;

        if (present[value]) {
            struct element *e;
            size_t count = 0;

            SWTAB_FOR_EACH_WITH_HASH (e, node, good_hash(value), &swtab) {
                count += e->value == value;
            }
            assert(count == 1);

            if (state & 0x10000) {
                swtab_remove(&swtab, &elements[value].node);
                present[value] = false;
            }
        } else {
            assert(swtab_first_with_hash(&swtab, good_hash(value)) == NULL);

            if (state & 0x20000) {
                elements[value].value = value;
                swtab_insert(&swtab, &elements[value].node, good_hash(value));
                present[value] = true;
            }
        }

        if (step % 37 == 0) {
            int values[N_ELEMS];
            size_t n = 0;
            size_t i;

            for (i = 0; i < N_ELEMS; i++) {
                if (present[i]) {
                    values[n++] = i;
                }
            }
            check_swtab(&swtab, values, n, good_hash);
        }
    }

    swtab_destroy(&swtab);
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
test_swtab_main(int argc OVS_UNUSED, char *argv[] OVS_UNUSED)
{
    test_swtab_initializer();
    printf(".");
    run_test(test_swtab_insert_delete);
    run_test(test_swtab_reserve);
    run_test(test_swtab_for_each_safe);
    test_swtab_helpers();
    printf(".");
    test_swtab_same_h2();
    printf(".");
    test_swtab_iterators();
    printf(".");
    test_swtab_reinsert_after_deletes();
    printf(".");
    test_swtab_next_after_remove();
    printf(".");
    test_swtab_randomized();
    printf(".");
    printf("\n");
}

OVSTEST_REGISTER("test-swtab", test_swtab_main);
