// bicg: the dataset from the command line (linear-algebra/kernels/bicg/bicg.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::kernels::bicg::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<42, 38>(), // MINI_DATASET: N = 42, M = 38
        1 => run::<124, 116>(), // SMALL_DATASET: N = 124, M = 116
        2 => run::<410, 390>(), // MEDIUM_DATASET: N = 410, M = 390
        3 => run::<2100, 1900>(), // LARGE_DATASET: N = 2100, M = 1900
        4 => run::<2200, 1800>(), // EXTRALARGE_DATASET: N = 2200, M = 1800
        _ => unreachable!(),
    }
}
