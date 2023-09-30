//  torcc: torcc_runtime.cpp
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mpi.h>
#include <mutex>
#include <torcc.h>
#include <torcc_internal.h>
#include <torcc_queue.h>

namespace torc::internal {

#define WAIT_COUNT 2
#define YIELD_TIME (0)

#define A0
#define A1 args[0]
#define A2 A1, args[1]
#define A3 A2, args[2]
#define A4 A3, args[3]
#define A5 A4, args[4]
#define A6 A5, args[5]
#define A7 A6, args[6]
#define A8 A7, args[7]
#define A9 A8, args[8]
#define A10 A9, args[9]
#define A11 A10, args[10]
#define A12 A11, args[11]
#define A13 A12, args[12]
#define A14 A13, args[13]
#define A15 A14, args[14]
#define A16 A15, args[15]
#define A17 A16, args[16]

#define DO_CASE(x)                                                                                 \
  case x: {                                                                                        \
    rte->work(A##x);                                                                               \
    break;                                                                                         \
  }

void core_execution(descriptor* rte) {
  intptr_t args[MAX_TORC_ARGS];

  if (rte->work_id >= 0) rte->work = mpi::get_func_ptr((i64) rte->work_id);

  if (node_id() == rte->homenode) {
    for (auto i = 0; i < rte->narg; i++) {
      if (rte->callway[i] == CALL_BY_COP)
        args[i] = (intptr_t) &rte->localarg[i];
      else
        args[i] = rte->localarg[i];
    }
  } else {
    for (auto i = 0; i < rte->narg; i++) {
      if (rte->callway[i] == CALL_BY_COP)
        args[i] = (intptr_t) &rte->temparg[i];
      else
        args[i] = rte->temparg[i];
    }
  }

  switch (rte->narg) {
    DO_CASE(0);
    DO_CASE(1);
    DO_CASE(2);
    DO_CASE(3);
    DO_CASE(4);
    DO_CASE(5);
    DO_CASE(6);
    DO_CASE(7);
    DO_CASE(8);
    DO_CASE(9);
    DO_CASE(10);
    DO_CASE(11);
    DO_CASE(12);
    DO_CASE(13);
    DO_CASE(14);
    DO_CASE(15);
    DO_CASE(16);
    DO_CASE(17);
    default: Error("rte function with more than 17 arguments..!"); break;
  }
}

void reset_statistics() {
  memset(created, 0, MAX_NVPS * sizeof(unsigned long));
  memset(executed, 0, MAX_NVPS * sizeof(unsigned long));
}

void print_statistics() {
  u64 total_created = 0, total_executed = 0;
  u64 prefetch_requests = torc_data_inst->_worker_record[0].prefetch_requests +
                          torc_data_inst->_worker_record[1].prefetch_requests;

  /* Runtime statistics */
  for (auto i = 0; i < kthreads; i++) {
    total_created += created[i];
    total_executed += executed[i];
  }

  auto i{0};
  printf(
      "[%2d] steals served/attempts/hits = %-3llu/%-3llu/%-3llu created = %3llu prefetches = %3llu "
      "executed = %3llu:(",
      node_id(), steal_served, steal_attempts, steal_hits, total_created, prefetch_requests,
      total_executed);
  for (i = 0; i < kthreads - 1; i++) { printf("%3llu,", executed[i]); }
  printf("%3llu)\n", executed[i]);
  fflush(0);
}

void torc_stats(void) { print_statistics(); }

descriptor* self() { return (descriptor*) get_curr_thread(); }

void dep_add(descriptor* rte, i32 ndeps) {
  std::lock_guard lock{rte->lock};
  rte->ndep += ndeps;
}

static void end(void) {
  int finalized = 0;

  MPI_Finalized(&finalized);
  if (finalized == 1) {
    torc_stats();
    _exit(0);
  }

  appl_finished = 1;
  if (num_nodes() > 1) mpi::terminate_workers();

  md_end();
}

void torc_finalize() { end(); }

void execute(void* arg) {
  descriptor* me = self();
  descriptor* rte = (descriptor*) arg;

  u64 vp = me->vp_id;
  f64 time = 0.0;

  torc_data_inst->_worker_record[me->level].tasks_executed[vp]++;
  time = MPI_Wtime();

  if (rte->rte_type == 1) executed[vp]++;

  rte->vp_id = me->vp_id;
  set_curr_thread(rte);
  core_execution(rte);

  last_task_duration = MPI_Wtime() - time;

  cleanup(rte);
  set_curr_thread(me);
}

i32 block() {
  descriptor* rte = self();

  {
    std::lock_guard lock{rte->lock};
    --rte->ndep;
    if (rte->ndep < 0) rte->ndep = 0;
  }

  while (true) {
    {
      std::lock_guard lock{rte->lock};
      u64 rem_deps = rte->ndep;
      if (rem_deps <= 0) { return 1; }
    }
    scheduler_loop(1);
  }

  return 0;
}

// (phadji)
// Q: What did I do here?
// A: Block until no more work exists at the cluster-layer. Useful for SPMD-like barriers
i32 block2() {
  descriptor* rte = self();

  {
    std::lock_guard lock{rte->lock};
    --rte->ndep;
    if (rte->ndep < 0) rte->ndep = 0;
  }

  while (true) {
    if (rte->ndep > 0) {
      {
        std::lock_guard lock{rte->lock};
        auto rem_deps = rte->ndep;
        if (rem_deps <= 0) {
          rte->ndep = 0;
          return 1;
        }
      }
    }

    auto work = scheduler_loop(1);
    if ((rte->ndep == 0) && (!work)) return 0;
  }

  return 0;
}

void set_work_routine(descriptor* rte, func_t work) {
  rte->work = work;
  rte->work_id = mpi::get_func_num(work);

  // if (rte->work_id == -1) printf("Internode function %p not registered\n", work);
}

i32 dep_satisfy(descriptor* rte) {
  std::lock_guard lock{rte->lock};
  auto deps = --rte->ndep;

  return !deps;
}

extern MPI_Comm comm_out;

void opt(int argc, char* argv[]) {
  auto largc = argc;
  char** largv = argv;
  std::string llargv = "";
  auto initialized = 0;
  int val;

  /* in case argv cannot be NULL (HPMPI) */
  if (argc == 0) largv = (char**) &llargv;

  MPI_Initialized(&initialized);

  i32 provided;
  if (initialized) {
    MPI_Query_thread(&provided);
  } else {
    int requested;
    requested = MPI_THREAD_MULTIPLE;
    MPI_Init_thread(&largc, &largv, requested, &provided);
  }

  if (provided != MPI_THREAD_MULTIPLE)
    thread_safe = 0;
  else
    thread_safe = 1;

  kthreads = TORC_DEF_CPUS;
  char* s = (char*) getenv("OMP_NUM_THREADS");
  if (s != 0 && sscanf(s, "%d", &val) == 1 && val > 0) kthreads = val;

  s = (char*) getenv("TORC_WORKERS");
  if (s != 0 && sscanf(s, "%d", &val) == 1 && val > 0) kthreads = val;

  yieldtime = TORC_DEF_YIELDTIME;
  s = (char*) getenv("TORC_YIELDTIME");
  if (s != 0 && sscanf(s, "%d", &val) == 1 && val > 0) yieldtime = val;

  throttling_factor = -1;
  s = (char*) getenv("TORC_THROTTLING_FACTOR");
  if (s != 0 && sscanf(s, "%d", &val) == 1 && val > 0) throttling_factor = val;

  MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &mpi_nodes);

  MPI_Barrier(MPI_COMM_WORLD);

  int namelen;
  char name[MPI_MAX_PROCESSOR_NAME];
  MPI_Get_processor_name(name, &namelen);
  printf("TORC_LITE ... rank %d of %d on host %s\n", mpi_rank, mpi_nodes, name);

  if (mpi_rank == 0)
    printf("The MPI implementation IS%s thread safe!\n", (thread_safe) ? "" : " NOT");

  MPI_Comm_dup(MPI_COMM_WORLD, &comm_out);
  MPI_Barrier(comm_out);

  mpi::comm::pre_init();
}

/* Package initialization */
void env_init(void) {
  md_init(); /* workers */
  mpi::comm::init();
}

descriptor* get_next_task() {
  auto self_node = node_id();
  auto nnodes = num_nodes();
  f64 steal_start = 0.0;

  descriptor* rte_next = nullptr;

  if (prefetching) {
    auto node = (self_node + 1) % nnodes;
    while (node != self_node) {
      mpi::prefetch_request(node);
      node = (node + 1) % nnodes;
    }
  }

  rte_next = queue::local_pq_dequeue();
  if (rte_next == NULL) {
    for (auto i = 9; i >= 0; i--) {
      if (rte_next == NULL)
        rte_next = queue::local_rq_dequeue(i);
      else
        break;
    }

    if (internode_stealing) {
      steal_start = MPI_Wtime();
      auto node = (self_node + 1) % nnodes;
      while ((rte_next == nullptr) && (node != self_node)) {
        rte_next = mpi::direct_synchronous_stealing_request(node);
        if (rte_next != nullptr) { break; }
        node = (node + 1) % nnodes;
      }
      if (rte_next == nullptr) internode_stealing = 0;
      last_steal_duration = MPI_Wtime() - steal_start;
    }
  }

  return rte_next;
}

i32 fetch_work() {
  descriptor* task = nullptr;

  task = get_next_task();
  if (task != nullptr) {
    queue::to_local_pq_end(task);
    return 1;
  } else {
    return 0;
  }
}

void cleanup(descriptor* rte) {
  if (rte->homenode != node_id()) {
    mpi::send_descriptor(rte->homenode, rte, mpi::ANSWER);
  } else {
    for (auto i = 0; i < rte->narg; i++) {
      if ((rte->callway[i] == CALL_BY_COP2) && (rte->quantity[i] > 1))
        if ((void*) rte->localarg[i] != nullptr) free((void*) rte->localarg[i]);
    }

    if (rte->parent) dep_satisfy(rte->parent);
  }
  queue::put_reused_desc(rte);
}

i32 scheduler_loop(i32 once) {
  descriptor* rte_next{nullptr};

  while (true) {
    rte_next = get_next_task();

    while (!rte_next) {
      // Checking for program completion
      if (appl_finished == 1) md_end();

      thread_sleep(yieldtime);

      rte_next = get_next_task();
      if (!rte_next) {
        if (once) return 0;
        thread_sleep(yieldtime);
      }
    }

    /* Execute selected task */
    execute(rte_next);
    if (once) return 1;
  }
}

}  // namespace torc::internal