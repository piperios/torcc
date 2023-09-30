//  torcc: torcc_base.h
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#pragma once


#include <array>
#include <mpi.h>
#include <mutex>
#include <thread>
#include <util.h>

namespace torc::internal {

// Lightweight RTE descriptor
struct descriptor {
  std::mutex lock;
  descriptor* prev;
  descriptor* next;
  size_t vp_id;

  descriptor* parent;
  i32 ndep;

  func_t work;
  i32 work_id;
  i32 narg;

  i32 homenode;
  i32 sourcenode;
  i32 sourcevpid;
  i32 target_queue;

  i32 inter_node;
  i32 insert_in_front;
  i32 insert_private;

  i32 rte_type;
  i32 type;
  i32 level;

  std::array<i32, MAX_TORC_ARGS> btype;
  std::array<MPI_Datatype, MAX_TORC_ARGS> dtype;
  std::array<i32, MAX_TORC_ARGS> quantity;
  std::array<i32, MAX_TORC_ARGS> callway;
  std::array<i64, MAX_TORC_ARGS> localarg;  // Data (address/value) in the owner node
  std::array<i64, MAX_TORC_ARGS> temparg;   // Data (address/value) in the remote node
};

class descriptor_queue {
public:
  descriptor_queue() = default;
  ~descriptor_queue() = default;

  descriptor* head() { return head_; }
  descriptor* tail() { return tail_; }

  void enqueue_head(descriptor* d) {
    std::lock_guard lock{mtx_};
    d->next = head();
    if (!head_) {
      head_ = tail_ = d;
    } else {
      head_ = d;
      d->next->prev = d;
    }
  }

  void enqueue_tail(descriptor* d) {
    std::lock_guard lock{mtx_};
    d->prev = head();
    if (!tail_) {
      head_ = tail_ = d;
    } else {
      tail_->next = d;
      tail_ = d;
    }
  }

  void dequeue(descriptor* thr) {
    if (head_) {
      std::lock_guard lock{mtx_};
      thr = head_;
      head_ = head_->next;
      if (!head_)
        tail_ = nullptr;
      else
        head_->prev = nullptr;
    }
  }

private:
  std::mutex mtx_{};
  descriptor* head_{nullptr};
  descriptor* tail_{nullptr};
};

struct torc_worker_record {
  u64 tasks_created[MAX_NVPS];
  u64 tasks_executed[MAX_NVPS];
  u64 prefetch_requests;
};

struct torc_data {
  u32 _global_vps;
  u32 _kthreads;

  u32 _physcpus;
  i32 _mpi_rank;
  i32 _mpi_nodes;
  u32 _appl_finished;

  i32 _thread_safe;
  u32 _internode_stealing;
  u32 _prefetching;
  i32 _yieldtime;
  i32 _throttling_factor;

  std::thread _server_thread;
  std::array<std::thread, MAX_NVPS> _worker_thread;

  /* read write */
  descriptor_queue _reuse_q{};
  descriptor_queue _private_grq{};
  descriptor_queue _public_grq[10]{};

  u64 _created[MAX_NVPS];
  u64 _executed[MAX_NVPS];
  std::array<torc_worker_record, 2> _worker_record;

  u64 _steal_hits;
  u64 _steal_served;
  u64 _steal_attempts;

  f64 _last_steal_duration;
  f64 _last_task_duration;

  pthread_key_t _vp_key;
  pthread_key_t _currt_key;
};


extern std::shared_ptr<torc_data> torc_data_inst;

#define global_vps torc_data_inst->_global_vps
#define kthreads torc_data_inst->_kthreads

#define physcpus torc_data_inst->_physcpus
#define mpi_rank torc_data_inst->_mpi_rank
#define mpi_nodes torc_data_inst->_mpi_nodes
#define appl_finished torc_data_inst->_appl_finished

#define thread_safe torc_data_inst->_thread_safe
#define internode_stealing torc_data_inst->_internode_stealing
#define prefetching torc_data_inst->_prefetching
#define yieldtime torc_data_inst->_yieldtime
#define throttling_factor torc_data_inst->_throttling_factor

#define server_thread torc_data_inst->_server_thread
#define worker_thread torc_data_inst->_worker_thread

#define reuse_q torc_data_inst->_reuse_q
#define private_grq torc_data_inst->_private_grq
#define public_grq torc_data_inst->_public_grq

#define created torc_data_inst->_created
#define executed torc_data_inst->_executed
#define steal_hits torc_data_inst->_steal_hits
#define steal_served torc_data_inst->_steal_served
#define steal_attempts torc_data_inst->_steal_attempts
#define worker_record torc_data_inst->_worker_record;
#define last_steal_duration torc_data_inst->_last_steal_duration
#define last_task_duration torc_data_inst->_last_task_duration
#define vp_key torc_data_inst->_vp_key
#define currt_key torc_data_inst->_currt_key

}  // namespace torc::internal