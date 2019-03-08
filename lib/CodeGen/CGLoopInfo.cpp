//===---- CGLoopInfo.cpp - LLVM CodeGen for loop metadata -*- C++ -*-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CGLoopInfo.h"
#include "CodeGenFunction.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"

using namespace clang::CodeGen;
using namespace llvm;

static MDNode *createMetadata(LLVMContext &Ctx, Function *F,
                              CodeGenFunction *CGF,const  LoopAttributes &Attrs,
                              const llvm::DebugLoc &StartLoc,
                              const llvm::DebugLoc &EndLoc, MDNode *AccGroup) {

  if (!Attrs.IsParallel && Attrs.VectorizeWidth == 0 &&
      Attrs.InterleaveCount == 0 && Attrs.UnrollCount == 0 &&
      Attrs.UnrollAndJamCount == 0 && !Attrs.PipelineDisabled &&
      Attrs.PipelineInitiationInterval == 0 &&
      Attrs.VectorizeEnable == LoopAttributes::Unspecified &&
      Attrs.UnrollEnable == LoopAttributes::Unspecified &&
      Attrs.UnrollAndJamEnable == LoopAttributes::Unspecified &&
      Attrs.DistributeEnable == LoopAttributes::Unspecified && !StartLoc &&
      !EndLoc && Attrs.LoopId.empty() && Attrs.TransformationStack.empty())
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

  // Setting unroll.count
  if (Attrs.UnrollCount > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll.count"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.UnrollCount))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  // Setting unroll_and_jam.count
  if (Attrs.UnrollAndJamCount > 0) {
    Metadata *Vals[] = {MDString::get(Ctx, "llvm.loop.unroll_and_jam.count"),
                        ConstantAsMetadata::get(ConstantInt::get(
                            Type::getInt32Ty(Ctx), Attrs.UnrollAndJamCount))};
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

  // Setting unroll_and_jam.full or unroll_and_jam.disable
  if (Attrs.UnrollAndJamEnable != LoopAttributes::Unspecified) {
    std::string Name;
    if (Attrs.UnrollAndJamEnable == LoopAttributes::Enable)
      Name = "llvm.loop.unroll_and_jam.enable";
    else if (Attrs.UnrollAndJamEnable == LoopAttributes::Full)
      Name = "llvm.loop.unroll_and_jam.full";
    else
      Name = "llvm.loop.unroll_and_jam.disable";
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


  if (AccGroup) 
    Args.push_back(MDNode::get(Ctx, {MDString::get(Ctx, "llvm.loop.parallel_accesses"), AccGroup}));
  

  if (Attrs.PipelineDisabled) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.pipeline.disable"),
        ConstantAsMetadata::get(ConstantInt::get(
            Type::getInt1Ty(Ctx), (Attrs.PipelineDisabled == true)))};
    Args.push_back(MDNode::get(Ctx, Vals));
  }

  if (Attrs.PipelineInitiationInterval > 0) {
    Metadata *Vals[] = {
        MDString::get(Ctx, "llvm.loop.pipeline.initiationinterval"),
        ConstantAsMetadata::get(ConstantInt::get(
            Type::getInt32Ty(Ctx), Attrs.PipelineInitiationInterval))};
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

  return LoopID;
}

LoopAttributes::LoopAttributes(bool IsParallel)
    : IsParallel(IsParallel), VectorizeEnable(LoopAttributes::Unspecified),
      UnrollEnable(LoopAttributes::Unspecified),
      UnrollAndJamEnable(LoopAttributes::Unspecified), VectorizeWidth(0),
      InterleaveCount(0), UnrollCount(0), UnrollAndJamCount(0),
      DistributeEnable(LoopAttributes::Unspecified), PipelineDisabled(false),
      PipelineInitiationInterval(0) {}

void LoopAttributes::clear() {
  IsParallel = false;
  VectorizeWidth = 0;
  InterleaveCount = 0;
  UnrollCount = 0;
  UnrollAndJamCount = 0;
  VectorizeEnable = LoopAttributes::Unspecified;
  UnrollEnable = LoopAttributes::Unspecified;
  UnrollAndJamEnable = LoopAttributes::Unspecified;
  DistributeEnable = LoopAttributes::Unspecified;
  PipelineDisabled = false;
  PipelineInitiationInterval = 0;
  LoopId = StringRef();
  TransformationStack.clear();
}

LoopInfo::LoopInfo(llvm::BasicBlock *Header, llvm::Function *F,
	clang::CodeGen::CodeGenFunction *CGF,  LoopAttributes &Attrs,
	const llvm::DebugLoc &StartLoc, const llvm::DebugLoc &EndLoc,
	LoopInfo *Parent)  : Header(Header), Attrs(Attrs), StartLoc(StartLoc), EndLoc(EndLoc), Parent(Parent), CGF(CGF) {

	if (Attrs.IsParallel) {
		// Create an access group for this loop.
		LLVMContext &Ctx = Header->getContext();
		AccGroup = MDNode::getDistinct(Ctx, {});
	}


	LLVMContext &Ctx = Header->getContext();
	MDNode *LegacyLoopID = createMetadata(Ctx, Header->getParent(),CGF, Attrs, StartLoc, EndLoc, AccGroup);
	bool HasLegacyTransformation   = LegacyLoopID && LegacyLoopID->getNumOperands()>1;
	bool HasOrderedTransformation = !Attrs.LoopId.empty() || !Attrs.TransformationStack.empty();
	bool AncestorHasOrderedTransformation = Parent && Parent->InTransformation ;
	if (HasLegacyTransformation || HasOrderedTransformation ||AncestorHasOrderedTransformation ) {
		VInfo = new VirtualLoopInfo();
		if (HasLegacyTransformation)
			VInfo->markNondefault();
		TempLoopID = MDNode::getTemporary(Header->getContext(), None);
		if (LegacyLoopID) {
			for(auto &LegacyAttr: drop_begin( LegacyLoopID->operands(),1) )  
				VInfo->addAttribute(LegacyAttr.get());
		}
	}




#if 0
	bool HasLagacyTransformation =!(!Attrs.IsParallel && Attrs.VectorizeWidth == 0 &&
		Attrs.InterleaveCount == 0 && Attrs.UnrollCount == 0 &&
		Attrs.UnrollAndJamCount == 0 && !Attrs.PipelineDisabled &&
		Attrs.PipelineInitiationInterval == 0 &&
		Attrs.VectorizeEnable == LoopAttributes::Unspecified &&
		Attrs.UnrollEnable == LoopAttributes::Unspecified &&
		Attrs.UnrollAndJamEnable == LoopAttributes::Unspecified &&
		Attrs.DistributeEnable == LoopAttributes::Unspecified && !StartLoc &&
		!EndLoc && !AccGroup);
#endif


	this->InTransformation = HasOrderedTransformation || AncestorHasOrderedTransformation;
		

}

llvm::MDNode *LoopInfo::getLoopID() const {
	if (!TempLoopID)
		return nullptr;
	return TempLoopID.get();
	//if (!VInfo)
	//	return nullptr;
	//return  VInfo->getLoopID();  
}


static void addDebugLoc(llvm::LLVMContext &Ctx, StringRef AttrName, const LoopTransformation &TheTransform,  VirtualLoopInfo *On) {
if (TheTransform.BeginLoc ) {
	if (TheTransform.EndLoc) 
		On->addTransformMD( MDNode::get(Ctx, {  MDString::get(Ctx, AttrName),	TheTransform.BeginLoc	,TheTransform.EndLoc}	 ) );
	else
		On->addTransformMD( MDNode::get(Ctx, {  MDString::get(Ctx, AttrName),	TheTransform.BeginLoc}	 ) );
}

}

 VirtualLoopInfo* LoopInfoStack::applyReversal(const LoopTransformation &TheTransform, VirtualLoopInfo *On) {
	assert(On);
	auto Result = new VirtualLoopInfo();

	// Inherit all attributes.
	for (auto X : On->Attributes) 
		Result->addAttribute(X);
	
	On->addFollowup("llvm.loop.reverse.followup_reversed", Result);
	if (!TheTransform.FollowupName.empty()) {
		assert(!NamedLoopMap.count(TheTransform.FollowupName));
		NamedLoopMap[TheTransform.FollowupName] = Result;
		Result->addTransformMD(MDNode::get( Ctx, { 
			MDString::get(Ctx, "llvm.loop.id"), 
			MDString::get(Ctx, TheTransform.FollowupName) }));
		Result->markNondefault();
	}

	
	On->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.loop.reverse.enable"), 
		                                   ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(1, 1))) }));
	addDebugLoc(Ctx, "llvm.loop.reverse.loc", TheTransform, On);


#if 0
	if (CGDebugInfo *DI = CGF. getDebugInfo()) {
		auto BeginLoc = TheTransform.Loc.getBegin();
	auto BeginDLoc = llvm::DebugLoc::get(DI->getLineNumber(BeginLoc),DI-> getColumnNumber(BeginLoc), TheTransform.Scope);

	if (auto StartLoc = CGF.SourceLocToDebugLoc()) {
		On->addTransformMD( MDNode::get(Ctx, {  MDString::get(Ctx, "llvm.loop.reverse.startloc"),	StartLoc		} ) );
	}
	if (auto EndLoc = CGF.SourceLocToDebugLoc(TheTransform.Loc.getEnd())) {
		On->addTransformMD( MDNode::get(Ctx, {  MDString::get(Ctx, "llvm.loop.reverse.endloc"),	EndLoc		} ) );
	}
	}
#endif

	On->markNondefault();
	On->markDisableHeuristic();
	invalidateVirtualLoop(On);

	return Result;
}

 VirtualLoopInfo* LoopInfoStack::applyTiling(const LoopTransformation &Transform,llvm::ArrayRef<VirtualLoopInfo*> On) {
	 assert(On.size()>=1);
	 assert(On.size() == Transform.TileSizes.size());
	 auto N = On.size();

	 SmallVector<VirtualLoopInfo*,4> OuterLoops;
	 SmallVector<VirtualLoopInfo*,4> InnerLoops;

	 On[0]->addTransformMD(MDNode::get( Ctx, { 
		 MDString::get(Ctx, "llvm.loop.tile.enable"), 
		 ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(1, 1))) 
		 }) );
	 On[0]->addTransformMD(MDNode::get( Ctx, { 
		 MDString::get(Ctx, "llvm.loop.tile.depth"), 
		 ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(32, N))) 
		 }) );
	 addDebugLoc(Ctx, "llvm.loop.tile.loc", Transform, On[0]);


	 for (auto i = 0; i < N;i+=1) {
		 auto Orig = On[i];
		 auto FloorId =( i < Transform.TileFloorIds.size()) ? Transform.TileFloorIds[i] : StringRef();
		 auto TileId = ( i < Transform.TileTileIds.size()) ?  Transform.TileTileIds[i]: StringRef();

		 auto Outer = new VirtualLoopInfo(FloorId);
		 OuterLoops.push_back(Outer);
		 if (!FloorId.empty()) {
			// Outer->Name = FloorId;
			 NamedLoopMap[FloorId] = Outer;
			 Outer->addTransformMD(MDNode::get( Ctx, { 
				 MDString::get(Ctx, "llvm.loop.id"), 
				 MDString::get(Ctx, FloorId) }));
			 Outer->markNondefault();
		 }

		 auto Inner = new VirtualLoopInfo(TileId);
		 InnerLoops.push_back(Inner);
		 if (!TileId.empty()) {
			// Inner->Name = TileId;
			 NamedLoopMap[TileId] = Inner;
			 Inner->addTransformMD(MDNode::get( Ctx, { 
				 MDString::get(Ctx, "llvm.loop.id"), 
				 MDString::get(Ctx, TileId) }));
			 Inner->markNondefault();
		 }

		 // Inherit all attributes.
		 for (auto X : Orig->Attributes)  {
			 Outer->addAttribute(X);
			 Inner->addAttribute(X);
		 }

		 Orig->addTransformMD(MDNode::get( Ctx, { 
			 MDString::get(Ctx, "llvm.loop.tile.size"), 
			 ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(32,Transform.TileSizes[i]))) 
			 }) );
		 Orig->addFollowup("llvm.loop.tile.followup_floor",Outer);
		 Orig->addFollowup("llvm.loop.tile.followup_tile",Inner);
		 Orig->markDisableHeuristic();
		 Orig->markNondefault();
	 }

	 return nullptr;
 }


 VirtualLoopInfo* LoopInfoStack::applyInterchange(const LoopTransformation &Transform,llvm:: ArrayRef<VirtualLoopInfo *>On) {
	 auto N = On.size();
	 assert(N>=2);
	 assert(Transform.Permutation.size()==N);

	 for (auto OldLoop : On) 
		 invalidateVirtualLoop(OldLoop);

	 auto TopmostOrig = On[0];
	 TopmostOrig->addTransformMD(MDNode::get( Ctx, { 
		 MDString::get(Ctx, "llvm.loop.interchange.enable"), 
		 ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(1, 1))) 
		 }) );
	 TopmostOrig->addTransformMD(MDNode::get( Ctx, { 
		 MDString::get(Ctx, "llvm.loop.interchange.depth"), 
		 ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(32, N))) 
		 }));
	 addDebugLoc(Ctx, "llvm.loop.interchange.loc", Transform, TopmostOrig);


	 StringMap<int> NewPos;
	 for (auto PermutedName : Transform.Permutation) {
		 NewPos.insert({PermutedName, NewPos.size()});
	 }

	 SmallVector<Metadata*,4> Permutation;
	 Permutation.push_back(MDString::get(Ctx, "llvm.loop.interchange.permutation"));
	 for (int i =0; i<N;i+=1) {
		 auto LoopName = On[i]->Name;
		 auto Pos = NewPos.lookup(LoopName);
		 Permutation.push_back( ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(32, Pos))));
	 }
	 TopmostOrig->addTransformMD(MDNode::get( Ctx,Permutation));


	 SmallVector<VirtualLoopInfo*,4> PermutedLoops;
	 for (int i =0; i<N;i+=1) {
		 auto Orig = On[i];
		 auto Permuted = new VirtualLoopInfo();
		 PermutedLoops.push_back(Permuted);

		 // Inherit all attributes.
		 for (auto X : Orig->Attributes)  
			 Permuted->addAttribute(X);
		 
		 Orig->markDisableHeuristic();
		 Orig->markNondefault();


		 Permuted->addFollowup("llvm.loop.interchange.followup_interchanged", Permuted);
	 }

	 for (auto NewLoop : PermutedLoops) {
	
	 }

	 return PermutedLoops[0];
 }



 static Metadata *createUnsignedMetadataConstant(LLVMContext &Ctx, uint64_t Val) {
	 APInt X{64, Val, false};
	 APInt Y{X.getActiveBits()+1, Val, false}; // One bit more such that interpretation as signed or unsigned is not important.
return ConstantAsMetadata::get(ConstantInt::get(Ctx, Y));
 }



 static Metadata *createSignedMetadataConstant(LLVMContext &Ctx, int64_t Val) {
	 APInt X{64, static_cast<uint64_t>( Val), true};
	 APInt Y{X.getMinSignedBits ()+1, static_cast<uint64_t>( Val), true};
	 return ConstantAsMetadata::get(ConstantInt::get(Ctx, Y));
 }


 static Metadata *createBoolMetadataConstant(LLVMContext &Ctx, bool Val) {
	 return ConstantAsMetadata::get(ConstantInt::get(Ctx, APInt(1, Val) ));
 }


 VirtualLoopInfo* LoopInfoStack::applyUnrolling(const LoopTransformation &Transform,llvm:: ArrayRef<VirtualLoopInfo *>On) {
	 assert(On.size()==1);
	 auto Orig = On[0];

	 auto Result = new VirtualLoopInfo();

	 // Inherit all attributes.
	 for (auto X : Orig->Attributes) 
		 Result->addAttribute(X);

	 Orig->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.loop.unroll.enable"),createBoolMetadataConstant(Ctx, true) }));
	 Orig->markDisableHeuristic();
	 Orig->markNondefault();

	 auto UnrollFactor = Transform.Factor;
	 auto IsFullUnroll = Transform.Full;
	 if (UnrollFactor > 0 && IsFullUnroll) {
		 llvm_unreachable("Contradicting state");
	 } else if (UnrollFactor > 0) {
		 Orig->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.loop.unroll.count"), createUnsignedMetadataConstant(Ctx, UnrollFactor) }));
	 } else if (IsFullUnroll) {
		 Orig->addTransformMD(MDNode::get( Ctx,  MDString::get(Ctx, "llvm.loop.unroll.full")  ));
	 } else {
		 // Determine unroll factor heuristically
	 }
	 addDebugLoc(Ctx, "llvm.loop.unroll.loc", Transform, On[0]);

	 Orig->addFollowup("llvm.loop.unroll.followup_unrolled", Result);
	 Orig->markNondefault();
	 Orig->markDisableHeuristic();
	 invalidateVirtualLoop(Orig);

	 if (!Transform.FollowupName.empty()) {
		 assert(!NamedLoopMap.count(Transform.FollowupName));
		 NamedLoopMap[Transform.FollowupName] = Result;
		 Result->addTransformMD(MDNode::get( Ctx, { 
			 MDString::get(Ctx, "llvm.loop.id"), 
			 MDString::get(Ctx, Transform.FollowupName) }));
		 Result->markNondefault();
	 }

	 return Result;
 }



 VirtualLoopInfo* LoopInfoStack::applyPack(const LoopTransformation &Transform, llvm::ArrayRef<VirtualLoopInfo *>On) {
	 assert(On.size()==1);
	 auto Orig = On[0];
	 assert(Orig);

	 // auto Var = Transform.Array->getDecl();
	 auto LVar = CGF.EmitLValue(Transform.Array);
	 auto Addr = cast<AllocaInst>(LVar.getPointer());
	 //assert(!Transform.ArrayBasePtr);
	 //Transform.ArrayBasePtr = Addr;
	 // TransformArgs.push_back(LocalAsMetadata::get(Addr));

	auto ArrayName = Transform.Array->getNameInfo().getAsString();
	MDNode *ArrayId;
	if (ArrayName.empty())
	  ArrayId =  MDNode::getDistinct(Ctx, {});
	else 
		ArrayId =  MDNode::getDistinct(Ctx, MDString::get(Ctx, ArrayName)  );
	AccessesToTrack.push_back({Addr, ArrayId });

	 auto Result = new VirtualLoopInfo();

	 // Inherit all attributes.
	 for (auto X : Orig->Attributes) 
		 Result->addAttribute(X);


	 Orig->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.data.pack.enable"),createBoolMetadataConstant(Ctx, true) }));
	 Orig->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.data.pack.array"), ArrayId }));

	 // FIXME: if no allocate() clause is specified, do no emit any llvm.array.pack.allocate
 	 if (Transform.OnHeap)
		 Orig->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.data.pack.allocate"), MDString::get(Ctx, "malloc") }));
	 else
		 Orig->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.data.pack.allocate"), MDString::get(Ctx, "alloca") }));
	 addDebugLoc(Ctx, "llvm.data.pack.loc", Transform, On[0]);
	 Orig->markDisableHeuristic();
	 Orig->markNondefault();

	 invalidateVirtualLoop(Orig);

	 return Result;
 }



 VirtualLoopInfo* LoopInfoStack::applyThreadParallel(const LoopTransformation &Transform, VirtualLoopInfo * On) {
	 assert(On);
	 auto Orig = On;

	 auto Result = new VirtualLoopInfo();

	 // Inherit all attributes.
	 for (auto X : Orig->Attributes) 
		 Result->addAttribute(X);

	 Orig->addTransformMD(MDNode::get( Ctx, { MDString::get(Ctx, "llvm.loop.parallelize_thread.enable"),createBoolMetadataConstant(Ctx, true) }));
	 addDebugLoc(Ctx, "llvm.loop.parallelize_thread.loc", Transform, On);
	 Orig->markDisableHeuristic();
	 Orig->markNondefault();
	 invalidateVirtualLoop(Orig);

	 return Result;



#if 0
	case LoopTransformation::ThreadParallel: {
		 SmallVector<Metadata *, 4> TransformArgs;
		 TransformArgs.push_back(
			 MDString::get(Ctx, "llvm.loop.parallelize_thread"));

		 auto ApplyOn = Transform.getApplyOn();
		 if (ApplyOn.empty()) {
			 assert(TopLoopId);
			 TransformArgs.push_back(TopLoopId);
		 } else {
			 TransformArgs.push_back(MDString::get(Ctx, ApplyOn));
		 }

		 auto MDTransform = MDNode::get(Ctx, TransformArgs);
		 Transform.TransformMD = MDTransform;
		 AdditionalTransforms.push_back(MDTransform);
		 AllTransforms.push_back(MDTransform);

		 // No further transformations after parallelizing
	 } break;


	 SmallVector<Metadata *, 4> TransformArgs;
	 TransformArgs.push_back(
		 MDString::get(Ctx, "llvm.loop.parallelize_thread"));

	 auto ApplyOn = Transform.getApplyOn();
	 if (ApplyOn.empty()) {
		 assert(TopLoopId);
		 TransformArgs.push_back(TopLoopId);
	 } else {
		 TransformArgs.push_back(MDString::get(Ctx, ApplyOn));
	 }

	 auto MDTransform = MDNode::get(Ctx, TransformArgs);
	 Transform.TransformMD = MDTransform;
	 AdditionalTransforms.push_back(MDTransform);
	 AllTransforms.push_back(MDTransform);

	 // No further transformations after parallelizing
#endif

 }



void LoopInfo::afterLoop(LoopInfoStack &LIS) {
//	LLVMContext &Ctx = Header->getContext();
	auto TopLoopId = VInfo;
	for (auto &Transform : reverse(Attrs.TransformationStack)) {
		assert(TopLoopId);
		TopLoopId = LIS.applyTransformation(Transform, TopLoopId);
	}
}

void LoopInfo::finish(LoopInfoStack &LIS) {
  // We did not annote the loop body instructions because there are no
  // attributes for this loop.
  if (!TempLoopID)
    return;

  LLVMContext &Ctx = Header->getContext();
 auto LoopID = VInfo->makeLoopID(Ctx);



  // Apply transformations

  TempLoopID->replaceAllUsesWith(LoopID);


//  TempLoopID->replaceAllUsesWith(LegacyLoopID);
}



MDNode * VirtualLoopInfo :: makeLoopID(llvm::LLVMContext &Ctx){
	SmallVector<Metadata *, 4> Args;
	// Reserve operand 0 for loop id self reference.
	auto TempNode = MDNode::getTemporary(Ctx, None);
	Args.push_back(TempNode.get());

	for (auto X : Attributes )
		Args.push_back(X);
	
	if (DisableHeuristic)
		Args.push_back(MDNode::get(Ctx,  MDString::get(Ctx, "llvm.loop.disable_nonforced") ));

	for (auto Y : Transforms)
		Args.push_back(Y);

	for (auto Z: Followups) {
		auto FollowupInfo = Z.second;
		if (!FollowupInfo->IsDefault) {
			auto FollowupLoopMD = FollowupInfo->makeLoopID(Ctx);
			Args.push_back( MDNode::get(Ctx, { MDString::get(Ctx, Z.first), FollowupLoopMD }) );
		}
	}

	// Set the first operand to itself.
	MDNode *LoopID = MDNode::get(Ctx, Args);
	LoopID->replaceOperandWith(0, LoopID);
	return LoopID;
}


LoopInfo* LoopInfoStack::push(BasicBlock *Header, Function *F,
                         const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc) {
	auto *Parent = Active.empty() ? nullptr : Active.back();
	auto NewLoop = new LoopInfo(Header, F,& CGF, StagedAttrs, StartLoc, EndLoc,Parent );
	if (Parent)
		Parent->addSubloop(NewLoop);
	OriginalLoops.push_back(NewLoop);
	Active.push_back(NewLoop);

  // Clear the attributes so nested loops do not inherit them.
  StagedAttrs.clear();
  return NewLoop;
}


VirtualLoopInfo::VirtualLoopInfo() {

}



VirtualLoopInfo::VirtualLoopInfo(StringRef Name) : Name(Name) {

}



void LoopInfoStack::push(BasicBlock *Header, Function *F,
                         clang::ASTContext &Ctx,
                         ArrayRef<const clang::Attr *> Attrs,
                         const llvm::DebugLoc &StartLoc,
                         const llvm::DebugLoc &EndLoc) {

  // Identify loop hint attributes from Attrs.
  for (const auto *Attr : Attrs) {
	  auto LocRange = Attr->getRange();
	  auto LocBegin = CGF.SourceLocToDebugLoc(LocRange.getBegin()) ;
	  auto LocEnd = CGF.SourceLocToDebugLoc(LocRange.getEnd()) ;

    if (auto LId = dyn_cast<LoopIdAttr>(Attr)) {
	//	assert( LId->getApplyOn().empty());
     auto Name = LId->getLoopName();
	 assert(Name.size()>0);
	 addTransformation(LoopTransformation::createId(LocBegin, LocEnd ,Name));
      continue;
    }

    if (auto LReversal = dyn_cast<LoopReversalAttr>(Attr)) {
      auto ApplyOn = LReversal->getApplyOn();
      if (ApplyOn.empty()) {
        // Apply to the following loop
      } else {
        // Apply on the loop with that name
      }

	  // FIXME: CGF.SourceLocToDebugLoc expects a lexical scop, but what is it supposed to be?

      addTransformation(LoopTransformation::createReversal(LocBegin, LocEnd ,ApplyOn, LReversal->getReversedId()));
      continue;
    }

    if (auto LTiling = dyn_cast<LoopTilingAttr>(Attr)) {
      SmallVector<int64_t, 4> TileSizes;
      for (auto TileSizeExpr : LTiling->tileSizes()) {
        llvm::APSInt ValueAPS = TileSizeExpr->EvaluateKnownConstInt(Ctx);
        auto ValueInt = ValueAPS.getSExtValue();
        TileSizes.push_back(ValueInt);
      }

      addTransformation(LoopTransformation::createTiling(LocBegin, LocEnd ,
          makeArrayRef(LTiling->applyOn_begin(), LTiling->applyOn_size()),
          TileSizes,
          makeArrayRef(LTiling->floorIds_begin(), LTiling->floorIds_size()),
          makeArrayRef(LTiling->tileIds_begin(), LTiling->tileIds_size())));
      continue;
    }

    if (auto LInterchange = dyn_cast<LoopInterchangeAttr>(Attr)) {
      addTransformation(LoopTransformation::createInterchange(LocBegin, LocEnd ,
          makeArrayRef(LInterchange->applyOn_begin(),
                       LInterchange->applyOn_size()),
          makeArrayRef(LInterchange->permutation_begin(),
                       LInterchange->permutation_size())));
      continue;
    }

    if (auto Pack = dyn_cast<PackAttr>(Attr)) {
      addTransformation(LoopTransformation::createPack(LocBegin, LocEnd ,
          Pack->getApplyOn(), cast<DeclRefExpr>(Pack->getArray()),
          Pack->getOnHeap()));
      continue;
    }

    if (auto Unrolling = dyn_cast<LoopUnrollingAttr>(Attr)) {
      auto Fac = Unrolling->getFactor();
      int64_t FactorInt = -1;
      if (Fac) {
        llvm::APSInt FactorAPS = Fac->EvaluateKnownConstInt(Ctx);
        FactorInt = FactorAPS.getSExtValue();
      }
      addTransformation(LoopTransformation::createUnrolling(LocBegin, LocEnd ,
          Unrolling->getApplyOn(), FactorInt, Unrolling->getFull()));
      continue;
    }

    if (auto ThreadParallel = dyn_cast<LoopParallelizeThreadAttr>(Attr)) {
      addTransformation(LoopTransformation::createThreadParallel(LocBegin, LocEnd ,
          ThreadParallel->getApplyOn()));
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
      case LoopHintAttr::UnrollAndJam:
        setUnrollAndJamState(LoopAttributes::Disable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(false);
        break;
      case LoopHintAttr::PipelineDisabled:
        setPipelineDisabled(true);
        break;
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::PipelineInitiationInterval:
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
      case LoopHintAttr::UnrollAndJam:
        setUnrollAndJamState(LoopAttributes::Enable);
        break;
      case LoopHintAttr::Distribute:
        setDistributeState(true);
        break;
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::PipelineDisabled:
      case LoopHintAttr::PipelineInitiationInterval:
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
      case LoopHintAttr::UnrollAndJam:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::PipelineDisabled:
      case LoopHintAttr::PipelineInitiationInterval:
        llvm_unreachable("Options cannot be used to assume mem safety.");
        break;
      }
      break;
    case LoopHintAttr::Full:
      switch (Option) {
      case LoopHintAttr::Unroll:
        setUnrollState(LoopAttributes::Full);
        break;
      case LoopHintAttr::UnrollAndJam:
        setUnrollAndJamState(LoopAttributes::Full);
        break;
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::UnrollCount:
      case LoopHintAttr::UnrollAndJamCount:
      case LoopHintAttr::VectorizeWidth:
      case LoopHintAttr::InterleaveCount:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::PipelineDisabled:
      case LoopHintAttr::PipelineInitiationInterval:
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
      case LoopHintAttr::UnrollAndJamCount:
        setUnrollAndJamCount(ValueInt);
        break;
      case LoopHintAttr::PipelineInitiationInterval:
        setPipelineInitiationInterval(ValueInt);
        break;
      case LoopHintAttr::Unroll:
      case LoopHintAttr::UnrollAndJam:
      case LoopHintAttr::Vectorize:
      case LoopHintAttr::Interleave:
      case LoopHintAttr::Distribute:
      case LoopHintAttr::PipelineDisabled:
        llvm_unreachable("Options cannot be assigned a value.");
        break;
      }
      break;
    }
  }

  /// Stage the attributes.
  push(Header, F, StartLoc, EndLoc);
}

void LoopInfoStack::pop() {
  assert(!Active.empty() && "No active loops to pop");
  Active.back()->afterLoop(*this);
  Active.pop_back();
}



static bool mayUseArray(AllocaInst *BasePtrAlloca, Value *PtrArg) {
  DenseSet<PHINode *> Closed;
  SmallVector<Value *, 16> Worklist;

  if (auto CurL = dyn_cast<LoadInst>(PtrArg)) {
    Worklist.push_back(CurL->getPointerOperand());
  } else if (auto CurS = dyn_cast<StoreInst>(PtrArg)) {
    Worklist.push_back(CurS->getPointerOperand());
  } else {
    // TODO: Support memcpy, memset, memmove
  }

  while (true) {
    if (Worklist.empty())
      break;
    auto Cur = Worklist.pop_back_val();

    if (auto CurPhi = dyn_cast<PHINode>(Cur)) {
      auto It = Closed.insert(CurPhi);
      if (!It.second)
        continue;
    }

    if (auto CurAlloca = dyn_cast<AllocaInst>(Cur)) {
      continue;
    }

    auto Ty = Cur->getType();
    if (!isa<PointerType>(Ty))
      continue;

#if 0
        if (auto CurGEP = dyn_cast< GetElementPtrInst>(Cur)) {
            // Fallback instead into the llvm::Operator case to also traverse indices?
            Worklist.push_back(CurGEP->getPointerOperand());
            continue;
        }
#endif

    if (auto CurL = dyn_cast<LoadInst>(Cur)) {
      if (CurL->getPointerOperand() == BasePtrAlloca)
        return true;
      continue;
    }

    if (auto CurInst = dyn_cast<Operator>(Cur)) {
      for (auto &Op : CurInst->operands()) {
        Worklist.push_back(Op.get());
      }
      continue;
    }
  }

  return false;
}

static void addArrayTransformUse(const LoopTransformation &Trans, Instruction *MemAcc) {
  auto AccessMD = MemAcc->getMetadata("llvm.access");
  if (!AccessMD) {
    AccessMD = MDNode::getDistinct(MemAcc->getContext(), {});
    MemAcc->setMetadata("llvm.access", AccessMD);
  }
  assert(AccessMD->isDistinct() && AccessMD->getNumOperands() == 0);

  auto PackMD = Trans.TransformMD;
  auto ListMD = cast<MDNode>(PackMD->getOperand(2).get());

  SmallVector<Metadata *, 8> NewList;
  NewList.reserve(ListMD->getNumOperands() + 1);
  NewList.append(ListMD->op_begin(), ListMD->op_end());
  NewList.push_back(AccessMD);

  auto NewListMD = MDNode::get(MemAcc->getContext(), NewList);
  PackMD->replaceOperandWith(2, NewListMD);
}

void LoopInfoStack::InsertHelper(Instruction *I) const {
  if (I->mayReadOrWriteMemory()) {
    SmallVector<Metadata *, 4> AccessGroups;
    for (const LoopInfo *AL : Active) {
      // Here we assume that every loop that has an access group is parallel.
      if (MDNode *Group = AL->getAccessGroup())
        AccessGroups.push_back(Group);
    }
    MDNode *UnionMD = nullptr;
    if (AccessGroups.size() == 1)
      UnionMD = cast<MDNode>(AccessGroups[0]);
    else if (AccessGroups.size() >= 2)
      UnionMD = MDNode::get(I->getContext(), AccessGroups);
    I->setMetadata("llvm.access.group", UnionMD);
  }

  if (!hasInfo())
    return;

  const LoopInfo &L = getInfo();
  if (!L.getLoopID())
    return;

  if (I->isTerminator()) {
    for (BasicBlock *Succ : successors(I))
      if (Succ == L.getHeader()) {
        I->setMetadata(llvm::LLVMContext::MD_loop, L.getLoopID());
        break;
      }
    return;
  }

#if 0
  if (I->mayReadOrWriteMemory()) {
    // SmallVector<Metadata *, 2> ParallelLoopIDs;
    for (const LoopInfo *AL : Active) {
      for (auto &Trans : AL->getAttributes().TransformationStack) {
        if (Trans.Kind == LoopTransformation::Pack) {
          auto BasePtr = Trans.ArrayBasePtr;
          auto MNode = Trans.TransformMD;
          assert(BasePtr && MNode);


          if (mayUseArray(BasePtr, I))
            addArrayTransformUse(Trans, I);
        }
      }
    }
  }
#endif
}

VirtualLoopInfo *LoopInfoStack::lookupNamedLoop(StringRef LoopName){
	return NamedLoopMap.lookup(LoopName);
}

VirtualLoopInfo * LoopInfoStack::applyTransformation(const LoopTransformation &Transform, VirtualLoopInfo *PrevLoop ) {
#if 0
	if (!Transform.ApplyOns.empty()) {


		// Do not apply any more transformation without apply-on clause.
		TopLoopId=nullptr; 
		continue;
	}
#endif


	SmallVector<VirtualLoopInfo*,4> ApplyTo;
	if (!Transform.ApplyOns.empty()) {
		for (auto LN : Transform.ApplyOns) {
			auto L = lookupNamedLoop(LN);
			assert(L);
			ApplyTo.push_back(L);
		}
	} else if (PrevLoop) {
		ApplyTo.push_back(PrevLoop);
	}

	switch (Transform.Kind) {
	default:
		llvm_unreachable("unexpected transformation");
		break;
	case LoopTransformation::Id:{
		assert(ApplyTo.size()==1);
		auto Name = Transform.Name;
		auto X = NamedLoopMap.insert({Name,ApplyTo[0]});
		assert(X.second && "Name already given");
		ApplyTo[0]->Name = Name;
		ApplyTo[0]->addTransformMD(MDNode::get( Ctx, { 
			MDString::get(Ctx, "llvm.loop.id"), 
			MDString::get(Ctx, Name) }));
		ApplyTo[0]->markNondefault();
		return nullptr;
	}
	case LoopTransformation::Reversal: 
		assert(ApplyTo.size()==1);
		return applyReversal( Transform, ApplyTo[0]);
	case LoopTransformation::Tiling: 
		return applyTiling(Transform, ApplyTo);
	case LoopTransformation::Interchange: 
		return applyInterchange(Transform,ApplyTo);
	case LoopTransformation::Unrolling: 
		return applyUnrolling(Transform,ApplyTo);
	case LoopTransformation::Pack:
		return applyPack(Transform,ApplyTo);
	case LoopTransformation::ThreadParallel:
		assert(ApplyTo.size()==1);
		return applyThreadParallel(Transform,ApplyTo[0]);



		}

}

void LoopInfoStack::invalidateVirtualLoop(VirtualLoopInfo *VLI) {
	// FIXME: Inefficiency
	for (auto It = NamedLoopMap.begin(), E = NamedLoopMap.end(); It !=E; ++It ) {
		if (It->second==VLI)
			NamedLoopMap.erase(It); // Must not invalidate iterator.
	}
}


void LoopInfoStack::finish(){
	//TODO: Replace TempLoopID with non-temp MDNodes

	for (auto T : PendingTransformations) {
		applyTransformation(T);
	}
	PendingTransformations.clear();

	for (auto L : OriginalLoops) {
		L->finish(*this);
	}

	// FIXME: Only accesses within the array-packing loop would be sufficient.
	for (auto X : AccessesToTrack) {
		auto PtrVar = X.first;
		auto MD = X.second;

		DenseSet<Value *> Closed;
		SmallVector<Value *, 16> Worklist;
		SmallVector<Instruction *, 16> Accesses;

		// Get the llvm::Value for the pointer.
		for (auto U : PtrVar->users()) {
			auto UserInst = dyn_cast<LoadInst>(U);
			if (!UserInst)
				continue;
			assert(UserInst->getType()->isPointerTy());
			Worklist.push_back(UserInst);
		}


		while (!Worklist.empty()) {
			auto T = Worklist.pop_back_val();
			if (Closed.count(T))
				continue;
			Closed.insert(T);

			auto Inst = dyn_cast<Instruction>(T);
			if (!Inst)
				continue;



	



			// TODO: Whitelist of instructions that 'derive' from a pointer.
			for (auto Op : T->users())  {
				if (isa<Instruction>(Op)&& cast<Instruction>(Op)  ->mayReadOrWriteMemory())
					Accesses.push_back(cast<Instruction>( Op));

				// Only follow pointers.
				if (!Op->getType()->isPointerTy())
					continue;

				// Don't follow memory indirections.
				if (auto LI = dyn_cast<LoadInst>(Op)) 
					continue;


				// Don't know wheter a return by a function is relatated to the memory address.
				if (auto CB = dyn_cast<CallBase>(Op)) {
					switch (CB->getIntrinsicID()) {
						// TODO: more whitelisted functions.
					case Intrinsic::expect:
						break;
					default:
						continue;
					}
				}


				
					Worklist.push_back(Op);
			}
		}


		for (auto A : Accesses) {
			assert(!A->getMetadata("llvm.access"));
			A->setMetadata("llvm.access", MD);
		}

	}

	for (auto L : OriginalLoops) {
		delete L;
	}
	OriginalLoops.clear();
	NamedLoopMap.clear();
}
