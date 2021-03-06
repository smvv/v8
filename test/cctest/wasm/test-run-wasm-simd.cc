// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/assembler-inl.h"
#include "src/wasm/wasm-macro-gen.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"
#include "test/cctest/wasm/wasm-run-utils.h"

using namespace v8::base;
using namespace v8::internal;
using namespace v8::internal::compiler;
using namespace v8::internal::wasm;

namespace {

typedef float (*FloatUnOp)(float);
typedef float (*FloatBinOp)(float, float);
typedef int (*FloatCompareOp)(float, float);
typedef int32_t (*Int32UnOp)(int32_t);
typedef int32_t (*Int32BinOp)(int32_t, int32_t);
typedef int (*Int32CompareOp)(int32_t, int32_t);
typedef int32_t (*Int32ShiftOp)(int32_t, int);
typedef int16_t (*Int16UnOp)(int16_t);
typedef int16_t (*Int16BinOp)(int16_t, int16_t);
typedef int (*Int16CompareOp)(int16_t, int16_t);
typedef int16_t (*Int16ShiftOp)(int16_t, int);
typedef int8_t (*Int8UnOp)(int8_t);
typedef int8_t (*Int8BinOp)(int8_t, int8_t);
typedef int (*Int8CompareOp)(int8_t, int8_t);
typedef int8_t (*Int8ShiftOp)(int8_t, int);

#if !V8_TARGET_ARCH_ARM && !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_IA32
#define SIMD_LOWERING_TARGET 1
#else
#define SIMD_LOWERING_TARGET 0
#endif  // !V8_TARGET_ARCH_ARM && !V8_TARGET_ARCH_X64 && !V8_TARGET_ARCH_IA32

// Generic expected value functions.
template <typename T>
T Negate(T a) {
  return -a;
}

template <typename T>
T Add(T a, T b) {
  return a + b;
}

template <typename T>
T Sub(T a, T b) {
  return a - b;
}

template <typename T>
T Mul(T a, T b) {
  return a * b;
}

template <typename T>
T Div(T a, T b) {
  return a / b;
}

template <typename T>
T Minimum(T a, T b) {
  return a <= b ? a : b;
}

template <typename T>
T Maximum(T a, T b) {
  return a >= b ? a : b;
}

// For float operands, Min and Max must return NaN if either operand is NaN.
#if V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
template <>
float Minimum(float a, float b) {
  if (std::isnan(a) || std::isnan(b))
    return std::numeric_limits<float>::quiet_NaN();
  return a <= b ? a : b;
}

template <>
float Maximum(float a, float b) {
  if (std::isnan(a) || std::isnan(b))
    return std::numeric_limits<float>::quiet_NaN();
  return a >= b ? a : b;
}
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET

template <typename T>
T UnsignedMinimum(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<UnsignedT>(a) <= static_cast<UnsignedT>(b) ? a : b;
}

template <typename T>
T UnsignedMaximum(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<UnsignedT>(a) >= static_cast<UnsignedT>(b) ? a : b;
}

template <typename T>
int Equal(T a, T b) {
  return a == b ? 1 : 0;
}

template <typename T>
int NotEqual(T a, T b) {
  return a != b ? 1 : 0;
}

template <typename T>
int Less(T a, T b) {
  return a < b ? 1 : 0;
}

template <typename T>
int LessEqual(T a, T b) {
  return a <= b ? 1 : 0;
}

template <typename T>
int Greater(T a, T b) {
  return a > b ? 1 : 0;
}

template <typename T>
int GreaterEqual(T a, T b) {
  return a >= b ? 1 : 0;
}

template <typename T>
int UnsignedLess(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<UnsignedT>(a) < static_cast<UnsignedT>(b) ? 1 : 0;
}

template <typename T>
int UnsignedLessEqual(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<UnsignedT>(a) <= static_cast<UnsignedT>(b) ? 1 : 0;
}

template <typename T>
int UnsignedGreater(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<UnsignedT>(a) > static_cast<UnsignedT>(b) ? 1 : 0;
}

template <typename T>
int UnsignedGreaterEqual(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<UnsignedT>(a) >= static_cast<UnsignedT>(b) ? 1 : 0;
}

template <typename T>
T LogicalShiftLeft(T a, int shift) {
  return a << shift;
}

template <typename T>
T LogicalShiftRight(T a, int shift) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<UnsignedT>(a) >> shift;
}

template <typename T>
int64_t Widen(T value) {
  static_assert(sizeof(int64_t) > sizeof(T), "T must be int32_t or smaller");
  return static_cast<int64_t>(value);
}

template <typename T>
int64_t UnsignedWiden(T value) {
  static_assert(sizeof(int64_t) > sizeof(T), "T must be int32_t or smaller");
  using UnsignedT = typename std::make_unsigned<T>::type;
  return static_cast<int64_t>(static_cast<UnsignedT>(value));
}

template <typename T>
T Clamp(int64_t value) {
  static_assert(sizeof(int64_t) > sizeof(T), "T must be int32_t or smaller");
  int64_t min = static_cast<int64_t>(std::numeric_limits<T>::min());
  int64_t max = static_cast<int64_t>(std::numeric_limits<T>::max());
  int64_t clamped = std::max(min, std::min(max, value));
  return static_cast<T>(clamped);
}

template <typename T>
T AddSaturate(T a, T b) {
  return Clamp<T>(Widen(a) + Widen(b));
}

template <typename T>
T SubSaturate(T a, T b) {
  return Clamp<T>(Widen(a) - Widen(b));
}

template <typename T>
T UnsignedAddSaturate(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return Clamp<UnsignedT>(UnsignedWiden(a) + UnsignedWiden(b));
}

template <typename T>
T UnsignedSubSaturate(T a, T b) {
  using UnsignedT = typename std::make_unsigned<T>::type;
  return Clamp<UnsignedT>(UnsignedWiden(a) - UnsignedWiden(b));
}

template <typename T>
T And(T a, T b) {
  return a & b;
}

template <typename T>
T Or(T a, T b) {
  return a | b;
}

template <typename T>
T Xor(T a, T b) {
  return a ^ b;
}

template <typename T>
T Not(T a) {
  return ~a;
}

template <typename T>
T LogicalNot(T a) {
  return a == 0 ? 1 : 0;
}

template <typename T>
T Sqrt(T a) {
  return std::sqrt(a);
}

template <typename T>
T Recip(T a) {
  return 1.0f / a;
}

template <typename T>
T RecipSqrt(T a) {
  return 1.0f / std::sqrt(a);
}

template <typename T>
T RecipRefine(T a, T b) {
  return 2.0f - a * b;
}

template <typename T>
T RecipSqrtRefine(T a, T b) {
  return (3.0f - a * b) * 0.5f;
}

}  // namespace

#define WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lane_value, lane_index) \
  WASM_IF(WASM_##LANE_TYPE##_NE(WASM_GET_LOCAL(lane_value),                  \
                                WASM_SIMD_##TYPE##_EXTRACT_LANE(             \
                                    lane_index, WASM_GET_LOCAL(value))),     \
          WASM_RETURN1(WASM_ZERO))

#define WASM_SIMD_CHECK4(TYPE, value, LANE_TYPE, lv0, lv1, lv2, lv3) \
  WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv0, 0)               \
  , WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv1, 1),            \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv2, 2),          \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv3, 3)

#define WASM_SIMD_CHECK_SPLAT4(TYPE, value, LANE_TYPE, lv) \
  WASM_SIMD_CHECK4(TYPE, value, LANE_TYPE, lv, lv, lv, lv)

#define WASM_SIMD_CHECK8(TYPE, value, LANE_TYPE, lv0, lv1, lv2, lv3, lv4, lv5, \
                         lv6, lv7)                                             \
  WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv0, 0)                         \
  , WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv1, 1),                      \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv2, 2),                    \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv3, 3),                    \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv4, 4),                    \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv5, 5),                    \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv6, 6),                    \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv7, 7)

#define WASM_SIMD_CHECK_SPLAT8(TYPE, value, LANE_TYPE, lv) \
  WASM_SIMD_CHECK8(TYPE, value, LANE_TYPE, lv, lv, lv, lv, lv, lv, lv, lv)

#define WASM_SIMD_CHECK16(TYPE, value, LANE_TYPE, lv0, lv1, lv2, lv3, lv4, \
                          lv5, lv6, lv7, lv8, lv9, lv10, lv11, lv12, lv13, \
                          lv14, lv15)                                      \
  WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv0, 0)                     \
  , WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv1, 1),                  \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv2, 2),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv3, 3),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv4, 4),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv5, 5),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv6, 6),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv7, 7),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv8, 8),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv9, 9),                \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv10, 10),              \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv11, 11),              \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv12, 12),              \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv13, 13),              \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv14, 14),              \
      WASM_SIMD_CHECK_LANE(TYPE, value, LANE_TYPE, lv15, 15)

#define WASM_SIMD_CHECK_SPLAT16(TYPE, value, LANE_TYPE, lv)                 \
  WASM_SIMD_CHECK16(TYPE, value, LANE_TYPE, lv, lv, lv, lv, lv, lv, lv, lv, \
                    lv, lv, lv, lv, lv, lv, lv, lv)

#define WASM_SIMD_CHECK_F32_LANE(value, lane_value, lane_index)             \
  WASM_IF(WASM_F32_NE(WASM_GET_LOCAL(lane_value),                           \
                      WASM_SIMD_F32x4_EXTRACT_LANE(lane_index,              \
                                                   WASM_GET_LOCAL(value))), \
          WASM_RETURN1(WASM_ZERO))

#define WASM_SIMD_CHECK_F32x4(value, lv0, lv1, lv2, lv3) \
  WASM_SIMD_CHECK_F32_LANE(value, lv0, 0)                \
  , WASM_SIMD_CHECK_F32_LANE(value, lv1, 1),             \
      WASM_SIMD_CHECK_F32_LANE(value, lv2, 2),           \
      WASM_SIMD_CHECK_F32_LANE(value, lv3, 3)

#define WASM_SIMD_CHECK_SPLAT_F32x4(value, lv) \
  WASM_SIMD_CHECK_F32x4(value, lv, lv, lv, lv)

#define WASM_SIMD_CHECK_F32_LANE_ESTIMATE(value, low, high, lane_index)       \
  WASM_IF(WASM_F32_GT(WASM_GET_LOCAL(low),                                    \
                      WASM_SIMD_F32x4_EXTRACT_LANE(lane_index,                \
                                                   WASM_GET_LOCAL(value))),   \
          WASM_RETURN1(WASM_ZERO))                                            \
  , WASM_IF(WASM_F32_LT(WASM_GET_LOCAL(high),                                 \
                        WASM_SIMD_F32x4_EXTRACT_LANE(lane_index,              \
                                                     WASM_GET_LOCAL(value))), \
            WASM_RETURN1(WASM_ZERO))

#define WASM_SIMD_CHECK_SPLAT_F32x4_ESTIMATE(value, low, high) \
  WASM_SIMD_CHECK_F32_LANE_ESTIMATE(value, low, high, 0)       \
  , WASM_SIMD_CHECK_F32_LANE_ESTIMATE(value, low, high, 1),    \
      WASM_SIMD_CHECK_F32_LANE_ESTIMATE(value, low, high, 2),  \
      WASM_SIMD_CHECK_F32_LANE_ESTIMATE(value, low, high, 3)

#define TO_BYTE(val) static_cast<byte>(val)
#define WASM_SIMD_OP(op) kSimdPrefix, TO_BYTE(op)
#define WASM_SIMD_SPLAT(Type, x) x, WASM_SIMD_OP(kExpr##Type##Splat)
#define WASM_SIMD_UNOP(op, x) x, WASM_SIMD_OP(op)
#define WASM_SIMD_BINOP(op, x, y) x, y, WASM_SIMD_OP(op)
#define WASM_SIMD_SHIFT_OP(op, shift, x) x, WASM_SIMD_OP(op), TO_BYTE(shift)
#define WASM_SIMD_SELECT(format, x, y, z) \
  x, y, z, WASM_SIMD_OP(kExprS##format##Select)
// Since boolean vectors can't be checked directly, materialize them into
// integer vectors using a Select operation.
#define WASM_SIMD_MATERIALIZE_BOOLS(format, x) \
  x, WASM_SIMD_I##format##_SPLAT(WASM_ONE),    \
      WASM_SIMD_I##format##_SPLAT(WASM_ZERO),  \
      WASM_SIMD_OP(kExprS##format##Select)

#define WASM_SIMD_F32x4_SPLAT(x) x, WASM_SIMD_OP(kExprF32x4Splat)
#define WASM_SIMD_F32x4_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprF32x4ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_F32x4_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprF32x4ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I32x4_SPLAT(x) x, WASM_SIMD_OP(kExprI32x4Splat)
#define WASM_SIMD_I32x4_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprI32x4ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_I32x4_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprI32x4ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I16x8_SPLAT(x) x, WASM_SIMD_OP(kExprI16x8Splat)
#define WASM_SIMD_I16x8_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprI16x8ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_I16x8_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprI16x8ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_I8x16_SPLAT(x) x, WASM_SIMD_OP(kExprI8x16Splat)
#define WASM_SIMD_I8x16_EXTRACT_LANE(lane, x) \
  x, WASM_SIMD_OP(kExprI8x16ExtractLane), TO_BYTE(lane)
#define WASM_SIMD_I8x16_REPLACE_LANE(lane, x, y) \
  x, y, WASM_SIMD_OP(kExprI8x16ReplaceLane), TO_BYTE(lane)

#define WASM_SIMD_F32x4_FROM_I32x4(x) x, WASM_SIMD_OP(kExprF32x4SConvertI32x4)
#define WASM_SIMD_F32x4_FROM_U32x4(x) x, WASM_SIMD_OP(kExprF32x4UConvertI32x4)
#define WASM_SIMD_I32x4_FROM_F32x4(x) x, WASM_SIMD_OP(kExprI32x4SConvertF32x4)
#define WASM_SIMD_U32x4_FROM_F32x4(x) x, WASM_SIMD_OP(kExprI32x4UConvertF32x4)

// Skip FP tests involving extremely large or extremely small values, which
// may fail due to non-IEEE-754 SIMD arithmetic on some platforms.
bool SkipFPValue(float x) {
  float abs_x = std::fabs(x);
  const float kSmallFloatThreshold = 1.0e-32f;
  const float kLargeFloatThreshold = 1.0e32f;
  return abs_x != 0.0f &&  // 0 or -0 are fine.
         (abs_x < kSmallFloatThreshold || abs_x > kLargeFloatThreshold);
}

// Skip tests where the expected value is a NaN, since our WASM test code
// doesn't handle NaNs. Also skip extreme values.
bool SkipFPExpectedValue(float x) { return std::isnan(x) || SkipFPValue(x); }

#if V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
WASM_EXEC_COMPILED_TEST(F32x4Splat) {
  FLAG_wasm_simd_prototype = true;

  WasmRunner<int32_t, float> r(kExecuteCompiled);
  byte lane_val = 0;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r,
        WASM_SET_LOCAL(simd, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(lane_val))),
        WASM_SIMD_CHECK_SPLAT_F32x4(simd, lane_val), WASM_RETURN1(WASM_ONE));

  FOR_FLOAT32_INPUTS(i) {
    if (SkipFPExpectedValue(*i)) continue;
    CHECK_EQ(1, r.Call(*i));
  }
}

WASM_EXEC_COMPILED_TEST(F32x4ReplaceLane) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, float, float> r(kExecuteCompiled);
  byte old_val = 0;
  byte new_val = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(old_val))),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_F32x4_REPLACE_LANE(0, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK_F32x4(simd, new_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_F32x4_REPLACE_LANE(1, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK_F32x4(simd, new_val, new_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_F32x4_REPLACE_LANE(2, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK_F32x4(simd, new_val, new_val, new_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_F32x4_REPLACE_LANE(3, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK_SPLAT_F32x4(simd, new_val), WASM_RETURN1(WASM_ONE));

  CHECK_EQ(1, r.Call(3.14159f, -1.5f));
}

// Tests both signed and unsigned conversion.
WASM_EXEC_COMPILED_TEST(F32x4FromInt32x4) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, float, float> r(kExecuteCompiled);
  byte a = 0;
  byte expected_signed = 1;
  byte expected_unsigned = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  byte simd2 = r.AllocateLocal(kWasmS128);
  BUILD(
      r, WASM_SET_LOCAL(simd0, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(a))),
      WASM_SET_LOCAL(simd1, WASM_SIMD_F32x4_FROM_I32x4(WASM_GET_LOCAL(simd0))),
      WASM_SIMD_CHECK_SPLAT_F32x4(simd1, expected_signed),
      WASM_SET_LOCAL(simd2, WASM_SIMD_F32x4_FROM_U32x4(WASM_GET_LOCAL(simd0))),
      WASM_SIMD_CHECK_SPLAT_F32x4(simd2, expected_unsigned),
      WASM_RETURN1(WASM_ONE));

  FOR_INT32_INPUTS(i) {
    CHECK_EQ(1, r.Call(*i, static_cast<float>(*i),
                       static_cast<float>(static_cast<uint32_t>(*i))));
  }
}

void RunF32x4UnOpTest(WasmOpcode simd_op, FloatUnOp expected_op,
                      float error = 0.0f) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, float, float, float> r(kExecuteCompiled);
  byte a = 0;
  byte low = 1;
  byte high = 2;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd, WASM_SIMD_UNOP(simd_op, WASM_GET_LOCAL(simd))),
        WASM_SIMD_CHECK_SPLAT_F32x4_ESTIMATE(simd, low, high),
        WASM_RETURN1(WASM_ONE));

  FOR_FLOAT32_INPUTS(i) {
    if (SkipFPValue(*i)) continue;
    float expected = expected_op(*i);
    if (SkipFPExpectedValue(expected)) continue;
    float abs_error = std::abs(expected) * error;
    CHECK_EQ(1, r.Call(*i, expected - abs_error, expected + abs_error));
  }
}

WASM_EXEC_COMPILED_TEST(F32x4Abs) { RunF32x4UnOpTest(kExprF32x4Abs, std::abs); }
WASM_EXEC_COMPILED_TEST(F32x4Neg) { RunF32x4UnOpTest(kExprF32x4Neg, Negate); }
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET

#if SIMD_LOWERING_TARGET
WASM_EXEC_COMPILED_TEST(F32x4Sqrt) { RunF32x4UnOpTest(kExprF32x4Sqrt, Sqrt); }
#endif  // SIMD_LOWERING_TARGET

#if V8_TARGET_ARCH_ARM
static const float kApproxError = 0.01f;

WASM_EXEC_COMPILED_TEST(F32x4RecipApprox) {
  RunF32x4UnOpTest(kExprF32x4RecipApprox, Recip, kApproxError);
}

WASM_EXEC_COMPILED_TEST(F32x4RecipSqrtApprox) {
  RunF32x4UnOpTest(kExprF32x4RecipSqrtApprox, RecipSqrt, kApproxError);
}
#endif  // V8_TARGET_ARCH_ARM

#if V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
void RunF32x4BinOpTest(WasmOpcode simd_op, FloatBinOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, float, float, float> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                              WASM_GET_LOCAL(simd1))),
        WASM_SIMD_CHECK_SPLAT_F32x4(simd1, expected), WASM_RETURN1(WASM_ONE));

  FOR_FLOAT32_INPUTS(i) {
    if (SkipFPValue(*i)) continue;
    FOR_FLOAT32_INPUTS(j) {
      if (SkipFPValue(*j)) continue;
      float expected = expected_op(*i, *j);
      if (SkipFPExpectedValue(expected)) continue;
      CHECK_EQ(1, r.Call(*i, *j, expected));
    }
  }
}

WASM_EXEC_COMPILED_TEST(F32x4Add) { RunF32x4BinOpTest(kExprF32x4Add, Add); }
WASM_EXEC_COMPILED_TEST(F32x4Sub) { RunF32x4BinOpTest(kExprF32x4Sub, Sub); }
WASM_EXEC_COMPILED_TEST(F32x4Mul) { RunF32x4BinOpTest(kExprF32x4Mul, Mul); }
WASM_EXEC_COMPILED_TEST(F32x4_Min) {
  RunF32x4BinOpTest(kExprF32x4Min, Minimum);
}
WASM_EXEC_COMPILED_TEST(F32x4_Max) {
  RunF32x4BinOpTest(kExprF32x4Max, Maximum);
}
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET

#if SIMD_LOWERING_TARGET
WASM_EXEC_COMPILED_TEST(F32x4Div) { RunF32x4BinOpTest(kExprF32x4Div, Div); }
#endif  // SIMD_LOWERING_TARGET

#if V8_TARGET_ARCH_ARM
WASM_EXEC_COMPILED_TEST(F32x4RecipRefine) {
  RunF32x4BinOpTest(kExprF32x4RecipRefine, RecipRefine);
}

WASM_EXEC_COMPILED_TEST(F32x4RecipSqrtRefine) {
  RunF32x4BinOpTest(kExprF32x4RecipSqrtRefine, RecipSqrtRefine);
}
#endif  // V8_TARGET_ARCH_ARM

#if V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
void RunF32x4CompareOpTest(WasmOpcode simd_op, FloatCompareOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, float, float, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1,
                       WASM_SIMD_MATERIALIZE_BOOLS(
                           32x4, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                                 WASM_GET_LOCAL(simd1)))),
        WASM_SIMD_CHECK_SPLAT4(I32x4, simd1, I32, expected), WASM_ONE);

  FOR_FLOAT32_INPUTS(i) {
    if (SkipFPValue(*i)) continue;
    FOR_FLOAT32_INPUTS(j) {
      if (SkipFPValue(*j)) continue;
      float diff = *i - *j;
      if (SkipFPExpectedValue(diff)) continue;
      CHECK_EQ(1, r.Call(*i, *j, expected_op(*i, *j)));
    }
  }
}

WASM_EXEC_COMPILED_TEST(F32x4Eq) { RunF32x4CompareOpTest(kExprF32x4Eq, Equal); }

WASM_EXEC_COMPILED_TEST(F32x4Ne) {
  RunF32x4CompareOpTest(kExprF32x4Ne, NotEqual);
}

WASM_EXEC_COMPILED_TEST(F32x4Gt) {
  RunF32x4CompareOpTest(kExprF32x4Gt, Greater);
}

WASM_EXEC_COMPILED_TEST(F32x4Ge) {
  RunF32x4CompareOpTest(kExprF32x4Ge, GreaterEqual);
}

WASM_EXEC_COMPILED_TEST(F32x4Lt) { RunF32x4CompareOpTest(kExprF32x4Lt, Less); }

WASM_EXEC_COMPILED_TEST(F32x4Le) {
  RunF32x4CompareOpTest(kExprF32x4Le, LessEqual);
}
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET

WASM_EXEC_COMPILED_TEST(I32x4Splat) {
  FLAG_wasm_simd_prototype = true;

  // Store SIMD value in a local variable, use extract lane to check lane values
  // This test is not a test for ExtractLane as Splat does not create
  // interesting SIMD values.
  //
  // SetLocal(1, I32x4Splat(Local(0)));
  // For each lane index
  // if(Local(0) != I32x4ExtractLane(Local(1), index)
  //   return 0
  //
  // return 1
  WasmRunner<int32_t, int32_t> r(kExecuteCompiled);
  byte lane_val = 0;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r,
        WASM_SET_LOCAL(simd, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(lane_val))),
        WASM_SIMD_CHECK_SPLAT4(I32x4, simd, I32, lane_val), WASM_ONE);

  FOR_INT32_INPUTS(i) { CHECK_EQ(1, r.Call(*i)); }
}

WASM_EXEC_COMPILED_TEST(I32x4ReplaceLane) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte old_val = 0;
  byte new_val = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(old_val))),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I32x4_REPLACE_LANE(0, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK4(I32x4, simd, I32, new_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I32x4_REPLACE_LANE(1, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK4(I32x4, simd, I32, new_val, new_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I32x4_REPLACE_LANE(2, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK4(I32x4, simd, I32, new_val, new_val, new_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I32x4_REPLACE_LANE(3, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK_SPLAT4(I32x4, simd, I32, new_val), WASM_ONE);

  CHECK_EQ(1, r.Call(1, 2));
}

#if V8_TARGET_ARCH_ARM

WASM_EXEC_COMPILED_TEST(I16x8Splat) {
  FLAG_wasm_simd_prototype = true;

  WasmRunner<int32_t, int32_t> r(kExecuteCompiled);
  byte lane_val = 0;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r,
        WASM_SET_LOCAL(simd, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(lane_val))),
        WASM_SIMD_CHECK_SPLAT8(I16x8, simd, I32, lane_val), WASM_ONE);

  FOR_INT16_INPUTS(i) { CHECK_EQ(1, r.Call(*i)); }
}

WASM_EXEC_COMPILED_TEST(I16x8ReplaceLane) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte old_val = 0;
  byte new_val = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(old_val))),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(0, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK8(I16x8, simd, I32, new_val, old_val, old_val, old_val,
                         old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(1, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK8(I16x8, simd, I32, new_val, new_val, old_val, old_val,
                         old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(2, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK8(I16x8, simd, I32, new_val, new_val, new_val, old_val,
                         old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(3, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK8(I16x8, simd, I32, new_val, new_val, new_val, new_val,
                         old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(4, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK8(I16x8, simd, I32, new_val, new_val, new_val, new_val,
                         new_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(5, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK8(I16x8, simd, I32, new_val, new_val, new_val, new_val,
                         new_val, new_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(6, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK8(I16x8, simd, I32, new_val, new_val, new_val, new_val,
                         new_val, new_val, new_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I16x8_REPLACE_LANE(7, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK_SPLAT8(I16x8, simd, I32, new_val), WASM_ONE);

  CHECK_EQ(1, r.Call(1, 2));
}

WASM_EXEC_COMPILED_TEST(I8x16Splat) {
  FLAG_wasm_simd_prototype = true;

  WasmRunner<int32_t, int32_t> r(kExecuteCompiled);
  byte lane_val = 0;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r,
        WASM_SET_LOCAL(simd, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(lane_val))),
        WASM_SIMD_CHECK_SPLAT8(I8x16, simd, I32, lane_val), WASM_ONE);

  FOR_INT8_INPUTS(i) { CHECK_EQ(1, r.Call(*i)); }
}

WASM_EXEC_COMPILED_TEST(I8x16ReplaceLane) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte old_val = 0;
  byte new_val = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(old_val))),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(0, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(1, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(2, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(3, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          old_val, old_val, old_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(4, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, old_val, old_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(5, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, old_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(6, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, old_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(7, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, old_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(8, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, old_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(9, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, new_val,
                          old_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(10, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, new_val,
                          new_val, old_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(11, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, new_val,
                          new_val, new_val, old_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(12, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, old_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(13, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, old_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(14, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK16(I8x16, simd, I32, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, new_val,
                          new_val, new_val, new_val, new_val, new_val, old_val),
        WASM_SET_LOCAL(simd,
                       WASM_SIMD_I8x16_REPLACE_LANE(15, WASM_GET_LOCAL(simd),
                                                    WASM_GET_LOCAL(new_val))),
        WASM_SIMD_CHECK_SPLAT16(I8x16, simd, I32, new_val), WASM_ONE);

  CHECK_EQ(1, r.Call(1, 2));
}
#endif  // V8_TARGET_ARCH_ARM

#if V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
// Determines if conversion from float to int will be valid.
bool CanRoundToZeroAndConvert(double val, bool unsigned_integer) {
  const double max_uint = static_cast<double>(0xffffffffu);
  const double max_int = static_cast<double>(kMaxInt);
  const double min_int = static_cast<double>(kMinInt);

  // Check for NaN.
  if (val != val) {
    return false;
  }

  // Round to zero and check for overflow. This code works because 32 bit
  // integers can be exactly represented by ieee-754 64bit floating-point
  // values.
  return unsigned_integer ? (val < (max_uint + 1.0)) && (val > -1)
                          : (val < (max_int + 1.0)) && (val > (min_int - 1.0));
}

int ConvertInvalidValue(double val, bool unsigned_integer) {
  if (val != val) {
    return 0;
  } else {
    if (unsigned_integer) {
      return (val < 0) ? 0 : 0xffffffffu;
    } else {
      return (val < 0) ? kMinInt : kMaxInt;
    }
  }
}

int32_t ConvertToInt(double val, bool unsigned_integer) {
  int32_t result =
      unsigned_integer ? static_cast<uint32_t>(val) : static_cast<int32_t>(val);

  if (!CanRoundToZeroAndConvert(val, unsigned_integer)) {
    result = ConvertInvalidValue(val, unsigned_integer);
  }
  return result;
}

// Tests both signed and unsigned conversion.
WASM_EXEC_COMPILED_TEST(I32x4FromFloat32x4) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, float, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte expected_signed = 1;
  byte expected_unsigned = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  byte simd2 = r.AllocateLocal(kWasmS128);
  BUILD(
      r, WASM_SET_LOCAL(simd0, WASM_SIMD_F32x4_SPLAT(WASM_GET_LOCAL(a))),
      WASM_SET_LOCAL(simd1, WASM_SIMD_I32x4_FROM_F32x4(WASM_GET_LOCAL(simd0))),
      WASM_SIMD_CHECK_SPLAT4(I32x4, simd1, I32, expected_signed),
      WASM_SET_LOCAL(simd2, WASM_SIMD_U32x4_FROM_F32x4(WASM_GET_LOCAL(simd0))),
      WASM_SIMD_CHECK_SPLAT4(I32x4, simd2, I32, expected_unsigned), WASM_ONE);

  FOR_FLOAT32_INPUTS(i) {
    if (SkipFPValue(*i)) continue;
    int32_t signed_value = ConvertToInt(*i, false);
    int32_t unsigned_value = ConvertToInt(*i, true);
    CHECK_EQ(1, r.Call(*i, signed_value, unsigned_value));
  }
}

void RunI32x4UnOpTest(WasmOpcode simd_op, Int32UnOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte expected = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd, WASM_SIMD_UNOP(simd_op, WASM_GET_LOCAL(simd))),
        WASM_SIMD_CHECK_SPLAT4(I32x4, simd, I32, expected), WASM_ONE);

  FOR_INT32_INPUTS(i) { CHECK_EQ(1, r.Call(*i, expected_op(*i))); }
}

WASM_EXEC_COMPILED_TEST(I32x4Neg) { RunI32x4UnOpTest(kExprI32x4Neg, Negate); }

WASM_EXEC_COMPILED_TEST(S128Not) { RunI32x4UnOpTest(kExprS128Not, Not); }
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET

void RunI32x4BinOpTest(WasmOpcode simd_op, Int32BinOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                              WASM_GET_LOCAL(simd1))),
        WASM_SIMD_CHECK_SPLAT4(I32x4, simd1, I32, expected), WASM_ONE);

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, expected_op(*i, *j))); }
  }
}

WASM_EXEC_COMPILED_TEST(I32x4Add) { RunI32x4BinOpTest(kExprI32x4Add, Add); }

WASM_EXEC_COMPILED_TEST(I32x4Sub) { RunI32x4BinOpTest(kExprI32x4Sub, Sub); }

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64 || SIMD_LOWERING_TARGET
WASM_EXEC_COMPILED_TEST(I32x4Mul) { RunI32x4BinOpTest(kExprI32x4Mul, Mul); }
#endif  // V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64 || SIMD_LOWERING_TARGET

#if V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
WASM_EXEC_COMPILED_TEST(S128And) { RunI32x4BinOpTest(kExprS128And, And); }

WASM_EXEC_COMPILED_TEST(S128Or) { RunI32x4BinOpTest(kExprS128Or, Or); }

WASM_EXEC_COMPILED_TEST(S128Xor) { RunI32x4BinOpTest(kExprS128Xor, Xor); }
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64 || SIMD_LOWERING_TARGET
WASM_EXEC_COMPILED_TEST(I32x4Min) {
  RunI32x4BinOpTest(kExprI32x4MinS, Minimum);
}

WASM_EXEC_COMPILED_TEST(I32x4MaxS) {
  RunI32x4BinOpTest(kExprI32x4MaxS, Maximum);
}

WASM_EXEC_COMPILED_TEST(I32x4MinU) {
  RunI32x4BinOpTest(kExprI32x4MinU, UnsignedMinimum);
}

WASM_EXEC_COMPILED_TEST(I32x4MaxU) {
  RunI32x4BinOpTest(kExprI32x4MaxU, UnsignedMaximum);
}

void RunI32x4CompareOpTest(WasmOpcode simd_op, Int32CompareOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1,
                       WASM_SIMD_MATERIALIZE_BOOLS(
                           32x4, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                                 WASM_GET_LOCAL(simd1)))),
        WASM_SIMD_CHECK_SPLAT4(I32x4, simd1, I32, expected), WASM_ONE);

  FOR_INT32_INPUTS(i) {
    FOR_INT32_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, expected_op(*i, *j))); }
  }
}

WASM_EXEC_COMPILED_TEST(I32x4Eq) { RunI32x4CompareOpTest(kExprI32x4Eq, Equal); }

WASM_EXEC_COMPILED_TEST(I32x4Ne) {
  RunI32x4CompareOpTest(kExprI32x4Ne, NotEqual);
}
#endif  // V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64 || SIMD_LOWERING_TARGET

#if V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
WASM_EXEC_COMPILED_TEST(I32x4LtS) {
  RunI32x4CompareOpTest(kExprI32x4LtS, Less);
}

WASM_EXEC_COMPILED_TEST(I32x4LeS) {
  RunI32x4CompareOpTest(kExprI32x4LeS, LessEqual);
}

WASM_EXEC_COMPILED_TEST(I32x4GtS) {
  RunI32x4CompareOpTest(kExprI32x4GtS, Greater);
}

WASM_EXEC_COMPILED_TEST(I32x4GeS) {
  RunI32x4CompareOpTest(kExprI32x4GeS, GreaterEqual);
}

WASM_EXEC_COMPILED_TEST(I32x4LtU) {
  RunI32x4CompareOpTest(kExprI32x4LtU, UnsignedLess);
}

WASM_EXEC_COMPILED_TEST(I32x4LeU) {
  RunI32x4CompareOpTest(kExprI32x4LeU, UnsignedLessEqual);
}

WASM_EXEC_COMPILED_TEST(I32x4GtU) {
  RunI32x4CompareOpTest(kExprI32x4GtU, UnsignedGreater);
}

WASM_EXEC_COMPILED_TEST(I32x4GeU) {
  RunI32x4CompareOpTest(kExprI32x4GeU, UnsignedGreaterEqual);
}
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64 || SIMD_LOWERING_TARGET
void RunI32x4ShiftOpTest(WasmOpcode simd_op, Int32ShiftOp expected_op,
                         int shift) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte expected = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(
            simd, WASM_SIMD_SHIFT_OP(simd_op, shift, WASM_GET_LOCAL(simd))),
        WASM_SIMD_CHECK_SPLAT4(I32x4, simd, I32, expected), WASM_ONE);

  FOR_INT32_INPUTS(i) { CHECK_EQ(1, r.Call(*i, expected_op(*i, shift))); }
}

WASM_EXEC_COMPILED_TEST(I32x4Shl) {
  RunI32x4ShiftOpTest(kExprI32x4Shl, LogicalShiftLeft, 1);
}

WASM_EXEC_COMPILED_TEST(I32x4ShrS) {
  RunI32x4ShiftOpTest(kExprI32x4ShrS, ArithmeticShiftRight, 1);
}

WASM_EXEC_COMPILED_TEST(I32x4ShrU) {
  RunI32x4ShiftOpTest(kExprI32x4ShrU, LogicalShiftRight, 1);
}
#endif  // V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64 || SIMD_LOWERING_TARGET

#if V8_TARGET_ARCH_ARM
void RunI16x8UnOpTest(WasmOpcode simd_op, Int16UnOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte expected = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd, WASM_SIMD_UNOP(simd_op, WASM_GET_LOCAL(simd))),
        WASM_SIMD_CHECK_SPLAT8(I16x8, simd, I32, expected), WASM_ONE);

  FOR_INT16_INPUTS(i) { CHECK_EQ(1, r.Call(*i, expected_op(*i))); }
}

WASM_EXEC_COMPILED_TEST(I16x8Neg) { RunI16x8UnOpTest(kExprI16x8Neg, Negate); }

void RunI16x8BinOpTest(WasmOpcode simd_op, Int16BinOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                              WASM_GET_LOCAL(simd1))),
        WASM_SIMD_CHECK_SPLAT8(I16x8, simd1, I32, expected), WASM_ONE);

  FOR_INT16_INPUTS(i) {
    FOR_INT16_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, expected_op(*i, *j))); }
  }
}

WASM_EXEC_COMPILED_TEST(I16x8Add) { RunI16x8BinOpTest(kExprI16x8Add, Add); }

WASM_EXEC_COMPILED_TEST(I16x8AddSaturateS) {
  RunI16x8BinOpTest(kExprI16x8AddSaturateS, AddSaturate);
}

WASM_EXEC_COMPILED_TEST(I16x8Sub) { RunI16x8BinOpTest(kExprI16x8Sub, Sub); }

WASM_EXEC_COMPILED_TEST(I16x8SubSaturateS) {
  RunI16x8BinOpTest(kExprI16x8SubSaturateS, SubSaturate);
}

WASM_EXEC_COMPILED_TEST(I16x8Mul) { RunI16x8BinOpTest(kExprI16x8Mul, Mul); }

WASM_EXEC_COMPILED_TEST(I16x8MinS) {
  RunI16x8BinOpTest(kExprI16x8MinS, Minimum);
}

WASM_EXEC_COMPILED_TEST(I16x8MaxS) {
  RunI16x8BinOpTest(kExprI16x8MaxS, Maximum);
}

WASM_EXEC_COMPILED_TEST(I16x8AddSaturateU) {
  RunI16x8BinOpTest(kExprI16x8AddSaturateU, UnsignedAddSaturate);
}

WASM_EXEC_COMPILED_TEST(I16x8SubSaturateU) {
  RunI16x8BinOpTest(kExprI16x8SubSaturateU, UnsignedSubSaturate);
}

WASM_EXEC_COMPILED_TEST(I16x8MinU) {
  RunI16x8BinOpTest(kExprI16x8MinU, UnsignedMinimum);
}

WASM_EXEC_COMPILED_TEST(I16x8MaxU) {
  RunI16x8BinOpTest(kExprI16x8MaxU, UnsignedMaximum);
}

void RunI16x8CompareOpTest(WasmOpcode simd_op, Int16CompareOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1,
                       WASM_SIMD_MATERIALIZE_BOOLS(
                           16x8, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                                 WASM_GET_LOCAL(simd1)))),
        WASM_SIMD_CHECK_SPLAT8(I16x8, simd1, I32, expected), WASM_ONE);

  FOR_INT16_INPUTS(i) {
    FOR_INT16_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, expected_op(*i, *j))); }
  }
}

WASM_EXEC_COMPILED_TEST(I16x8Eq) { RunI16x8CompareOpTest(kExprI16x8Eq, Equal); }

WASM_EXEC_COMPILED_TEST(I16x8Ne) {
  RunI16x8CompareOpTest(kExprI16x8Ne, NotEqual);
}

WASM_EXEC_COMPILED_TEST(I16x8LtS) {
  RunI16x8CompareOpTest(kExprI16x8LtS, Less);
}

WASM_EXEC_COMPILED_TEST(I16x8LeS) {
  RunI16x8CompareOpTest(kExprI16x8LeS, LessEqual);
}

WASM_EXEC_COMPILED_TEST(I16x8GtS) {
  RunI16x8CompareOpTest(kExprI16x8GtS, Greater);
}

WASM_EXEC_COMPILED_TEST(I16x8GeS) {
  RunI16x8CompareOpTest(kExprI16x8GeS, GreaterEqual);
}

WASM_EXEC_COMPILED_TEST(I16x8GtU) {
  RunI16x8CompareOpTest(kExprI16x8GtU, UnsignedGreater);
}

WASM_EXEC_COMPILED_TEST(I16x8GeU) {
  RunI16x8CompareOpTest(kExprI16x8GeU, UnsignedGreaterEqual);
}

WASM_EXEC_COMPILED_TEST(I16x8LtU) {
  RunI16x8CompareOpTest(kExprI16x8LtU, UnsignedLess);
}

WASM_EXEC_COMPILED_TEST(I16x8LeU) {
  RunI16x8CompareOpTest(kExprI16x8LeU, UnsignedLessEqual);
}

void RunI16x8ShiftOpTest(WasmOpcode simd_op, Int16ShiftOp expected_op,
                         int shift) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte expected = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I16x8_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(
            simd, WASM_SIMD_SHIFT_OP(simd_op, shift, WASM_GET_LOCAL(simd))),
        WASM_SIMD_CHECK_SPLAT8(I16x8, simd, I32, expected), WASM_ONE);

  FOR_INT16_INPUTS(i) { CHECK_EQ(1, r.Call(*i, expected_op(*i, shift))); }
}

WASM_EXEC_COMPILED_TEST(I16x8Shl) {
  RunI16x8ShiftOpTest(kExprI16x8Shl, LogicalShiftLeft, 1);
}

WASM_EXEC_COMPILED_TEST(I16x8ShrS) {
  RunI16x8ShiftOpTest(kExprI16x8ShrS, ArithmeticShiftRight, 1);
}

WASM_EXEC_COMPILED_TEST(I16x8ShrU) {
  RunI16x8ShiftOpTest(kExprI16x8ShrU, LogicalShiftRight, 1);
}

void RunI8x16UnOpTest(WasmOpcode simd_op, Int8UnOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte expected = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd, WASM_SIMD_UNOP(simd_op, WASM_GET_LOCAL(simd))),
        WASM_SIMD_CHECK_SPLAT16(I8x16, simd, I32, expected), WASM_ONE);

  FOR_INT8_INPUTS(i) { CHECK_EQ(1, r.Call(*i, expected_op(*i))); }
}

WASM_EXEC_COMPILED_TEST(I8x16Neg) { RunI8x16UnOpTest(kExprI8x16Neg, Negate); }

void RunI8x16BinOpTest(WasmOpcode simd_op, Int8BinOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                              WASM_GET_LOCAL(simd1))),
        WASM_SIMD_CHECK_SPLAT16(I8x16, simd1, I32, expected), WASM_ONE);

  FOR_INT8_INPUTS(i) {
    FOR_INT8_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, expected_op(*i, *j))); }
  }
}

WASM_EXEC_COMPILED_TEST(I8x16Add) { RunI8x16BinOpTest(kExprI8x16Add, Add); }

WASM_EXEC_COMPILED_TEST(I8x16AddSaturateS) {
  RunI8x16BinOpTest(kExprI8x16AddSaturateS, AddSaturate);
}

WASM_EXEC_COMPILED_TEST(I8x16Sub) { RunI8x16BinOpTest(kExprI8x16Sub, Sub); }

WASM_EXEC_COMPILED_TEST(I8x16SubSaturateS) {
  RunI8x16BinOpTest(kExprI8x16SubSaturateS, SubSaturate);
}

WASM_EXEC_COMPILED_TEST(I8x16Mul) { RunI8x16BinOpTest(kExprI8x16Mul, Mul); }

WASM_EXEC_COMPILED_TEST(I8x16MinS) {
  RunI8x16BinOpTest(kExprI8x16MinS, Minimum);
}

WASM_EXEC_COMPILED_TEST(I8x16MaxS) {
  RunI8x16BinOpTest(kExprI8x16MaxS, Maximum);
}

WASM_EXEC_COMPILED_TEST(I8x16AddSaturateU) {
  RunI8x16BinOpTest(kExprI8x16AddSaturateU, UnsignedAddSaturate);
}

WASM_EXEC_COMPILED_TEST(I8x16SubSaturateU) {
  RunI8x16BinOpTest(kExprI8x16SubSaturateU, UnsignedSubSaturate);
}

WASM_EXEC_COMPILED_TEST(I8x16MinU) {
  RunI8x16BinOpTest(kExprI8x16MinU, UnsignedMinimum);
}

WASM_EXEC_COMPILED_TEST(I8x16MaxU) {
  RunI8x16BinOpTest(kExprI8x16MaxU, UnsignedMaximum);
}

void RunI8x16CompareOpTest(WasmOpcode simd_op, Int8CompareOp expected_op) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte b = 1;
  byte expected = 2;
  byte simd0 = r.AllocateLocal(kWasmS128);
  byte simd1 = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd0, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(simd1, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(b))),
        WASM_SET_LOCAL(simd1,
                       WASM_SIMD_MATERIALIZE_BOOLS(
                           8x16, WASM_SIMD_BINOP(simd_op, WASM_GET_LOCAL(simd0),
                                                 WASM_GET_LOCAL(simd1)))),
        WASM_SIMD_CHECK_SPLAT16(I8x16, simd1, I32, expected), WASM_ONE);

  FOR_INT8_INPUTS(i) {
    FOR_INT8_INPUTS(j) { CHECK_EQ(1, r.Call(*i, *j, expected_op(*i, *j))); }
  }
}

WASM_EXEC_COMPILED_TEST(I8x16Eq) { RunI8x16CompareOpTest(kExprI8x16Eq, Equal); }

WASM_EXEC_COMPILED_TEST(I8x16Ne) {
  RunI8x16CompareOpTest(kExprI8x16Ne, NotEqual);
}

WASM_EXEC_COMPILED_TEST(I8x16GtS) {
  RunI8x16CompareOpTest(kExprI8x16GtS, Greater);
}

WASM_EXEC_COMPILED_TEST(I8x16GeS) {
  RunI8x16CompareOpTest(kExprI8x16GeS, GreaterEqual);
}

WASM_EXEC_COMPILED_TEST(I8x16LtS) {
  RunI8x16CompareOpTest(kExprI8x16LtS, Less);
}

WASM_EXEC_COMPILED_TEST(I8x16LeS) {
  RunI8x16CompareOpTest(kExprI8x16LeS, LessEqual);
}

WASM_EXEC_COMPILED_TEST(I8x16GtU) {
  RunI8x16CompareOpTest(kExprI8x16GtU, UnsignedGreater);
}

WASM_EXEC_COMPILED_TEST(I8x16GeU) {
  RunI8x16CompareOpTest(kExprI8x16GeU, UnsignedGreaterEqual);
}

WASM_EXEC_COMPILED_TEST(I8x16LtU) {
  RunI8x16CompareOpTest(kExprI8x16LtU, UnsignedLess);
}

WASM_EXEC_COMPILED_TEST(I8x16LeU) {
  RunI8x16CompareOpTest(kExprI8x16LeU, UnsignedLessEqual);
}

void RunI8x16ShiftOpTest(WasmOpcode simd_op, Int8ShiftOp expected_op,
                         int shift) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);
  byte a = 0;
  byte expected = 1;
  byte simd = r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(simd, WASM_SIMD_I8x16_SPLAT(WASM_GET_LOCAL(a))),
        WASM_SET_LOCAL(
            simd, WASM_SIMD_SHIFT_OP(simd_op, shift, WASM_GET_LOCAL(simd))),
        WASM_SIMD_CHECK_SPLAT16(I8x16, simd, I32, expected), WASM_ONE);

  FOR_INT8_INPUTS(i) { CHECK_EQ(1, r.Call(*i, expected_op(*i, shift))); }
}

WASM_EXEC_COMPILED_TEST(I8x16Shl) {
  RunI8x16ShiftOpTest(kExprI8x16Shl, LogicalShiftLeft, 1);
}

WASM_EXEC_COMPILED_TEST(I8x16ShrS) {
  RunI8x16ShiftOpTest(kExprI8x16ShrS, ArithmeticShiftRight, 1);
}

WASM_EXEC_COMPILED_TEST(I8x16ShrU) {
  RunI8x16ShiftOpTest(kExprI8x16ShrU, LogicalShiftRight, 1);
}
#endif  // V8_TARGET_ARCH_ARM

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64
// Test Select by making a mask where the first two lanes are true and the rest
// false, and comparing for non-equality with zero to materialize a bool vector.
#define WASM_SIMD_SELECT_TEST(format)                                         \
  WASM_EXEC_COMPILED_TEST(S##format##Select) {                                \
    FLAG_wasm_simd_prototype = true;                                          \
    WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);                \
    byte val1 = 0;                                                            \
    byte val2 = 1;                                                            \
    byte src1 = r.AllocateLocal(kWasmS128);                                   \
    byte src2 = r.AllocateLocal(kWasmS128);                                   \
    byte zero = r.AllocateLocal(kWasmS128);                                   \
    byte mask = r.AllocateLocal(kWasmS128);                                   \
    BUILD(r, WASM_SET_LOCAL(                                                  \
                 src1, WASM_SIMD_I##format##_SPLAT(WASM_GET_LOCAL(val1))),    \
          WASM_SET_LOCAL(src2,                                                \
                         WASM_SIMD_I##format##_SPLAT(WASM_GET_LOCAL(val2))),  \
          WASM_SET_LOCAL(zero, WASM_SIMD_I##format##_SPLAT(WASM_ZERO)),       \
          WASM_SET_LOCAL(mask, WASM_SIMD_I##format##_REPLACE_LANE(            \
                                   1, WASM_GET_LOCAL(zero), WASM_I32V(-1))),  \
          WASM_SET_LOCAL(mask, WASM_SIMD_I##format##_REPLACE_LANE(            \
                                   2, WASM_GET_LOCAL(mask), WASM_I32V(-1))),  \
          WASM_SET_LOCAL(                                                     \
              mask,                                                           \
              WASM_SIMD_SELECT(format, WASM_SIMD_BINOP(kExprI##format##Ne,    \
                                                       WASM_GET_LOCAL(mask),  \
                                                       WASM_GET_LOCAL(zero)), \
                               WASM_GET_LOCAL(src1), WASM_GET_LOCAL(src2))),  \
          WASM_SIMD_CHECK_LANE(I##format, mask, I32, val2, 0),                \
          WASM_SIMD_CHECK_LANE(I##format, mask, I32, val1, 1),                \
          WASM_SIMD_CHECK_LANE(I##format, mask, I32, val1, 2),                \
          WASM_SIMD_CHECK_LANE(I##format, mask, I32, val2, 3), WASM_ONE);     \
                                                                              \
    CHECK_EQ(1, r.Call(0x12, 0x34));                                          \
  }

WASM_SIMD_SELECT_TEST(32x4)
#endif  // V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_X64

#if V8_TARGET_ARCH_ARM
WASM_SIMD_SELECT_TEST(16x8)
WASM_SIMD_SELECT_TEST(8x16)

// Boolean unary operations are 'AllTrue' and 'AnyTrue', which return an integer
// result. Use relational ops on numeric vectors to create the boolean vector
// test inputs. Test inputs with all true, all false, one true, and one false.
#define WASM_SIMD_BOOL_REDUCTION_TEST(format, lanes)                           \
  WASM_EXEC_TEST(ReductionTest##lanes) {                                       \
    FLAG_wasm_simd_prototype = true;                                           \
    WasmRunner<int32_t> r(kExecuteCompiled);                                   \
    byte zero = r.AllocateLocal(kWasmS128);                                    \
    byte one_one = r.AllocateLocal(kWasmS128);                                 \
    byte reduced = r.AllocateLocal(kWasmI32);                                  \
    BUILD(r, WASM_SET_LOCAL(zero, WASM_SIMD_I##format##_SPLAT(WASM_ZERO)),     \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AnyTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                      WASM_GET_LOCAL(zero),    \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_EQ(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AnyTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                      WASM_GET_LOCAL(zero),    \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_NE(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AllTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                      WASM_GET_LOCAL(zero),    \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_EQ(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AllTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                      WASM_GET_LOCAL(zero),    \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_NE(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_SET_LOCAL(one_one,                                              \
                         WASM_SIMD_I##format##_REPLACE_LANE(                   \
                             lanes - 1, WASM_GET_LOCAL(zero), WASM_ONE)),      \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AnyTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                      WASM_GET_LOCAL(one_one), \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_EQ(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AnyTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                      WASM_GET_LOCAL(one_one), \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_EQ(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AllTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Eq,      \
                                                      WASM_GET_LOCAL(one_one), \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_NE(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_SET_LOCAL(                                                      \
              reduced, WASM_SIMD_UNOP(kExprS1x##lanes##AllTrue,                \
                                      WASM_SIMD_BINOP(kExprI##format##Ne,      \
                                                      WASM_GET_LOCAL(one_one), \
                                                      WASM_GET_LOCAL(zero)))), \
          WASM_IF(WASM_I32_NE(WASM_GET_LOCAL(reduced), WASM_ZERO),             \
                  WASM_RETURN1(WASM_ZERO)),                                    \
          WASM_ONE);                                                           \
    CHECK_EQ(1, r.Call());                                                     \
  }

WASM_SIMD_BOOL_REDUCTION_TEST(32x4, 4)
WASM_SIMD_BOOL_REDUCTION_TEST(16x8, 8)
WASM_SIMD_BOOL_REDUCTION_TEST(8x16, 16)

#define WASM_SIMD_UNOP_HELPER(format, lanes, lane_size)                        \
  void RunS1x##lanes##UnOpTest(WasmOpcode simd_op,                             \
                               Int##lane_size##UnOp expected_op) {             \
    FLAG_wasm_simd_prototype = true;                                           \
    WasmRunner<int32_t, int32_t, int32_t> r(kExecuteCompiled);                 \
    byte a = 0;                                                                \
    byte expected = 1;                                                         \
    byte zero = r.AllocateLocal(kWasmS128);                                    \
    byte simd = r.AllocateLocal(kWasmS128);                                    \
    BUILD(                                                                     \
        r, WASM_SET_LOCAL(zero, WASM_SIMD_I##format##_SPLAT(WASM_ZERO)),       \
        WASM_SET_LOCAL(simd, WASM_SIMD_I##format##_SPLAT(WASM_GET_LOCAL(a))),  \
        WASM_SET_LOCAL(                                                        \
            simd,                                                              \
            WASM_SIMD_MATERIALIZE_BOOLS(                                       \
                format, WASM_SIMD_UNOP(                                        \
                            simd_op, WASM_SIMD_BINOP(kExprI##format##Ne,       \
                                                     WASM_GET_LOCAL(simd),     \
                                                     WASM_GET_LOCAL(zero))))), \
        WASM_SIMD_CHECK_SPLAT##lanes(I##format, simd, I32, expected),          \
        WASM_ONE);                                                             \
                                                                               \
    for (int i = 0; i <= 1; i++) {                                             \
      CHECK_EQ(1, r.Call(i, expected_op(i)));                                  \
    }                                                                          \
  }
WASM_SIMD_UNOP_HELPER(32x4, 4, 32);
WASM_SIMD_UNOP_HELPER(16x8, 8, 16);
WASM_SIMD_UNOP_HELPER(8x16, 16, 8);
#undef WASM_SIMD_UNOP_HELPER

WASM_EXEC_COMPILED_TEST(S1x4Not) { RunS1x4UnOpTest(kExprS1x4Not, LogicalNot); }

WASM_EXEC_COMPILED_TEST(S1x8Not) { RunS1x8UnOpTest(kExprS1x8Not, LogicalNot); }

WASM_EXEC_COMPILED_TEST(S1x16Not) {
  RunS1x16UnOpTest(kExprS1x16Not, LogicalNot);
}

#define WASM_SIMD_BINOP_HELPER(format, lanes, lane_size)                       \
  void RunS1x##lanes##BinOpTest(WasmOpcode simd_op,                            \
                                Int##lane_size##BinOp expected_op) {           \
    FLAG_wasm_simd_prototype = true;                                           \
    WasmRunner<int32_t, int32_t, int32_t, int32_t> r(kExecuteCompiled);        \
    byte a = 0;                                                                \
    byte b = 1;                                                                \
    byte expected = 2;                                                         \
    byte zero = r.AllocateLocal(kWasmS128);                                    \
    byte simd0 = r.AllocateLocal(kWasmS128);                                   \
    byte simd1 = r.AllocateLocal(kWasmS128);                                   \
    BUILD(                                                                     \
        r, WASM_SET_LOCAL(zero, WASM_SIMD_I##format##_SPLAT(WASM_ZERO)),       \
        WASM_SET_LOCAL(simd0, WASM_SIMD_I##format##_SPLAT(WASM_GET_LOCAL(a))), \
        WASM_SET_LOCAL(simd1, WASM_SIMD_I##format##_SPLAT(WASM_GET_LOCAL(b))), \
        WASM_SET_LOCAL(                                                        \
            simd1,                                                             \
            WASM_SIMD_MATERIALIZE_BOOLS(                                       \
                format,                                                        \
                WASM_SIMD_BINOP(                                               \
                    simd_op,                                                   \
                    WASM_SIMD_BINOP(kExprI##format##Ne, WASM_GET_LOCAL(simd0), \
                                    WASM_GET_LOCAL(zero)),                     \
                    WASM_SIMD_BINOP(kExprI##format##Ne, WASM_GET_LOCAL(simd1), \
                                    WASM_GET_LOCAL(zero))))),                  \
        WASM_SIMD_CHECK_SPLAT##lanes(I##format, simd1, I32, expected),         \
        WASM_ONE);                                                             \
                                                                               \
    for (int i = 0; i <= 1; i++) {                                             \
      for (int j = 0; j <= 1; j++) {                                           \
        CHECK_EQ(1, r.Call(i, j, expected_op(i, j)));                          \
      }                                                                        \
    }                                                                          \
  }

WASM_SIMD_BINOP_HELPER(32x4, 4, 32);
WASM_SIMD_BINOP_HELPER(16x8, 8, 16);
WASM_SIMD_BINOP_HELPER(8x16, 16, 8);
#undef WASM_SIMD_BINOP_HELPER

WASM_EXEC_COMPILED_TEST(S1x4And) { RunS1x4BinOpTest(kExprS1x4And, And); }

WASM_EXEC_COMPILED_TEST(S1x4Or) { RunS1x4BinOpTest(kExprS1x4Or, Or); }

WASM_EXEC_COMPILED_TEST(S1x4Xor) { RunS1x4BinOpTest(kExprS1x4Xor, Xor); }

WASM_EXEC_COMPILED_TEST(S1x8And) { RunS1x8BinOpTest(kExprS1x8And, And); }

WASM_EXEC_COMPILED_TEST(S1x8Or) { RunS1x8BinOpTest(kExprS1x8Or, Or); }

WASM_EXEC_COMPILED_TEST(S1x8Xor) { RunS1x8BinOpTest(kExprS1x8Xor, Xor); }

WASM_EXEC_COMPILED_TEST(S1x16And) { RunS1x16BinOpTest(kExprS1x16And, And); }

WASM_EXEC_COMPILED_TEST(S1x16Or) { RunS1x16BinOpTest(kExprS1x16Or, Or); }

WASM_EXEC_COMPILED_TEST(S1x16Xor) { RunS1x16BinOpTest(kExprS1x16Xor, Xor); }

WASM_EXEC_COMPILED_TEST(SimdI32x4ExtractWithF32x4) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  BUILD(r, WASM_IF_ELSE_I(
               WASM_I32_EQ(WASM_SIMD_I32x4_EXTRACT_LANE(
                               0, WASM_SIMD_F32x4_SPLAT(WASM_F32(30.5))),
                           WASM_I32_REINTERPRET_F32(WASM_F32(30.5))),
               WASM_I32V(1), WASM_I32V(0)));
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdF32x4ExtractWithI32x4) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  BUILD(r,
        WASM_IF_ELSE_I(WASM_F32_EQ(WASM_SIMD_F32x4_EXTRACT_LANE(
                                       0, WASM_SIMD_I32x4_SPLAT(WASM_I32V(15))),
                                   WASM_F32_REINTERPRET_I32(WASM_I32V(15))),
                       WASM_I32V(1), WASM_I32V(0)));
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdF32x4AddWithI32x4) {
  FLAG_wasm_simd_prototype = true;
  // Choose two floating point values whose sum is normal and exactly
  // representable as a float.
  const int kOne = 0x3f800000;
  const int kTwo = 0x40000000;
  WasmRunner<int32_t> r(kExecuteCompiled);
  BUILD(r,
        WASM_IF_ELSE_I(
            WASM_F32_EQ(
                WASM_SIMD_F32x4_EXTRACT_LANE(
                    0, WASM_SIMD_BINOP(kExprF32x4Add,
                                       WASM_SIMD_I32x4_SPLAT(WASM_I32V(kOne)),
                                       WASM_SIMD_I32x4_SPLAT(WASM_I32V(kTwo)))),
                WASM_F32_ADD(WASM_F32_REINTERPRET_I32(WASM_I32V(kOne)),
                             WASM_F32_REINTERPRET_I32(WASM_I32V(kTwo)))),
            WASM_I32V(1), WASM_I32V(0)));
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdI32x4AddWithF32x4) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  BUILD(r,
        WASM_IF_ELSE_I(
            WASM_I32_EQ(
                WASM_SIMD_I32x4_EXTRACT_LANE(
                    0, WASM_SIMD_BINOP(kExprI32x4Add,
                                       WASM_SIMD_F32x4_SPLAT(WASM_F32(21.25)),
                                       WASM_SIMD_F32x4_SPLAT(WASM_F32(31.5)))),
                WASM_I32_ADD(WASM_I32_REINTERPRET_F32(WASM_F32(21.25)),
                             WASM_I32_REINTERPRET_F32(WASM_F32(31.5)))),
            WASM_I32V(1), WASM_I32V(0)));
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdI32x4Local) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(0, WASM_SIMD_I32x4_SPLAT(WASM_I32V(31))),

        WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_GET_LOCAL(0)));
  CHECK_EQ(31, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdI32x4SplatFromExtract) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(0, WASM_SIMD_I32x4_EXTRACT_LANE(
                                 0, WASM_SIMD_I32x4_SPLAT(WASM_I32V(76)))),
        WASM_SET_LOCAL(1, WASM_SIMD_I32x4_SPLAT(WASM_GET_LOCAL(0))),
        WASM_SIMD_I32x4_EXTRACT_LANE(1, WASM_GET_LOCAL(1)));
  CHECK_EQ(76, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdI32x4For) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmS128);
  BUILD(r,

        WASM_SET_LOCAL(1, WASM_SIMD_I32x4_SPLAT(WASM_I32V(31))),
        WASM_SET_LOCAL(1, WASM_SIMD_I32x4_REPLACE_LANE(1, WASM_GET_LOCAL(1),
                                                       WASM_I32V(53))),
        WASM_SET_LOCAL(1, WASM_SIMD_I32x4_REPLACE_LANE(2, WASM_GET_LOCAL(1),
                                                       WASM_I32V(23))),
        WASM_SET_LOCAL(0, WASM_I32V(0)),
        WASM_LOOP(
            WASM_SET_LOCAL(
                1, WASM_SIMD_BINOP(kExprI32x4Add, WASM_GET_LOCAL(1),
                                   WASM_SIMD_I32x4_SPLAT(WASM_I32V(1)))),
            WASM_IF(WASM_I32_NE(WASM_INC_LOCAL(0), WASM_I32V(5)), WASM_BR(1))),
        WASM_SET_LOCAL(0, WASM_I32V(1)),
        WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_GET_LOCAL(1)),
                            WASM_I32V(36)),
                WASM_SET_LOCAL(0, WASM_I32V(0))),
        WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(1, WASM_GET_LOCAL(1)),
                            WASM_I32V(58)),
                WASM_SET_LOCAL(0, WASM_I32V(0))),
        WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(2, WASM_GET_LOCAL(1)),
                            WASM_I32V(28)),
                WASM_SET_LOCAL(0, WASM_I32V(0))),
        WASM_IF(WASM_I32_NE(WASM_SIMD_I32x4_EXTRACT_LANE(3, WASM_GET_LOCAL(1)),
                            WASM_I32V(36)),
                WASM_SET_LOCAL(0, WASM_I32V(0))),
        WASM_GET_LOCAL(0));
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdF32x4For) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  r.AllocateLocal(kWasmI32);
  r.AllocateLocal(kWasmS128);
  BUILD(r, WASM_SET_LOCAL(1, WASM_SIMD_F32x4_SPLAT(WASM_F32(21.25))),
        WASM_SET_LOCAL(1, WASM_SIMD_F32x4_REPLACE_LANE(3, WASM_GET_LOCAL(1),
                                                       WASM_F32(19.5))),
        WASM_SET_LOCAL(0, WASM_I32V(0)),
        WASM_LOOP(
            WASM_SET_LOCAL(
                1, WASM_SIMD_BINOP(kExprF32x4Add, WASM_GET_LOCAL(1),
                                   WASM_SIMD_F32x4_SPLAT(WASM_F32(2.0)))),
            WASM_IF(WASM_I32_NE(WASM_INC_LOCAL(0), WASM_I32V(3)), WASM_BR(1))),
        WASM_SET_LOCAL(0, WASM_I32V(1)),
        WASM_IF(WASM_F32_NE(WASM_SIMD_F32x4_EXTRACT_LANE(0, WASM_GET_LOCAL(1)),
                            WASM_F32(27.25)),
                WASM_SET_LOCAL(0, WASM_I32V(0))),
        WASM_IF(WASM_F32_NE(WASM_SIMD_F32x4_EXTRACT_LANE(3, WASM_GET_LOCAL(1)),
                            WASM_F32(25.5)),
                WASM_SET_LOCAL(0, WASM_I32V(0))),
        WASM_GET_LOCAL(0));
  CHECK_EQ(1, r.Call());
}

WASM_EXEC_COMPILED_TEST(SimdI32x4GetGlobal) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t> r(kExecuteCompiled);
  int32_t* global = r.module().AddGlobal<int32_t>(kWasmS128);
  *(global) = 0;
  *(global + 1) = 1;
  *(global + 2) = 2;
  *(global + 3) = 3;
  r.AllocateLocal(kWasmI32);
  BUILD(
      r, WASM_SET_LOCAL(1, WASM_I32V(1)),
      WASM_IF(WASM_I32_NE(WASM_I32V(0),
                          WASM_SIMD_I32x4_EXTRACT_LANE(0, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_IF(WASM_I32_NE(WASM_I32V(1),
                          WASM_SIMD_I32x4_EXTRACT_LANE(1, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_IF(WASM_I32_NE(WASM_I32V(2),
                          WASM_SIMD_I32x4_EXTRACT_LANE(2, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_IF(WASM_I32_NE(WASM_I32V(3),
                          WASM_SIMD_I32x4_EXTRACT_LANE(3, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_GET_LOCAL(1));
  CHECK_EQ(1, r.Call(0));
}

WASM_EXEC_COMPILED_TEST(SimdI32x4SetGlobal) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t> r(kExecuteCompiled);
  int32_t* global = r.module().AddGlobal<int32_t>(kWasmS128);
  BUILD(r, WASM_SET_GLOBAL(0, WASM_SIMD_I32x4_SPLAT(WASM_I32V(23))),
        WASM_SET_GLOBAL(0, WASM_SIMD_I32x4_REPLACE_LANE(1, WASM_GET_GLOBAL(0),
                                                        WASM_I32V(34))),
        WASM_SET_GLOBAL(0, WASM_SIMD_I32x4_REPLACE_LANE(2, WASM_GET_GLOBAL(0),
                                                        WASM_I32V(45))),
        WASM_SET_GLOBAL(0, WASM_SIMD_I32x4_REPLACE_LANE(3, WASM_GET_GLOBAL(0),
                                                        WASM_I32V(56))),
        WASM_I32V(1));
  CHECK_EQ(1, r.Call(0));
  CHECK_EQ(*global, 23);
  CHECK_EQ(*(global + 1), 34);
  CHECK_EQ(*(global + 2), 45);
  CHECK_EQ(*(global + 3), 56);
}

WASM_EXEC_COMPILED_TEST(SimdF32x4GetGlobal) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t> r(kExecuteCompiled);
  float* global = r.module().AddGlobal<float>(kWasmS128);
  *(global) = 0.0;
  *(global + 1) = 1.5;
  *(global + 2) = 2.25;
  *(global + 3) = 3.5;
  r.AllocateLocal(kWasmI32);
  BUILD(
      r, WASM_SET_LOCAL(1, WASM_I32V(1)),
      WASM_IF(WASM_F32_NE(WASM_F32(0.0),
                          WASM_SIMD_F32x4_EXTRACT_LANE(0, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_IF(WASM_F32_NE(WASM_F32(1.5),
                          WASM_SIMD_F32x4_EXTRACT_LANE(1, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_IF(WASM_F32_NE(WASM_F32(2.25),
                          WASM_SIMD_F32x4_EXTRACT_LANE(2, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_IF(WASM_F32_NE(WASM_F32(3.5),
                          WASM_SIMD_F32x4_EXTRACT_LANE(3, WASM_GET_GLOBAL(0))),
              WASM_SET_LOCAL(1, WASM_I32V(0))),
      WASM_GET_LOCAL(1));
  CHECK_EQ(1, r.Call(0));
}

WASM_EXEC_COMPILED_TEST(SimdF32x4SetGlobal) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t, int32_t> r(kExecuteCompiled);
  float* global = r.module().AddGlobal<float>(kWasmS128);
  BUILD(r, WASM_SET_GLOBAL(0, WASM_SIMD_F32x4_SPLAT(WASM_F32(13.5))),
        WASM_SET_GLOBAL(0, WASM_SIMD_F32x4_REPLACE_LANE(1, WASM_GET_GLOBAL(0),
                                                        WASM_F32(45.5))),
        WASM_SET_GLOBAL(0, WASM_SIMD_F32x4_REPLACE_LANE(2, WASM_GET_GLOBAL(0),
                                                        WASM_F32(32.25))),
        WASM_SET_GLOBAL(0, WASM_SIMD_F32x4_REPLACE_LANE(3, WASM_GET_GLOBAL(0),
                                                        WASM_F32(65.0))),
        WASM_I32V(1));
  CHECK_EQ(1, r.Call(0));
  CHECK_EQ(*global, 13.5);
  CHECK_EQ(*(global + 1), 45.5);
  CHECK_EQ(*(global + 2), 32.25);
  CHECK_EQ(*(global + 3), 65.0);
}

WASM_EXEC_COMPILED_TEST(SimdLoadStoreLoad) {
  FLAG_wasm_simd_prototype = true;
  WasmRunner<int32_t> r(kExecuteCompiled);
  int32_t* memory = r.module().AddMemoryElems<int32_t>(4);

  BUILD(r,
        WASM_STORE_MEM(MachineType::Simd128(), WASM_ZERO,
                       WASM_LOAD_MEM(MachineType::Simd128(), WASM_ZERO)),
        WASM_SIMD_I32x4_EXTRACT_LANE(
            0, WASM_LOAD_MEM(MachineType::Simd128(), WASM_ZERO)));

  FOR_INT32_INPUTS(i) {
    int32_t expected = *i;
    r.module().WriteMemory(&memory[0], expected);
    CHECK_EQ(expected, r.Call());
  }
}
#endif  // V8_TARGET_ARCH_ARM || SIMD_LOWERING_TARGET
