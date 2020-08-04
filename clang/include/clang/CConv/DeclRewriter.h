//=--DeclRewriter.h-----------------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file contains the DeclRewriter class which is used to rewrite variable
// declarations in a program using the checked pointers types solved for by the
// the conversion tool.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CCONV_DECLREWRITER_H
#define LLVM_CLANG_LIB_CCONV_DECLREWRITER_H

#include "clang/AST/Decl.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/ASTContext.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/CConv/RewriteUtils.h"

using namespace llvm;
using namespace clang;

class DeclRewriter {
public:
  DeclRewriter(Rewriter &R, ASTContext &A, GlobalVariableGroups &GP)
      : R(R), A(A), GP(GP), Skip(DComp(A.getSourceManager())) {}

  // The publicly accessible interface for performing declaration rewriting.
  // All declarations for variables with checked types in the variable map of
  // Info parameter are rewritten.
  static void rewriteDecls(ASTContext &Context, ProgramInfo &Info, Rewriter &R,
                           std::set<FileID> &TouchedFiles);
private:
  Rewriter &R;
  ASTContext &A;
  GlobalVariableGroups &GP;
  // Skip indicates some rewrites that we should skip because they have already
  // been applied. This is used when rewriting a single declaration that
  // declares multiple variables into multiple declarations that each declare
  // one variable.
  RSet Skip;

  // TODO: I don't like having this be static, but it needs to be static in
  //       order to pass information between different translation units. A
  //       new instance of this class (and the RewriteConsumer class) is created
  //       for each translation unit.
  static std::map<std::string, std::string> NewFuncSig;

  // Visit each Decl in ToRewrite and apply the appropriate pointer type
  // to that Decl. ToRewrite is the set of all declarations to rewrite.
  // TouchedFiles is used to collect and return the set of FileId's that have
  // edited. This is later used to know which files need to be updated.
  void rewrite(RSet &ToRewrite, std::set<FileID> &TouchedFiles);

  // Rewrite a specific variable declaration using the replacement string in the
  // DAndReplace structure. Each of these functions is specialized to handling
  // one subclass of declarations.
  void rewriteVarDecl(const DAndReplace &N, RSet &ToRewrite);
  void rewriteParmVarDecl(const DAndReplace &N);
  void rewriteFunctionDecl(const DAndReplace &N);

  unsigned int getParameterIndex(ParmVarDecl *PV, FunctionDecl *FD);
  SourceLocation deleteAllDeclarationsOnLine(VarDecl *VD, DeclStmt *Stmt);
  void getDeclsOnSameLine(VarDecl *VD, DeclStmt *Stmt, std::set<Decl *> &Decls);
  bool isSingleDeclaration(VarDecl *VD, DeclStmt *Stmt);
  bool areDeclarationsOnSameLine(VarDecl *VD1, DeclStmt *Stmt1, VarDecl *VD2,
                                 DeclStmt *Stmt2);
};
#endif //LLVM_CLANG_LIB_CCONV_DECLREWRITER_H
