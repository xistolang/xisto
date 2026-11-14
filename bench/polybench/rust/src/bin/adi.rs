// adi: the dataset from the command line (stencils/adi/adi.h), then the kernel, its time and the dump.
use polybench_rs::stencils::adi::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<20, 20>(), // MINI_DATASET: N = 20, TSTEPS = 20
        1 => run::<60, 40>(), // SMALL_DATASET: N = 60, TSTEPS = 40
        2 => run::<200, 100>(), // MEDIUM_DATASET: N = 200, TSTEPS = 100
        3 => run::<1000, 500>(), // LARGE_DATASET: N = 1000, TSTEPS = 500
        4 => run::<2000, 1000>(), // EXTRALARGE_DATASET: N = 2000, TSTEPS = 1000
        _ => unreachable!(),
    }
}
