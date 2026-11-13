// 3mm: Go transliteration of linear-algebra/kernels/3mm/3mm.c
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
func initArray(ni, nj, nk, nl, nm int, A *[NI][NK]float64, B *[NK][NJ]float64, C *[NJ][NM]float64, D *[NM][NL]float64) {
	for i := 0; i < ni; i++ {
		for j := 0; j < nk; j++ {
			A[i][j] = float64((i*j+1)%ni) / float64(5*ni)
		}
	}
	for i := 0; i < nk; i++ {
		for j := 0; j < nj; j++ {
			B[i][j] = float64((i*(j+1)+2)%nj) / float64(5*nj)
		}
	}
	for i := 0; i < nj; i++ {
		for j := 0; j < nm; j++ {
			C[i][j] = float64(i*(j+3)%nl) / float64(5*nl)
		}
	}
	for i := 0; i < nm; i++ {
		for j := 0; j < nl; j++ {
			D[i][j] = float64((i*(j+2)+2)%nk) / float64(5*nk)
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(ni, nl int, G *[NI][NL]float64) {
	dump.Start()
	dump.Begin("G")
	for i := 0; i < ni; i++ {
		for j := 0; j < nl; j++ {
			if (i*ni+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(G[i][j])
		}
	}
	dump.End("G")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
func kernel3mm(ni, nj, nk, nl, nm int, E *[NI][NJ]float64, A *[NI][NK]float64, B *[NK][NJ]float64, F *[NJ][NL]float64, C *[NJ][NM]float64, D *[NM][NL]float64, G *[NI][NL]float64) {
	// E := A*B
	for i := 0; i < ni; i++ {
		for j := 0; j < nj; j++ {
			E[i][j] = 0.0
			for k := 0; k < nk; k++ {
				E[i][j] += float64(A[i][k] * B[k][j])
			}
		}
	}
	// F := C*D
	for i := 0; i < nj; i++ {
		for j := 0; j < nl; j++ {
			F[i][j] = 0.0
			for k := 0; k < nm; k++ {
				F[i][j] += float64(C[i][k] * D[k][j])
			}
		}
	}
	// G := E*F
	for i := 0; i < ni; i++ {
		for j := 0; j < nl; j++ {
			G[i][j] = 0.0
			for k := 0; k < nj; k++ {
				G[i][j] += float64(E[i][k] * F[k][j])
			}
		}
	}
}

func main() {
	// Retrieve problem size.
	ni := NI
	nj := NJ
	nk := NK
	nl := NL
	nm := NM

	// Variable declaration/allocation.
	E := new([NI][NJ]float64)
	A := new([NI][NK]float64)
	B := new([NK][NJ]float64)
	F := new([NJ][NL]float64)
	C := new([NJ][NM]float64)
	D := new([NM][NL]float64)
	G := new([NI][NL]float64)

	// Initialize array(s).
	initArray(ni, nj, nk, nl, nm, A, B, C, D)

	// Run kernel.
	start := time.Now()
	kernel3mm(ni, nj, nk, nl, nm, E, A, B, F, C, D, G)
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
		printArray(ni, nl, G)
	}
}
