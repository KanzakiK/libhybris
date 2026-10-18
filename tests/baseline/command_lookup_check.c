/* SPDX-License-Identifier: Apache-2.0 */
#define VK_NO_PROTOTYPES
#include "bc_context.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static struct hybris_bc_device owners[2];
static struct hybris_bc_command stable;
static void *churn(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 100000; ++i) {
        struct hybris_bc_command temporary = {.handle=(VkCommandBuffer)(uintptr_t)0xabcdef00};
        hybris_bc_command_publish(&owners[1], &temporary);
        struct hybris_bc_device *owner;
        assert(hybris_bc_command_find(temporary.handle, &owner) == &temporary);
        assert(owner == &owners[1]);
        hybris_bc_command_unpublish(&temporary);
    }
    return NULL;
}
int main(void)
{
    const unsigned count = 8192;
    struct hybris_bc_command *commands = calloc(count, sizeof(*commands));
    assert(commands);
    for (unsigned i = 0; i < count; ++i) {
        commands[i].handle = (VkCommandBuffer)(uintptr_t)((i + 1) * 256);
        hybris_bc_command_publish(&owners[i % 2], &commands[i]);
    }
    for (unsigned i = 0; i < count; ++i) {
        struct hybris_bc_device *owner;
        assert(hybris_bc_command_find(commands[i].handle, &owner) == &commands[i]);
        assert(owner == &owners[i % 2]);
    }
    for (unsigned i = 0; i < count; i += 2) hybris_bc_command_unpublish(&commands[i]);
    for (unsigned i = 0; i < count; ++i) {
        struct hybris_bc_device *owner = &owners[0];
        assert(hybris_bc_command_find(commands[i].handle, &owner) == (i % 2 ? &commands[i] : NULL));
        assert(owner == (i % 2 ? &owners[1] : NULL));
    }
    /* Reused driver handle must resolve to the newly published object/owner. */
    struct hybris_bc_command reused = {.handle=commands[0].handle};
    hybris_bc_command_publish(&owners[1], &reused);
    struct hybris_bc_device *owner;
    assert(hybris_bc_command_find(reused.handle, &owner) == &reused && owner == &owners[1]);
    hybris_bc_command_unpublish(&reused);
    for (unsigned i = 1; i < count; i += 2) hybris_bc_command_unpublish(&commands[i]);
    free(commands);
    stable.handle = (VkCommandBuffer)(uintptr_t)0xfedcba00;
    hybris_bc_command_publish(&owners[0], &stable);
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, churn, NULL));
    for (unsigned i = 0; i < 100000; ++i)
        assert(hybris_bc_command_find(stable.handle, &owner) == &stable && owner == &owners[0]);
    assert(!pthread_join(worker, NULL));
    hybris_bc_command_unpublish(&stable);
    assert(!hybris_bc_command_find(stable.handle, &owner) && !owner);
    puts("PASS command lookup: collisions, removal, handle reuse, owner isolation, concurrent unrelated lifetime");
}
