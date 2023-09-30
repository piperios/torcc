//  torcc: torcc_queue.cpp
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#include <cstring>
#include <torcc.h>
#include <torcc_base.h>
#include <torcc_internal.h>
#include <torcc_queue.h>

namespace torc::internal::queue {

namespace {

/* Reuse of descriptors */
void to_local_reuse_queue(descriptor* rte) { reuse_q.enqueue_head(rte); }

void to_local_reuse_queue_end(descriptor* rte) { reuse_q.enqueue_tail(rte); }

descriptor* local_reuse_queue_dequeue() {
  descriptor* rte = nullptr;
  reuse_q.dequeue(rte);
  return rte;
}

void read_arguments(descriptor* rte) {
  for (auto i = 0; i < rte->narg; i++) rte->temparg[i] = rte->localarg[i];
}


}  // namespace

descriptor* get_reused_desc() {
  static u64 offset = sizeof(std::mutex);
  static u64 torc_size = sizeof(descriptor);

  auto* rte = local_reuse_queue_dequeue();
  if (rte != nullptr) {
    auto ptr = (char*) rte;
    ptr += offset;
    memset(ptr, 0, torc_size - offset);
  } else {
    rte = new descriptor();
  }

  return rte;
}

void put_reused_desc(descriptor* rte) { to_local_reuse_queue_end(rte); }


/*********************************************************************/
/************************* Intra-node Queues *************************/
/*********************************************************************/

void to_local_pq(descriptor* rte) { private_grq.enqueue_head(rte); }
void to_local_pq_end(descriptor* rte) { private_grq.enqueue_tail(rte); }

descriptor* local_pq_dequeue() {
  descriptor* rte = nullptr;
  private_grq.dequeue(rte);
  return rte;
}


void to_local_rq(descriptor* rte) {
  int lvl = (rte->level <= 1) ? 0 : rte->level - 1;
  if (lvl >= 10) lvl = 9;
  public_grq[lvl].enqueue_head(rte);
}


void to_local_rq_end(descriptor* rte) {
  auto lvl = (rte->level <= 1) ? 0 : rte->level - 1;
  if (lvl >= 10) lvl = 9;
  public_grq[lvl].enqueue_tail(rte);
}


descriptor* local_rq_dequeue(i32 lvl) {
  descriptor* rte = nullptr;
  public_grq[lvl].dequeue(rte);
  return rte;
}


/*********************************************************************/
/************************* Inter-node Queues *************************/
/*********************************************************************/
void to_nrq(i32 target_node, descriptor* rte) {
  rte->insert_in_front = 1;
  rte->inter_node = 1;  // Alternatively, it can be set only when it goes outside
  rte->target_queue = -1;
  if (node_id() != target_node) {
    mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_rq(rte);
  }
}

void to_nrq_end(i32 target_node, descriptor* rte) {
  rte->inter_node = 1;  // Alternatively, it can be set only when it goes outside
  rte->target_queue = -1;
  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_rq_end(rte);
  }
}


// Public global queue - general version
void to_rq(descriptor* rte) {
  static i32 initialized{};
  static i32 target_node{};

  auto total_nodes = num_nodes();

  if (initialized == 0) {
    initialized = 1;
    target_node = node_id();
  }

  rte->insert_in_front = 1;
  rte->insert_private = 0;
  rte->inter_node = 1;
  rte->target_queue = -1;  // Global
  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_rq(rte);
  }

  target_node = (target_node + 1) % total_nodes;
}

// Node version
void to_rq_end__(descriptor* rte) {
  static i32 initialized{};
  static i32 target_node{};

  auto total_nodes = num_nodes();

  if (initialized == 0) {
    initialized = 1;
    target_node = node_id();
  }

  rte->insert_private = 0;
  rte->inter_node = 1;
  rte->target_queue = -1;  // Global
  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_rq_end(rte);
  }
  target_node = (target_node + 1) % total_nodes;
}

// Worker version
void torc_to_rq_end(descriptor* rte) {
  static i32 initialized{};
  static i32 target_worker{};
  auto total_workers = num_workers();

  if (initialized == 0) {
    initialized = 1;
    target_worker = worker_id();
  }

  if (num_nodes() == 1) {
    to_local_rq_end(rte);
    return;
  }

  auto target_node = internal::mpi::global_thread_id_to_node_id(target_worker);
  auto target_queue = internal::mpi::global_thread_id_to_local_thread_id(target_worker);

  rte->insert_private = 0;
  rte->inter_node = 1;
  rte->target_queue = target_queue;  // Global
  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_rq_end(rte);
  }

  target_worker = (target_worker + 1) % total_workers;
}


// Public local (worker) queue
void to_lrq_end(i32 target, descriptor* rte) {
  if (num_nodes() == 1) {
    to_local_rq_end(rte);
    return;
  }

  auto target_node = internal::mpi::global_thread_id_to_node_id(target);
  auto target_queue = internal::mpi::global_thread_id_to_local_thread_id(target);

  rte->inter_node = 1;
  rte->target_queue = target_queue;
  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_rq_end(rte);
  }
}


void to_lrq(i32 target, descriptor* rte) {
  if (num_nodes() == 1) {
    to_local_rq(rte);
    return;
  }

  auto target_node = internal::mpi::global_thread_id_to_node_id(target);
  auto target_queue = internal::mpi::global_thread_id_to_local_thread_id(target);

  rte->inter_node = 1;
  rte->target_queue = target_queue;
  rte->insert_in_front = 1;
  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_rq(rte);
  }
}


// Private global queue
void to_npq(i32 target_node, descriptor* rte) {
  rte->insert_private = 1;
  rte->insert_in_front = 1;
  rte->inter_node = 1;  // Alternatively, it can be set only when it goes outside
  rte->target_queue = -1;

  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_pq(rte);
  }
}

void to_npq_end(int target_node, descriptor* rte) {
  rte->insert_private = 1;
  rte->inter_node = 1;  // Alternatively, it can be set only when it goes outside

  rte->target_queue = -1;
  if (node_id() != target_node) {
    internal::mpi::send_descriptor(target_node, rte, internal::mpi::NORMAL_ENQUEUE);
    put_reused_desc(rte);
  } else {
    read_arguments(rte);
    to_local_pq_end(rte);
  }
}

}  // namespace torc::internal::queue