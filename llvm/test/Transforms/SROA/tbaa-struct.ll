; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -S -sroa %s | FileCheck %s

; SROA should keep `!tbaa.struct` metadata

%vector = type { float, float }
declare void @llvm.memcpy.p0i8.p0i8.i64(i8* writeonly, i8* readonly, i64, i1 immarg)
declare <2 x float> @foo(%vector* %0)

define void @bar(%vector* %y2) {
; CHECK-LABEL: @bar(
; CHECK-NEXT:    [[X14:%.*]] = call <2 x float> @foo(%vector* [[Y2:%.*]])
; CHECK-NEXT:    [[X7_SROA_0_0_X18_SROA_CAST:%.*]] = bitcast %vector* [[Y2]] to <2 x float>*
; CHECK-NEXT:    store <2 x float> [[X14]], <2 x float>* [[X7_SROA_0_0_X18_SROA_CAST]], align 4, !tbaa.struct !0
; CHECK-NEXT:    ret void
;
  %x7 = alloca %vector
  %x14 = call <2 x float> @foo(%vector* %y2)
  %x15 = bitcast %vector* %x7 to <2 x float>*
  store <2 x float> %x14, <2 x float>* %x15
  %x19 = bitcast %vector* %x7 to i8*
  %x18 = bitcast %vector* %y2 to i8*
  call void @llvm.memcpy.p0i8.p0i8.i64(i8* align 4 %x18, i8* align 4 %x19, i64 8, i1 false), !tbaa.struct !10
  ret void
}

!4 = !{!"omnipotent char", !5, i64 0}
!5 = !{!"Simple C++ TBAA"}
!7 = !{!"vector", !8, i64 0, !8, i64 4}
!8 = !{!"float", !4, i64 0}
!10 = !{i64 0, i64 4, !11, i64 4, i64 4, !11}
!11 = !{!8, !8, i64 0}
