//===- ProfileSummaryInfoTest.cpp - ProfileSummaryInfo unit tests ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BlockFrequencyInfoImpl.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

namespace llvm {
namespace {

class ProfileSummaryInfoTest : public testing::Test {
protected:
  LLVMContext C;
  std::unique_ptr<BranchProbabilityInfo> BPI;
  std::unique_ptr<DominatorTree> DT;
  std::unique_ptr<LoopInfo> LI;

  ProfileSummaryInfo buildPSI(Module *M) {
    return ProfileSummaryInfo(*M);
  }
  BlockFrequencyInfo buildBFI(Function &F) {
    DT.reset(new DominatorTree(F));
    LI.reset(new LoopInfo(*DT));
    BPI.reset(new BranchProbabilityInfo(F, *LI));
    return BlockFrequencyInfo(F, *BPI, *LI);
  }
  std::unique_ptr<Module> makeLLVMModule(const char *ProfKind = nullptr) {
    const char *ModuleString =
        "define i32 @g(i32 %x) !prof !21 {{\n"
        "  ret i32 0\n"
        "}\n"
        "define i32 @h(i32 %x) !prof !22 {{\n"
        "  ret i32 0\n"
        "}\n"
        "define i32 @f(i32 %x) !prof !20 {{\n"
        "bb0:\n"
        "  %y1 = icmp eq i32 %x, 0 \n"
        "  br i1 %y1, label %bb1, label %bb2, !prof !23 \n"
        "bb1:\n"
        "  %z1 = call i32 @g(i32 %x)\n"
        "  br label %bb3\n"
        "bb2:\n"
        "  %z2 = call i32 @h(i32 %x)\n"
        "  br label %bb3\n"
        "bb3:\n"
        "  %y2 = phi i32 [0, %bb1], [1, %bb2] \n"
        "  ret i32 %y2\n"
        "}\n"
        "!20 = !{{!\"function_entry_count\", i64 400}\n"
        "!21 = !{{!\"function_entry_count\", i64 1}\n"
        "!22 = !{{!\"function_entry_count\", i64 100}\n"
        "!23 = !{{!\"branch_weights\", i32 64, i32 4}\n"
        "{0}";
    const char *SummaryString = "!llvm.module.flags = !{{!1}"
                                "!1 = !{{i32 1, !\"ProfileSummary\", !2}"
                                "!2 = !{{!3, !4, !5, !6, !7, !8, !9, !10}"
                                "!3 = !{{!\"ProfileFormat\", !\"{0}\"}"
                                "!4 = !{{!\"TotalCount\", i64 10000}"
                                "!5 = !{{!\"MaxCount\", i64 10}"
                                "!6 = !{{!\"MaxInternalCount\", i64 1}"
                                "!7 = !{{!\"MaxFunctionCount\", i64 1000}"
                                "!8 = !{{!\"NumCounts\", i64 3}"
                                "!9 = !{{!\"NumFunctions\", i64 3}"
                                "!10 = !{{!\"DetailedSummary\", !11}"
                                "!11 = !{{!12, !13, !14}"
                                "!12 = !{{i32 10000, i64 1000, i32 1}"
                                "!13 = !{{i32 999000, i64 300, i32 3}"
                                "!14 = !{{i32 999999, i64 5, i32 10}";
    SMDiagnostic Err;
    if (ProfKind)
      return parseAssemblyString(
          formatv(ModuleString, formatv(SummaryString, ProfKind).str()).str(),
          Err, C);
    else
      return parseAssemblyString(formatv(ModuleString, "").str(), Err, C);
  }
};

TEST_F(ProfileSummaryInfoTest, TestNoProfile) {
  auto M = makeLLVMModule(/*ProfKind=*/nullptr);
  Function *F = M->getFunction("f");

  ProfileSummaryInfo PSI = buildPSI(M.get());
  // In the absence of profiles, is{Hot|Cold}X methods should always return
  // false.
  EXPECT_FALSE(PSI.isHotCount(1000));
  EXPECT_FALSE(PSI.isHotCount(0));
  EXPECT_FALSE(PSI.isColdCount(1000));
  EXPECT_FALSE(PSI.isColdCount(0));

  EXPECT_FALSE(PSI.isFunctionEntryHot(F));
  EXPECT_FALSE(PSI.isFunctionEntryCold(F));

  BasicBlock &BB0 = F->getEntryBlock();
  BasicBlock *BB1 = BB0.getTerminator()->getSuccessor(0);

  BlockFrequencyInfo BFI = buildBFI(*F);
  EXPECT_FALSE(PSI.isHotBB(&BB0, &BFI));
  EXPECT_FALSE(PSI.isColdBB(&BB0, &BFI));

  CallSite CS1(BB1->getFirstNonPHI());
  EXPECT_FALSE(PSI.isHotCallSite(CS1, &BFI));
  EXPECT_FALSE(PSI.isColdCallSite(CS1, &BFI));
}
TEST_F(ProfileSummaryInfoTest, TestCommon) {
  auto M = makeLLVMModule("InstrProf");
  Function *F = M->getFunction("f");
  Function *G = M->getFunction("g");
  Function *H = M->getFunction("h");

  ProfileSummaryInfo PSI = buildPSI(M.get());
  EXPECT_TRUE(PSI.isHotCount(400));
  EXPECT_TRUE(PSI.isColdCount(2));
  EXPECT_FALSE(PSI.isColdCount(100));
  EXPECT_FALSE(PSI.isHotCount(100));

  EXPECT_TRUE(PSI.isFunctionEntryHot(F));
  EXPECT_FALSE(PSI.isFunctionEntryHot(G));
  EXPECT_FALSE(PSI.isFunctionEntryHot(H));
}

TEST_F(ProfileSummaryInfoTest, InstrProf) {
  auto M = makeLLVMModule("InstrProf");
  Function *F = M->getFunction("f");
  ProfileSummaryInfo PSI = buildPSI(M.get());

  BasicBlock &BB0 = F->getEntryBlock();
  BasicBlock *BB1 = BB0.getTerminator()->getSuccessor(0);
  BasicBlock *BB2 = BB0.getTerminator()->getSuccessor(1);
  BasicBlock *BB3 = BB1->getSingleSuccessor();

  BlockFrequencyInfo BFI = buildBFI(*F);
  EXPECT_TRUE(PSI.isHotBB(&BB0, &BFI));
  EXPECT_TRUE(PSI.isHotBB(BB1, &BFI));
  EXPECT_FALSE(PSI.isHotBB(BB2, &BFI));
  EXPECT_TRUE(PSI.isHotBB(BB3, &BFI));

  CallSite CS1(BB1->getFirstNonPHI());
  auto *CI2 = BB2->getFirstNonPHI();
  CallSite CS2(CI2);

  EXPECT_TRUE(PSI.isHotCallSite(CS1, &BFI));
  EXPECT_FALSE(PSI.isHotCallSite(CS2, &BFI));

  // Test that adding an MD_prof metadata with a hot count on CS2 does not
  // change its hotness as it has no effect in instrumented profiling.
  MDBuilder MDB(M->getContext());
  CI2->setMetadata(llvm::LLVMContext::MD_prof, MDB.createBranchWeights({400}));
  EXPECT_FALSE(PSI.isHotCallSite(CS2, &BFI));
}

TEST_F(ProfileSummaryInfoTest, SampleProf) {
  auto M = makeLLVMModule("SampleProfile");
  Function *F = M->getFunction("f");
  ProfileSummaryInfo PSI = buildPSI(M.get());

  BasicBlock &BB0 = F->getEntryBlock();
  BasicBlock *BB1 = BB0.getTerminator()->getSuccessor(0);
  BasicBlock *BB2 = BB0.getTerminator()->getSuccessor(1);
  BasicBlock *BB3 = BB1->getSingleSuccessor();

  BlockFrequencyInfo BFI = buildBFI(*F);
  EXPECT_TRUE(PSI.isHotBB(&BB0, &BFI));
  EXPECT_TRUE(PSI.isHotBB(BB1, &BFI));
  EXPECT_FALSE(PSI.isHotBB(BB2, &BFI));
  EXPECT_TRUE(PSI.isHotBB(BB3, &BFI));

  CallSite CS1(BB1->getFirstNonPHI());
  auto *CI2 = BB2->getFirstNonPHI();
  CallSite CS2(CI2);

  EXPECT_TRUE(PSI.isHotCallSite(CS1, &BFI));
  EXPECT_FALSE(PSI.isHotCallSite(CS2, &BFI));

  // Test that CS2 is considered hot when it gets an MD_prof metadata with
  // weights that exceed the hot count threshold.
  MDBuilder MDB(M->getContext());
  CI2->setMetadata(llvm::LLVMContext::MD_prof, MDB.createBranchWeights({400}));
  EXPECT_TRUE(PSI.isHotCallSite(CS2, &BFI));
}

} // end anonymous namespace
} // end namespace llvm
