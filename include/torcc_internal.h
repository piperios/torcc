//  torcc: torcc_internal.h
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#pragma once

#include <torcc_config.h>
#include <torcc_mpi_internal.h>
#include <unistd.h>
#include <util.h>

namespace torc::internal {

/* Internal */
descriptor* self();
void runtime_stats();
void dep_add(descriptor*, i32);
i32 block();
i32 block2();
i32 dep_satisfy(descriptor*);
void md_init();
void md_end();
void reset_statistics();
void core_execution(descriptor*);
void set_work_routine(descriptor*, func_t);
void env_init();
void opt(i32, char**);
void* worker(void* arg);
void switch_worker(descriptor*, descriptor*, i32);
void execute(void*);
void cleanup(descriptor*);
i32 scheduler_loop(i32);
void set_vpid(i64);
i64 get_vpid();
void set_curr_thread(descriptor*);
descriptor* get_curr_thread();

#define TORC_DEF_CPUS 1        // Run sequentialy
#define TORC_DEF_YIELDTIME 10  // 10ms default yield-time

}  // namespace torc::internal