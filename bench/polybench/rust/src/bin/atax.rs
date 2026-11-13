// atax: the dataset from the command line (linear-algebra/kernels/atax/atax.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::kernels::atax::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<38, 42>(), // MINI_DATASET: M = 38, N = 42
        1 => run::<116, 124>(), // SMALL_DATASET: M = 116, N = 124
        2 => run::<390, 410>(), // MEDIUM_DATASET: M = 390, N = 410
        3 => run::<1900, 2100>(), // LARGE_DATASET: M = 1900, N = 2100
        4 => run::<1800, 2200>(), // EXTRALARGE_DATASET: M = 1800, N = 2200
        _ => unreachable!(),
    }
}
