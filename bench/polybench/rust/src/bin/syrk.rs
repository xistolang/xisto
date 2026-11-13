// syrk: the dataset from the command line (linear-algebra/blas/syrk/syrk.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::blas::syrk::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<30, 20>(), // MINI_DATASET: N = 30, M = 20
        1 => run::<80, 60>(), // SMALL_DATASET: N = 80, M = 60
        2 => run::<240, 200>(), // MEDIUM_DATASET: N = 240, M = 200
        3 => run::<1200, 1000>(), // LARGE_DATASET: N = 1200, M = 1000
        4 => run::<2600, 2000>(), // EXTRALARGE_DATASET: N = 2600, M = 2000
        _ => unreachable!(),
    }
}
