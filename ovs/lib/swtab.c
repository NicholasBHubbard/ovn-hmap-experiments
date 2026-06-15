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
#include "openvswitch/swtab.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

static void
swtab_init_capacity(struct swtab *swtab, size_t capacity)
{
    size_t groups = 1;
    size_t n_slots;

    while (swtab_max_live(groups * SWTAB_GROUP_SIZE) < capacity) {
        ovs_assert(groups <= SIZE_MAX / 2);
        groups *= 2;
    }

    n_slots = groups * SWTAB_GROUP_SIZE;
    swtab->ctrl = xmalloc(n_slots);
    memset(swtab->ctrl, SWTAB_EMPTY, n_slots);
    swtab->slots = xcalloc(n_slots, sizeof *swtab->slots);
    swtab->mask = groups - 1;
    swtab->n = 0;
    swtab->n_occupied = 0;
}

static void
swtab_resize(struct swtab *swtab, size_t capacity)
{
    struct swtab old = *swtab;
    size_t n_slots = swtab_slot_count(&old);
    size_t i;

    swtab_init_capacity(swtab, capacity);
    for (i = 0; i < n_slots; i++) {
        if (old.ctrl[i] >= 0) {
            swtab_insert_into_available(swtab, old.slots[i],
                                        old.slots[i]->hash);
        }
    }
    free(old.ctrl);
    free(old.slots);
}

/* Initializes 'swtab' as an empty Swiss hash table. */
void
swtab_init(struct swtab *swtab)
{
    swtab->ctrl = NULL;
    swtab->slots = NULL;
    swtab->mask = 0;
    swtab->n = 0;
    swtab->n_occupied = 0;
}

/* Frees memory reserved by 'swtab'.  The nodes themselves are not freed. */
void
swtab_destroy(struct swtab *swtab)
{
    if (swtab) {
        free(swtab->ctrl);
        free(swtab->slots);
    }
}

/* Removes all nodes from 'swtab', without freeing table memory. */
void
swtab_clear(struct swtab *swtab)
{
    size_t n_slots = swtab_slot_count(swtab);

    if (n_slots) {
        memset(swtab->ctrl, SWTAB_EMPTY, n_slots);
        memset(swtab->slots, 0, n_slots * sizeof *swtab->slots);
    }
    swtab->n = 0;
    swtab->n_occupied = 0;
}

void
swtab_swap(struct swtab *a, struct swtab *b)
{
    struct swtab tmp = *a;

    *a = *b;
    *b = tmp;
}

/* Reserves enough space for 'capacity' nodes. */
void
swtab_reserve_at(struct swtab *swtab, size_t capacity,
                 const char *where OVS_UNUSED)
{
    if (capacity > swtab_capacity(swtab)) {
        swtab_resize(swtab, capacity);
    }
}

/* Slow path for swtab_insert_at(), used when the table must grow or rehash. */
void
swtab_insert_slow_at__(struct swtab *swtab, struct swtab_node *node,
                       size_t hash, const char *where OVS_UNUSED)
{
    size_t capacity = swtab_capacity(swtab);

    if (swtab->n_occupied + 1 > capacity) {
        swtab_resize(swtab, swtab->n + 1 <= capacity
                     ? capacity
                     : swtab->n + 1);
    }
    swtab_insert_into_available(swtab, node, hash);
}
