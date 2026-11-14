// Go side of PolyBench/C 4.2.1 (d3a15bb): a line-by-line transliteration of the
// 30 kernels, one cmd/<kernel>/main.go each (no community Go port exists).
//
// Build:  go build -o bin/<kernel> ./cmd/<kernel>   (LARGE_DATASET, dump on)
//         -tags mini    MINI_DATASET sizes (mini.go instead of large.go)
//         -tags nodump  no dump, like C without -DPOLYBENCH_DUMP_ARRAYS
//
// Choices:
//   - double A[N][M] -> fixed-size Go arrays allocated on the heap with
//     new([N][M]float64), passed as *[N][M]float64: contiguous, row-major,
//     sizes as constants, the direct equivalent of the C declaration.
//   - Types as in C: float64; deriche float32 (expf/powf through float64);
//     floyd-warshall and nussinov int32 (nussinov's char base -> int8).
//   - FMA: Go may fuse x*y+z (arm64; amd64 with GOAMD64=v3). Every product
//     that is added or subtracted is written float64(x*y) (float32 in
//     deriche): the spec says an explicit conversion rounds and prevents
//     fusion, so the results are the doubles of C with -ffp-contract=off.
//   - No goroutines, no unsafe, no hand optimisation.
//   - The dump (stderr) is byte-identical to C with -DPOLYBENCH_DUMP_ARRAYS.
module polybench

go 1.24
