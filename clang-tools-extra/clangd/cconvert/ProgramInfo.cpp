//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Implementation of ProgramInfo methods.
//===----------------------------------------------------------------------===//
#include "ProgramInfo.h"
#include "MappingVisitor.h"
#include "ConstraintBuilder.h"
#include "CCGlobalOptions.h"
#include "llvm/ADT/StringSwitch.h"
#include "clang/Lex/Lexer.h"
#include <sstream>

using namespace clang;

ProgramInfo::ProgramInfo() :
  freeKey(0), persisted(true) {
  ArrBoundsInfo = new ArrayBoundsInformation(*this);
  OnDemandFuncDeclConstraint.clear();
}

void ProgramInfo::print(raw_ostream &O) const {
  CS.print(O);
  O << "\n";

  O << "Constraint Variables\n";
  for( const auto &I : Variables ) {
    PersistentSourceLoc L = I.first;
    const std::set<ConstraintVariable*> &S = I.second;
    L.print(O);
    O << "=>";
    for(const auto &J : S) {
      O << "[ ";
      J->print(O);
      O << " ]";
    }
    O << "\n";
  }

  O << "Dummy Declaration Constraint Variables\n";
  for(const auto &declCons: OnDemandFuncDeclConstraint) {
    O << "Func Name:" << declCons.first << " => ";
    const std::set<ConstraintVariable*> &S = declCons.second;
    for(const auto &J : S) {
      O << "[ ";
      J->print(O);
      O << " ]";
    }
    O << "\n";
  }
}

void ProgramInfo::dump_json(llvm::raw_ostream &O) const {
  O << "{\"Setup\":";
  CS.dump_json(O);
  // dump the constraint variables.
  O << ", \"ConstraintVariables\":[";
  bool addComma = false;
  for( const auto &I : Variables ) {
    if(addComma) {
      O << ",\n";
    }
    PersistentSourceLoc L = I.first;
    const std::set<ConstraintVariable*> &S = I.second;

    O << "{\"line\":\"";
    L.print(O);
    O << "\",";
    O << "\"Variables\":[";
    bool addComma1 = false;
    for(const auto &J : S) {
      if(addComma1) {
        O << ",";
      }
      J->dump_json(O);
      addComma1 = true;
    }
    O << "]";
    O << "}";
    addComma = true;
  }
  O << "]";
  // dump on demand constraints
  O << ", \"DummyFunctionConstraints\":[";
  addComma = false;
  for(const auto &declCons: OnDemandFuncDeclConstraint) {
    if(addComma) {
      O << ",";
    }
    O << "{\"functionName\":\"" << declCons.first << "\"";
    O << ", \"Constraints\":[";
    const std::set<ConstraintVariable*> &S = declCons.second;
    bool addComma1 = false;
    for(const auto &J : S) {
      if(addComma1) {
        O << ",";
      }
      J->dump_json(O);
      addComma1 = true;
    }
    O << "]}";
    addComma = true;
    O << "\n";
  }
  O << "]";
  O << "}";
}

// Given a ConstraintVariable V, retrieve all of the unique
// constraint variables used by V. If V is just a 
// PointerVariableConstraint, then this is just the contents 
// of 'vars'. If it either has a function pointer, or V is
// a function, then recurses on the return and parameter
// constraints.
static
CVars getVarsFromConstraint(ConstraintVariable *V, CVars T) {
  CVars R = T;

  if (PVConstraint *PVC = dyn_cast<PVConstraint>(V)) {
    R.insert(PVC->getCvars().begin(), PVC->getCvars().end());
   if (FVConstraint *FVC = PVC->getFV()) 
     return getVarsFromConstraint(FVC, R);
  } else if (FVConstraint *FVC = dyn_cast<FVConstraint>(V)) {
    for (const auto &C : FVC->getReturnVars()) {
      CVars tmp = getVarsFromConstraint(C, R);
      R.insert(tmp.begin(), tmp.end());
    }
    for (unsigned i = 0; i < FVC->numParams(); i++) {
      for (const auto &C : FVC->getParamVar(i)) {
        CVars tmp = getVarsFromConstraint(C, R);
        R.insert(tmp.begin(), tmp.end());
      }
    }
  }

  return R;
}

// Print out statistics of constraint variables on a per-file basis.
void ProgramInfo::print_stats(std::set<std::string> &F, raw_ostream &O, bool onlySummary) {
  if(!onlySummary) {
    O << "Enable itype propagation:" << enablePropThruIType << "\n";
    O << "Merge multiple function declaration:" << !seperateMultipleFuncDecls << "\n";
    O << "Sound handling of var args functions:" << handleVARARGS << "\n";
  }
  std::map<std::string, std::tuple<int, int, int, int, int> > filesToVars;
  Constraints::EnvironmentMap env = CS.getVariables();
  unsigned int totC, totP, totNt, totA, totWi;
  totC = totP = totNt = totA = totWi = 0;

  // First, build the map and perform the aggregation.
  for (auto &I : Variables) {
    std::string fileName = I.first.getFileName();
    if (F.count(fileName)) {
      int varC = 0;
      int pC = 0;
      int ntAC = 0;
      int aC = 0;
      int wC = 0;

      auto J = filesToVars.find(fileName);
      if (J != filesToVars.end())
        std::tie(varC, pC, ntAC, aC, wC) = J->second;

      CVars foundVars;
      for (auto &C : I.second) {
        CVars tmp = getVarsFromConstraint(C, foundVars);
        foundVars.insert(tmp.begin(), tmp.end());
        }

      varC += foundVars.size();
      for (const auto &N : foundVars) {
        VarAtom *V = CS.getVar(N);
        assert(V != nullptr);
        auto K = env.find(V);
        assert(K != env.end());

        ConstAtom *CA = K->second;
        switch (CA->getKind()) {
          case Atom::A_Arr:
            aC += 1;
            break;
          case Atom::A_NTArr:
            ntAC += 1;
            break;
          case Atom::A_Ptr:
            pC += 1;
            break;
          case Atom::A_Wild:
            wC += 1;
            break;
          case Atom::A_Var:
          case Atom::A_Const:
            llvm_unreachable("bad constant in environment map");
        }
      }

      filesToVars[fileName] = std::tuple<int, int, int, int, int>(varC, pC, ntAC, aC, wC);
    }
  }

  // Then, dump the map to output.
  // if not only summary then dump everything.
  if (!onlySummary) {
    O << "file|#constraints|#ptr|#ntarr|#arr|#wild\n";
  }
  for (const auto &I : filesToVars) {
    int v, p, nt, a, w;
    std::tie(v, p, nt, a, w) = I.second;

    totC += v;
    totP += p;
    totNt += nt;
    totA += a;
    totWi += w;
    if (!onlySummary) {
      O << I.first << "|" << v << "|" << p << "|" << nt << "|" << a << "|" << w;
      O << "\n";
    }
  }

  O << "Summary\nTotalConstraints|TotalPtrs|TotalNTArr|TotalArr|TotalWild\n";
  O << totC << "|" << totP << "|" << totNt << "|" << totA << "|" << totWi << "\n";

}

// Check the equality of VTy and UTy. There are some specific rules that
// fire, and a general check is yet to be implemented. 
bool ProgramInfo::checkStructuralEquality(std::set<ConstraintVariable*> V, 
                                          std::set<ConstraintVariable*> U,
                                          QualType VTy,
                                          QualType UTy) 
{
  // First specific rule: Are these types directly equal? 
  if (VTy == UTy) {
    return true;
  } else {
    // Further structural checking is TODO.
    return false;
  } 
}

bool ProgramInfo::checkStructuralEquality(QualType D, QualType S) {
  if (D == S)
    return true;

  return D->isPointerType() == S->isPointerType();
}

bool ProgramInfo::isExplicitCastSafe(clang::QualType dstType,
                                     clang::QualType srcType) {

  // check if both types are same.
  if (srcType == dstType)
    return true;

  const clang::Type *srcTypePtr = srcType.getTypePtr();
  const clang::Type *dstTypePtr = dstType.getTypePtr();

  const clang::PointerType *srcPtrTypePtr = dyn_cast<PointerType>(srcTypePtr);
  const clang::PointerType *dstPtrTypePtr = dyn_cast<PointerType>(dstTypePtr);
  // both are pointers? check their pointee
  if (srcPtrTypePtr && dstPtrTypePtr)
    return isExplicitCastSafe(dstPtrTypePtr->getPointeeType(), srcPtrTypePtr->getPointeeType());
  // only one of them is pointer?
  if (srcPtrTypePtr || dstPtrTypePtr)
    return false;

  // if both are not scalar types? Then the types must be exactly same.
  if (!(srcTypePtr->isScalarType() && dstTypePtr->isScalarType()))
    return srcTypePtr == dstTypePtr;

  // check if both types are compatible.
  unsigned bothNotChar = srcTypePtr->isCharType() ^ dstTypePtr->isCharType();
  unsigned bothNotInt = srcTypePtr->isIntegerType() ^ dstTypePtr->isIntegerType();
  unsigned bothNotFloat = srcTypePtr->isFloatingType() ^ dstTypePtr->isFloatingType();


  return !(bothNotChar || bothNotInt || bothNotFloat);
}

bool ProgramInfo::isExternOkay(std::string ext) {
  return llvm::StringSwitch<bool>(ext)
    .Cases("malloc", "free", true)
    .Default(false);
}

bool ProgramInfo::link() {
  // For every global symbol in all the global symbols that we have found
  // go through and apply rules for whether they are functions or variables.
  if (Verbose)
    llvm::errs() << "Linking!\n";

  // Multiple Variables can be at the same PersistentSourceLoc. We should
  // constrain that everything that is at the same location is explicitly
  // equal.
  for (const auto &V : Variables) {
    std::set<ConstraintVariable*> C = V.second;

    if (C.size() > 1) {
      std::set<ConstraintVariable*>::iterator I = C.begin();
      std::set<ConstraintVariable*>::iterator J = C.begin();
      ++J;

      while (J != C.end()) {
        constrainEq(*I, *J, *this, nullptr, nullptr);
        ++I;
        ++J;
      }
    }
  }

  // equate the constraints for all global variables. This is needed for variables
  // that are defined as extern.
  for (const auto &V: GlobalVariableSymbols) {
    const std::set<PVConstraint*> &C = V.second;

    if (C.size() > 1) {
      std::set<PVConstraint*>::iterator I = C.begin();
      std::set<PVConstraint*>::iterator J = C.begin();
      ++J;
      if (Verbose)
        llvm::errs() << "Global variables:" << V.first << "\n";
      while (J != C.end()) {
        constrainEq(*I, *J, *this, nullptr, nullptr);
        ++I;
        ++J;
      }
    }
  }

  if (!seperateMultipleFuncDecls) {
      int gap = 0;
      for (const auto &S : GlobalFunctionSymbols) {
          std::string fname = S.first;
          std::set<FVConstraint*> P = S.second;

          if (P.size() > 1) {
              std::set<FVConstraint*>::iterator I = P.begin();
              std::set<FVConstraint*>::iterator J = P.begin();
              ++J;

              while (J != P.end()) {
                  FVConstraint *P1 = *I;
                  FVConstraint *P2 = *J;

                  if (P2->hasBody()) { // skip over decl with fun body
                       gap = 1; ++J; continue;
                  }
                  // Constrain the return values to be equal
                  if (!P1->hasBody() && !P2->hasBody()) {
                      constrainEq(P1->getReturnVars(), P2->getReturnVars(), *this, nullptr, nullptr);

                      // Constrain the parameters to be equal, if the parameter arity is
                      // the same. If it is not the same, constrain both to be wild.
                      if (P1->numParams() == P2->numParams()) {
                          for ( unsigned i = 0;
                                i < P1->numParams();
                                i++)
                          {
                              constrainEq(P1->getParamVar(i), P2->getParamVar(i), *this, nullptr, nullptr);
                          }

                      } else {
                          // It could be the case that P1 or P2 is missing a prototype, in
                          // which case we don't need to constrain anything.
                          if (P1->hasProtoType() && P2->hasProtoType()) {
                              // Nope, we have no choice. Constrain everything to wild.
                              std::string rsn = "Return value of function:" + P1->getName();
                              P1->constrainTo(CS, CS.getWild(), rsn, true);
                              P2->constrainTo(CS, CS.getWild(), rsn, true);
                          }
                      }
                  }
                  ++I;
                  if (!gap) {
                      ++J;
                  }
                  else {
                      gap = 0;
                  }
              }
          }
      }
  }


  // For every global function that is an unresolved external, constrain 
  // its parameter types to be wild. Unless it has a bounds-safe annotation. 
  for (const auto &U : ExternFunctions) {
    // If we've seen this symbol, but never seen a body for it, constrain
    // everything about it.
    if (U.second == false && isExternOkay(U.first) == false) {
      // Some global symbols we don't need to constrain to wild, like 
      // malloc and free. Check those here and skip if we find them. 
      std::string UnkSymbol = U.first;
      std::map<std::string, std::set<FVConstraint*> >::iterator I =
          GlobalFunctionSymbols.find(UnkSymbol);
      assert(I != GlobalFunctionSymbols.end());
      const std::set<FVConstraint*> &Gs = (*I).second;

      for (const auto &G : Gs) {
        for (const auto &U : G->getReturnVars()) {
          std::string rsn = "Return value of function:" + (*I).first;
          U->constrainTo(CS, CS.getWild(), rsn, true);
        }

        std::string rsn = "Inner pointer of a parameter to external function.";
        for (unsigned i = 0; i < G->numParams(); i++)
          for (const auto &PVar : G->getParamVar(i)) {
            if (PVConstraint *PVC = dyn_cast<PVConstraint>(PVar)) {
              // remove the first constraint var and make all the internal
              // constraint vars WILD. For more details, refer Section 5.3 of
              // http://www.cs.umd.edu/~mwh/papers/checkedc-incr.pdf
              CVars C = PVC->getCvars();
              if (!C.empty())
                C.erase(C.begin());
              for (auto cVar: C)
                CS.addConstraint(CS.createEq(CS.getVar(cVar), CS.getWild(), rsn));
            } else {
              PVar->constrainTo(CS, CS.getWild(), rsn,true);
            }
          }
      }
    }
  }

  return true;
}

void ProgramInfo::seeFunctionDecl(FunctionDecl *F, ASTContext *C) {
  if (!F->isGlobal())
    return;

  // Track if we've seen a body for this function or not.
  std::string fn = F->getNameAsString();
  if (!ExternFunctions[fn])
    ExternFunctions[fn] = (F->isThisDeclarationADefinition() && F->hasBody());
  
  // Add this to the map of global symbols. 
  std::set<FVConstraint*> toAdd;
  // get the constraint variable directly.
  std::set<ConstraintVariable*> K;
  VariableMap::iterator I = Variables.find(PersistentSourceLoc::mkPSL(F, *C));
  if (I != Variables.end()) {
    K = I->second;
  }
  for (const auto &J : K)
    if(FVConstraint *FJ = dyn_cast<FVConstraint>(J))
      toAdd.insert(FJ);

  assert(toAdd.size() > 0);

  std::map<std::string, std::set<FVConstraint*> >::iterator it =
      GlobalFunctionSymbols.find(fn);
  
  if (it == GlobalFunctionSymbols.end()) {
    GlobalFunctionSymbols.insert(std::pair<std::string, std::set<FVConstraint*> >
      (fn, toAdd));
  } else {
    (*it).second.insert(toAdd.begin(), toAdd.end());
  }

  // Look up the constraint variables for the return type and parameter 
  // declarations of this function, if any.
  /*
  std::set<uint32_t> returnVars;
  std::vector<std::set<uint32_t> > parameterVars(F->getNumParams());
  PersistentSourceLoc PLoc = PersistentSourceLoc::mkPSL(F, *C);
  int i = 0;

  std::set<ConstraintVariable*> FV = getVariable(F, C);
  assert(FV.size() == 1);
  const ConstraintVariable *PFV = (*(FV.begin()));
  assert(PFV != NULL);
  const FVConstraint *FVC = dyn_cast<FVConstraint>(PFV);
  assert(FVC != NULL);

  //returnVars = FVC->getReturnVars();
  //unsigned i = 0;
  //for (unsigned i = 0; i < FVC->numParams(); i++) {
  //  parameterVars.push_back(FVC->getParamVar(i));
  //}

  assert(PLoc.valid());
  GlobalFunctionSymbol *GF = 
    new GlobalFunctionSymbol(fn, PLoc, parameterVars, returnVars);

  // Add this to the map of global symbols. 
  std::map<std::string, std::set<GlobalSymbol*> >::iterator it = 
    GlobalFunctionSymbols.find(fn);
  
  if (it == GlobalFunctionSymbols.end()) {
    std::set<GlobalSymbol*> N;
    N.insert(GF);
    GlobalFunctionSymbols.insert(std::pair<std::string, std::set<GlobalSymbol*> >
      (fn, N));
  } else {
    (*it).second.insert(GF);
  }*/
}

void ProgramInfo::seeGlobalDecl(clang::VarDecl *G, ASTContext *C) {
  std::string variableName = G->getName();

  // Add this to the map of global symbols.
  std::set<PVConstraint*> toAdd;
  // get the constraint variable directly.
  std::set<ConstraintVariable*> K;
  VariableMap::iterator I = Variables.find(PersistentSourceLoc::mkPSL(G, *C));
  if (I != Variables.end()) {
    K = I->second;
  }
  for (const auto &J : K)
    if(PVConstraint *FJ = dyn_cast<PVConstraint>(J))
      toAdd.insert(FJ);

  assert(toAdd.size() > 0);

  if (GlobalVariableSymbols.find(variableName) != GlobalVariableSymbols.end()) {
    GlobalVariableSymbols[variableName].insert(toAdd.begin(), toAdd.end());
  } else {
    GlobalVariableSymbols[variableName] = toAdd;
  }

}

// Populate Variables, VarDeclToStatement, RVariables, and DepthMap with
// AST data structures that correspond do the data stored in PDMap and
// ReversePDMap.
void ProgramInfo::enterCompilationUnit(ASTContext &Context) {
  assert(persisted == true);
  // Get a set of all of the PersistentSourceLoc's we need to fill in
  std::set<PersistentSourceLoc> P;
  //for (auto I : PersistentVariables)
  //  P.insert(I.first);

  // Resolve the PersistentSourceLoc to one of Decl,Stmt,Type.
  MappingVisitor V(P, Context);
  TranslationUnitDecl *TUD = Context.getTranslationUnitDecl();
  for (const auto &D : TUD->decls())
    V.TraverseDecl(D);

  persisted = false;
  return;
}

// Remove any references we maintain to AST data structure pointers.
// After this, the Variables, VarDeclToStatement, RVariables, and DepthMap
// should all be empty.
void ProgramInfo::exitCompilationUnit() {
  assert(persisted == false);
  persisted = true;
  return;
}

template <typename T>
bool ProgramInfo::hasConstraintType(std::set<ConstraintVariable*> &S) {
  for (const auto &I : S) {
    if (isa<T>(I)) {
      return true;
    }
  }
  return false;
}

// For each pointer type in the declaration of D, add a variable to the
// constraint system for that pointer type.
bool ProgramInfo::addVariable(DeclaratorDecl *D, DeclStmt *St, ASTContext *C) {
  assert(persisted == false);
  PersistentSourceLoc PLoc = 
    PersistentSourceLoc::mkPSL(D, *C);
  assert(PLoc.valid());
  // What is the nature of the constraint that we should be adding? This is 
  // driven by the type of Decl. 
  //  - Decl is a pointer-type VarDecl - we will add a PVConstraint
  //  - Decl has type Function - we will add a FVConstraint
  //  If Decl is both, then we add both. If it has neither, then we add
  //  neither.
  // We only add a PVConstraint or an FVConstraint if the set at 
  // Variables[PLoc] does not contain one already. This allows either 
  // PVConstraints or FVConstraints declared at the same physical location
  // in the program to implicitly alias.

  const Type *Ty = nullptr;
  if (VarDecl *VD = dyn_cast<VarDecl>(D))
    Ty = VD->getTypeSourceInfo()->getTypeLoc().getTypePtr();
  else if (FieldDecl *FD = dyn_cast<FieldDecl>(D))
    Ty = FD->getTypeSourceInfo()->getTypeLoc().getTypePtr();
  else if (FunctionDecl *UD = dyn_cast<FunctionDecl>(D))
    Ty = UD->getTypeSourceInfo()->getTypeLoc().getTypePtr();
  else
    llvm_unreachable("unknown decl type");
  
  FVConstraint *F = nullptr;
  PVConstraint *P = nullptr;
  
  if (Ty->isPointerType() || Ty->isArrayType()) 
    // Create a pointer value for the type.
    P = new PVConstraint(D, freeKey, CS, *C);

  // Only create a function type if the type is a base Function type. The case
  // for creating function pointers is handled above, with a PVConstraint that
  // contains a FVConstraint.
  if (Ty->isFunctionType()) 
    // Create a function value for the type.
    F = new FVConstraint(D, freeKey, CS, *C);

  std::set<ConstraintVariable*> &S = Variables[PLoc];
  bool newFunction = false;

  if(F != nullptr && !hasConstraintType<FVConstraint>(S)) {
    // insert the function constraint only if it doesn't exist
    newFunction = true;
    S.insert(F);

    // if this is a function. Save the created constraint.
    // this needed for resolving function subtypes later.
    // we create a unique key for the declaration and definition
    // of a function.
    // We save the mapping between these unique keys.
    // This is needed so that later when we have to
    // resolve function subtyping. where for each function
    // we need access to teh definition and declaration
    // constraint variables.
    FunctionDecl *UD = dyn_cast<FunctionDecl>(D);
    std::string funcKey =  getUniqueDeclKey(UD, C);
    // this is a definition. Create a constraint variable
    // and save the mapping between defintion and declaration.
    if(UD->isThisDeclarationADefinition() && UD->hasBody()) {
      CS.getFuncDefnVarMap()[funcKey].insert(F);
      // this is a definition.
      // get the declartion and store the unique key mapping
      FunctionDecl *FDecl = getDeclaration(UD);
      if(FDecl != nullptr) {
        std::string fDeclKey = getUniqueDeclKey(FDecl, C);
        CS.getFuncDefnDeclMap().set(funcKey, fDeclKey);
      }
    } else {
      // this is a declaration, just save the constraint variable.
      CS.getFuncDeclVarMap()[funcKey].insert(F);
    }
  }

  if(P != nullptr && !hasConstraintType<PVConstraint>(S)) {
    // if there is no pointer constraint in this location
    // insert it.
    S.insert(P);
  }

  // Did we create a function and it is a newly added function
  if (F && newFunction) {
    // If we did, then we need to add some additional stuff to Variables. 
    //  * A mapping from the parameters PLoc to the constraint variables for
    //    the parameters.
    FunctionDecl *FD = dyn_cast<FunctionDecl>(D);
    assert(FD != nullptr);
    // We just created this, so they should be equal.
    assert(FD->getNumParams() == F->numParams());
    for (unsigned i = 0; i < FD->getNumParams(); i++) {
      ParmVarDecl *PVD = FD->getParamDecl(i);
      std::set<ConstraintVariable*> S = F->getParamVar(i); 
      if (S.size()) {
        PersistentSourceLoc PSL = PersistentSourceLoc::mkPSL(PVD, *C);
        Variables[PSL].insert(S.begin(), S.end());
      }
    }
  }

  // The Rewriter won't let us re-write things that are in macros. So, we 
  // should check to see if what we just added was defined within a macro.
  // If it was, we should constrain it to top. This is sad. Hopefully, 
  // someday, the Rewriter will become less lame and let us re-write stuff
  // in macros.
  std::string pointerInMacro = "Pointer in Macro declaration.";
  if (!Rewriter::isRewritable(D->getLocation())) 
    for (const auto &C : S)
      C->constrainTo(CS, CS.getWild(), pointerInMacro);

  return true;
}

// This is a bit of a hack. What we need to do is traverse the AST in a
// bottom-up manner, and, for a given expression, decide which singular,
// if any, constraint variable is involved in that expression. However,
// in the current version of clang (3.8.1), bottom-up traversal is not
// supported. So instead, we do a manual top-down traversal, considering
// the different cases and their meaning on the value of the constraint
// variable involved. This is probably incomplete, but, we're going to
// go with it for now.
//
// V is (currentVariable, baseVariable, limitVariable)
// E is an expression to recursively traverse.
//
// Returns true if E resolves to a constraint variable q_i and the
// currentVariable field of V is that constraint variable. Returns false if
// a constraint variable cannot be found.
// ifc mirrors the inFunctionContext boolean parameter to getVariable. 
std::set<ConstraintVariable *> 
ProgramInfo::getVariableHelper( Expr                            *E,
                                std::set<ConstraintVariable *>  V,
                                ASTContext                      *C,
                                bool                            ifc)
{
  E = E->IgnoreParenImpCasts();
  if (DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(E)) {
    return getVariable(DRE->getDecl(), C, ifc);
  } else if (MemberExpr *ME = dyn_cast<MemberExpr>(E)) {
    return getVariable(ME->getMemberDecl(), C, ifc);
  } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(E)) {
    std::set<ConstraintVariable*> T1 = getVariableHelper(BO->getLHS(), V, C, ifc);
    std::set<ConstraintVariable*> T2 = getVariableHelper(BO->getRHS(), V, C, ifc);
    T1.insert(T2.begin(), T2.end());
    return T1;
  } else if (ArraySubscriptExpr *AE = dyn_cast<ArraySubscriptExpr>(E)) {
    // In an array subscript, we want to do something sort of similar to taking
    // the address or doing a dereference. 
    std::set<ConstraintVariable *> T = getVariableHelper(AE->getBase(), V, C, ifc);
    std::set<ConstraintVariable*> tmp;
    for (const auto &CV : T) {
      if (PVConstraint *PVC = dyn_cast<PVConstraint>(CV)) {
        // Subtract one from this constraint. If that generates an empty 
        // constraint, then, don't add it 
        std::set<uint32_t> C = PVC->getCvars();
        if(C.size() > 0) {
          C.erase(C.begin());
          if (C.size() > 0) {
            bool a = PVC->getArrPresent();
            bool c = PVC->getItypePresent();
            std::string d = PVC->getItype();
            FVConstraint *b = PVC->getFV();
            tmp.insert(new PVConstraint(C, PVC->getTy(), PVC->getName(), b, a, c, d));
          }
        }
      }
    }

    T.swap(tmp);
    return T;
  } else if (UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    std::set<ConstraintVariable *> T = 
      getVariableHelper(UO->getSubExpr(), V, C, ifc);
   
    std::set<ConstraintVariable*> tmp;
    if (UO->getOpcode() == UO_Deref) {
      for (const auto &CV : T) {
        if (PVConstraint *PVC = dyn_cast<PVConstraint>(CV)) {
          // Subtract one from this constraint. If that generates an empty 
          // constraint, then, don't add it 
          std::set<uint32_t> C = PVC->getCvars();
          if(C.size() > 0) {
            C.erase(C.begin());
            if (C.size() > 0) {
              bool a = PVC->getArrPresent();
              FVConstraint *b = PVC->getFV();
              bool c = PVC->getItypePresent();
              std::string d = PVC->getItype();
              tmp.insert(new PVConstraint(C, PVC->getTy(), PVC->getName(), b, a, c, d));
            }
          }
        } else {
          llvm_unreachable("Shouldn't dereference a function pointer!");
        }
      }
      T.swap(tmp);
    }

    return T;
  } else if (ImplicitCastExpr *IE = dyn_cast<ImplicitCastExpr>(E)) {
    return getVariableHelper(IE->getSubExpr(), V, C, ifc);
  } else if (ExplicitCastExpr *ECE = dyn_cast<ExplicitCastExpr>(E)) {
    return getVariableHelper(ECE->getSubExpr(), V, C, ifc);
  } else if (ParenExpr *PE = dyn_cast<ParenExpr>(E)) {
    return getVariableHelper(PE->getSubExpr(), V, C, ifc);
  } else if (CHKCBindTemporaryExpr *CBE = dyn_cast<CHKCBindTemporaryExpr>(E)) {
    return getVariableHelper(CBE->getSubExpr(), V, C, ifc);
  } else if (CallExpr *CE = dyn_cast<CallExpr>(E)) {
    // call expression should always get out-of context
    // constraint variable.
    ifc = false;
    // Here, we need to look up the target of the call and return the
    // constraints for the return value of that function.
    Decl *D = CE->getCalleeDecl();
    if (D == nullptr) {
      // There are a few reasons that we couldn't get a decl. For example,
      // the call could be done through an array subscript. 
      Expr *CalledExpr = CE->getCallee();
      std::set<ConstraintVariable*> tmp = getVariableHelper(CalledExpr, V, C, ifc);
      std::set<ConstraintVariable*> T;

      for (ConstraintVariable *C : tmp) {
        if (FVConstraint *FV = dyn_cast<FVConstraint>(C)) {
          T.insert(FV->getReturnVars().begin(), FV->getReturnVars().end());
        } else if(PVConstraint *PV = dyn_cast<PVConstraint>(C)) {
          if (FVConstraint *FV = PV->getFV()) {
            T.insert(FV->getReturnVars().begin(), FV->getReturnVars().end());
          }
        }
      }

      return T;
    }
    assert(D != nullptr);
    // D could be a FunctionDecl, or a VarDecl, or a FieldDecl. 
    // Really it could be any DeclaratorDecl. 
    if (DeclaratorDecl *FD = dyn_cast<DeclaratorDecl>(D)) {
      std::set<ConstraintVariable*> CS = getVariable(FD, C, ifc);
      std::set<ConstraintVariable*> TR;
      FVConstraint *FVC = nullptr;
      for (const auto &J : CS) {
        if (FVConstraint *tmp = dyn_cast<FVConstraint>(J))
          // The constraint we retrieved is a function constraint already.
          // This happens if what is being called is a reference to a 
          // function declaration, but it isn't all that can happen.
          FVC = tmp;
        else if (PVConstraint *tmp = dyn_cast<PVConstraint>(J))
          if (FVConstraint *tmp2 = tmp->getFV())
            // Or, we could have a PVConstraint to a function pointer. 
            // In that case, the function pointer value will work just
            // as well.
            FVC = tmp2;
      }

      if (FVC) {
        TR.insert(FVC->getReturnVars().begin(), FVC->getReturnVars().end());
      } else {
        // Our options are slim. For some reason, we have failed to find a 
        // FVConstraint for the Decl that we are calling. This can't be good
        // so we should constrain everything in the caller to top. We can
        // fake this by returning a nullary-ish FVConstraint and that will
        // make the logic above us freak out and over-constrain everything.
        TR.insert(new FVConstraint()); 
      }

      return TR;
    } else {
      // If it ISN'T, though... what to do? How could this happen?
      llvm_unreachable("TODO");
    }
  } else if (ConditionalOperator *CO = dyn_cast<ConditionalOperator>(E)) {
    // Explore the three exprs individually.
    std::set<ConstraintVariable*> T;
    std::set<ConstraintVariable*> R;
    T = getVariableHelper(CO->getCond(), V, C, ifc);
    R.insert(T.begin(), T.end());
    T = getVariableHelper(CO->getLHS(), V, C, ifc);
    R.insert(T.begin(), T.end());
    T = getVariableHelper(CO->getRHS(), V, C, ifc);
    R.insert(T.begin(), T.end());
    return R;
  } else if(StringLiteral *exr = dyn_cast<StringLiteral>(E)) {
    // if this is a string literal. i.e., "foo"
    // we create a new constraint variable and constraint it to an Nt_array
    std::set<ConstraintVariable *> T;
    // create a new constraint var number.
    CVars V;
    V.insert(freeKey);
    CS.getOrCreateVar(freeKey);
    freeKey++;
    ConstraintVariable *newC = new PointerVariableConstraint(V, "const char*", exr->getBytes(),
                                                             nullptr, false, false, "");
    // constraint the newly created variable to NTArray.
    newC->constrainTo(CS, CS.getNTArr());
    T.insert(newC);
    return T;

  } else {
    return std::set<ConstraintVariable*>();
  }
}

std::map<std::string, std::set<ConstraintVariable*>>& ProgramInfo::getOnDemandFuncDeclConstraintMap() {
  return OnDemandFuncDeclConstraint;
}

std::string ProgramInfo::getUniqueDeclKey(Decl *decl, ASTContext *C) {
  auto Psl = PersistentSourceLoc::mkPSL(decl, *C);
  std::string fileName = Psl.getFileName() + ":" + std::to_string(Psl.getLineNo());
  std::string name = decl->getDeclKindName();
  if(FunctionDecl *FD = dyn_cast<FunctionDecl>(decl)) {
    name = FD->getNameAsString();
  }
  std::string declKey = fileName + ":" + name;
  return declKey;
}

std::string ProgramInfo::getUniqueFuncKey(FunctionDecl *funcDecl, ASTContext *C) {
  // get unique key for a function: which is function name, file and line number
  if(FunctionDecl *funcDefn = getDefinition(funcDecl)) {
    funcDecl = funcDefn;
  }
  return getUniqueDeclKey(funcDecl, C);
}

std::set<ConstraintVariable*>&
ProgramInfo::getOnDemandFuncDeclarationConstraint(FunctionDecl *targetFunc, ASTContext *C) {
  std::string declKey = getUniqueFuncKey(targetFunc, C);
  if(OnDemandFuncDeclConstraint.find(declKey) == OnDemandFuncDeclConstraint.end()) {
    const Type *Ty = targetFunc->getTypeSourceInfo()->getTypeLoc().getTypePtr();
    assert (!(Ty->isPointerType() || Ty->isArrayType()) && "");
    assert(Ty->isFunctionType() && "");
    FVConstraint *F = new FVConstraint(targetFunc, freeKey, CS, *C);
    OnDemandFuncDeclConstraint[declKey].insert(F);
    // insert into declaration map.
    CS.getFuncDeclVarMap()[declKey].insert(F);
  }
  return OnDemandFuncDeclConstraint[declKey];
}

std::set<ConstraintVariable*>&
ProgramInfo::getFuncDefnConstraints(FunctionDecl *targetFunc, ASTContext *C) {
  std::string funcKey = getUniqueDeclKey(targetFunc, C);

  if(targetFunc->isThisDeclarationADefinition() && targetFunc->hasBody()) {
    return CS.getFuncDefnVarMap()[funcKey];
  } else {
    // if this is function declaration? see if we have definition.
    // have we seen a definition of this function?
    if (CS.getFuncDefnDeclMap().hasValue(funcKey)) {
      auto fdefKey = *(CS.getFuncDefnDeclMap().valueMap().at(funcKey).begin());
      return  CS.getFuncDefnVarMap()[fdefKey];
    }
    return CS.getFuncDeclVarMap()[funcKey];
  }
}

std::set<ConstraintVariable*>
ProgramInfo::getVariable(clang::Decl *D, clang::ASTContext *C, FunctionDecl *FD, int parameterIndex) {
  // if this is a parameter.
  if(parameterIndex >= 0) {
    // get the parameter index of the
    // requested function declaration
    D = FD->getParamDecl(parameterIndex);
  } else {
    // this is the return value of the function
    D = FD;
  }
  VariableMap::iterator I = Variables.find(PersistentSourceLoc::mkPSL(D, *C));
  assert(I != Variables.end());
  return I->second;

}

std::set<ConstraintVariable*>
ProgramInfo::getVariable(clang::Decl *D, clang::ASTContext *C, bool inFunctionContext) {
  // here, we auto-correct the inFunctionContext flag.
  // if someone is asking for in context variable of a function
  // always give the declaration context.

  // if this a function declaration
  // set in context to false.
  if(dyn_cast<FunctionDecl>(D)) {
    inFunctionContext = false;
  }
  return getVariableOnDemand(D, C, inFunctionContext);
}

// Given a decl, return the variables for the constraints of the Decl.
std::set<ConstraintVariable*>
ProgramInfo::getVariableOnDemand(Decl *D, ASTContext *C, bool inFunctionContext) {
  assert(persisted == false);
  VariableMap::iterator I = Variables.find(PersistentSourceLoc::mkPSL(D, *C));
  if (I != Variables.end()) {
    // If we are looking up a variable, and that variable is a parameter variable,
    // or return value
    // then we should see if we're looking this up in the context of a function or
    // not. If we are not, then we should find a declaration
    ParmVarDecl *PD = nullptr;
    FunctionDecl *funcDefinition = nullptr;
    FunctionDecl *funcDeclaration = nullptr;
    // get the function declaration and definition
    if(D != nullptr && dyn_cast<FunctionDecl>(D)) {
      funcDeclaration = getDeclaration(dyn_cast<FunctionDecl>(D));
      funcDefinition = getDefinition(dyn_cast<FunctionDecl>(D));
    }
    int parameterIndex = -1;
    if(PD = dyn_cast<ParmVarDecl>(D)) {
      // okay, we got a request for a parameter
      DeclContext *DC = PD->getParentFunctionOrMethod();
      assert(DC != nullptr);
      FunctionDecl *FD = dyn_cast<FunctionDecl>(DC);
      // get the parameter index with in the function.
      for (unsigned i = 0; i < FD->getNumParams(); i++) {
        const ParmVarDecl *tmp = FD->getParamDecl(i);
        if (tmp == D) {
          parameterIndex = i;
          break;
        }
      }

      // get declaration and definition
      funcDeclaration = getDeclaration(FD);
      funcDefinition = getDefinition(FD);
      
      // if this is an external function and we are unable
      // to find the body. Get the FD object from the parameter.
      if(!funcDefinition && !funcDeclaration) {
        funcDeclaration = FD;
      }
      assert(parameterIndex >= 0 && "Got request for invalid parameter");
    }
    if(funcDeclaration || funcDefinition || parameterIndex != -1) {
      // if we are asking for the constraint variable of a function
      // and that function is an external function.
      // then use declaration.
      if(dyn_cast<FunctionDecl>(D) && funcDefinition == nullptr) {
        funcDefinition = funcDeclaration;
      }
      // this means either we got a
      // request for function return value or parameter
      if(inFunctionContext) {
        assert(funcDefinition != nullptr && "Requesting for in-context constraints, "
                                            "but there is no definition for this function");
        // return the constraint variable
        // that belongs to the function definition.
        return getVariable(D, C, funcDefinition, parameterIndex);
      } else {
        if(funcDeclaration == nullptr) {
          // we need constraint variable
          // with in the function declaration,
          // but there is no declaration
          // get on demand declaration.
          std::set<ConstraintVariable*> &fvConstraints = getOnDemandFuncDeclarationConstraint(funcDefinition, C);
          if(parameterIndex != -1) {
            // this is a parameter.
            std::set<ConstraintVariable*> parameterConstraints;
            parameterConstraints.clear();
            assert(fvConstraints.size() && "Unable to find on demand fv constraints.");
            // get all parameters from all the FVConstraints.
            for(auto fv: fvConstraints) {
              auto currParamConstraint = (dyn_cast<FunctionVariableConstraint>(fv))->getParamVar(parameterIndex);
              parameterConstraints.insert(currParamConstraint.begin(), currParamConstraint.end());
            }
            return parameterConstraints;
          }
          return fvConstraints;
        } else {
          // return the variable with in
          // the function declaration
          return getVariable(D, C, funcDeclaration, parameterIndex);
        }
      }
      // we got a request for function return or parameter
      // but we failed to handle the request.
      assert(false && "Invalid state reached.");
    }
    // neither parameter or return value.
    // just return the original constraint.
    return I->second;
  } else {
    return std::set<ConstraintVariable*>();
  }
}
// Given some expression E, what is the top-most constraint variable that
// E refers to? It could be none, in which case the returned set is empty. 
// Otherwise, the returned setcontains the constraint variable(s) that E 
// refers to.
std::set<ConstraintVariable*>
ProgramInfo::getVariable(Expr *E, ASTContext *C, bool inFunctionContext) {
  assert(persisted == false);

  // Get the constraint variables represented by this Expr
  std::set<ConstraintVariable*> T;
  if (E)
    return getVariableHelper(E, T, C, inFunctionContext);
  else
    return T;
}

VariableMap &ProgramInfo::getVarMap() {
  return Variables;
}

bool ProgramInfo::isAValidPVConstraint(ConstraintVariable *C) {
  if (C != nullptr) {
    if (PVConstraint *PV = dyn_cast<PVConstraint>(C))
      return !PV->getCvars().empty();
  }
  return false;
}

std::set<ConstraintVariable*> *ProgramInfo::getFuncDeclConstraintSet(std::string funcDefKey) {
  std::set<ConstraintVariable*> *declCVarsPtr = nullptr;
  auto &defnDeclKeyMap = CS.getFuncDefnDeclMap();
  auto &declConstrains = CS.getFuncDeclVarMap();
  // see if we do not have constraint variables for declaration
  if(defnDeclKeyMap.hasKey(funcDefKey)) {
    auto funcDeclKey = defnDeclKeyMap.keyMap().at(funcDefKey);
    // if this has a declaration constraint?
    // then fetch the constraint.
    if(declConstrains.find(funcDeclKey) != declConstrains.end()) {
      declCVarsPtr = &(declConstrains[funcDeclKey]);
    }
  } else {
    // no? then check the ondemand declarations
    auto &onDemandMap = getOnDemandFuncDeclConstraintMap();
    if(onDemandMap.find(funcDefKey) != onDemandMap.end()) {
      declCVarsPtr = &(onDemandMap[funcDefKey]);
    }
  }
  return declCVarsPtr;
}

bool ProgramInfo::applySubtypingRelation(ConstraintVariable *srcCVar, ConstraintVariable *dstCVar) {
  bool retVal = false;
  PVConstraint *pvSrc = dyn_cast<PVConstraint>(srcCVar);
  PVConstraint *pvDst = dyn_cast<PVConstraint>(dstCVar);

  if (!pvSrc->getCvars().empty() && !pvDst->getCvars().empty()) {

    CVars srcCVars(pvSrc->getCvars());
    CVars dstCVars(pvDst->getCvars());

    // cvars adjustment!
    // if the number of CVars is different, then adjust the number
    // of cvars to be same.
    if (srcCVars.size() != dstCVars.size()) {
      auto &bigCvars = srcCVars;
      auto &smallCvars = dstCVars;
      if (srcCVars.size() < dstCVars.size()) {
        bigCvars = dstCVars;
        smallCvars = srcCVars;
      }

      while (bigCvars.size() > smallCvars.size())
        bigCvars.erase(*(bigCvars.begin()));
    }

    // function subtyping only applies for the top level pointer.
    ConstAtom *outerMostSrcVal = CS.getAssignment(*srcCVars.begin());
    ConstAtom *outputMostDstVal = CS.getAssignment(*dstCVars.begin());

    if (*outputMostDstVal < *outerMostSrcVal) {
      CS.addConstraint(CS.createEq(CS.getVar(*dstCVars.begin()), outerMostSrcVal));
      retVal = true;
    }

    // for all the other pointer types they should be exactly same.
    // more details refer: https://github.com/microsoft/checkedc-clang/issues/676
    srcCVars.erase(srcCVars.begin());
    dstCVars.erase(dstCVars.begin());

    if (srcCVars.size() == dstCVars.size()) {
      CVars::iterator SB = srcCVars.begin();
      CVars::iterator DB = dstCVars.begin();

      while (SB != srcCVars.end()) {
        ConstAtom *sVal = CS.getAssignment(*SB);
        ConstAtom *dVal = CS.getAssignment(*DB);
        // if these are not equal.
        if (*sVal < *dVal || *dVal < *sVal) {
          // get the highest type.
          ConstAtom *finalVal = *sVal < *dVal ? dVal : sVal;
          // get the lowest constraint variable to change.
          VarAtom *toChange = *sVal < *dVal ? CS.getVar(*SB) : CS.getVar(*DB);
          CS.addConstraint(CS.createEq(toChange, finalVal));
          retVal = true;
        }
        SB++;
        DB++;
      }
    }
  }
  return retVal;
}

bool ProgramInfo::handleFunctionSubtyping() {
  // The subtyping rule for functions is:
  // T2 <: S2
  // S1 <: T1
  //--------------------
  // T1 -> T2 <: S1 -> S2
  // A way of interpreting this is that the type of a declaration argument `S1` can be a
  // subtype of a definition parameter type `T1`, and the type of a definition
  // return type `S2` can be a subtype of the declaration expected type `T2`.
  //
  bool retVal = false;
  auto &envMap = CS.getVariables();
  for (auto &currFDef: CS.getFuncDefnVarMap()) {
    // get the key for the function definition.
    auto funcDefKey = currFDef.first;
    std::set<ConstraintVariable*> &defCVars = currFDef.second;

    std::set<ConstraintVariable*> *declCVarsPtr = getFuncDeclConstraintSet(funcDefKey);

    if (declCVarsPtr != nullptr) {
      // if we have declaration constraint variables?
      std::set<ConstraintVariable *> &declCVars = *declCVarsPtr;
      // get the highest def and decl FVars
      auto defCVar = getHighestT<FVConstraint>(defCVars, *this);
      auto declCVar = getHighestT<FVConstraint>(declCVars, *this);
      if (defCVar != nullptr && declCVar != nullptr) {

        // handle the return types.
        auto defRetPVCons = getHighestT<PVConstraint>(defCVar->getReturnVars(), *this);
        auto declRetPVCons = getHighestT<PVConstraint>(declCVar->getReturnVars(), *this);

        if (isAValidPVConstraint(defRetPVCons) && isAValidPVConstraint(declRetPVCons)) {
          // these are the constraint variables for top most pointers
          auto topDefCVar = *(defRetPVCons->getCvars().begin());
          auto topDeclCVar = *(declRetPVCons->getCvars().begin());
          // if the top-most constraint variable in the definition is WILD?
          // This is important in the cases of nested pointers.
          // i.e., int** foo().
          // if the top most pointer is WILD then we have to make everything WILD.
          // We cannot have Ptr<int>*. However, we can have Ptr<int*>

          // the function is returning WILD with in the body?
          if (CS.isWild(topDefCVar)) {
            // make everything WILD.
            std::string wildReason = "Function Returning WILD within the body.";
            for (const auto &B : defRetPVCons->getCvars())
              CS.addConstraint(CS.createEq(CS.getOrCreateVar(B), CS.getWild(), wildReason));

            for (const auto &B : declRetPVCons->getCvars())
              CS.addConstraint(CS.createEq(CS.getOrCreateVar(B), CS.getWild(), wildReason));

            retVal = true;
          } else if (CS.isWild(topDeclCVar)) {
            // if the declaration return type is WILD ?
            // get the highest non-wild checked type.
            ConstraintVariable* baseConsVar = ConstraintVariable::getHighestNonWildConstraint(declRetPVCons->getArgumentConstraints(),
                                                            envMap, *this);
            PVConstraint *highestNonWildCvar = declRetPVCons;
            if (isAValidPVConstraint(baseConsVar))
              highestNonWildCvar = dyn_cast<PVConstraint>(baseConsVar);

            topDeclCVar = *(highestNonWildCvar->getCvars().begin());

            auto defAssignment = CS.getAssignment(topDefCVar);
            auto declAssignment = CS.getAssignment(topDeclCVar);

            // okay, both declaration and definition are checked types.
            // here we should apply the sub-typing relation.
            if (!CS.isWild(topDeclCVar) && *defAssignment < *declAssignment) {
              // i.e., definition is not a subtype of declaration.
              // e.g., def = PTR and decl = ARR,
              //  here PTR is not a subtype of ARR
              // Oh, definition is more restrictive than declaration.
              // promote the type of definition to higher type.
              retVal = applySubtypingRelation(highestNonWildCvar, defRetPVCons) || retVal;
            }

          }

        }

        // handle the parameter types.
        if (declCVar->numParams() == defCVar->numParams()) {
          std::set<ConstraintVariable *> toChangeCVars;
          // Compare parameters.
          for (unsigned i = 0; i < declCVar->numParams(); ++i) {
            auto declParam = getHighestT<PVConstraint>(declCVar->getParamVar(i), *this);
            auto defParam = getHighestT<PVConstraint>(defCVar->getParamVar(i), *this);
            if (isAValidPVConstraint(declParam) && isAValidPVConstraint(defParam)) {
              toChangeCVars.clear();
              auto topDefCVar = *(defParam->getCvars().begin());
              auto topDeclCVar = *(declParam->getCvars().begin());

              if (!CS.isWild(topDefCVar)) {
                // the declaration is not WILD.
                // so, we just need to check with the declaration.
                if (!CS.isWild(topDeclCVar)) {
                  toChangeCVars.insert(declParam);
                } else {
                  // the declaration is WILD. So, we need to iterate through all
                  // the argument constraints and try to change them.
                  // this is because if we only change the declaration, as some caller
                  // is making it WILD, it will not propagate to all the arguments.
                  // we need to explicitly change each of the non-WILD arguments.
                  for (auto argOrigCons: declParam->getArgumentConstraints()) {
                    if (isAValidPVConstraint(argOrigCons)) {
                      PVConstraint *argPVCons = dyn_cast<PVConstraint>(argOrigCons);
                      auto topArgCVar = *(argPVCons->getCvars().begin());
                      CVars defPCVars(defParam->getCvars());

                      // is the top constraint variable WILD?
                      if (!CS.isWild(topArgCVar)) {
                        if (defPCVars.size() > argPVCons->getCvars().size()) {

                          while (defPCVars.size() > argPVCons->getCvars().size())
                            defPCVars.erase(defPCVars.begin());

                          if (!CS.isWild(*(defPCVars.begin())))
                            toChangeCVars.insert(argPVCons);
                        } else {
                          toChangeCVars.insert(argPVCons);
                        }
                      }
                    }
                  }
                }
                // here we should apply the sub-typing relation
                // for all the toChageVars
                for (auto currToChangeVar: toChangeCVars) {
                  // i.e., declaration is not a subtype of definition.
                  // e.g., decl = PTR and defn = ARR,
                  //  here PTR is not a subtype of ARR
                  // Oh, declaration is more restrictive than definition.
                  // promote the type of declaration to higher type.
                  retVal = applySubtypingRelation(defParam, currToChangeVar) || retVal;
                }
              }
            }
          }
        }
      }
    }
  }
  return retVal;
}

bool ProgramInfo::computePointerDisjointSet() {
  ConstraintDisjointSet.clear();
  CVars allWILDPtrs;
  allWILDPtrs.clear();
  for (auto currC: CS.getConstraints()) {
    if (Eq *EC = dyn_cast<Eq>(currC)) {
      VarAtom *VLhs = dyn_cast<VarAtom>(EC->getLHS());
      if (dyn_cast<WildAtom>(EC->getRHS())) {
        ConstraintDisjointSet.realWildPtrsWithReasons[VLhs->getLoc()].wildPtrReason = EC->getReason();
        if (!EC->sourceFileName.empty() && EC->lineNo != 0) {
          ConstraintDisjointSet.realWildPtrsWithReasons[VLhs->getLoc()].isValid = true;
          ConstraintDisjointSet.realWildPtrsWithReasons[VLhs->getLoc()].sourceFileName = EC->sourceFileName;
          ConstraintDisjointSet.realWildPtrsWithReasons[VLhs->getLoc()].lineNo = EC->lineNo;
          ConstraintDisjointSet.realWildPtrsWithReasons[VLhs->getLoc()].colStart = EC->colStart;
        }
        allWILDPtrs.insert(VLhs->getLoc());
      } else {
        VarAtom *Vrhs = dyn_cast<VarAtom>(EC->getRHS());
        if (Vrhs != nullptr)
          ConstraintDisjointSet.addElements(VLhs->getLoc(), Vrhs->getLoc());
      }
    }
  }

  // perform adjustment of group leaders. So that, the real-WILD
  // pointers are the leaders for each group.
  for (auto &realCP: ConstraintDisjointSet.realWildPtrsWithReasons) {
    auto &realCVar = realCP.first;
    // check if the leader CVar is a real WILD Ptr
    if (ConstraintDisjointSet.leaders.find(realCVar) != ConstraintDisjointSet.leaders.end()) {
      auto oldGroupLeader = ConstraintDisjointSet.leaders[realCVar];
      // if not?
      if (ConstraintDisjointSet.realWildPtrsWithReasons.find(oldGroupLeader) ==
          ConstraintDisjointSet.realWildPtrsWithReasons.end()) {
        for (auto &leadersP: ConstraintDisjointSet.leaders) {
          if (leadersP.second == oldGroupLeader) {
            leadersP.second = realCVar;
          }
        }

        auto &oldG = ConstraintDisjointSet.groups[oldGroupLeader];
        ConstraintDisjointSet.groups[realCVar].insert(oldG.begin(), oldG.end());
        ConstraintDisjointSet.groups[realCVar].insert(realCVar);
        ConstraintDisjointSet.groups.erase(oldGroupLeader);
      }
    }
  }

  // compute non-direct WILD pointers.
  for (auto &gm : ConstraintDisjointSet.groups) {
    // is this group a WILD pointer group?
    if (ConstraintDisjointSet.realWildPtrsWithReasons.find(gm.first) !=
        ConstraintDisjointSet.realWildPtrsWithReasons.end()) {
        ConstraintDisjointSet.totalNonDirectWildPointers.insert(gm.second.begin(), gm.second.end());
    }
  }

  CVars tmpCKeys;
  tmpCKeys.clear();
  // remove direct WILD pointers from non-direct wild pointers.
  std::set_difference(ConstraintDisjointSet.totalNonDirectWildPointers.begin(),
                      ConstraintDisjointSet.totalNonDirectWildPointers.end(),
                      allWILDPtrs.begin(), allWILDPtrs.end(),
                      std::inserter(tmpCKeys, tmpCKeys.begin()));

  // update the totalNonDirectWildPointers
  ConstraintDisjointSet.totalNonDirectWildPointers.clear();
  ConstraintDisjointSet.totalNonDirectWildPointers.insert(tmpCKeys.begin(), tmpCKeys.end());

  for ( const auto &I : Variables ) {
    PersistentSourceLoc L = I.first;
    std::string filePath = L.getFileName();
    if (canWrite(filePath)) {
      ConstraintDisjointSet.validSourceFiles.insert(filePath);
    } else {
      continue;
    }
    const std::set<ConstraintVariable *> &S = I.second;
    for (auto *CV: S) {
      if (PVConstraint *PV = dyn_cast<PVConstraint>(CV)) {
        for (auto ck: PV->getCvars()) {
          ConstraintDisjointSet.PtrSourceMap[ck] = (PersistentSourceLoc*)(&(I.first));
        }
      }
      if (FVConstraint *FV = dyn_cast<FVConstraint>(CV)) {
        for (auto PV: FV->getReturnVars()) {
          if (PVConstraint *RPV = dyn_cast<PVConstraint>(PV)) {
            for (auto ck: RPV->getCvars()) {
              ConstraintDisjointSet.PtrSourceMap[ck] = (PersistentSourceLoc*)(&(I.first));
            }
          }
        }
      }
    }
  }


  // compute all the WILD pointers.
  CVars WildCkeys;
  for (auto &gm : ConstraintDisjointSet.groups) {
    WildCkeys.clear();
    std::set_intersection(gm.second.begin(), gm.second.end(), allWILDPtrs.begin(), allWILDPtrs.end(),
                          std::inserter(WildCkeys, WildCkeys.begin()));

    if (!WildCkeys.empty()) {
      ConstraintDisjointSet.allWildPtrs.insert(WildCkeys.begin(), WildCkeys.end());
    }
  }

  return true;
}
