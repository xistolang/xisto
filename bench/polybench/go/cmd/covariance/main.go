// covariance: Go transliteration of datamining/covariance/covariance.c
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
func initArray(m, n int, floatN *float64, data *[N][M]float64) {
	*floatN = float64(n)
	for i := 0; i < N; i++ {
		for j := 0; j < M; j++ {
			data[i][j] = (float64(i) * float64(j)) / M
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(m int, cov *[M][M]float64) {
	dump.Start()
	dump.Begin("cov")
	for i := 0; i < m; i++ {
		for j := 0; j < m; j++ {
			if (i*m+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(cov[i][j])
		}
	}
	dump.End("cov")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
func kernelCovariance(m, n int, floatN float64, data *[N][M]float64, cov *[M][M]float64, mean *[M]float64) {
	for j := 0; j < m; j++ {
		mean[j] = 0.0
		for i := 0; i < n; i++ {
			mean[j] += data[i][j]
		}
		mean[j] /= floatN
	}

	for i := 0; i < n; i++ {
		for j := 0; j < m; j++ {
			data[i][j] -= mean[j]
		}
	}

	for i := 0; i < m; i++ {
		for j := i; j < m; j++ {
			cov[i][j] = 0.0
			for k := 0; k < n; k++ {
				cov[i][j] += float64(data[k][i] * data[k][j])
			}
			cov[i][j] /= (floatN - 1.0)
			cov[j][i] = cov[i][j]
		}
	}
}

func main() {
	// Retrieve problem size.
	n := N
	m := M

	// Variable declaration/allocation.
	var floatN float64
	data := new([N][M]float64)
	cov := new([M][M]float64)
	mean := new([M]float64)

	// Initialize array(s).
	initArray(m, n, &floatN, data)

	// Run kernel.
	start := time.Now()
	kernelCovariance(m, n, floatN, data, cov, mean)
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
		printArray(m, cov)
	}
}
