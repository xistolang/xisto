// gemm: the dataset from the command line (linear-algebra/blas/gemm/gemm.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::blas::gemm::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<20, 25, 30>(), // MINI_DATASET: NI = 20, NJ = 25, NK = 30
        1 => run::<60, 70, 80>(), // SMALL_DATASET: NI = 60, NJ = 70, NK = 80
        2 => run::<200, 220, 240>(), // MEDIUM_DATASET: NI = 200, NJ = 220, NK = 240
        3 => run::<1000, 1100, 1200>(), // LARGE_DATASET: NI = 1000, NJ = 1100, NK = 1200
        4 => run::<2000, 2300, 2600>(), // EXTRALARGE_DATASET: NI = 2000, NJ = 2300, NK = 2600
        _ => unreachable!(),
    }
}
