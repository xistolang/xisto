// gesummv: the dataset from the command line (linear-algebra/blas/gesummv/gesummv.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::blas::gesummv::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<30>(), // MINI_DATASET: N = 30
        1 => run::<90>(), // SMALL_DATASET: N = 90
        2 => run::<250>(), // MEDIUM_DATASET: N = 250
        3 => run::<1300>(), // LARGE_DATASET: N = 1300
        4 => run::<2800>(), // EXTRALARGE_DATASET: N = 2800
        _ => unreachable!(),
    }
}
