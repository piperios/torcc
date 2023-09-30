//  torcc: torcc_thread.cpp
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <torcc.h>
#include <torcc_internal.h>

namespace torc::internal {

// File scope
namespace {

std::mutex file_mtx{};
int work_created = 0;
typedef void (*sched_f)();

}  // namespace

void* torc_worker(void* arg) {
  u64 vp_id = reinterpret_cast<u64>(arg);
  auto* rte = new descriptor{};

  rte->vp_id = vp_id;
  rte->work = (sched_f) scheduler_loop;

#if DBG
  printf("[RTE %p]: NODE %d: WORKER THREAD %ld --> 0x%lx\n", rte, torc_node_id(), rte->vp_id,
         pthread_self());
  fflush(0);
#endif
  {
    std::lock_guard lock{file_mtx};
    ++work_created;
  }

  worker_thread[rte->vp_id] = std::thread{pthread_self()};

  set_vpid(vp_id);
  set_curr_thread(rte);

  int repeat;
  while (work_created < kthreads) {
    {
      std::lock_guard lock(file_mtx);
      repeat = (work_created < kthreads);
    }
    if (repeat)
      thread_sleep(10);
    else
      break;
  }

  if (vp_id == 0) {
    std::lock_guard lock(comm_lock);
    MPI_Barrier(comm_out);
  }

  if (node_id() == 0 && vp_id == 0) return 0;
  scheduler_loop(0);

  return 0;
}

void start_worker(i64 id) { worker_thread[id] = std::thread{worker, id}; }

void shutdown_worker(int id) {
  auto this_node = node_id();
  mpi::node_info_inst[this_node]->nworkers--;
}

std::mutex al{};
static u64 active_workers{0};


void md_init() {
  pthread_key_create(&vp_key, NULL);
  pthread_key_create(&currt_key, NULL);

  if (num_nodes() > 1) { start_server_thread(); }

  /*    node_info[this_node].nworkers = 1;*/
  // pthread_barrier_init(&bar, NULL, kthreads);
  for (auto i = 1; i < kthreads; i++) {
    /*        node_info[this_node].nworkers++;*/
    start_worker(static_cast<u64>(i));
  }
  active_workers = kthreads;
}


void md_end() {
  auto my_vp = get_vpid();

#if DBG
  printf("worker_thread %d exits\n", my_vp);
  fflush(0);
#endif
  if (my_vp != 0) {
    std::lock_guard lock{al};
    active_workers--;
    pthread_exit(0);
  }

  if (my_vp == 0) {
    while (1) {
      std::lock_guard lock{al};
      if (active_workers == 1) {
        break;
      } else {
        sched_yield();
      }
    }

    /* We need a barrier here to avoid potential deadlock problems */
    {
      std::lock_guard l{comm_lock};
      MPI_Barrier(comm_out);
    }

    if (num_nodes() > 1) { shutdown_server_thread(); }
    runtime_stats();

    MPI_Barrier(comm_out);
    MPI_Finalize();
    exit(0);
  }
}


void thread_sleep(int ms) {
  struct timespec req, rem;

  req.tv_sec = ms / 1000;
  req.tv_nsec = (ms % 1000) * 1E6;
  nanosleep(&req, &rem);
}

void _torc_set_vpid(long vp) { pthread_setspecific(vp_key, (void*) vp); }

i64 get_vpid() {
  pthread_getspecific(vp_key);
  return 1;
}

void set_curr_thread(descriptor* task) { pthread_setspecific(currt_key, (void*) task); }

descriptor* get_curr_thread() {
  auto task = (descriptor*) pthread_getspecific(currt_key);
  return task;
}

}  // namespace torc::internal
