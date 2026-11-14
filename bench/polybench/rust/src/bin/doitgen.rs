// doitgen: the dataset from the command line (linear-algebra/kernels/doitgen/doitgen.h), then the kernel, its time and the dump.
use polybench_rs::linear_algebra::kernels::doitgen::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<12, 8, 10>(), // MINI_DATASET: NP = 12, NQ = 8, NR = 10
        1 => run::<30, 20, 25>(), // SMALL_DATASET: NP = 30, NQ = 20, NR = 25
        2 => run::<60, 40, 50>(), // MEDIUM_DATASET: NP = 60, NQ = 40, NR = 50
        3 => run::<160, 140, 150>(), // LARGE_DATASET: NP = 160, NQ = 140, NR = 150
        4 => run::<270, 220, 250>(), // EXTRALARGE_DATASET: NP = 270, NQ = 220, NR = 250
        _ => unreachable!(),
    }
}
