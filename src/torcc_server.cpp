//  torcc: torcc_server.cpp
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#include "torcc_base.h"
#include "torcc_mpi_internal.h"

#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <torcc.h>
#include <torcc_internal.h>
#include <torcc_queue.h>

namespace torc::internal::mpi {

namespace {

internal::descriptor no_work_desc{};
constexpr u64 torc_desc_size = sizeof(internal::descriptor);
volatile i32 termination_flag = 0;

i32 process_a_received_descriptor(internal::descriptor* work) {
  i32 reuse = 0;
  internal::descriptor* stolen_work;
  MPI_Status status;
  i32 istat, tag;

  work->next = nullptr;

  // tag == MAX_NVPS occurs with asynchronous stealing
  tag = work->sourcevpid;
  if ((tag < 0) || (tag > MAX_NVPS)) {
    printf("...Invalid message tag %d from node %d [type=%d]\n", tag, work->sourcenode, work->type);
    fflush(0);
    MPI_Abort(comm_out, 1);
    return 1;
  }

  switch (work->type) {
    case ANSWER: {
      for (auto i = 0; i < work->narg; i++) {
        if (work->quantity[i] == 0) continue;

        if ((work->callway[i] == CALL_BY_RES) || (work->callway[i] == CALL_BY_REF)) {
          std::lock_guard lock{comm_lock};
          work->dtype[i] = b2mpi_type(work->btype[i]);
          istat = MPI_Recv((void*) work->localarg[i], work->quantity[i], work->dtype[i],
                           work->sourcenode, tag, comm_out, &status);
        }
      }

      for (auto i = 0; i < work->narg; i++) {
        if (work->quantity[i] == 0) continue;

        if (work->callway[i] == CALL_BY_COP2) {
          free((void*) work->localarg[i]);
          work->localarg[i] = 0;
        }
      }

      if (work->parent) dep_satisfy(work->parent);
      reuse = 1;

      return reuse;
    }
    case NORMAL_ENQUEUE: {
      if (work->homenode == node_id()) {
        queue::to_local_rq(work);
        reuse = 0;
        return reuse;
      } else { /* homenode != torc_node_id() */
        receive_arguments(work, tag);

        // Direct execution
        if (work->rte_type == 20) {
          core_execution(work);
          send_descriptor(work->homenode, work, ANSWER);
          reuse = 0;
          return reuse;
        }
        // 1: per node - 2 : per virtual processor
        if (work->insert_private) {
          if (work->insert_in_front)
            queue::to_local_pq(work);
          else
            queue::to_local_pq_end(work);
        } else { /* 0: public queues */
          if (work->insert_in_front)
            queue::to_local_rq(work);
          else
            queue::to_local_rq_end(work);
        }

        reuse = 0;
        return reuse;
      }
    }
    case DIRECT_SYNCHRONOUS_STEALING_REQUEST: {
      steal_attempts++;
      stolen_work = queue::local_rq_dequeue(0);

      for (auto i = 1; i < 10; i++) {
        if (!stolen_work)
          stolen_work = queue::local_rq_dequeue(i);
        else
          break;
      }

      if (stolen_work) {
        direct_send_descriptor(DIRECT_SYNCHRONOUS_STEALING_REQUEST, work->sourcenode,
                               work->sourcevpid, stolen_work);
        steal_served++;
      } else {
        direct_send_descriptor(DIRECT_SYNCHRONOUS_STEALING_REQUEST, work->sourcenode,
                               work->sourcevpid, &no_work_desc);
      }
      return reuse = 0;
      break;
    }
    case PREFETCH_REQUEST: {
      steal_attempts++;
      stolen_work = queue::local_rq_dequeue(0);

      auto i = 0;
      while (!stolen_work && (i++ < (i32) kthreads)) stolen_work = queue::local_rq_dequeue(i);

      if (stolen_work) {
        stolen_work->target_queue = work->sourcevpid;
        queue::to_npq_end(work->sourcenode, stolen_work);
      }

      reuse = 1;
      return reuse;
    }
    case TERMINATE_LOCAL_SERVER_THREAD: {
      pthread_exit(0);
      reuse = 1;
      return reuse;
    }
    case TERMINATE_WORKER_THREADS: {
      termination_flag = true;
      if (work->localarg[0] != node_id()) { appl_finished++; }

      reuse = 1;
      return reuse;
    }
    case ENABLE_INTERNODE_STEALING: {
      internode_stealing = 1;
      reuse = 1;
      break;
    }
    case DISABLE_INTERNODE_STEALING: {
      internode_stealing = 0;
      reuse = 1;
      break;
    }
    case ENABLE_PREFETCHING: {
      prefetching = 1;
      reuse = 1;
      break;
    }
    case DISABLE_PREFETCHING: {
      prefetching = 0;
      reuse = 1;
      break;
    }
    case RESET_STATISTICS: {
      reset_statistics();
      reuse = 1;
      break;
    }
    case BCAST: {
      void* va = (void*) work->localarg[1];
      i32 count = work->localarg[2];
      MPI_Datatype dtype = b2mpi_type(work->localarg[3]);

      std::lock_guard lock{comm_lock};
      istat = MPI_Recv((void*) va, count, dtype, work->sourcenode, tag, comm_out, &status);

      reuse = 1;
      return reuse;
    }
    default: Error1("Unkown descriptor type on node %d", node_id()); break;
  }

  reuse = 1;
  return reuse;
}

i32 prefetch_decision() {
  i32 executes = 0;

  for (auto i = 0; i < MAX_NVPS; ++i) executes += executed[i];

  bool was_slow = last_steal_duration > 0.1 * last_task_duration;
  bool steals_were_effective = (steal_attempts > 0) ? (static_cast<f32>(steal_served) / steal_attempts) > 0.8 : 1;

  return ((executes > 0) ? static_cast<f32>(steal_served) / executes : 0.0) > 0.4 ||
         (was_slow && steals_were_effective);
}

void prefetch_request(i32 target_node) {
  descriptor data;
  auto id = node_id();
  auto record = worker_record;

  if (termination_flag || !prefetch_decision()) return;

  memset(&data, 0, sizeof(data));
  data.localarg[0] = id;
  data.homenode = id;

  record.at(get_level()).prefetch_requests++;
  send_descriptor(target_node, &data, PREFETCH_REQUEST);
}

}  // private namespace

void* server_loop(void* arg) {
  descriptor* work;
  int reuse = 0;
  MPI_Status status;
  int istat;

  memset(&no_work_desc, 0, sizeof(descriptor));
  no_work_desc.type = NO_WORK; /* ... */

  while (true) {
    if (!reuse) work = queue::get_reused_desc();

    memset(work, 0, sizeof(descriptor)); /* lock ..?*/

    if (thread_safe) {
      std::lock_guard lock{comm_lock};
      istat = MPI_Recv(work, torc_desc_size, MPI_CHAR, MPI_ANY_SOURCE, MAX_NVPS, comm_out, &status);
    } else {
      int flag = 0;
      MPI_Request request;

      if (termination_flag >= 1) { pthread_exit(0); }

      {
        std::lock_guard lock{comm_lock};
        istat =
            MPI_Irecv(work, torc_desc_size, MPI_CHAR, MPI_ANY_SOURCE, MAX_NVPS, comm_out, &request);
      }

      while (true) {
        if (termination_flag >= 1) {
          printf("server threads exits!\n");
          fflush(0);
          pthread_exit(0);
        }

        {
          std::lock_guard lock{comm_lock};
          MPI_Test(&request, &flag, &status);
        }

        if (flag == 1) {
          break;
        } else {
          thread_sleep(yieldtime);
        }
      }
    }

    reuse = process_a_received_descriptor(work);
    if (reuse) {
      queue::put_reused_desc(work);
      reuse = 0;
    }
  }

  return 0;
}

void terminate_workers() {
  descriptor my_data;
  auto nnodes = num_nodes();
  auto curr_node = node_id();

  std::memset(&my_data, 0, sizeof(my_data));
  my_data.localarg[0] = curr_node;
  my_data.homenode = curr_node;

  for (auto node = 0; node < nnodes; ++node) {
    if (node != curr_node) send_descriptor(node, &my_data, TERMINATE_WORKER_THREADS);
  }
}
static std::mutex server_thread_lock{};
static bool server_thread_alive = false;

/*************************************************************************/
/**********************     INTERNODE STEALING      **********************/
/*************************************************************************/
std::mutex sl{};

descriptor* direct_synchronous_stealing_request(i32 target_node) {
  i32 vp = target_node;
  descriptor my_data, *work;

  if (termination_flag) { return nullptr; }

  {
    std::lock_guard lock{sl};
    work = queue::get_reused_desc();

    memset(&my_data, 0, sizeof(my_data));
    my_data.localarg[0] = node_id();
    my_data.homenode = node_id();

    send_descriptor(vp, &my_data, DIRECT_SYNCHRONOUS_STEALING_REQUEST);
    receive_descriptor(vp, work);
    work->next = nullptr;
  }

  if (work->type == NO_WORK) {
    usleep(100 * 1000);
    queue::put_reused_desc(work);
    return nullptr;
  } else {
    steal_hits++;
    return work;
  }
}

}  // namespace torc::internal::mpi

namespace torc {

void start_server_thread() {
  std::unique_lock lock{internal::mpi::server_thread_lock};
  if (internal::mpi::server_thread_alive) return;

  internal::server_thread = std::thread{internal::mpi::server_loop, nullptr};
  internal::mpi::server_thread_alive = true;
}

void shutdown_server_thread() {
  static internal::descriptor my_data;

  std::unique_lock lock{internal::mpi::server_thread_lock};
  if (!internal::mpi::server_thread_alive) return;

  std::memset(&my_data, 0, sizeof(my_data));
  if (internal::thread_safe)
    internal::mpi::send_descriptor(node_id(), &my_data, internal::mpi::TERMINATE_LOCAL_SERVER_THREAD);
  else
    internal::mpi::termination_flag = true;

  internal::server_thread.join();
  internal::mpi::server_thread_alive = false;
}

void torc_disable_stealing() {
  internal::descriptor my_data;
  i32 node, nnodes = num_nodes();

  internal::internode_stealing = 0;
  memset(&my_data, 0, sizeof(my_data));
  my_data.localarg[0] = node_id();
  my_data.homenode = my_data.sourcenode = node_id();

  for (node = 0; node < nnodes; node++) {
    if (node != node_id())
      internal::mpi::send_descriptor(node, &my_data, internal::mpi::DISABLE_INTERNODE_STEALING);
  }
}

void torc_enable_stealing() {
  internal::descriptor my_data;
  i32 node, nnodes = num_nodes();

  internal::internode_stealing = 1;
  memset(&my_data, 0, sizeof(my_data));
  my_data.localarg[0] = (long) node_id();
  my_data.homenode = my_data.sourcenode = node_id();

  for (node = 0; node < nnodes; node++) {
    if (node != node_id())
      internal::mpi::send_descriptor(node, &my_data, internal::mpi::ENABLE_INTERNODE_STEALING);
  }
}

void torc_disable_prefetching() {
  internal::descriptor my_data;
  i32 node, nnodes = num_nodes();

  internal::prefetching = 0;
  memset(&my_data, 0, sizeof(my_data));
  my_data.localarg[0] = node_id();
  my_data.homenode = my_data.sourcenode = node_id();

  for (node = 0; node < nnodes; node++) {
    if (node != node_id())
      internal::mpi::send_descriptor(node, &my_data, internal::mpi::DISABLE_PREFETCHING);
  }
}

void torc_enable_prefetching() {
  internal::descriptor my_data;
  i32 node, nnodes = num_nodes();

  internal::prefetching = 1;
  memset(&my_data, 0, sizeof(my_data));
  my_data.localarg[0] = (long) node_id();
  my_data.homenode = my_data.sourcenode = node_id();

  for (node = 0; node < nnodes; node++) {
    if (node != node_id())
      internal::mpi::send_descriptor(node, &my_data, internal::mpi::ENABLE_PREFETCHING);
  }
}

void local_enable_stealing() { internal::internode_stealing = 1; }

void local_disable_stealing() { internal::internode_stealing = 0; }

void reset_statistics() {
  internal::descriptor my_data;
  i32 node, nnodes = num_nodes();

  memset(&my_data, 0, sizeof(my_data));
  for (node = 0; node < nnodes; node++) {
    if (node != node_id())
      internal::mpi::send_descriptor(node, &my_data, internal::mpi::RESET_STATISTICS);
    else
      internal::reset_statistics();
  }
}

}  // namespace torc
