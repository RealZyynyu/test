/* Host-only stand-in for the three Limine structs used by the PMM tests.
 * DO NOT copy this file into the freestanding kernel build or final ISO.
 * GitHub native-kernel job uses upstream Limine headers instead.
 */
#ifndef NOVA_TEST_LIMINE_STUB_H
#define NOVA_TEST_LIMINE_STUB_H
#include <stdint.h>
#define LIMINE_MEMMAP_USABLE 0u
#define LIMINE_MEMMAP_RESERVED 1u
struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
    uint64_t unused;
};
struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    struct limine_memmap_entry **entries;
};
struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};
#endif
