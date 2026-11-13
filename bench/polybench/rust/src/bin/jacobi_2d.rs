// jacobi-2d: the dataset from the command line (stencils/jacobi-2d/jacobi-2d.h), then the kernel, its time and the dump.
use polybench_rs::stencils::jacobi_2d::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<30, 20>(), // MINI_DATASET: N = 30, TSTEPS = 20
        1 => run::<90, 40>(), // SMALL_DATASET: N = 90, TSTEPS = 40
        2 => run::<250, 100>(), // MEDIUM_DATASET: N = 250, TSTEPS = 100
        3 => run::<1300, 500>(), // LARGE_DATASET: N = 1300, TSTEPS = 500
        4 => run::<2800, 1000>(), // EXTRALARGE_DATASET: N = 2800, TSTEPS = 1000
        _ => unreachable!(),
    }
}
