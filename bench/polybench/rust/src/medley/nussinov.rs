use crate::config::medley::nussinov::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

type Base = i8;

unsafe fn init_array<const N: usize>(
    n: usize,
    seq: &mut Array1D<Base, N>,
    table: &mut Array2D<DataType, N, N>,
) {
    for i in 0..n {
        seq[i] = ((i + 1) % 4) as Base;
    }

    for i in 0..n {
        for j in 0..n {
            table[i][j] = 0 as DataType;
        }
    }
}

unsafe fn kernel_nussinov<const N: usize>(
    n: usize,
    seq: &Array1D<Base, N>,
    table: &mut Array2D<DataType, N, N>,
) {
    let match_base = |b1, b2| {
        if b1 + b2 == 3 {
            1
        } else {
            0
        }
    };
    let max_score = |s1, s2| {
        if s1 >= s2 {
            s1
        } else {
            s2
        }
    };

    for i in (0..n).rev() {
        for j in (i as isize + 1)..(n as isize) {
            if j - 1 >= 0 {
                let j = j as usize;
                table[i][j] = max_score(table[i][j], table[i][j - 1]);
            }
            if i + 1 < n {
                let j = j as usize;
                table[i][j] = max_score(table[i][j], table[i + 1][j]);
            }

            if j - 1 >= 0 && i + 1 < n {
                let j = j as usize;
                if i < j - 1 {
                    table[i][j] = max_score(
                        table[i][j],
                        table[i + 1][j - 1] + match_base(seq[i], seq[j]),
                    );
                } else {
                    table[i][j] = max_score(table[i][j], table[i + 1][j - 1]);
                }
            }

            let j = j as usize;
            for k in (i + 1)..j {
                table[i][j] = max_score(table[i][j], table[i][k] + table[k + 1][j]);
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(n: usize, table: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("table");
    let mut t = 0;
    for i in 0..n {
        for j in i..n {
            if t % 20 == 0 {
                d.nl();
            }
            d.i32(table[i][j]);
            t += 1;
        }
    }
    d.end("table");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut seq = Array1D::zeroed();
    let mut table = Array2D::<DataType, N, N>::zeroed();

    unsafe {
        init_array(n, &mut seq, &mut table);
        let t = util::Timer::start();
        kernel_nussinov(n, &seq, &mut table);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &table); }
    }
}

#[test]
fn check() {
    run::<25>();
}
