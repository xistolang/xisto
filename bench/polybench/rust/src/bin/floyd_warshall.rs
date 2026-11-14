// floyd-warshall: the dataset from the command line (medley/floyd-warshall/floyd-warshall.h), then the kernel, its time and the dump.
use polybench_rs::medley::floyd_warshall::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<60>(), // MINI_DATASET: N = 60
        1 => run::<180>(), // SMALL_DATASET: N = 180
        2 => run::<500>(), // MEDIUM_DATASET: N = 500
        3 => run::<2800>(), // LARGE_DATASET: N = 2800
        4 => run::<5600>(), // EXTRALARGE_DATASET: N = 5600
        _ => unreachable!(),
    }
}
