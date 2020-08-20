//=--CastPlacement.cpp---------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This class contains implementation of the functions and
// classes of CastPlacement.h
//===----------------------------------------------------------------------===//

#include <clang/Tooling/Refactoring/SourceCode.h>
#include "clang/CConv/ConstraintResolver.h"
#include "clang/CConv/CastPlacement.h"
#include "clang/CConv/CCGlobalOptions.h"

using namespace clang;

bool CastPlacementVisitor::VisitCallExpr(CallExpr *CE) {
  Decl *D = CE->getCalleeDecl();
  if (D != nullptr && Rewriter::isRewritable(CE->getExprLoc())) {
    PersistentSourceLoc PL = PersistentSourceLoc::mkPSL(CE, *Context);
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      // Get the constraint variable for the function.
      std::set<FVConstraint *> *V = Info.getFuncConstraints(FD, Context);
      // Function has no definition i.e., external function.
      assert("Function has no definition" && V != nullptr);

      // Did we see this function in another file?
      auto Fname = FD->getNameAsString();
      if (!V->empty() && !ConstraintResolver::canFunctionBeSkipped(Fname)) {
        // Get the FV constraint for the Callee.
        FVConstraint *FV = *(V->begin());
        // Now we need to check the type of the arguments and corresponding
        // parameters to see if any explicit casting is needed.
        if (FV) {
          ProgramInfo::CallTypeParamBindingsT TypeVars;
          if (Info.hasTypeParamBindings(CE, Context))
            TypeVars = Info.getTypeParamBindings(CE, Context);
          auto PInfo = Info.get_MF()[Fname];
          unsigned PIdx = 0;
          for (const auto &A : CE->arguments()) {
            if (PIdx < FD->getNumParams()) {

              // Avoid adding incorrect casts to generic function arguments by
              // removing implicit casts when on arguments with a consistently
              // used generic type.
              Expr *ArgExpr = A;
              const TypeVariableType
                  *TyVar = getTypeVariableType(FD->getParamDecl(PIdx));
              if (TyVar && TypeVars.find(TyVar->GetIndex()) != TypeVars.end()
                  && TypeVars[TyVar->GetIndex()] != nullptr)
                ArgExpr = ArgExpr->IgnoreImpCasts();

              CVarSet ArgumentConstraints = CR.getExprConstraintVars(ArgExpr);
              CVarSet &ParameterConstraints = FV->getParamVar(PIdx);
              for (auto *ArgumentC : ArgumentConstraints) {
                bool CastInserted = false;
                for (auto *ParameterC : ParameterConstraints) {
                  auto Dinfo = PIdx < PInfo.size() ? PInfo[PIdx] : CHECKED;
                  if (needCasting(ArgumentC, ParameterC, Dinfo)) {
                    // We expect the cast string to end with "(".
                    std::string CastString =
                        getCastString(ArgumentC, ParameterC, Dinfo);
                    surroundByCast(CastString, A);
                    CastInserted = true;
                    break;
                  }
                }
                // If we have already inserted a cast, then break.
                if (CastInserted) break;
              }
            }
            PIdx++;
          }
        }
      }
    }
  }
  return true;
}

  
// Check whether an explicit casting is needed when the pointer represented
// by src variable is assigned to dst.
bool CastPlacementVisitor::needCasting(ConstraintVariable *Src,
                                       ConstraintVariable *Dst,
                                       IsChecked Dinfo) {
  auto &E = Info.getConstraints().getVariables();
  // Check if the src is a checked type.
  if (Src->isChecked(E)) {
    // If Dst has an itype, Src must have exactly the same checked type. If this
    // is not the case, we must insert a case.
    if (Dst->hasItype())
      return !Dst->solutionEqualTo(Info.getConstraints(), Src);

    // Is Dst Wild?
    if (!Dst->isChecked(E) || Dinfo == WILD)
      return true;
  }
  return false;
}

// Get the type name to insert for casting.
std::string CastPlacementVisitor::getCastString(ConstraintVariable *Src,
                                                ConstraintVariable *Dst,
                                                IsChecked Dinfo) {
  assert(needCasting(Src, Dst, Dinfo) && "No casting needed.");
  return "((" + Dst->getRewritableOriginalTy() + ")";
}

void CastPlacementVisitor::surroundByCast(const std::string &CastPrefix,
                                          Expr *E) {
  if (Writer.InsertTextAfterToken(E->getEndLoc(), ")")) {
    // This means we failed to insert the text at the end of the RHS.
    // This can happen because of Macro expansion.
    // We will see if this is a single expression statement?
    // If yes, then we will use parent statement to add ")"
    auto CRA = CharSourceRange::getTokenRange(E->getSourceRange());
    auto NewCRA = clang::Lexer::makeFileCharRange(CRA,
                                                  Context->getSourceManager(),
                                                  Context->getLangOpts());
    std::string SrcText = clang::tooling::getText(CRA, *Context);
    // Only insert if there is anything to write.
    if (!SrcText.empty())
      Writer.ReplaceText(NewCRA, CastPrefix + SrcText + ")");
  } else {
    Writer.InsertTextBefore(E->getBeginLoc(), CastPrefix);
  }
}