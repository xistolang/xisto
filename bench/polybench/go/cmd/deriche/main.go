// deriche: Go transliteration of medley/deriche/deriche.c
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
	"math"
	"os"

	"polybench/dump"
)

// DATA_TYPE is float in deriche.h: EXP_FUN is expf and POW_FUN is powf.
// Go has no float32 math, so they go through float64 and round back.
func expf(x float32) float32    { return float32(math.Exp(float64(x))) }
func powf(x, y float32) float32 { return float32(math.Pow(float64(x), float64(y))) }

// Array initialization.
func initArray(w, h int, alpha *float32, imgIn, imgOut *[W][H]float32) {
	*alpha = 0.25 // parameter of the filter

	// input should be between 0 and 1 (grayscale image pixel)
	for i := 0; i < w; i++ {
		for j := 0; j < h; j++ {
			imgIn[i][j] = float32((313*i+991*j)%65536) / 65535.0
		}
	}
}

// DCE code. Must scan the entire live-out data.
func printArray(w, h int, imgOut *[W][H]float32) {
	dump.Start()
	dump.Begin("imgOut")
	for i := 0; i < w; i++ {
		for j := 0; j < h; j++ {
			if (i*h+j)%20 == 0 {
				dump.Newline()
			}
			dump.Float(float64(imgOut[i][j]))
		}
	}
	dump.End("imgOut")
	dump.Finish()
	dump.Flush()
}

// Main computational kernel.
// Original code provided by Gael Deest
func kernelDeriche(w, h int, alpha float32, imgIn, imgOut, y1, y2 *[W][H]float32) {
	var xm1, tm1, ym1, ym2 float32
	var xp1, xp2 float32
	var tp1, tp2 float32
	var yp1, yp2 float32

	var k float32
	var a1, a2, a3, a4, a5, a6, a7, a8 float32
	var b1, b2, c1, c2 float32

	k = (1.0 - expf(-alpha)) * (1.0 - expf(-alpha)) / (1.0 + float32(2.0*alpha*expf(-alpha)) - expf(2.0*alpha))
	a1 = k
	a5 = k
	a2 = k * expf(-alpha) * (alpha - 1.0)
	a6 = a2
	a3 = k * expf(-alpha) * (alpha + 1.0)
	a7 = a3
	a4 = -k * expf(-2.0*alpha)
	a8 = a4
	b1 = powf(2.0, -alpha)
	b2 = -expf(-2.0 * alpha)
	c1 = 1
	c2 = 1

	for i := 0; i < w; i++ {
		ym1 = 0.0
		ym2 = 0.0
		xm1 = 0.0
		for j := 0; j < h; j++ {
			y1[i][j] = float32(a1*imgIn[i][j]) + float32(a2*xm1) + float32(b1*ym1) + float32(b2*ym2)
			xm1 = imgIn[i][j]
			ym2 = ym1
			ym1 = y1[i][j]
		}
	}

	for i := 0; i < w; i++ {
		yp1 = 0.0
		yp2 = 0.0
		xp1 = 0.0
		xp2 = 0.0
		for j := h - 1; j >= 0; j-- {
			y2[i][j] = float32(a3*xp1) + float32(a4*xp2) + float32(b1*yp1) + float32(b2*yp2)
			xp2 = xp1
			xp1 = imgIn[i][j]
			yp2 = yp1
			yp1 = y2[i][j]
		}
	}

	for i := 0; i < w; i++ {
		for j := 0; j < h; j++ {
			imgOut[i][j] = c1 * (y1[i][j] + y2[i][j])
		}
	}

	for j := 0; j < h; j++ {
		tm1 = 0.0
		ym1 = 0.0
		ym2 = 0.0
		for i := 0; i < w; i++ {
			y1[i][j] = float32(a5*imgOut[i][j]) + float32(a6*tm1) + float32(b1*ym1) + float32(b2*ym2)
			tm1 = imgOut[i][j]
			ym2 = ym1
			ym1 = y1[i][j]
		}
	}

	for j := 0; j < h; j++ {
		tp1 = 0.0
		tp2 = 0.0
		yp1 = 0.0
		yp2 = 0.0
		for i := w - 1; i >= 0; i-- {
			y2[i][j] = float32(a7*tp1) + float32(a8*tp2) + float32(b1*yp1) + float32(b2*yp2)
			tp2 = tp1
			tp1 = imgOut[i][j]
			yp2 = yp1
			yp1 = y2[i][j]
		}
	}

	for i := 0; i < w; i++ {
		for j := 0; j < h; j++ {
			imgOut[i][j] = c2 * (y1[i][j] + y2[i][j])
		}
	}
}

func main() {
	// Retrieve problem size.
	w := W
	h := H

	// Variable declaration/allocation.
	var alpha float32
	imgIn := new([W][H]float32)
	imgOut := new([W][H]float32)
	y1 := new([W][H]float32)
	y2 := new([W][H]float32)

	// Initialize array(s).
	initArray(w, h, &alpha, imgIn, imgOut)

	// Run kernel.
	kernelDeriche(w, h, alpha, imgIn, imgOut, y1, y2)

	// Prevent dead-code elimination. All live-out data must be printed.
	if dump.Enabled || len(os.Args) > 42 && os.Args[0] == "" {
		printArray(w, h, imgOut)
	}
}
