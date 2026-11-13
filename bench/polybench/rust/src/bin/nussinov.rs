// nussinov: the dataset from the command line (medley/nussinov/nussinov.h), then the kernel, its time and the dump.
use polybench_rs::medley::nussinov::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<60>(), // MINI_DATASET: N = 60
        1 => run::<180>(), // SMALL_DATASET: N = 180
        2 => run::<500>(), // MEDIUM_DATASET: N = 500
        3 => run::<2500>(), // LARGE_DATASET: N = 2500
        4 => run::<5500>(), // EXTRALARGE_DATASET: N = 5500
        _ => unreachable!(),
    }
}
