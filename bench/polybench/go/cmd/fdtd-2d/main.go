// fdtd-2d: Go transliteration of stencils/fdtd-2d/fdtd-2d.c
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
func initArray(tmax, nx, ny int, ex, ey, hz *[NX][NY]float64, _fict_ *[TMAX]float64) {
	for i := 0; i < tmax; i++ {
		_fict_[i] = float64(i)
	}
	for i := 0; i < nx; i++ {
		for j := 0; j < ny; j++ {
			ex[i][j] = (float64(i) * float64(j+1)) / float64(nx)
			ey[i][j] = (float64(i) * float64(j+2)) / float64(ny)
			hz[i][j] = (float64(i) * float64(j+3)) / float64(nx)
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(nx, ny int, ex, ey, hz *[NX][NY]float64) {
	dump.Start()
	dump.Begin("ex")
	for i := 0; i < nx; i++ {
		for j := 0; j < ny; j++ {
			if (i*nx+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(ex[i][j])
		}
	}
	dump.End("ex")
	dump.Finish()

	dump.Begin("ey")
	for i := 0; i < nx; i++ {
		for j := 0; j < ny; j++ {
			if (i*nx+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(ey[i][j])
		}
	}
	dump.End("ey")

	dump.Begin("hz")
	for i := 0; i < nx; i++ {
		for j := 0; j < ny; j++ {
			if (i*nx+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(hz[i][j])
		}
	}
	dump.End("hz")
	dump.Flush()
}

// Main computational kernel.
func kernelFdtd2d(tmax, nx, ny int, ex, ey, hz *[NX][NY]float64, _fict_ *[TMAX]float64) {
	for t := 0; t < tmax; t++ {
		for j := 0; j < ny; j++ {
			ey[0][j] = _fict_[t]
		}
		for i := 1; i < nx; i++ {
			for j := 0; j < ny; j++ {
				ey[i][j] = ey[i][j] - float64(0.5*(hz[i][j]-hz[i-1][j]))
			}
		}
		for i := 0; i < nx; i++ {
			for j := 1; j < ny; j++ {
				ex[i][j] = ex[i][j] - float64(0.5*(hz[i][j]-hz[i][j-1]))
			}
		}
		for i := 0; i < nx-1; i++ {
			for j := 0; j < ny-1; j++ {
				hz[i][j] = hz[i][j] - float64(0.7*(ex[i][j+1]-ex[i][j]+
					ey[i+1][j]-ey[i][j]))
			}
		}
	}
}

func main() {
	// Retrieve problem size.
	tmax := TMAX
	nx := NX
	ny := NY

	// Variable declaration/allocation.
	ex := new([NX][NY]float64)
	ey := new([NX][NY]float64)
	hz := new([NX][NY]float64)
	_fict_ := new([TMAX]float64)

	// Initialize array(s).
	initArray(tmax, nx, ny, ex, ey, hz, _fict_)

	// Run kernel.
	start := time.Now()
	kernelFdtd2d(tmax, nx, ny, ex, ey, hz, _fict_)
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
		printArray(nx, ny, ex, ey, hz)
	}
}
