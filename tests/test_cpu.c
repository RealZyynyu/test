#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/cpu.h"

int main(void) {
    uint32_t a, b, c, d;
    assert(nova_cpu_detect());
    assert(nova_cpu.vendor[0] != '\0');
    assert(nova_cpu.vendor[12] == '\0');
    assert(nova_cpu.long_mode);

    nova_cpuid(0, 0, &a, &b, &c, &d);
    assert(nova_cpu.max_basic_leaf == a);

    if (a >= 1) {
        nova_cpuid(1, 0, &a, &b, &c, &d);
        assert(nova_cpu.sse2 == ((d & (1u << 26)) != 0));
        assert(nova_cpu.apic == ((d & (1u << 9)) != 0));
        assert(nova_cpu.xsave == ((c & (1u << 26)) != 0));
        assert(nova_cpu.hypervisor == ((c & (1u << 31)) != 0));
        assert(nova_cpu.vmx == ((c & (1u << 5)) != 0));
    } else {
        assert(!nova_cpu.sse2 && !nova_cpu.apic && !nova_cpu.xsave);
        assert(!nova_cpu.hypervisor && !nova_cpu.vmx);
    }

    nova_cpuid(0x80000000u, 0, &a, &b, &c, &d);
    if (a >= 0x80000001u) {
        nova_cpuid(0x80000001u, 0, &a, &b, &c, &d);
        assert(nova_cpu.svm == ((c & (1u << 2)) != 0));
        assert(nova_cpu.long_mode == ((d & (1u << 29)) != 0));
    } else {
        assert(!nova_cpu.svm);
        assert(!nova_cpu.long_mode);
    }

    puts("NOVA CPU feature detection tests passed.");
    return 0;
}
