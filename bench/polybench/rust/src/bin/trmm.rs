// trmm: the dataset from the command line (linear-algebra/blas/trmm/trmm.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::blas::trmm::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<20, 30>(), // MINI_DATASET: M = 20, N = 30
        1 => run::<60, 80>(), // SMALL_DATASET: M = 60, N = 80
        2 => run::<200, 240>(), // MEDIUM_DATASET: M = 200, N = 240
        3 => run::<1000, 1200>(), // LARGE_DATASET: M = 1000, N = 1200
        4 => run::<2000, 2600>(), // EXTRALARGE_DATASET: M = 2000, N = 2600
        _ => unreachable!(),
    }
}
