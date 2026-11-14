// covariance: the dataset from the command line (datamining/covariance/covariance.h), then the kernel, its time and the dump.
use polybench_rs::datamining::covariance::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<32, 28>(), // MINI_DATASET: N = 32, M = 28
        1 => run::<100, 80>(), // SMALL_DATASET: N = 100, M = 80
        2 => run::<260, 240>(), // MEDIUM_DATASET: N = 260, M = 240
        3 => run::<1400, 1200>(), // LARGE_DATASET: N = 1400, M = 1200
        4 => run::<3000, 2600>(), // EXTRALARGE_DATASET: N = 3000, M = 2600
        _ => unreachable!(),
    }
}
