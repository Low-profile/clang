// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc19.0.24215 -std=c99 -ast-print %s | FileCheck --check-prefix=PRINT --match-full-lines %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc19.0.24215 -std=c99 -emit-llvm -disable-llvm-passes -o - %s | FileCheck --check-prefix=IR %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc19.0.24215 -std=c99 -emit-llvm -O3 -mllvm -polly -mllvm -polly-process-unprofitable -mllvm -polly-use-llvm-names -mllvm -debug-only=polly-ast -o /dev/null %s 2>&1 > /dev/null | FileCheck --check-prefix=AST %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc19.0.24215 -std=c99 -emit-llvm -O3 -fno-unroll-loops -mllvm -polly -mllvm -polly-process-unprofitable -mllvm -polly-allow-nonaffine -mllvm -polly-use-llvm-names -o - %s | FileCheck --check-prefix=TRANS %s
// RUN: %clang -DMAIN -std=c99 -O3 -fno-unroll-loops -mllvm -polly -mllvm -polly-process-unprofitable %s -o %t_pragma_pack%exeext
// RUN: %t_pragma_pack%exeext | FileCheck --check-prefix=RESULT %s

__attribute__((noinline))
void matmul(int M, int N, int K, double C[const restrict static M][N], double A[const restrict static M][K], double B[const restrict static K][N]) {
  // #pragma clang loop(i2,i2) vectorize 
  #pragma clang loop(j2/*,k2*/) pack array(B) allocate(malloc)
  #pragma clang loop(i2/*,k2*/) pack array(A) allocate(malloc)
  #pragma clang loop(i1,j1,k1,i2,j2) interchange permutation(j1,k1,i1,j2,i2)
  #pragma clang loop(i,j,k) tile sizes(96,2048,256) pit_ids(i1,j1,k1) tile_ids(i2,j2,k2)
  #pragma clang loop id(i)
  for (int i = 0; i < M; i += 1)
    #pragma clang loop id(j)
    for (int j = 0; j < N; j += 1)
      #pragma clang loop id(k)
      for (int k = 0; k < K; k += 1)
        C[i][j] += A[i][k] * B[k][j];
}


#ifdef MAIN
#include <stdio.h>
#include <string.h>
int main() {
  double C[16][32];
  double A[16][64];
  double B[64][32];
  memset(C, 0, sizeof(C));
  memset(A, 0, sizeof(A));
  memset(B, 0, sizeof(B));
  A[1][2] = 2;
  A[1][3] = 3;
  B[2][4] = 5;
  B[3][4] = 7;
  matmul(16,32,64,C,A,B);
  printf("(%0.0f)\n", C[1][4]); // C[1][4] = A[1][2]*B[2][4] + A[1][3]*B[3][4] = 2*5 + 3*7 = 10 + 21 = 31
  return 0;
}
#endif


// PRINT-LABEL: void matmul(int M, int N, int K, double C[const restrict static M][N], double A[const restrict static M][K], double B[const restrict static K][N]) __attribute__((noinline)) {
// PRINT-NEXT: #pragma clang loop(j2) pack array(B) allocate(malloc)
// PRINT-NEXT: #pragma clang loop(i2) pack array(A) allocate(malloc)
// PRINT-NEXT: #pragma clang loop(i1, j1, k1, i2, j2) interchange permutation(j1, k1, i1, j2, i2)
// PRINT-NEXT: #pragma clang loop(i, j, k) tile sizes(96, 2048, 256) pit_ids(i1, j1, k1) tile_ids(i2, j2, k2)
// PRINT-NEXT: #pragma clang loop id(i)
// PRINT-NEXT:     for (int i = 0; i < M; i += 1)
// PRINT-NEXT: #pragma clang loop id(j)
// PRINT-NEXT:         for (int j = 0; j < N; j += 1)
// PRINT-NEXT: #pragma clang loop id(k)
// PRINT-NEXT:             for (int k = 0; k < K; k += 1)
// PRINT-NEXT:                 C[i][j] += A[i][k] * B[k][j];
// PRINT-NEXT: }


// IR-LABEL: define dso_local void @matmul(i32 %M, i32 %N, i32 %K, double* noalias nonnull %C, double* noalias nonnull %A, double* noalias nonnull %B) #0 !looptransform !2 {
// IR: !"llvm.loop.tile"
// IR: !"llvm.loop.interchange"
// IR: !"llvm.data.pack"
// IR: !"llvm.data.pack"


// AST: if (1
// AST:     // Loop_j1
// AST:     for (int c0 = 0; c0 <= floord(N - 1, 2048); c0 += 1) {
// AST:       // Loop_k1
// AST:       for (int c1 = 0; c1 <= floord(M - 1, 96); c1 += 1) {
// AST:         // Loop_i1
// AST:         for (int c2 = 0; c2 <= floord(K - 1, 256); c2 += 1) {
// AST:           for (int c6 = 0; c6 <= min(255, K - 256 * c2 - 1); c6 += 1)
// AST:             for (int c7 = 0; c7 <= min(2047, N - 2048 * c0 - 1); c7 += 1)
// AST:               CopyStmt_2(c0, c1, c2, c6, c7);
// AST:           // Loop_j2
// AST:           for (int c3 = 0; c3 <= min(2047, N - 2048 * c0 - 1); c3 += 1) {
// AST:             for (int c8 = 0; c8 <= min(95, M - 96 * c1 - 1); c8 += 1)
// AST:               for (int c9 = 0; c9 <= min(255, K - 256 * c2 - 1); c9 += 1)
// AST:                 CopyStmt_0(c0, c1, c2, c3, c8, c9);
// AST:             // Loop_i2
// AST:             for (int c4 = 0; c4 <= min(255, K - 256 * c2 - 1); c4 += 1) {
// AST:               // Loop_k2
// AST:               for (int c5 = 0; c5 <= min(95, M - 96 * c1 - 1); c5 += 1)
// AST:                 Stmt_for_body8_us_us(96 * c1 + c5, 2048 * c0 + c3, 256 * c2 + c4);
// AST:             }
// AST:             for (int c8 = 0; c8 <= min(95, M - 96 * c1 - 1); c8 += 1)
// AST:               for (int c9 = 0; c9 <= min(255, K - 256 * c2 - 1); c9 += 1)
// AST:                 CopyStmt_1(c0, c1, c2, c3, c8, c9);
// AST:           }
// AST:           for (int c6 = 0; c6 <= min(255, K - 256 * c2 - 1); c6 += 1)
// AST:             for (int c7 = 0; c7 <= min(2047, N - 2048 * c0 - 1); c7 += 1)
// AST:               CopyStmt_3(c0, c1, c2, c6, c7);
// AST:         }
// AST:       }
// AST:     }
// AST: else
// AST:     {  /* original code */ }


// TRANS: %malloccall = tail call i8* @malloc(i64 196608)
// TRANS: %malloccall84 = tail call i8* @malloc(i64 4194304)
// TRANS: tail call void @free(i8* %malloccall)
// TRANS: tail call void @free(i8* %malloccall84)
// TRANS-DAG: Packed_MemRef_A
// TRANS-DAG: Packed_MemRef_B


// RESULT: (31)
