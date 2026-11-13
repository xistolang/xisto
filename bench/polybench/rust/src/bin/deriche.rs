// deriche: the dataset from the command line (medley/deriche/deriche.h), then the kernel, its time and the dump.
use polybench_rs::medley::deriche::run;

fn main() {
    match polybench_rs::util::dataset() {
        0 => run::<64, 64>(), // MINI_DATASET: H = 64, W = 64
        1 => run::<128, 192>(), // SMALL_DATASET: H = 128, W = 192
        2 => run::<480, 720>(), // MEDIUM_DATASET: H = 480, W = 720
        3 => run::<2160, 4096>(), // LARGE_DATASET: H = 2160, W = 4096
        4 => run::<4320, 7680>(), // EXTRALARGE_DATASET: H = 4320, W = 7680
        _ => unreachable!(),
    }
}
