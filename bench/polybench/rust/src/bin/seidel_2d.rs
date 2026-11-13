// seidel-2d: the dataset from the command line (stencils/seidel-2d/seidel-2d.h), then the kernel, its time and the dump.
use polybench_rs::stencils::seidel_2d::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<40, 20>(), // MINI_DATASET: N = 40, TSTEPS = 20
        1 => run::<120, 40>(), // SMALL_DATASET: N = 120, TSTEPS = 40
        2 => run::<400, 100>(), // MEDIUM_DATASET: N = 400, TSTEPS = 100
        3 => run::<2000, 500>(), // LARGE_DATASET: N = 2000, TSTEPS = 500
        4 => run::<4000, 1000>(), // EXTRALARGE_DATASET: N = 4000, TSTEPS = 1000
        _ => unreachable!(),
    }
}
