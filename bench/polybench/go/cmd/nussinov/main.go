// nussinov: Go transliteration of medley/nussinov/nussinov.c
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

// RNA bases represented as chars, range is [0,3]
type base = int8

func match(b1, b2 base) int32 {
	if b1+b2 == 3 {
		return 1
	}
	return 0
}

func maxScore(s1, s2 int32) int32 {
	if s1 >= s2 {
		return s1
	}
	return s2
}

// Array initialization.
func initArray(n int, seq *[N]base, table *[N][N]int32) {
	// base is AGCT/0..3
	for i := 0; i < n; i++ {
		seq[i] = base((i + 1) % 4)
	}

	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			table[i][j] = 0
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(n int, table *[N][N]int32) {
	t := 0

	dump.Start()
	dump.Begin("table")
	for i := 0; i < n; i++ {
		for j := i; j < n; j++ {
			if t%20 == 0 {
				dump.Newline()
			}
			dump.Int(table[i][j])
			t++
		}
	}
	dump.End("table")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
// Original version by Dave Wonnacott at Haverford College <davew@cs.haverford.edu>,
// with help from Allison Lake, Ting Zhou, and Tian Jin,
// based on algorithm by Nussinov, described in Allison Lake's senior thesis.
func kernelNussinov(n int, seq *[N]base, table *[N][N]int32) {
	for i := n - 1; i >= 0; i-- {
		for j := i + 1; j < n; j++ {
			if j-1 >= 0 {
				table[i][j] = maxScore(table[i][j], table[i][j-1])
			}
			if i+1 < n {
				table[i][j] = maxScore(table[i][j], table[i+1][j])
			}

			if j-1 >= 0 && i+1 < n {
				// don't allow adjacent elements to bond
				if i < j-1 {
					table[i][j] = maxScore(table[i][j], table[i+1][j-1]+match(seq[i], seq[j]))
				} else {
					table[i][j] = maxScore(table[i][j], table[i+1][j-1])
				}
			}

			for k := i + 1; k < j; k++ {
				table[i][j] = maxScore(table[i][j], table[i][k]+table[k+1][j])
			}
		}
	}
}

func main() {
	// Retrieve problem size.
	n := N

	// Variable declaration/allocation.
	seq := new([N]base)
	table := new([N][N]int32)

	// Initialize array(s).
	initArray(n, seq, table)

	// Run kernel.
	kernelNussinov(n, seq, table)

	// Prevent dead-code elimination. All live-out data must be printed.
	if dump.Enabled || len(os.Args) > 42 && os.Args[0] == "" {
		printArray(n, table)
	}
}
