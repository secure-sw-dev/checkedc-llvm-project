; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=thumbv7m-arm-none-eabi -mattr=+execute-only,+fp-armv8 | FileCheck %s --check-prefixes=CHECK,VMOVSR
; RUN: llc < %s -mtriple=thumbv7m-arm-none-eabi -mattr=+execute-only,+fp-armv8,+neon,+neonfp | FileCheck %s --check-prefixes=CHECK,NEON

define arm_aapcs_vfpcc float @foo0(float %a0) local_unnamed_addr {
; CHECK-LABEL: foo0:
; CHECK:       @ %bb.0:
; CHECK-NEXT:    vcmp.f32 s0, #0
; CHECK-NEXT:    vmov.f32 s2, #5.000000e-01
; CHECK-NEXT:    vmrs APSR_nzcv, fpscr
; CHECK-NEXT:    vmov.f32 s4, #-5.000000e-01
; CHECK-NEXT:    it mi
; CHECK-NEXT:    vmovmi.f32 s2, s4
; CHECK-NEXT:    vmov.f32 s0, s2
; CHECK-NEXT:    bx lr
  %1 = fcmp nsz olt float %a0, 0.000000e+00
  %2 = select i1 %1, float -5.000000e-01, float 5.000000e-01
  ret float %2
}

define arm_aapcs_vfpcc float @float1(float %a0) local_unnamed_addr {
; CHECK-LABEL: float1:
; CHECK:       @ %bb.0: @ %.end
; CHECK-NEXT:    vmov.f32 s2, #1.000000e+00
; CHECK-NEXT:    vmov.f32 s4, #5.000000e-01
; CHECK-NEXT:    vmov.f32 s6, #-5.000000e-01
; CHECK-NEXT:    vcmp.f32 s2, s0
; CHECK-NEXT:    vmrs APSR_nzcv, fpscr
; CHECK-NEXT:    vselgt.f32 s0, s6, s4
; CHECK-NEXT:    bx lr
  br i1 undef, label %.end, label %1

  %2 = fcmp nsz olt float %a0, 1.000000e+00
  %3 = select i1 %2, float -5.000000e-01, float 5.000000e-01
  br label %.end

.end:
  %4 = phi float [ undef, %0 ], [ %3, %1]
  ret float %4
}

define arm_aapcs_vfpcc float @float128(float %a0) local_unnamed_addr {
; VMOVSR-LABEL: float128:
; VMOVSR:       @ %bb.0:
; VMOVSR-NEXT:    mov.w r0, #1124073472
; VMOVSR-NEXT:    vmov.f32 s4, #5.000000e-01
; VMOVSR-NEXT:    vmov s2, r0
; VMOVSR-NEXT:    vmov.f32 s6, #-5.000000e-01
; VMOVSR-NEXT:    vcmp.f32 s2, s0
; VMOVSR-NEXT:    vmrs APSR_nzcv, fpscr
; VMOVSR-NEXT:    vselgt.f32 s0, s6, s4
; VMOVSR-NEXT:    bx lr
;
; NEON-LABEL: float128:
; NEON:       @ %bb.0:
; NEON-NEXT:    mov.w r0, #1124073472
; NEON-NEXT:    vmov.f32 s2, #5.000000e-01
; NEON-NEXT:    vmov d3, r0, r0
; NEON-NEXT:    vmov.f32 s4, #-5.000000e-01
; NEON-NEXT:    vcmp.f32 s6, s0
; NEON-NEXT:    vmrs APSR_nzcv, fpscr
; NEON-NEXT:    vselgt.f32 s0, s4, s2
; NEON-NEXT:    bx lr
  %1 = fcmp nsz olt float %a0, 128.000000e+00
  %2 = select i1 %1, float -5.000000e-01, float 5.000000e-01
  ret float %2
}

define arm_aapcs_vfpcc double @double1(double %a0) local_unnamed_addr {
; CHECK-LABEL: double1:
; CHECK:       @ %bb.0:
; CHECK-NEXT:    vmov.f64 d18, #1.000000e+00
; CHECK-NEXT:    vcmp.f64 d18, d0
; CHECK-NEXT:    vmrs APSR_nzcv, fpscr
; CHECK-NEXT:    vmov.f64 d16, #5.000000e-01
; CHECK-NEXT:    vmov.f64 d17, #-5.000000e-01
; CHECK-NEXT:    vselgt.f64 d0, d17, d16
; CHECK-NEXT:    bx lr
  %1 = fcmp nsz olt double %a0, 1.000000e+00
  %2 = select i1 %1, double -5.000000e-01, double 5.000000e-01
  ret double %2
}

define arm_aapcs_vfpcc double @double128(double %a0) local_unnamed_addr {
; CHECK-LABEL: double128:
; CHECK:       @ %bb.0:
; CHECK-NEXT:    movs r0, #0
; CHECK-NEXT:    movs r1, #0
; CHECK-NEXT:    movt r0, #16480
; CHECK-NEXT:    vmov.f64 d16, #5.000000e-01
; CHECK-NEXT:    vmov d18, r1, r0
; CHECK-NEXT:    vcmp.f64 d18, d0
; CHECK-NEXT:    vmrs APSR_nzcv, fpscr
; CHECK-NEXT:    vmov.f64 d17, #-5.000000e-01
; CHECK-NEXT:    vselgt.f64 d0, d17, d16
; CHECK-NEXT:    bx lr
  %1 = fcmp nsz olt double %a0, 128.000000e+00
  %2 = select i1 %1, double -5.000000e-01, double 5.000000e-01
  ret double %2
}
