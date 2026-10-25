/* NOVA development-only system-call broker tests.
 * These exercise a kernel-mode simulation, NEVER a ring-3 system call.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/broker.h"

static void set_path(struct nova_broker_request *req,const char *path){
    memset(req->path,0,sizeof(req->path));
    size_t n=strlen(path);
    assert(n<sizeof(req->path));
    memcpy(req->path,path,n);
}
static void expect(const struct nova_broker_context *ctx,uint32_t pid,
                   struct nova_broker_request *req,
                   struct nova_broker_response *out,enum nova_broker_status want){
    memset(out,0xCD,sizeof(*out));
    assert(nova_broker_dispatch(ctx,pid,req,out)==want);
    assert(out->status==want);
    if(want!=NOVA_BROKER_OK){
        assert(out->version==0&&out->length==0&&out->file_size==0);
        for(unsigned i=0;i<NOVA_BROKER_MAX_BYTES;i++)assert(out->bytes[i]==0);
    }
}
int main(void){
    assert(nova_ramfs_self_test());
    struct nova_process_table processes={0};
    /* Synthetic staged PID: no program is loaded or executed by this test. */
    processes.slots[0].staged=true;
    processes.slots[0].pid=42;
    struct nova_cap_registry rights={0};
    struct nova_vfs vfs={.disk=NULL};
    struct nova_vfs_entry first_entry;
    assert(nova_vfs_list(&vfs,"RAM:/",0,&first_entry));
    assert(first_entry.name[0]!=0);
    struct nova_fat16 nonexistent={0};
    assert(!nova_fat16_mount(&nonexistent,NULL,NULL,0));
    assert(nova_elf_result_name(NOVA_ELF_OK)[0]!=0);
    struct nova_broker_context ctx={
        .rights=&rights,.processes=&processes,.vfs=&vfs};
    struct nova_broker_request req={0};
    struct nova_broker_response out;

    assert(nova_cap_register(&rights,&processes,42));
    assert(nova_cap_current(&rights,&processes,42)==0);
    req.operation=NOVA_BROKER_VERSION;
    expect(&ctx,42,&req,&out,NOVA_BROKER_OK);
    assert(out.version==1&&out.length==0);
    expect(&ctx,0,&req,&out,NOVA_BROKER_DENIED);
    expect(&ctx,7,&req,&out,NOVA_BROKER_DENIED);
    assert(nova_broker_dispatch(&ctx,42,&req,NULL)==NOVA_BROKER_INVALID_REQUEST);
    expect(NULL,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    expect(&ctx,42,NULL,&out,NOVA_BROKER_INVALID_REQUEST);
    req.length=1;
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.length=0;req.offset=1;
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.offset=0;req.operation=0;
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.operation=999;
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.operation=NOVA_BROKER_READ;
    set_path(&req,"RAM:/NOTES.TXT");
    req.length=5;
    expect(&ctx,42,&req,&out,NOVA_BROKER_DENIED); /* default deny */
    assert(nova_cap_grant_trusted(&rights,&processes,42,NOVA_CAP_RAM_READ));
    expect(&ctx,42,&req,&out,NOVA_BROKER_OK);
    assert(out.length==5);
    assert(!memcmp(out.bytes,"HELLO",5));
    req.operation=NOVA_BROKER_STAT;req.length=0;
    expect(&ctx,42,&req,&out,NOVA_BROKER_OK);
    assert(out.file_size>0&&out.read_only);
    req.length=1;
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.length=0;req.offset=5;
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.offset=0;
    req.operation=NOVA_BROKER_READ;req.length=65;
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.length=1;req.offset=UINT64_MAX;
    expect(&ctx,42,&req,&out,NOVA_BROKER_IO_ERROR);
    req.offset=0;
    req.path[31]='X'; /* nonzero trailing bytes after NUL */
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.path[31]=0;
    memset(req.path,'A',sizeof(req.path)); /* no NUL */
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    set_path(&req,"RAM:/../NOTES.TXT");
    expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    set_path(&req,"DISK:/FILE.TXT");
    expect(&ctx,42,&req,&out,NOVA_BROKER_DENIED);
    assert(nova_cap_grant_trusted(&rights,&processes,42,NOVA_CAP_DISK_READ));
    expect(&ctx,42,&req,&out,NOVA_BROKER_IO_ERROR); /* no disk mounted */

    assert(nova_vfs_create(&vfs,"RAM:/GAME.TXT"));
    set_path(&req,"RAM:/GAME.TXT");
    req.operation=NOVA_BROKER_WRITE_RAM;
    req.length=4;memcpy(req.bytes,"SAVE",4);
    expect(&ctx,42,&req,&out,NOVA_BROKER_DENIED); /* no RAM write right */
    assert(nova_cap_grant_trusted(&rights,&processes,42,NOVA_CAP_RAM_WRITE));
    expect(&ctx,42,&req,&out,NOVA_BROKER_OK);
    assert(out.length==4);
    req.operation=NOVA_BROKER_READ;
    expect(&ctx,42,&req,&out,NOVA_BROKER_OK);
    assert(!memcmp(out.bytes,"SAVE",4));
    req.operation=NOVA_BROKER_WRITE_RAM;
    set_path(&req,"RAM:/NOTES.TXT");
    expect(&ctx,42,&req,&out,NOVA_BROKER_IO_ERROR); /* protected RAM file */
    set_path(&req,"DISK:/FILE.TXT");
    expect(&ctx,42,&req,&out,NOVA_BROKER_DENIED); /* no disk writes ever */
    set_path(&req,"RAM:/GAME.TXT");
    req.offset=1;expect(&ctx,42,&req,&out,NOVA_BROKER_INVALID_REQUEST);
    req.offset=0;
    assert(nova_cap_revoke_trusted(&rights,&processes,42,NOVA_CAP_RAM_WRITE));
    expect(&ctx,42,&req,&out,NOVA_BROKER_DENIED);
    req.operation=NOVA_BROKER_READ;
    assert(nova_cap_revoke_trusted(&rights,&processes,42,NOVA_CAP_RAM_READ));
    expect(&ctx,42,&req,&out,NOVA_BROKER_DENIED);
    assert(nova_cap_unregister(&rights,42));
    req.operation=NOVA_BROKER_VERSION;req.length=0;
    expect(&ctx,42,&req,&out,NOVA_BROKER_DENIED);
    assert(nova_vfs_remove(&vfs,"RAM:/GAME.TXT"));
    puts("Broker v1 tests: default-deny, bounded requests, copy-safe responses, read/write rights and revocation passed.");
    return 0;
}
