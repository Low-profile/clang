//===---- CGLoopInfo.cpp - LLVM CodeGen for loop metadata -*- C++ -*-------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "CGLoopInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/Sema/LoopHint.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "CodeGenFunction.h"

using namespace clang::CodeGen;
using namespace llvm;

static MDNode *createMetadata(LLVMContext &Ctx, Function *F, CodeGenFunction *CGF,
                              const LoopAttributes &Attrs,
                              const llvm::DebugLoc &StartLoc,
                              const llvm::DebugLoc &EndLoc) {

  if (!Attrs.IsParallel && Attrs.VectorizeWidth == 0 &&
      Attrs.InterleaveCount == 0 && Attrs.UnrollCount == 0 &&
      Attrs.VectorizeEnable == LoopAttributes::Unspecified &&
      Attrs.UnrollEnable == LoopAttributes::Unspecified &&
      Attrs.DistributeEnable == LoopAttributes::Unspecified &&
      Attrs.LoopId.empty() &&
      !StartLoc && !EndLoc)
    return nullptr;

  SmallVector<Metadata *, 4> Args;
  // Reserve operand 0 for loop id self reference.
  auto TempNode = MDNode::getTemporary(Ctx, None);
  Args.push_back(TempNode.get());

  // If we have a valid start debug location for the loop, add it.
  if (StartLoc) {
    Args.push_back(StartLoc.getAsMDNode());

    // If we also have a valid end debug location for the loop, add it.
    if (EndLoc)
      Args.push_back(EndLoc.getAsMDNode());
  }

  // Setting vectorize.width
  if (Attrs.VectorizeWidth > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.vectorize.width"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.VectorizeWidth))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting interleave.count
  if (Attrs.InterleaveCount > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.interleave.count"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.InterleaveCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting interleave.count
  if (Attrs.UnrollCount > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll.count"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.UnrollCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting vectorize.enable
  if (Attrs.VectorizeEnable != LoopAttributes::Unspecified) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.vectorize.enable"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt1Ty(Ctx), (Attrs.VectorizeEnable ==
                                                   LoopAttributes::Enable)))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting unroll.full or unroll.disable
  if (Attrs.UnrollEnable != LoopAttributes::Unspecified) {
    std::string Name;
    if (Attrs.UnrollEnable == LoopAttributes::Enable)
      Name = "llvm.loop.unroll.enable";
    else if (Attrs.UnrollEnable == LoopAttributes::Full)
      Name = "llvm.loop.unroll.full";
    else
      Name = "llvm.loop.unroll.disable";
    Metadata *Vals[] = {MDString::get(Ctx, Name)};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.DistributeEnable != LoopAttributes::Unspecified) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.distribute.enable"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt1Ty(Ctx), (Attrs.DistributeEnable ==
                                                   LoopAttributes::Enable)))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (!Attrs.LoopId.empty()) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.id"),
                        MDString::get(Ctx, Attrs.LoopId)};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Set the first operand to itself.
  MDNode *LoopID = MDNode::get(Ctx, Args);
  LoopID->replaceOperandWith(0, LoopID);

  SmallVector<MDNode *, 4> AdditionalTransforms;
  SmallVector<Metadata *, 8> AllTransforms;
  auto FuncMD = F->getMetadata("looptransform");
  if (FuncMD) {
    for (auto &X : FuncMD->operands()) {
      auto Op = cast<MDNode>(X.get());
      AllTransforms.push_back(Op);
    }
  }

  auto TopLoopId = LoopID;
  for (auto &Transform : Attrs.TransformationStack) {
    switch (Transform.Kind) {
    default:
        llvm_unreachable("unexpected transformation");
    case LoopTransformation::Reversal: {
      SmallVector<Metadata *, 4> TransformArgs;
      TransformArgs.push_back(MDString::get(Ctx, "llvm.loop.reverse"));

      auto ApplyOn = Transform.getApplyOn();
      if (ApplyOn.empty()) {
        // Apply on TopLoopId
        assert(TopLoopId);
        TransformArgs.push_back(TopLoopId);
      } else {
        // Apply on Transform.ApplyOn
        // TODO: Search for LoopID instead of using the name?
        TransformArgs.push_back(MDString::get(Ctx, ApplyOn));
      }

      auto MDTransform = MDNode::get(Ctx, TransformArgs);

      // auto Transforms =  MDNode::get(Ctx, MDTransform); // FIXME: Allow
      // multiple transformation
      // F->addMetadata("looptransform", *Transforms);
      AdditionalTransforms.push_back(MDTransform);
      AllTransforms.push_back(MDTransform);

      // TODO: Different scheme for transformations that output more than one
      TopLoopId = MDTransform;
    } break;
    case LoopTransformation::Tiling: {
      SmallVector<Metadata *, 4> TransformArgs;
      TransformArgs.push_back(MDString::get(Ctx, "llvm.loop.tile"));

      SmallVector<Metadata *, 4> ApplyOnArgs;
      if (Transform.ApplyOns.empty()) {
        // Apply on top loop
        assert(TopLoopId);
        ApplyOnArgs.push_back(TopLoopId);
      } else {
        for (auto ApplyOn : Transform.ApplyOns) {
          assert(!ApplyOn.empty() && "Must specify loops to tile");
          ApplyOnArgs.push_back(MDString::get(Ctx, ApplyOn));
        }
      }
      TransformArgs.push_back(MDNode::get(Ctx, ApplyOnArgs));

      SmallVector<Metadata *, 4> TileSizeArgs;
      for (auto TileSize : Transform.TileSizes) {
        assert(TileSize > 0 && "Must specify tile size");
        TileSizeArgs.push_back(ConstantAsMetadata::get(
            ConstantInt::get(Type::getInt64Ty(Ctx), TileSize)));
      }
      TransformArgs.push_back(MDNode::get(Ctx, TileSizeArgs));

      assert(TileSizeArgs.empty() ||
             (TileSizeArgs.size() == ApplyOnArgs.size()));
      auto MDTransform = MDNode::get(Ctx, TransformArgs);
      // auto Transforms =  MDNode::get(Ctx, MDTransform); // FIXME: Allow
      // multiple transformation
      // F->addMetadata("looptransform", *Transforms);
      AdditionalTransforms.push_back(MDTransform);
      AllTransforms.push_back(MDTransform);

      TopLoopId = nullptr; // No unique follow-up node
    } break;
         case LoopTransformation::Interchange : {
            SmallVector<Metadata *, 4> TransformArgs;
            TransformArgs.push_back(MDString::get(Ctx, "llvm.loop.interchange"));

                  SmallVector<Metadata *, 4> ApplyOnArgs;
                  assert(!Transform.ApplyOns.empty());
                  assert(Transform.ApplyOns.size() == Transform.Permutation.size() );

        for (auto ApplyOn : Transform.ApplyOns) {
          assert(!ApplyOn.empty() && "Must specify loops to tile");
          ApplyOnArgs.push_back(MDString::get(Ctx, ApplyOn));
        }
      TransformArgs.push_back(MDNode::get(Ctx, ApplyOnArgs));

           SmallVector<Metadata *, 4> PermutationArgs;
             for (auto PermuteItem : Transform.Permutation ) {
                    assert(!PermuteItem.empty() && "Must specify loop id");
                  PermutationArgs.push_back(MDString::get(Ctx, PermuteItem));
             }
             TransformArgs.push_back(MDNode::get(Ctx, PermutationArgs));

                                                                   auto MDTransform = MDNode::get(Ctx, TransformArgs);
      AdditionalTransforms.push_back(MDTransform);
      AllTransforms.push_back(MDTransform);

      TopLoopId = nullptr; // No unique follow-up node
         } break;
         case LoopTransformation::Pack: {
           SmallVector<Metadata *, 4> TransformArgs;
      TransformArgs.push_back(MDString::get(Ctx, "llvm.data.pack"));

      auto ApplyOn = Transform.getApplyOn();
      if (ApplyOn.empty()) {
        // Apply on TopLoopId
        assert(TopLoopId);
        TransformArgs.push_back(TopLoopId);
      } else {
        // Apply on Transform.ApplyOn
        // TODO: Search for LoopID instead of using the name?
        TransformArgs.push_back(MDString::get(Ctx, ApplyOn));
      }

    auto Var = Transform.Array->getDecl();
    auto LVar = CGF-> EmitLValue(Transform.Array);
    auto Addr = LVar.getPointer();
      TransformArgs.push_back(LocalAsMetadata::get(Addr));

      auto MDTransform = MDNode::get(Ctx, TransformArgs);
      AdditionalTransforms.push_back(MDTransform);
      AllTransforms.push_back(MDTransform);

      // Follow-ups use this one
      TopLoopId = MDTransform;
    } break;
    }
  }

  if (!AdditionalTransforms.empty()) {
    auto AllTransformsMD = MDNode::get(Ctx, AllTransforms);
    F->setMetadata("looptransform", AllTransformsMD);
  }

  return LoopID;
}

LoopAttributes::LoopAttributes(bool IsParallel)
    : IsParallel(IsParallel), VectorizeEnable(LoopAttributes::Unspecified),
      UnrollEnable(LoopAttributes::Unspecified), VectorizeWidth(0),
      InterleaveCount(0), UnrollCount(0),
      DistributeEnable(LoopAttributes::Unspecified) {}

void LoopAttributes::clear() {
  IsParallel = false;
  VectorizeWidth = 0;
  InterleaveCount = 0;
  UnrollCount = 0;
  VectorizeEnable = LoopAttributes::Unspecified;
  UnrollEnable = LoopAttributes::Unspecified;
  DistributeEnable = LoopAttributes::Unspecified;
  LoopId = StringRef();
  TransformationStack.clear();
}

LoopInfo::LoopInfo(BasicBlock *Header, Function *F, clang::CodeGen::CodeGenFunction *CGF, const LoopAttributes &Attrs,
                   const llvm::DebugLoc &StartLoc, const llvm::DebugLoc &EndLoc)
    : LoopID(nullptr), Header(Header), Attrs(Attrs) {
  LoopID = createMetadata(Header->getContext(), F, CGF, Attrs, StartLoc, EndLoc);
}

void LoopInfoStack::push(BasicBlock *Header, Function *F, clang::CodeGen::CodeGenFunction *CGF,
                         const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc) {
  Active.push_back(LoopInfo(Header, F, CGF, StagedAttrs, StartLoc, EndLoc));
  // Clear the attributes so nested loops do not inherit them.
  StagedAttrs.clear();
}

void LoopInfoStack::push(BasicBlock *Header, Function *F,clang::CodeGen::CodeGenFunction *CGF,
                         clang::ASTContext &Ctx,
                         ArrayRef<const clang::Attr *> Attrs,
                         const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc) {

  // Identify loop hint attributes from Attrs.
  for (const auto *Attr : Attrs) {
    if (auto LId = dyn_cast<LoopIdAttr>(Attr)) {
      setLoopId(LId->getLoopName());
      continue;
    }
    if (auto LReversal = dyn_cast<LoopReversalAttr>(Attr)) {
      auto ApplyOn = LReversal->getApplyOn();
      if (ApplyOn.empty()) {
        // Apply to the following loop
      } else {
        // Apply on the loop with that name
      }

      addTransformation(LoopTransformation::createReversal(ApplyOn));
      continue;
    }

    if (auto LTiling = dyn_cast<LoopTilingAttr>(Attr)) {
      SmallVector<int64_t, 4> TileSizes;
      for (auto TileSizeExpr : LTiling->tileSizes()) {
        llvm::APSInt ValueAPS = TileSizeExpr->EvaluateKnownConstInt(Ctx);
        auto ValueInt = ValueAPS.getSExtValue();
        TileSizes.push_back(ValueInt);
      }

      addTransformation(LoopTransformation::createTiling(
          makeArrayRef(LTiling->applyOn_begin(), LTiling->applyOn_size()),
          TileSizes));
      continue;
    }

        if (auto LInterchange = dyn_cast<LoopInterchangeAttr>(Attr)) {
                  addTransformation(LoopTransformation::createInterchange(
                      makeArrayRef(LInterchange->applyOn_begin(), LInterchange->applyOn_size()),   
                      makeArrayRef(LInterchange->permutation_begin(), LInterchange->permutation_size()) 
                      ));
      continue;
        }

                if (auto Pack = dyn_cast<PackAttr>(Attr)) {
                   addTransformation(LoopTransformation::createPack(Pack->getApplyOn(), cast<DeclRefExpr>( Pack->getArray())));
      continue;
        }

    const LoopHintAttr *LH = dyn_cast<LoopHintAttr>(Attr);
    const OpenCLUnrollHintAttr *OpenCLHint =
        dyn_cast<OpenCLUnrollHintAttr>(Attr);

    // Skip non loop hint attributes
    if (!LH && !OpenCLHint) {
      continue;
    }

    LoopHintAttr::OptionType Option = LoopHintAttr::Unroll;
    LoopHintAttr::LoopHintState State = LoopHintAttr::Disable;
    unsigned ValueInt = 1;
    // Translate opencl_unroll_hint attribute argument to
    // equivalent LoopHintAttr enums.
    // OpenCL v2.0 s6.11.5:
    // 0 - full unroll (no argument).
    // 1 - disable unroll.
    // other positive integer n - unroll by n.
    if (OpenCLHint) {
      ValueInt = OpenCLHint->getUnrollHint();
      if (ValueInt == 0) {
        State = LoopHintAttr::Full;
      } else if (ValueInt != 1) {
        Option = LoopHintAttr::UnrollCount;
        State = LoopHintAttr::Numeric;
      }
    } else if (LH) {
      auto *ValueExpr = LH->getValue();
      if (ValueExpr) {
        llvm::APSInt ValueAPS = ValueExpr->EvaluateKnownConstInt(Ctx);
        ValueInt = ValueAPS.getSExtValue();
      }

      Option = LH->getOption();
      State = LH->getState();
    }
    switch (State) {
    case LoopHintAttr::Disable:
      switch (Option) {
      case LoopHintAttr::Vectorize:
        // Disable vectorization by specifying a width of 1.
        setVectorizeWidth(1);
        break;
      case LoopHintAttr::Interleave:
        // Disable interleaving by speciyfing a count of 1.
        setInterleaveCount(1);
        break;
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Disable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(false);
        break;
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
        llvm_unreachable("Options cannot be disabled.");
        break;
      }
      break;
    case LoopHintAttr::Enable:
      switch (Option) {
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
        setVectorizeEnable(true);
        break;
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Enable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(true);
        break;
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
        llvm_unreachable("Options cannot enabled.");
        break;
      }
      break;
    case LoopHintAttr::AssumeSafety:
      switch (Option) {
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
        // Apply "llvm.mem.parallel_loop_access" metadata to load/stores.
        setParallel(true);
        setVectorizeEnable(true);
        break;
      case LoopHintAttr::Unroll:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
        llvm_unreachable("Options cannot be used to assume mem safety.");
        break;
      }
      break;
    case LoopHintAttr::Full:
      switch (Option) {
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Full);
        break;
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
        llvm_unreachable("Options cannot be used with 'full' hint.");
        break;
      }
      break;
    case LoopHintAttr::Numeric:
      switch (Option) {
      case LoopHintAttr::VectorizeWidth:
        setVectorizeWidth(ValueInt);
        break;
      case LoopHintAttr::InterleaveCount:
        setInterleaveCount(ValueInt);
        break;
      case LoopHintAttr::UnrollCount:
        setUnrollCount(ValueInt);
        break;
      case LoopHintAttr::Unroll:
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::Distribute:
        llvm_unreachable("Options cannot be assigned a value.");
        break;
      }
      break;
    }
  }

  /// Stage the attributes.
  push(Header, F, CGF, StartLoc, EndLoc);
}

void LoopInfoStack::pop() {
  assert(!Active.empty() && "No active loops to pop");
  Active.pop_back();
}

void LoopInfoStack::InsertHelper(Instruction *I) const {
  if (!hasInfo())
    return;

  const LoopInfo &L = getInfo();
  if (!L.getLoopID())
    return;

  if (TerminatorInst *TI = dyn_cast<TerminatorInst>(I)) {
    for (unsigned i = 0, ie = TI->getNumSuccessors(); i < ie; ++i)
      if (TI->getSuccessor(i) == L.getHeader()) {
        TI->setMetadata(llvm::LLVMContext::MD_loop, L.getLoopID());
        break;
      }
    return;
  }

  if (L.getAttributes().IsParallel && I->mayReadOrWriteMemory())
    I->setMetadata("llvm.mem.parallel_loop_access", L.getLoopID());
}
