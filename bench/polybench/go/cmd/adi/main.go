// adi: Go transliteration of stencils/adi/adi.c
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
func initArray(n int, u *[N][N]float64) {
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			u[i][j] = float64(i+n-j) / float64(n)
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(n int, u *[N][N]float64) {
	dump.Start()
	dump.Begin("u")
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			if (i*n+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(u[i][j])
		}
	}
	dump.End("u")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
// Based on a Fortran code fragment from Figure 5 of
// "Automatic Data and Computation Decomposition on Distributed Memory Parallel Computers"
// by Peizong Lee and Zvi Meir Kedem, TOPLAS, 2002
func kernelAdi(tsteps, n int, u, v, p, q *[N][N]float64) {
	var DX, DY, DT float64
	var B1, B2 float64
	var mul1, mul2 float64
	var a, b, c, d, e, f float64

	DX = 1.0 / float64(n)
	DY = 1.0 / float64(n)
	DT = 1.0 / float64(tsteps)
	B1 = 2.0
	B2 = 1.0
	mul1 = B1 * DT / (DX * DX)
	mul2 = B2 * DT / (DY * DY)

	a = -mul1 / 2.0
	b = 1.0 + mul1
	c = a
	d = -mul2 / 2.0
	e = 1.0 + mul2
	f = d

	for t := 1; t <= tsteps; t++ {
		// Column Sweep
		for i := 1; i < n-1; i++ {
			v[0][i] = 1.0
			p[i][0] = 0.0
			q[i][0] = v[0][i]
			for j := 1; j < n-1; j++ {
				p[i][j] = -c / (float64(a*p[i][j-1]) + b)
				q[i][j] = (float64(-d*u[j][i-1]) + float64((1.0+float64(2.0*d))*u[j][i]) - float64(f*u[j][i+1]) - float64(a*q[i][j-1])) / (float64(a*p[i][j-1]) + b)
			}

			v[n-1][i] = 1.0
			for j := n - 2; j >= 1; j-- {
				v[j][i] = float64(p[i][j]*v[j+1][i]) + q[i][j]
			}
		}
		// Row Sweep
		for i := 1; i < n-1; i++ {
			u[i][0] = 1.0
			p[i][0] = 0.0
			q[i][0] = u[i][0]
			for j := 1; j < n-1; j++ {
				p[i][j] = -f / (float64(d*p[i][j-1]) + e)
				q[i][j] = (float64(-a*v[i-1][j]) + float64((1.0+float64(2.0*a))*v[i][j]) - float64(c*v[i+1][j]) - float64(d*q[i][j-1])) / (float64(d*p[i][j-1]) + e)
			}
			u[i][n-1] = 1.0
			for j := n - 2; j >= 1; j-- {
				u[i][j] = float64(p[i][j]*u[i][j+1]) + q[i][j]
			}
		}
	}
}

func main() {
	// Retrieve problem size.
	n := N
	tsteps := TSTEPS

	// Variable declaration/allocation.
	u := new([N][N]float64)
	v := new([N][N]float64)
	p := new([N][N]float64)
	q := new([N][N]float64)

	// Initialize array(s).
	initArray(n, u)

	// Run kernel.
	start := time.Now()
	kernelAdi(tsteps, n, u, v, p, q)
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
		printArray(n, u)
	}
}
