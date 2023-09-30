//  torcc: torcc.h
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#pragma once

#include <mpi.h>
#include <torcc_base.h>
#include <torcc_internal.h>
#include <util.h>


#define TORC_LITE 1

// Master-worker model:
// Only one of the MPI processes executes the main routine
// and the rest of them become workers.
#define MODE_MW 0
#define MODE_MS 0

/******  Exported Interface *******/
namespace torc {

void init(int argc, char** argv, int ms);
void reset_statistics();

typedef f64 time_t;
time_t get_time();

i32 worker_id_local();
i32 num_workers_local();

i32 worker_id();
i32 num_workers();
i32 get_level();

constexpr u32 CALL_BY_COP = 0x0001;   // IN    - By copy, through poi32er to private copy (C)
constexpr u32 CALL_BY_REF = 0x0002;   // INOUT - By reference
constexpr u32 CALL_BY_RES = 0x0003;   // OUT   - By result
constexpr u32 CALL_BY_PTR = 0x0004;   // IN    - By value, from address
constexpr u32 CALL_BY_VAL = 0x0001;   // IN    - By value, from address (4: C, 0: Fortran
constexpr u32 CALL_BY_COP2 = 0x0005;  // IN    - By copy, through poi32er to private copy (C)

void enable_stealing();
void disable_stealing();

void enable_prefetching();
void disable_prefetching();

void enable_stealing_local();
void disable_stealing_local();

void start_server_thread();
void shutdown_server_thread();

void task_init();
void wait_all();
void wait_all2();
void wait_all3();
void task_sync();

i32 scheduler_loop(i32);

void task(i32 queue, func_t f, i32 narg, ...);
void task_detached(i32 queue, func_t f, i32 narg, ...);
void task_ex(i32 queue, bool invisible, func_t f, i32 narg, ...);
void task_direct(i32 queue, func_t f, i32 narg, ...);

#define create task
#define create_detached task_detached
#define create_ex task_ex
#define create_direct task_direct

i32 node_id();
i32 num_nodes();

void broadcast(void* a, i64 count, MPI_Datatype dtype);
void broadcast_ox(void* a, i64 count, i32 dtype);
void thread_sleep(i32 ms);
void finalize();
void register_task(void* f);
i32 fetch_work();

}  // namespace torc
