#![allow(non_snake_case)]

use crate::config::linear_algebra::blas::syrk::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize, const M: usize>(
    n: usize,
    m: usize,
    alpha: &mut DataType,
    beta: &mut DataType,
    C: &mut Array2D<DataType, N, N>,
    A: &mut Array2D<DataType, N, M>,
) {
    *alpha = 1.5;
    *beta = 1.2;
    for i in 0..n {
        for j in 0..m {
            A[i][j] = ((i * j + 1) % n) as DataType / n as DataType;
        }
    }

    for i in 0..n {
        for j in 0..n {
            C[i][j] = ((i * j + 2) % m) as DataType / m as DataType;
        }
    }
}

unsafe fn kernel_syrk<const N: usize, const M: usize>(
    n: usize,
    m: usize,
    alpha: DataType,
    beta: DataType,
    C: &mut Array2D<DataType, N, N>,
    A: &Array2D<DataType, N, M>,
) {
    for i in 0..n {
        for j in 0..=i {
            C[i][j] *= beta;
        }
        for k in 0..m {
            for j in 0..=i {
                C[i][j] += alpha * A[i][k] * A[j][k];
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(n: usize, C: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("C");
    for i in 0..n {
        for j in 0..n {
            if (i * n + j) % 20 == 0 {
                d.nl();
            }
            d.f64(C[i][j]);
        }
    }
    d.end("C");
    d.finish();
}

pub fn run<const N: usize, const M: usize>() {
    let n = N;
    let m = M;

    let mut alpha = 0.0;
    let mut beta = 0.0;
    let mut C = Array2D::<DataType, N, N>::zeroed();
    let mut A = Array2D::<DataType, N, M>::zeroed();

    unsafe {
        init_array(n, m, &mut alpha, &mut beta, &mut C, &mut A);
        let t = util::Timer::start();
        kernel_syrk(n, m, alpha, beta, &mut C, &A);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &C); }
    }
}

#[test]
fn check() {
    run::<10, 12>();
}
