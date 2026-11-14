// gemver: Go transliteration of linear-algebra/blas/gemver/gemver.c
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
	"os"
	"time"

	"polybench/dump"
)

// Array initialization.
func initArray(n int, alpha, beta *float64, A *[N][N]float64, u1, v1, u2, v2, w, x, y, z *[N]float64) {
	*alpha = 1.5
	*beta = 1.2

	fn := float64(n)

	for i := 0; i < n; i++ {
		u1[i] = float64(i)
		u2[i] = (float64(i+1) / fn) / 2.0
		v1[i] = (float64(i+1) / fn) / 4.0
		v2[i] = (float64(i+1) / fn) / 6.0
		y[i] = (float64(i+1) / fn) / 8.0
		z[i] = (float64(i+1) / fn) / 9.0
		x[i] = 0.0
		w[i] = 0.0
		for j := 0; j < n; j++ {
			A[i][j] = float64(i*j%n) / float64(n)
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(n int, w *[N]float64) {
	dump.Start()
	dump.Begin("w")
	for i := 0; i < n; i++ {
		if i%20 == 0 {
			dump.Newline()
		}
		dump.Float(w[i])
	}
	dump.End("w")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
func kernelGemver(n int, alpha, beta float64, A *[N][N]float64, u1, v1, u2, v2, w, x, y, z *[N]float64) {
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			A[i][j] = A[i][j] + float64(u1[i]*v1[j]) + float64(u2[i]*v2[j])
		}
	}

	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			x[i] = x[i] + float64(beta*A[j][i]*y[j])
		}
	}

	for i := 0; i < n; i++ {
		x[i] = x[i] + z[i]
	}

	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			w[i] = w[i] + float64(alpha*A[i][j]*x[j])
		}
	}
}

func main() {
	// Retrieve problem size.
	n := N

	// Variable declaration/allocation.
	var alpha, beta float64
	A := new([N][N]float64)
	u1 := new([N]float64)
	v1 := new([N]float64)
	u2 := new([N]float64)
	v2 := new([N]float64)
	w := new([N]float64)
	x := new([N]float64)
	y := new([N]float64)
	z := new([N]float64)

	// Initialize array(s).
	initArray(n, &alpha, &beta, A, u1, v1, u2, v2, w, x, y, z)

	// Run kernel.
	start := time.Now()
	kernelGemver(n, alpha, beta, A, u1, v1, u2, v2, w, x, y, z)
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
		printArray(n, w)
	}
}
