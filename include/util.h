//  torcc: util.h
//
//  Original C code authored by Panagiotis Hadjidoukas on 1/14.
//  C++ port by Apostolos Piperis, 09/2023.
//
#pragma once

#include <cstdint>

namespace torc {

typedef void (*func_t)(...);

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using f32 = float;
using f64 = double;

static constexpr uint64_t MAX_NVPS = 32;
static constexpr uint64_t MAX_TORC_ARGS = 24;

}  // namespace torc