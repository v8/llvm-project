// NOTE: Assertions have been autogenerated by utils/update_cc_test_checks.py
// RUN: %clang_cc1 -triple thumbv8.1m.main-arm-none-eabi -target-feature +mve.fp -mfloat-abi hard -O0 -disable-O0-optnone -S -emit-llvm -o - %s | opt -S -mem2reg | FileCheck %s
// RUN: %clang_cc1 -triple thumbv8.1m.main-arm-none-eabi -target-feature +mve.fp -mfloat-abi hard -O0 -disable-O0-optnone -DPOLYMORPHIC -S -emit-llvm -o - %s | opt -S -mem2reg | FileCheck %s

#include <arm_mve.h>

// CHECK-LABEL: @test_vaddq_u32(
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[TMP0:%.*]] = add <4 x i32> [[A:%.*]], [[B:%.*]]
// CHECK-NEXT:    ret <4 x i32> [[TMP0]]
//
uint32x4_t test_vaddq_u32(uint32x4_t a, uint32x4_t b)
{
#ifdef POLYMORPHIC
    return vaddq(a, b);
#else /* POLYMORPHIC */
    return vaddq_u32(a, b);
#endif /* POLYMORPHIC */
}

// CHECK-LABEL: @test_vsubq_f16(
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[TMP0:%.*]] = fsub <8 x half> [[A:%.*]], [[B:%.*]]
// CHECK-NEXT:    ret <8 x half> [[TMP0]]
//
float16x8_t test_vsubq_f16(float16x8_t a, float16x8_t b)
{
#ifdef POLYMORPHIC
    return vsubq(a, b);
#else /* POLYMORPHIC */
    return vsubq_f16(a, b);
#endif /* POLYMORPHIC */
}

// CHECK-LABEL: @test_vaddq_m_s8(
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[TMP0:%.*]] = zext i16 [[P:%.*]] to i32
// CHECK-NEXT:    [[TMP1:%.*]] = call <16 x i1> @llvm.arm.mve.pred.i2v.v16i1(i32 [[TMP0]])
// CHECK-NEXT:    [[TMP2:%.*]] = call <16 x i8> @llvm.arm.mve.add.predicated.v16i8.v16i1(<16 x i8> [[A:%.*]], <16 x i8> [[B:%.*]], <16 x i1> [[TMP1]], <16 x i8> [[INACTIVE:%.*]])
// CHECK-NEXT:    ret <16 x i8> [[TMP2]]
//
int8x16_t test_vaddq_m_s8(int8x16_t inactive, int8x16_t a, int8x16_t b, mve_pred16_t p)
{
#ifdef POLYMORPHIC
    return vaddq_m(inactive, a, b, p);
#else /* POLYMORPHIC */
    return vaddq_m_s8(inactive, a, b, p);
#endif /* POLYMORPHIC */
}

// CHECK-LABEL: @test_vsubq_m_f32(
// CHECK-NEXT:  entry:
// CHECK-NEXT:    [[TMP0:%.*]] = zext i16 [[P:%.*]] to i32
// CHECK-NEXT:    [[TMP1:%.*]] = call <4 x i1> @llvm.arm.mve.pred.i2v.v4i1(i32 [[TMP0]])
// CHECK-NEXT:    [[TMP2:%.*]] = call <4 x float> @llvm.arm.mve.sub.predicated.v4f32.v4i1(<4 x float> [[A:%.*]], <4 x float> [[B:%.*]], <4 x i1> [[TMP1]], <4 x float> [[INACTIVE:%.*]])
// CHECK-NEXT:    ret <4 x float> [[TMP2]]
//
float32x4_t test_vsubq_m_f32(float32x4_t inactive, float32x4_t a, float32x4_t b, mve_pred16_t p)
{
#ifdef POLYMORPHIC
    return vsubq_m(inactive, a, b, p);
#else /* POLYMORPHIC */
    return vsubq_m_f32(inactive, a, b, p);
#endif /* POLYMORPHIC */
}
