// symm: Go transliteration of linear-algebra/blas/symm/symm.c
//
// Derived from PolyBench/C 4.2.1 (commit d3a15bb). The original copyright notice,
// conditions and disclaimer follow; the full text is in bench/polybench/LICENSE-PolyBench.txt.
//
// OHIO STATE UNIVERSITY SOFTWARE DISTRIBUTION LICENSE
//
// PolyBench/C, a collection of benchmarks containing static control
// parts (the “Software”)
// Copyright (c) 2010-2016, Ohio State University. All rights reserved.
//
// The Software is available for download and use subject to the terms
// and conditions of this License.  Access or use of the Software
// constitutes acceptance and agreement to the terms and conditions of
// this License.  Redistribution and use of the Software in source and
// binary forms, with or without modification, are permitted provided
// that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the capitalized paragraph below.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the capitalized paragraph below in
// the documentation and/or other materials provided with the
// distribution.
//
// 3. The name of Ohio State University, or its faculty, staff or
// students may not be used to endorse or promote products derived from
// the Software without specific prior written permission.
//
// This software was produced with support from the U.S. Defense Advanced
// Research Projects Agency (DARPA), the U.S. Department of Energy (DoE)
// and the U.S. National Science Foundation. Nothing in this work should
// be construed as reflecting the official policy or position of the
// Defense Department, the United States government or Ohio State
// University.
//
// THIS SOFTWARE HAS BEEN APPROVED FOR PUBLIC RELEASE, UNLIMITED
// DISTRIBUTION.  THE SOFTWARE IS PROVIDED “AS IS” AND WITHOUT ANY
// EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT LIMITED
// TO, WARRANTIES OF ACCURACY, COMPLETENESS, NONINFRINGEMENT,
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// ACCESS OR USE OF THE SOFTWARE IS ENTIRELY AT THE USER’S RISK.  IN NO
// EVENT SHALL OHIO STATE UNIVERSITY OR ITS FACULTY, STAFF OR STUDENTS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  THE SOFTWARE USER SHALL
// INDEMNIFY, DEFEND AND HOLD HARMLESS OHIO STATE UNIVERSITY AND ITS
// FACULTY, STAFF AND STUDENTS FROM ANY AND ALL CLAIMS, ACTIONS, DAMAGES,
// LOSSES, LIABILITIES, COSTS AND EXPENSES, INCLUDING ATTORNEYS’ FEES AND
// COURT COSTS, DIRECTLY OR INDIRECTLY ARISING OUT OF OR IN CONNECTION
// WITH ACCESS OR USE OF THE SOFTWARE.

package main

import (
	"os"

	"polybench/dump"
)

// Array initialization.
func initArray(m, n int, alpha, beta *float64, C *[M][N]float64, A *[M][M]float64, B *[M][N]float64) {
	*alpha = 1.5
	*beta = 1.2
	for i := 0; i < m; i++ {
		for j := 0; j < n; j++ {
			C[i][j] = float64((i+j)%100) / float64(m)
			B[i][j] = float64((n+i-j)%100) / float64(m)
		}
	}
	for i := 0; i < m; i++ {
		for j := 0; j <= i; j++ {
			A[i][j] = float64((i+j)%100) / float64(m)
		}
		for j := i + 1; j < m; j++ {
			A[i][j] = -999 // regions of arrays that should not be used
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(m, n int, C *[M][N]float64) {
	dump.Start()
	dump.Begin("C")
	for i := 0; i < m; i++ {
		for j := 0; j < n; j++ {
			if (i*m+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(C[i][j])
		}
	}
	dump.End("C")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
func kernelSymm(m, n int, alpha, beta float64, C *[M][N]float64, A *[M][M]float64, B *[M][N]float64) {
	var temp2 float64

	// BLAS PARAMS
	// SIDE = 'L'
	// UPLO = 'L'
	// =>  Form  C := alpha*A*B + beta*C
	// A is MxM
	// B is MxN
	// C is MxN
	// note that due to Fortran array layout, the code below more closely resembles upper triangular case in BLAS
	for i := 0; i < m; i++ {
		for j := 0; j < n; j++ {
			temp2 = 0
			for k := 0; k < i; k++ {
				C[k][j] += float64(alpha * B[i][j] * A[i][k])
				temp2 += float64(B[k][j] * A[i][k])
			}
			C[i][j] = float64(beta*C[i][j]) + float64(alpha*B[i][j]*A[i][i]) + float64(alpha*temp2)
		}
	}
}

func main() {
	// Retrieve problem size.
	m := M
	n := N

	// Variable declaration/allocation.
	var alpha, beta float64
	C := new([M][N]float64)
	A := new([M][M]float64)
	B := new([M][N]float64)

	// Initialize array(s).
	initArray(m, n, &alpha, &beta, C, A, B)

	// Run kernel.
	kernelSymm(m, n, alpha, beta, C, A, B)

	// Prevent dead-code elimination. All live-out data must be printed.
	if dump.Enabled || len(os.Args) > 42 && os.Args[0] == "" {
		printArray(m, n, C)
	}
}
