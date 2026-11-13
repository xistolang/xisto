// 3mm: the dataset from the command line (linear-algebra/kernels/3mm/3mm.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::kernels::_3mm::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<16, 18, 20, 22, 24>(), // MINI_DATASET: NI = 16, NJ = 18, NK = 20, NL = 22, NM = 24
        1 => run::<40, 50, 60, 70, 80>(), // SMALL_DATASET: NI = 40, NJ = 50, NK = 60, NL = 70, NM = 80
        2 => run::<180, 190, 200, 210, 220>(), // MEDIUM_DATASET: NI = 180, NJ = 190, NK = 200, NL = 210, NM = 220
        3 => run::<800, 900, 1000, 1100, 1200>(), // LARGE_DATASET: NI = 800, NJ = 900, NK = 1000, NL = 1100, NM = 1200
        4 => run::<1600, 1800, 2000, 2200, 2400>(), // EXTRALARGE_DATASET: NI = 1600, NJ = 1800, NK = 2000, NL = 2200, NM = 2400
        _ => unreachable!(),
    }
}
