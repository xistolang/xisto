// heat-3d: the dataset from the command line (stencils/heat-3d/heat-3d.h), then the kernel, its time and the dump.
use polybench_rs::stencils::heat_3d::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<10, 20>(), // MINI_DATASET: N = 10, TSTEPS = 20
        1 => run::<20, 40>(), // SMALL_DATASET: N = 20, TSTEPS = 40
        2 => run::<40, 100>(), // MEDIUM_DATASET: N = 40, TSTEPS = 100
        3 => run::<120, 500>(), // LARGE_DATASET: N = 120, TSTEPS = 500
        4 => run::<200, 1000>(), // EXTRALARGE_DATASET: N = 200, TSTEPS = 1000
        _ => unreachable!(),
    }
}
