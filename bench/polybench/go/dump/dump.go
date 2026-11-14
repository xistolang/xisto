// dump: Go version of the POLYBENCH_DUMP_* macros of utilities/polybench.h
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

// Package dump is the Go side of PolyBench's POLYBENCH_DUMP_* macros
// (utilities/polybench.h): it writes the live-out arrays to stderr with the
// same bytes as the C code built with -DPOLYBENCH_DUMP_ARRAYS.
package dump

import (
	"bufio"
	"math"
	"os"
	"strconv"
)

var (
	w   = bufio.NewWriterSize(os.Stderr, 1<<16)
	buf []byte
)

// Start is POLYBENCH_DUMP_START.
func Start() { w.WriteString("==BEGIN DUMP_ARRAYS==\n") }

// Finish is POLYBENCH_DUMP_FINISH.
func Finish() { w.WriteString("==END   DUMP_ARRAYS==\n") }

// Begin is POLYBENCH_DUMP_BEGIN(s).
func Begin(s string) {
	w.WriteString("begin dump: ")
	w.WriteString(s)
}

// End is POLYBENCH_DUMP_END(s).
func End(s string) {
	w.WriteString("\nend   dump: ")
	w.WriteString(s)
	w.WriteString("\n")
}

// Newline is fprintf(POLYBENCH_DUMP_TARGET, "\n").
func Newline() { w.WriteByte('\n') }

// Float is fprintf(POLYBENCH_DUMP_TARGET, "%0.2f ", x) (also "%0.2lf ").
func Float(x float64) {
	switch {
	case math.IsNaN(x):
		if math.Signbit(x) {
			w.WriteString("-nan ")
		} else {
			w.WriteString("nan ")
		}
	case math.IsInf(x, 1):
		w.WriteString("inf ")
	case math.IsInf(x, -1):
		w.WriteString("-inf ")
	default:
		buf = strconv.AppendFloat(buf[:0], x, 'f', 2, 64)
		buf = append(buf, ' ')
		w.Write(buf)
	}
}

// Int is fprintf(POLYBENCH_DUMP_TARGET, "%d ", x).
func Int(x int32) {
	buf = strconv.AppendInt(buf[:0], int64(x), 10)
	buf = append(buf, ' ')
	w.Write(buf)
}

// Flush writes what is buffered (C's stderr is unbuffered).
func Flush() { w.Flush() }
