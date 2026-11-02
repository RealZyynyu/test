/* Small cooperative in-kernel job runner. Does NOT implement multitasking,
 * context switching, user processes, privilege separation or preemption.
 */
#ifndef NOVA_SCHED_H
#define NOVA_SCHED_H
#include <stdint.h>
#include <stdbool.h>
#define NOVA_MAX_JOBS 5u
typedef void (*nova_job_fn)(void);
struct nova_job {
    const char *name;
    nova_job_fn fn;
    uint64_t runs;
    bool enabled;
};
static struct nova_job nova_jobs[NOVA_MAX_JOBS];
static uint32_t nova_job_count=0,nova_job_next=0;

static bool nova_job_name_equal(const char *a,const char *b){
    if(!a||!b)return false;
    while(*a&&*b&&*a==*b){a++;b++;}
    return *a==0&&*b==0;
}
static bool nova_job_add(const char *name,nova_job_fn fn){
    if(nova_job_count>=NOVA_MAX_JOBS||!fn||!name||!name[0])return false;
    for(uint32_t i=0;i<nova_job_count;i++)
        if(nova_job_name_equal(nova_jobs[i].name,name))return false;
    struct nova_job *j=&nova_jobs[nova_job_count++];
    j->name=name;j->fn=fn;j->runs=0;j->enabled=true;
    return true;
}
/* Enable or pause a registered job by its unique name. A paused job keeps
 * its run counter and position; nova_job_step skips it without blocking peers.
 */
static bool nova_job_set_enabled(const char *name,bool enabled){
    if(!name||!name[0])return false;
    for(uint32_t i=0;i<nova_job_count;i++){
        if(nova_job_name_equal(nova_jobs[i].name,name)){
            nova_jobs[i].enabled=enabled;
            return true;
        }
    }
    return false;
}
static void nova_job_step(void){
    if(nova_job_count==0)return;
    for(uint32_t n=0;n<nova_job_count;n++){
        struct nova_job *j=&nova_jobs[nova_job_next];
        nova_job_next=(nova_job_next+1)%nova_job_count;
        if(!j->enabled)continue;
        j->fn();j->runs++;
        return;
    }
}
static uint64_t nova_job_total_runs(void){
    uint64_t sum=0;
    for(uint32_t i=0;i<nova_job_count;i++)sum+=nova_jobs[i].runs;
    return sum;
}
#endif
