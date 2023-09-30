//  torcc: torcc_comm.cpp
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <torcc.h>
#include <torcc_base.h>
#include <torcc_internal.h>
#include <torcc_mpi_internal.h>

namespace torc::internal::mpi {

std::shared_ptr<node_info*> node_info_inst;


namespace {

constexpr u32 torc_desc_size = sizeof(internal::descriptor);

i32 thread_id() {
  if (std::this_thread::get_id() == server_thread.get_id())
    return MAX_NVPS;
  else
    return get_vpid();
}

i32 num_threads(i32 node_id) { return node_info_inst[node_id]->nworkers; }

}  // namespace


/*************************************************************************/
/************************  INTER-NODE ROUTINES   *************************/
/*************************************************************************/

bool aslr_flag = false;
static u32 number_of_functions = 0;
static func_t internode_function_table[32];

void torc_register_task(void* f) {
  internode_function_table[number_of_functions] = reinterpret_cast<func_t>(f);
  number_of_functions++;
}

i32 get_func_num(func_t f) {
  for (auto i = 0; i < number_of_functions; i++)
    if (f == internode_function_table[i]) return i;
  return -1;
}

func_t get_func_ptr(i32 func_pos) { return internode_function_table[func_pos]; }

void check_aslr() {
  u64 vaddr[MAX_NODES];
  u64 vaddr_me = reinterpret_cast<u64>(check_aslr);
  int me = node_id();

  {
    std::lock_guard l{comm_lock};
    MPI_Allgather(&vaddr_me, 1, MPI_UNSIGNED_LONG, vaddr, 1, MPI_UNSIGNED_LONG, comm_out);
  }

  if (me == 0) {
    for (auto i = 0; i < num_nodes(); i++) { printf("node %2d -> %p\n", i, (void*) vaddr[i]); }
  }
  for (auto i = 0; i < num_nodes(); i++) {
    if (vaddr_me != vaddr[i]) {
      aslr_flag = true;
      return;
    }
  }

  return;
}

bool get_aslr() { return aslr_flag; }

/*************************************************************************/
/**********************   INITIALIZATION ROUTINES   **********************/
/*************************************************************************/

void comm::pre_init() {
  static int already_called = -1;

  already_called++;
  if (already_called) {
    printf("_rte_comm_pre_init has been called already\n");
    return;
  }
  node_info_inst = std::make_unique<node_info*>();
}

void comm::init() {
  int workers[MAX_NODES];
  int workers_me;

  workers_me = kthreads;

  check_aslr();

  {
    std::lock_guard l{comm_lock};
    MPI_Allgather(&workers_me, 1, MPI_INT, workers, 1, MPI_INT, comm_out);
    MPI_Barrier(comm_out);
  }

  // Workers have been started. The node_info array must be combined by all nodes
  for (auto i = 0; i < num_nodes(); i++) node_info_inst[i]->nworkers = workers[i];

  {
    std::lock_guard l{comm_lock};
    MPI_Barrier(comm_out);
  }
}


/*************************************************************************/
/******   EXPICLIT COMMUNICATION FOR DESCRIPTORS (SEND, RECEIVE)    ******/
/*************************************************************************/

void send_arguments(i32 node, i32 tag, descriptor* desc) {
  for (auto i = 0; i < desc->narg; i++) {
    if (desc->quantity[i] == 0) continue;

    // By copy || By address
    if ((desc->callway[i] == CALL_BY_COP) || (desc->callway[i] == CALL_BY_VAL)) {
      // Do not send anything - the value is in the descriptor
      if (desc->quantity[i] == 1) continue;

      std::lock_guard lock{comm_lock};
      if (desc->homenode != desc->sourcenode)
        MPI_Send(&desc->temparg[i], desc->quantity[i], desc->dtype[i], node, tag, comm_out);
      else
        MPI_Send(&desc->localarg[i], desc->quantity[i], desc->dtype[i], node, tag, comm_out);
    } else if ((desc->callway[i] == CALL_BY_REF) || (desc->callway[i] == CALL_BY_PTR) ||
               (desc->callway[i] == CALL_BY_COP2)) {
      // By reference || By value || By copy
      std::lock_guard lock{comm_lock};
      if (desc->homenode != desc->sourcenode) {
        MPI_Send((void*) desc->temparg[i], desc->quantity[i], desc->dtype[i], node, tag, comm_out);
      } else {
        MPI_Send((void*) desc->localarg[i], desc->quantity[i], desc->dtype[i], node, tag, comm_out);
      }
    } else
      // CALL_BY_RES: noop
      ;
  }
}

// Send a descriptor to the target node (always to a server thread)
void send_descriptor(i32 node, descriptor* desc, i32 type) {
  i32 tag = thread_id();

  desc->sourcenode = node_id();
  desc->sourcevpid = tag;
  desc->type = type;

  {
    std::lock_guard l{comm_lock};
    MPI_Send(desc, torc_desc_size, MPI_CHAR, node, MAX_NVPS, comm_out);
  }

  switch (desc->type) {
    case DIRECT_SYNCHRONOUS_STEALING_REQUEST:
    case BCAST: return;
    case ANSWER:
      for (auto i = 0; i < desc->narg; i++) {
        if (desc->quantity[i] == 0) continue;

        if ((desc->callway[i] == CALL_BY_COP2) && (desc->quantity[i] > 1))
          free((void*) desc->temparg[i]);

        // Send the result back
        if ((desc->callway[i] == CALL_BY_REF) || (desc->callway[i] == CALL_BY_RES)) {
          std::lock_guard lock{comm_lock};
          MPI_Send((void*) desc->temparg[i], desc->quantity[i], desc->dtype[i], desc->homenode, tag,
                   comm_out);
          if (desc->quantity[i] > 1) free((void*) desc->temparg[i]);
        }
      }
      return;
    default:
      // TORC_NORMAL_ENQUEUE
      if (desc->homenode == node) return;
      send_arguments(node, tag, desc);
      return;
  }
}

void direct_send_descriptor(i32 dummy, i32 sourcenode, i32 sourcevpid, descriptor* desc) {
  desc->sourcenode = node_id();
  desc->sourcevpid = MAX_NVPS; /* the server thread responds to a stealing request from a worker*/

  if (sourcevpid == MAX_NVPS) {
    // printf("direct send to (%d, %d)\n", sourcenode, sourcevpid); fflush(0);
    sourcevpid = MAX_NVPS + 1; /* response to server's thread direct stealing request */
  }

  auto tag = sourcevpid + 100;

  {
    std::lock_guard lock{comm_lock};
    MPI_Send(desc, torc_desc_size, MPI_CHAR, sourcenode, tag, comm_out);
  }

  if (desc->homenode == sourcenode) { return; }

  // Send_arguments (sourcenode, sourcevpid, desc);
  send_arguments(sourcenode, tag, desc);
}

void receive_arguments(descriptor* work, i32 tag) {
  int typesize;
  MPI_Status status;

  for (auto i = 0; i < work->narg; i++) {
    if (work->quantity[i] == 0) continue;

    if ((work->quantity[i] > 1) ||
        ((work->callway[i] != CALL_BY_COP) && (work->callway[i] != CALL_BY_VAL))) {
      work->dtype[i] = b2mpi_type(work->btype[i]);
      MPI_Type_size(work->dtype[i], &typesize);
      auto mem = (char*) calloc(1, work->quantity[i] * typesize);
      work->temparg[i] = (i64) mem;

      if ((work->callway[i] != CALL_BY_RES)) {
        std::lock_guard lock{comm_lock};
        (void) MPI_Recv((void*) work->temparg[i], work->quantity[i], work->dtype[i],
                        work->sourcenode, tag, comm_out, &status);
      }
    } else {
      work->temparg[i] = work->localarg[i];
    }
  }
}

int receive_descriptor(i32 node, descriptor* rte) {
  MPI_Status status;
  i32 istat;
  i32 tag = thread_id() + 100;

  if (thread_safe) {
    std::lock_guard lock{comm_lock};
    istat = MPI_Recv(rte, torc_desc_size, MPI_CHAR, node, tag, comm_out, &status);
  } else {
    /* irecv for non thread-safe MPI libraries */
    i32 flag = 0;
    MPI_Request request;

    {
      std::lock_guard lock{comm_lock};
      istat = MPI_Irecv(rte, torc_desc_size, MPI_CHAR, node, tag, comm_out, &request);
    }

    while (true) {
      if (appl_finished == 1) {
        rte->type = NO_WORK;
        return 1;
      }

      std::lock_guard lock{comm_lock};
      istat = MPI_Test(&request, &flag, &status);
      if (flag == 1) { /* check of istat ? */
        // if (istat == MPI_SUCCESS)
        break;
      } else {
        thread_sleep(yieldtime);
      }
    }
  }

  if (istat != MPI_SUCCESS) {
    rte->type = NO_WORK;
    return 1;
  }

  if (rte->homenode != node_id()) receive_arguments(rte, tag);

  return 0;
}


/*************************************************************************/
/**************************  THREAD MANAGEMENT  **************************/
/*************************************************************************/

i32 total_num_threads() {
  int nodes = num_nodes();
  int sum_vp = 0;

  for (int i = 0; i < nodes; i++) { sum_vp += num_threads(i); }
  return sum_vp;
}

int global_thread_id_to_node_id(int global_thread_id) {
  int nodes = num_nodes();
  int sum_vp = 0;

#if DBG
  printf("global_thread_id = %d\n", global_thread_id);
#endif

  for (int i = 0; i < nodes; i++) {
    sum_vp += num_threads(i);
    if (global_thread_id < sum_vp) return i;
  }
  Error("target_to_node failed");
  return -1; /* never reached */
}

i32 local_thread_id_to_global_thread_id(i32 local_thread_id) {
  int mynode = node_id();
  int sum_vp = 0;

  for (int i = 0; i < mynode; i++) { sum_vp += num_threads(i); }
  return sum_vp + local_thread_id;
}

int global_thread_id_to_local_thread_id(int global_thread_id) {
  int mynode = global_thread_id_to_node_id(global_thread_id);
  int sum_vp = 0;

  for (int i = 0; i < mynode; i++) { sum_vp += num_threads(i); }
  return global_thread_id - sum_vp;
}

/*************************************************************************/
/**************************    BROADCASTING     **************************/
/*************************************************************************/

void torc_broadcast(void* a, long count, MPI_Datatype datatype) {
  long mynode = node_id();

  descriptor mydata;
  int nnodes = num_nodes();
  int tag = thread_id();

#if DBG
  printf("Broadcasting data ...\n");
  fflush(0);
#endif
  memset(&mydata, 0, sizeof(descriptor));
  mydata.localarg[0] = (i64) mynode;
  mydata.localarg[1] = *reinterpret_cast<long*>(a);
  mydata.localarg[2] = (i64) count;
#if 1
  /* yyy */
  mydata.localarg[3] = (i64) mpi2b_type(datatype);
//    mydata.localarg[3] = datatype;
#endif
  mydata.homenode = mydata.sourcenode = mynode;

  for (int node = 0; node < nnodes; node++) {
    if (node != mynode) {
      /* OK. This descriptor is a stack variable */
      send_descriptor(node, &mydata, BCAST);

      std::lock_guard lock{comm_lock};
      MPI_Ssend(a, count, datatype, node, tag, comm_out);
    }
  }
}

torc_mpi_types mpi2b_type(MPI_Datatype dtype) {
  if (dtype == MPI_INT)
    return torc_mpi_types::T_MPI_INT;
  else if (dtype == MPI_LONG)
    return torc_mpi_types::T_MPI_LONG;
  else if (dtype == MPI_FLOAT)
    return torc_mpi_types::T_MPI_FLOAT;
  else if (dtype == MPI_DOUBLE)
    return torc_mpi_types::T_MPI_DOUBLE;
  else
    Error("unsupported MPI data type");

  return torc_mpi_types::T_MPI_CHAR;  // Never reached
}

MPI_Datatype _torc_b2mpi_type(torc_mpi_types btype) {
  switch (btype) {
    case torc_mpi_types::T_MPI_INT: return MPI_INT; break;
    case torc_mpi_types::T_MPI_LONG: return MPI_LONG; break;
    case torc_mpi_types::T_MPI_FLOAT: return MPI_FLOAT; break;
    case torc_mpi_types::T_MPI_DOUBLE: return MPI_DOUBLE; break;
    default:
      printf("btype = %d\n", (i32) btype);
      Error("unsupported MPI datatype");
      break;
  }
  return 0;
}

}  // namespace torc::internal::mpi
