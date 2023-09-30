//  torcc: torcc.cpp
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#include "torcc_base.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/time.h>
#include <torcc.h>
#include <torcc_internal.h>
#include <torcc_queue.h>

namespace torc {

namespace {

#define _initialize(rte)                                                                           \
  {                                                                                                \
    rte->homenode = rte->sourcenode = node_id();                                                   \
    rte->target_queue = rte->vp_id = -1;                                                           \
    rte->work_id = -1;                                                                             \
  }

static i32 invisible_flag = 0;
void set_invisible(i32 flag) { invisible_flag = flag; }

i32 local_num_workers() { return internal::kthreads; }

} // private namespace

void wait_all() { internal::block(); }

void wait_all2() { internal::block2(); }

void wait_all3() {
  internal::descriptor* self = internal::self();

  {
    std::lock_guard lock{self->lock};
    --self->ndep;
  }

  i32 remdeps;
  while (true) {
    {
      std::lock_guard lock{self->lock};
      remdeps = self->ndep;
    }

    if (remdeps == 0)
      break;
    else
      thread_sleep(0);
  }
}

i32 scheduler_loop(i32 once) { return internal::scheduler_loop(once); }

void task_detached(i32 queue, func_t work, i32 narg, ...) {
  va_list ap;
  int i;
  internal::descriptor* rte;

  rte = internal::queue::get_reused_desc();

  _initialize(rte);

  set_work_routine(rte, work);

  rte->narg = narg;
  rte->rte_type = 1; /* external */
  rte->inter_node = 1;
  rte->parent = nullptr;
  rte->level = 0;

  va_start(ap, narg);

  for (i = 0; i < narg; i++) {
    rte->quantity[i] = va_arg(ap, int);
    rte->dtype[i] = va_arg(ap, MPI_Datatype);
    rte->btype[i] = (i32) internal::mpi::mpi2b_type(rte->dtype[i]);
    rte->callway[i] = va_arg(ap, int);

    if ((rte->callway[i] == CALL_BY_COP) && (rte->quantity[i] > 1)) rte->callway[i] = CALL_BY_COP2;
  }

  for (i = 0; i < narg; i++) {
    if (rte->quantity[i] == 0) {  // peh: 02.07.2015
      intptr_t dummy = va_arg(ap, intptr_t);
      (void) dummy;
      continue;
    }
    if (rte->callway[i] == CALL_BY_COP) {
      int typesize;
      MPI_Type_size(rte->dtype[i], &typesize);
      switch (typesize) {
        case 4: rte->localarg[i] = *va_arg(ap, i32*); break;
        case 8: rte->localarg[i] = *va_arg(ap, i64*); break;
        default: Error("typesize not 4 or 8!"); break;
      }
    } else if (rte->callway[i] == CALL_BY_COP2) {
      intptr_t addr = va_arg(ap, intptr_t);
      int typesize;
      void* pmem;
      MPI_Type_size(rte->dtype[i], &typesize);
      pmem = malloc(rte->quantity[i] * typesize);
      memcpy(pmem, (void*) addr, rte->quantity[i] * typesize);
      rte->localarg[i] = (i64) pmem;  // yyyyyy
    } else {
      rte->localarg[i] = va_arg(ap, intptr_t); /* pointer (C: PTR, VAL) */
    }
  }

  if (queue == -1)
    internal::queue::to_rq_end(rte);
  else
    internal::queue::to_lrq_end(queue, rte);
}

void task(int queue, func_t work, int narg, ...) {
  va_list ap;
  internal::descriptor* rte;
  internal::descriptor* self = internal::self();

  /* Check if rte_init has been called */
  {
    std::lock_guard lock{self->lock};
    if (self->ndep == 0) self->ndep = 1;
  }

  internal::dep_add(self, 1);

  rte = internal::queue::get_reused_desc();

  _initialize(rte);

  set_work_routine(rte, work);

  rte->narg = narg;
  rte->rte_type = 1; /* external */
  rte->inter_node = 1;
  rte->parent = self;
  rte->level = self->level + 1;

  internal::torc_data_inst->_worker_record[self->level].tasks_created[self->vp_id]++;

  if (!invisible_flag) internal::created[self->vp_id]++;
  if (invisible_flag) rte->rte_type = 2; /* invisible */

  if (narg > MAX_TORC_ARGS) Error("narg > MAX_TORC_ARGS");

  va_start(ap, narg);

  for (auto i = 0; i < narg; i++) {
    rte->quantity[i] = va_arg(ap, int);
    rte->dtype[i] = va_arg(ap, MPI_Datatype);
    rte->btype[i] = (i32) internal::mpi::mpi2b_type(rte->dtype[i]);
    rte->callway[i] = va_arg(ap, int);

    if ((rte->callway[i] == CALL_BY_COP) && (rte->quantity[i] > 1)) rte->callway[i] = CALL_BY_COP2;
  }

  for (auto i = 0; i < narg; i++) {
    if (rte->quantity[i] == 0) {  // peh: 02.07.2015
      intptr_t dummy = va_arg(ap, intptr_t);
      (void) dummy;
      continue;
    }
    if (rte->callway[i] == CALL_BY_COP) {
      int typesize;
      MPI_Type_size(rte->dtype[i], &typesize);

      switch (typesize) {
        case 4: rte->localarg[i] = *va_arg(ap, i32*); break;
        case 8: rte->localarg[i] = *va_arg(ap, i64*); break;
        default: Error("typesize not 4 or 8!"); break;
      }
    } else if (rte->callway[i] == CALL_BY_COP2) {
      intptr_t addr = va_arg(ap, intptr_t);
      int typesize;
      void* pmem;

      MPI_Type_size(rte->dtype[i], &typesize);
      pmem = malloc(rte->quantity[i] * typesize);
      memcpy(pmem, (void*) addr, rte->quantity[i] * typesize);
      rte->localarg[i] = (i64) pmem;
    } else {
      rte->localarg[i] = va_arg(ap, intptr_t);
    }
  }

  if (queue == -1)
    internal::queue::to_rq_end(rte);
  else
    internal::queue::to_lrq_end(queue, rte);
}

void torc_task_ex(int queue, int invisible, func_t work, int narg, ...) {
  va_list ap;
  internal::descriptor* rte;
  internal::descriptor* self = internal::self();

  /* Check if rte_init has been called */
  {
    std::lock_guard lock{self->lock};
    if (self->ndep == 0) self->ndep = 1;
  }

  internal::dep_add(self, 1);

  rte = internal::queue::get_reused_desc();

  _initialize(rte);

  internal::set_work_routine(rte, work);

  rte->narg = narg;
  rte->rte_type = 1; /* external */
  rte->inter_node = 1;
  rte->parent = self;
  rte->level = self->level + 1;

  internal::torc_data_inst->_worker_record[self->level].tasks_created[self->vp_id]++;

  if (!invisible) internal::created[self->vp_id]++;
  if (invisible) rte->rte_type = 2;

  if (narg > MAX_TORC_ARGS) Error("narg > MAX_TORC_ARGS");

  va_start(ap, narg);

  for (auto i = 0; i < narg; i++) {
    rte->quantity[i] = va_arg(ap, int);
    rte->dtype[i] = va_arg(ap, MPI_Datatype);
    rte->btype[i] = (i32) internal::mpi::mpi2b_type(rte->dtype[i]);
    rte->callway[i] = va_arg(ap, int);

    if ((rte->callway[i] == CALL_BY_COP) && (rte->quantity[i] > 1)) rte->callway[i] = CALL_BY_COP2;
  }

  for (auto i = 0; i < narg; i++) {
    if (rte->quantity[i] == 0) {  // peh: 02.07.2015
      intptr_t dummy = va_arg(ap, intptr_t);
      (void) dummy;
      continue;
    }

    if (rte->callway[i] == CALL_BY_COP) {
      int typesize;
      MPI_Type_size(rte->dtype[i], &typesize);
      switch (typesize) {
        case 4: rte->localarg[i] = *va_arg(ap, i32*); break;
        case 8: rte->localarg[i] = *va_arg(ap, i64*); break;
        default: Error("typesize not 4 or 8!"); break;
      }
    } else if (rte->callway[i] == CALL_BY_COP2) {
      intptr_t addr = va_arg(ap, intptr_t);
      int typesize;
      void* pmem;
      MPI_Type_size(rte->dtype[i], &typesize);
      pmem = malloc(rte->quantity[i] * typesize);
      memcpy(pmem, (void*) addr, rte->quantity[i] * typesize);
      rte->localarg[i] = (i64) pmem;
    } else {
      rte->localarg[i] = va_arg(ap, intptr_t);
    }
  }

  if (queue == -1)
    internal::queue::to_rq_end(rte);
  else
    internal::queue::to_lrq_end(queue, rte);
}

void task_direct(i32 queue, func_t work, i32 narg, ...) {
  va_list ap;
  internal::descriptor* rte;
  internal::descriptor* self = internal::self();

  /* Check if rte_init has been called */
  {
    std::lock_guard lock{self->lock};
    if (self->ndep == 0) self->ndep = 1;
  }

  internal::dep_add(self, 1);

  rte = internal::queue::get_reused_desc();

  _initialize(rte);

  internal::set_work_routine(rte, work);

  rte->narg = narg;
  rte->rte_type = 20; /* external - direct execution */
  rte->inter_node = 1;
  rte->parent = self;
  rte->level = self->level + 1;

  if (narg > MAX_TORC_ARGS) Error("narg > MAX_TORC_ARGS");

  va_start(ap, narg);

  for (auto i = 0; i < narg; i++) {
    rte->quantity[i] = va_arg(ap, int);
    rte->dtype[i] = va_arg(ap, MPI_Datatype);
    rte->btype[i] = (i32) internal::mpi::mpi2b_type(rte->dtype[i]);
    rte->callway[i] = va_arg(ap, int);

    if ((rte->callway[i] == CALL_BY_COP) && (rte->quantity[i] > 1)) rte->callway[i] = CALL_BY_COP2;
  }

  for (auto i = 0; i < narg; i++) {
    if (rte->quantity[i] == 0) {  // peh: 02.07.2015
      intptr_t dummy = va_arg(ap, intptr_t);
      (void) dummy;
      continue;
    }
    if (rte->callway[i] == CALL_BY_COP) {
      int typesize;
      MPI_Type_size(rte->dtype[i], &typesize);
      switch (typesize) {
        case 4: rte->localarg[i] = *va_arg(ap, i32*); break;
        case 8: rte->localarg[i] = *va_arg(ap, i64*); break;
        default: Error("typesize not 4 or 8!"); break;
      }
    } else if (rte->callway[i] == CALL_BY_COP2) {
      intptr_t addr = va_arg(ap, intptr_t);
      int typesize;
      void* pmem;
      MPI_Type_size(rte->dtype[i], &typesize);
      pmem = malloc(rte->quantity[i] * typesize);
      memcpy(pmem, (void*) addr, rte->quantity[i] * typesize);
      rte->localarg[i] = (i64) pmem;
    } else {
      rte->localarg[i] = va_arg(ap, intptr_t);
    }
  }

  if (queue == -1)
    internal::queue::to_rq_end(rte);
  else
    internal::queue::to_lrq_end(queue, rte);
}

time_t get_time() {
  struct timeval t;
  gettimeofday(&t, NULL);
  return (double) t.tv_sec + (double) t.tv_usec * 1.0E-6;
}

i32 get_level() { return internal::self()->level; }

i32 node_id() { return internal::mpi_rank; }
i32 num_nodes() { return internal::mpi_nodes; }

i32 num_workers() {
  if (num_nodes() > 1)
    return internal::mpi::total_num_threads();
  else
    return local_num_workers();
}

i32 worker_id() {
  if (num_nodes() > 1)
    return internal::mpi::local_thread_id_to_global_thread_id(internal::get_vpid());
  else
    return internal::get_vpid();
}

std::shared_ptr<internal::torc_data> internal::torc_data_inst;

void init(int argc, char* argv[], int ms) {
  static bool torc_initialized = 0;

  if (torc_initialized) return;
  torc_initialized = true;

  internal::torc_data_inst = std::make_shared<internal::torc_data>();
  internal::opt(argc, argv);
  internal::env_init();
  internal::worker(0);
}

}  // namespace torc