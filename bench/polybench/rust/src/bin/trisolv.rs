// trisolv: the dataset from the command line (linear-algebra/solvers/trisolv/trisolv.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::solvers::trisolv::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<40>(), // MINI_DATASET: N = 40
        1 => run::<120>(), // SMALL_DATASET: N = 120
        2 => run::<400>(), // MEDIUM_DATASET: N = 400
        3 => run::<2000>(), // LARGE_DATASET: N = 2000
        4 => run::<4000>(), // EXTRALARGE_DATASET: N = 4000
        _ => unreachable!(),
    }
}
