// 2mm: the dataset from the command line (linear-algebra/kernels/2mm/2mm.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::kernels::_2mm::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<16, 18, 22, 24>(), // MINI_DATASET: NI = 16, NJ = 18, NK = 22, NL = 24
        1 => run::<40, 50, 70, 80>(), // SMALL_DATASET: NI = 40, NJ = 50, NK = 70, NL = 80
        2 => run::<180, 190, 210, 220>(), // MEDIUM_DATASET: NI = 180, NJ = 190, NK = 210, NL = 220
        3 => run::<800, 900, 1100, 1200>(), // LARGE_DATASET: NI = 800, NJ = 900, NK = 1100, NL = 1200
        4 => run::<1600, 1800, 2200, 2400>(), // EXTRALARGE_DATASET: NI = 1600, NJ = 1800, NK = 2200, NL = 2400
        _ => unreachable!(),
    }
}
