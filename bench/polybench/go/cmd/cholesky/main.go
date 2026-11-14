// cholesky: Go transliteration of linear-algebra/solvers/cholesky/cholesky.c
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
	"fmt"
	"math"
	"os"
	"time"

	"polybench/dump"
)

// Array initialization.
func initArray(n int, A *[N][N]float64) {
	for i := 0; i < n; i++ {
		for j := 0; j <= i; j++ {
			A[i][j] = float64(-j%n)/float64(n) + 1
		}
		for j := i + 1; j < n; j++ {
			A[i][j] = 0
		}
		A[i][i] = 1
	}

	// Make the matrix positive semi-definite.
	B := new([N][N]float64)
	for r := 0; r < n; r++ {
		for s := 0; s < n; s++ {
			B[r][s] = 0
		}
	}
	for t := 0; t < n; t++ {
		for r := 0; r < n; r++ {
			for s := 0; s < n; s++ {
				B[r][s] += float64(A[r][t] * A[s][t])
			}
		}
	}
	for r := 0; r < n; r++ {
		for s := 0; s < n; s++ {
			A[r][s] = B[r][s]
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(n int, A *[N][N]float64) {
	dump.Start()
	dump.Begin("A")
	for i := 0; i < n; i++ {
		for j := 0; j <= i; j++ {
			if (i*n+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(A[i][j])
		}
	}
	dump.End("A")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
func kernelCholesky(n int, A *[N][N]float64) {
	for i := 0; i < n; i++ {
		// j<i
		for j := 0; j < i; j++ {
			for k := 0; k < j; k++ {
				A[i][j] -= float64(A[i][k] * A[j][k])
			}
			A[i][j] /= A[j][j]
		}
		// i==j case
		for k := 0; k < i; k++ {
			A[i][i] -= float64(A[i][k] * A[i][k])
		}
		A[i][i] = math.Sqrt(A[i][i])
	}
}

func main() {
	// Retrieve problem size.
	n := N

	// Variable declaration/allocation.
	A := new([N][N]float64)

	// Initialize array(s).
	initArray(n, A)

	// Run kernel.
	start := time.Now()
	kernelCholesky(n, A)
	elapsed := time.Since(start)

	// Print timer: the kernel's seconds on stdout with the "time" argument (-DPOLYBENCH_TIME).
	for _, a := range os.Args[1:] {
		if a == "time" {
			fmt.Printf("%0.6f\n", elapsed.Seconds())
			break
		}
	}

	// Prevent dead-code elimination. All live-out data must be printed.
	if dump.Enabled || len(os.Args) > 42 && os.Args[0] == "" {
		printArray(n, A)
	}
}
