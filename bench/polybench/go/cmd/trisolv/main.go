// trisolv: Go transliteration of linear-algebra/solvers/trisolv/trisolv.c
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
func initArray(n int, L *[N][N]float64, x, b *[N]float64) {
	for i := 0; i < n; i++ {
		x[i] = -999
		b[i] = float64(i)
		for j := 0; j <= i; j++ {
			L[i][j] = float64(i+n-j+1) * 2 / float64(n)
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(n int, x *[N]float64) {
	dump.Start()
	dump.Begin("x")
	for i := 0; i < n; i++ {
		dump.Float(x[i])
		if i%20 == 0 {
			dump.Newline()
		}
	}
	dump.End("x")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
func kernelTrisolv(n int, L *[N][N]float64, x, b *[N]float64) {
	for i := 0; i < n; i++ {
		x[i] = b[i]
		for j := 0; j < i; j++ {
			x[i] -= float64(L[i][j] * x[j])
		}
		x[i] = x[i] / L[i][i]
	}
}

func main() {
	// Retrieve problem size.
	n := N

	// Variable declaration/allocation.
	L := new([N][N]float64)
	x := new([N]float64)
	b := new([N]float64)

	// Initialize array(s).
	initArray(n, L, x, b)

	// Run kernel.
	start := time.Now()
	kernelTrisolv(n, L, x, b)
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
		printArray(n, x)
	}
}
