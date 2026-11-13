// fdtd-2d: the dataset from the command line (stencils/fdtd-2d/fdtd-2d.h), then the kernel, its time and the dump.
use polybench_rs::stencils::fdtd_2d::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<20, 30, 20>(), // MINI_DATASET: NX = 20, NY = 30, TMAX = 20
        1 => run::<60, 80, 40>(), // SMALL_DATASET: NX = 60, NY = 80, TMAX = 40
        2 => run::<200, 240, 100>(), // MEDIUM_DATASET: NX = 200, NY = 240, TMAX = 100
        3 => run::<1000, 1200, 500>(), // LARGE_DATASET: NX = 1000, NY = 1200, TMAX = 500
        4 => run::<2000, 2600, 1000>(), // EXTRALARGE_DATASET: NX = 2000, NY = 2600, TMAX = 1000
        _ => unreachable!(),
    }
}
