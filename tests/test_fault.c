/* Pure host tests for x86-64 fatal exception diagnostics (no faults fired).
 * Runtime exception entry itself needs subsequent QEMU tests before release.
 */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/fault.h"

static void check_vector_metadata(void){
    const unsigned cpu_errors[]={8,10,11,12,13,14,17,21,29,30};
    for(unsigned n=0;n<32;n++){
        bool expected=false;
        for(size_t i=0;i<sizeof(cpu_errors)/sizeof(cpu_errors[0]);i++)
            if(cpu_errors[i]==n)expected=true;
        assert(nova_fault_has_cpu_error(n)==expected);
    }
    assert(!nova_fault_has_cpu_error(255));
    assert(!strcmp(nova_fault_name(0),"DIVIDE_ERROR"));
    assert(!strcmp(nova_fault_name(6),"INVALID_OPCODE"));
    assert(!strcmp(nova_fault_name(8),"DOUBLE_FAULT"));
    assert(!strcmp(nova_fault_name(13),"GENERAL_PROTECTION"));
    assert(!strcmp(nova_fault_name(14),"PAGE_FAULT"));
    assert(!strcmp(nova_fault_name(21),"CONTROL_PROTECTION"));
    assert(!strcmp(nova_fault_name(255),"UNKNOWN_EXCEPTION"));
}
static void check_diagnostic_report(void){
    const struct nova_fault_frame page_fault={
        .vector=14,.error_code=0x7,.rip=UINT64_C(0xFFFFFFFF80001234),
        .cs=0x28,.rflags=0x202,.r15=0x1234};
    char report[320];
    memset(report,0x7a,sizeof(report));
    assert(nova_fault_format(&page_fault,0xDEADBEEF,report,sizeof(report)));
    assert(strstr(report,"NOVA_EXCEPTION_FATAL")!=NULL);
    assert(strstr(report,"PAGE_FAULT")!=NULL);
    assert(strstr(report,"vector=0x000000000000000E")!=NULL);
    assert(strstr(report,"error=0x0000000000000007")!=NULL);
    assert(strstr(report,"rip=0xFFFFFFFF80001234")!=NULL);
    assert(strstr(report,"cs=0x0000000000000028")!=NULL);
    assert(strstr(report,"rflags=0x0000000000000202")!=NULL);
    assert(strstr(report,"cr2=0x00000000DEADBEEF")!=NULL);

    struct nova_fault_frame invalid_opcode={.vector=6,.error_code=0,
                                         .rip=0x401000,.cs=8,.rflags=2};
    assert(nova_fault_format(&invalid_opcode,UINT64_MAX,report,sizeof(report)));
    assert(strstr(report,"INVALID_OPCODE")!=NULL);
    assert(strstr(report,"cr2=")==NULL); /* CR2 only meaningful for #PF */

    /* A bounded printer must not spill into adjacent stack storage. */
    for(size_t cap=0;cap<=12;cap++){
        char guarded[16];memset(guarded,0x6b,sizeof(guarded));
        assert(!nova_fault_format(&page_fault,0x4000,guarded,cap));
        for(size_t i=cap;i<sizeof(guarded);i++)assert(guarded[i]==0x6b);
        if(cap)assert(guarded[cap-1]==0);
    }
    assert(!nova_fault_format(NULL,0,report,sizeof(report)));
    assert(!nova_fault_format(&page_fault,0,NULL,4));
    assert(!nova_fault_format(&page_fault,0,NULL,0));
    assert(nova_fault_format(&page_fault,0,report,sizeof(report)));
    assert(report[strlen(report)]==0);
}
int main(void){
    check_vector_metadata();
    check_diagnostic_report();
    puts("CPU exception reports: vectors, error flags, fault registers and bounded formatting OK.");
    return 0;
}
