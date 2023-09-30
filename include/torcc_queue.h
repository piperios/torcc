//  torcc: torcc_queue.h
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#pragma once

#include <torcc_internal.h>
#include <util.h>

namespace torc::internal::queue {

void rq_init();

void to_local_pq(descriptor* rte);
void to_local_pq_end(descriptor* rte);
descriptor* local_pq_dequeue();

void to_local_rq(descriptor* rte);
void to_local_rq_end(descriptor* rte);
descriptor* local_rq_dequeue(i32 lvl);

void to_local_lrq(i32 which, descriptor* rte);
void to_local_lrq_end(i32 which, descriptor* rte);
descriptor* local_lrq_dequeue(i32 which);
descriptor* local_lrq_dequeue_end(i32 which);
descriptor* local_lrq_dequeue_inner(i32 which);
descriptor* local_lrq_dequeue_end_inner(i32 which);

void put_reused_desc(descriptor* rte);
descriptor* get_reused_desc();

void to_nrq(i32 node, descriptor* rte);
void to_nrq_end(i32 node, descriptor* rte);

void to_rq(descriptor* rte);
void to_rq_end(descriptor* rte);

void to_lrq(i32 worker, descriptor* rte);
void to_lrq_end(i32 worker, descriptor* rte);

void to_npq(i32 node, descriptor* rte);
void to_npq_end(i32 node, descriptor* rte);

}  // namespace torc::internal::queue