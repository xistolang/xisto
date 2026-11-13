// 2mm: Go transliteration of linear-algebra/kernels/2mm/2mm.c
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
func initArray(ni, nj, nk, nl int, alpha, beta *float64, A *[NI][NK]float64, B *[NK][NJ]float64, C *[NJ][NL]float64, D *[NI][NL]float64) {
	*alpha = 1.5
	*beta = 1.2
	for i := 0; i < ni; i++ {
		for j := 0; j < nk; j++ {
			A[i][j] = float64((i*j+1)%ni) / float64(ni)
		}
	}
	for i := 0; i < nk; i++ {
		for j := 0; j < nj; j++ {
			B[i][j] = float64(i*(j+1)%nj) / float64(nj)
		}
	}
	for i := 0; i < nj; i++ {
		for j := 0; j < nl; j++ {
			C[i][j] = float64((i*(j+3)+1)%nl) / float64(nl)
		}
	}
	for i := 0; i < ni; i++ {
		for j := 0; j < nl; j++ {
			D[i][j] = float64(i*(j+2)%nk) / float64(nk)
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(ni, nl int, D *[NI][NL]float64) {
	dump.Start()
	dump.Begin("D")
	for i := 0; i < ni; i++ {
		for j := 0; j < nl; j++ {
			if (i*ni+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(D[i][j])
		}
	}
	dump.End("D")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
func kernel2mm(ni, nj, nk, nl int, alpha, beta float64, tmp *[NI][NJ]float64, A *[NI][NK]float64, B *[NK][NJ]float64, C *[NJ][NL]float64, D *[NI][NL]float64) {
	// D := alpha*A*B*C + beta*D
	for i := 0; i < ni; i++ {
		for j := 0; j < nj; j++ {
			tmp[i][j] = 0.0
			for k := 0; k < nk; k++ {
				tmp[i][j] += float64(alpha * A[i][k] * B[k][j])
			}
		}
	}
	for i := 0; i < ni; i++ {
		for j := 0; j < nl; j++ {
			D[i][j] *= beta
			for k := 0; k < nj; k++ {
				D[i][j] += float64(tmp[i][k] * C[k][j])
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

	// Variable declaration/allocation.
	var alpha, beta float64
	tmp := new([NI][NJ]float64)
	A := new([NI][NK]float64)
	B := new([NK][NJ]float64)
	C := new([NJ][NL]float64)
	D := new([NI][NL]float64)

	// Initialize array(s).
	initArray(ni, nj, nk, nl, &alpha, &beta, A, B, C, D)

	// Run kernel.
	kernel2mm(ni, nj, nk, nl, alpha, beta, tmp, A, B, C, D)

	// Prevent dead-code elimination. All live-out data must be printed.
	if dump.Enabled || len(os.Args) > 42 && os.Args[0] == "" {
		printArray(ni, nl, D)
	}
}
