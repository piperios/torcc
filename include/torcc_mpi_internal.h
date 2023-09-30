//  torcc: torcc_mpi_internal.h
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/1/14.
//  Copyright 2014 ETH Zurich. All rights reserved.
//
//  C++ port by Apostolos Piperis, 09/2023.
//
#pragma once

#include <mpi.h>
#include <torcc_base.h>
#include <util.h>

static MPI_Comm comm_out;
static std::mutex comm_lock{};

namespace torc::internal::mpi {

namespace comm {

void pre_init();
void init();

}  // namespace comm

enum class torc_mpi_types : i32 {
  T_MPI_CHAR = 0,
  T_MPI_SIGNED_CHAR,
  T_MPI_UNSIGNED_CHAR,
  T_MPI_BYTE,
  T_MPI_WCHAR,
  T_MPI_SHORT,
  T_MPI_UNSIGNED_SHORT,
  T_MPI_INT,
  T_MPI_UNSIGNED,
  T_MPI_LONG,
  T_MPI_UNSIGNED_LONG,
  T_MPI_FLOAT,
  T_MPI_DOUBLE,
  T_MPI_LONG_DOUBLE,
  T_MPI_LONG_LONG_INT,
  T_MPI_UNSIGNED_LONG_LONG,
  T_MPI_LONG_LONG = T_MPI_LONG_LONG_INT,
};

i32 global_thread_id_to_node_id(i32 global_thread_id);
i32 local_thread_id_to_global_thread_id(i32 local_thread_id);
i32 global_thread_id_to_local_thread_id(i32 global_thread_id);

i32 total_num_threads();

void* server_loop(void* arg);
void terminate_workers();

void send_descriptor(i32, descriptor*, i32);
void direct_send_descriptor(i32 dummy, i32 source_node, i32 source_vpid, descriptor* desc);
void receive_arguments(descriptor* work, i32 tag);
i32 receive_descriptor(i32 node, descriptor* work);
descriptor* direct_synchronous_stealing_request(i32 target_node);
void prefetch_request(i32 target_node);

func_t get_func_ptr(i32 func_pos);
i32 get_func_num(func_t f);

torc_mpi_types mpi2b_type(MPI_Datatype dtype);
MPI_Datatype b2mpi_type(i32 btype);

constexpr u64 MAX_NODES = 1024;

constexpr u64 TERMINATE_LOCAL_SERVER_THREAD = 120;
constexpr u64 TERMINATE_WORKER_THREADS = 121;
constexpr u64 DIRECT_SYNCHRONOUS_STEALING_REQUEST = 123;
constexpr u64 DISABLE_INTERNODE_STEALING = 124;
constexpr u64 ENABLE_INTERNODE_STEALING = 125;
constexpr u64 RESET_STATISTICS = 126;
constexpr u64 DISABLE_PREFETCHING = 127;
constexpr u64 ENABLE_PREFETCHING = 128;

constexpr u64 NORMAL = 139;
constexpr u64 ANSWER = 140;
constexpr u64 NORMAL_ENQUEUE = 141;
constexpr u64 NO_WORK = 142;
constexpr u64 BCAST = 145;

constexpr u64 PREFETCH_REQUEST = 146;

struct node_info {
  i32 nprocessors{};
  i32 nworkers{};
};
extern std::shared_ptr<node_info*> node_info_inst;

#define Error(msg)                                                                                 \
  {                                                                                                \
    printf("ERROR in %s: %s\n", __func__, msg);                                                    \
    MPI_Abort(comm_out, 1);                                                                        \
  }

#define Error1(msg, arg)                                                                           \
  {                                                                                                \
    printf("ERROR in %s:", __func__);                                                              \
    printf(msg, arg);                                                                              \
    printf("\n");                                                                                  \
    MPI_Abort(comm_out, 1);                                                                        \
  }

#define Warning1(msg, arg)                                                                         \
  {                                                                                                \
    printf("WARNING in %s:", __func__);                                                            \
    printf(msg, arg);                                                                              \
    printf("\n");                                                                                  \
  }

}  // namespace torc::internal::mpi
