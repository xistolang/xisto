#![allow(non_snake_case)]

use crate::config::linear_algebra::blas::symm::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const M: usize, const N: usize>(
    m: usize,
    n: usize,
    alpha: &mut DataType,
    beta: &mut DataType,
    C: &mut Array2D<DataType, M, N>,
    A: &mut Array2D<DataType, M, M>,
    B: &mut Array2D<DataType, M, N>,
) {
    *alpha = 1.5;
    *beta = 1.2;
    for i in 0..m {
        for j in 0..n {
            C[i][j] = ((i + j) % 100) as DataType / m as DataType;
            B[i][j] = ((n + i - j) % 100) as DataType / m as DataType;
        }
    }

    for i in 0..m {
        for j in 0..=i {
            A[i][j] = ((i + j) % 100) as DataType / m as DataType;
        }
        for j in (i + 1)..m {
            A[i][j] = -999 as DataType;
        }
    }
}

unsafe fn kernel_symm<const M: usize, const N: usize>(
    m: usize,
    n: usize,
    alpha: DataType,
    beta: DataType,
    C: &mut Array2D<DataType, M, N>,
    A: &Array2D<DataType, M, M>,
    B: &Array2D<DataType, M, N>,
) {
    for i in 0..m {
        for j in 0..n {
            let mut temp2 = 0.0;
            for k in 0..i {
                C[k][j] += alpha * B[i][j] * A[i][k];
                temp2 += B[k][j] * A[i][k];
            }
            C[i][j] = beta * C[i][j] + alpha * B[i][j] * A[i][i] + alpha * temp2;
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(m: usize, n: usize, C: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("C");
    for i in 0..m {
        for j in 0..n {
            if (i * m + j) % 20 == 0 {
                d.nl();
            }
            d.f64(C[i][j]);
        }
    }
    d.end("C");
    d.finish();
}

pub fn run<const M: usize, const N: usize>() {
    let m = M;
    let n = N;

    let mut alpha = 0.0;
    let mut beta = 0.0;
    let mut C = Array2D::<DataType, M, N>::zeroed();
    let mut A = Array2D::<DataType, M, M>::zeroed();
    let mut B = Array2D::<DataType, M, N>::zeroed();

    unsafe {
        init_array(m, n, &mut alpha, &mut beta, &mut C, &mut A, &mut B);
        let t = util::Timer::start();
        kernel_symm(m, n, alpha, beta, &mut C, &A, &B);
        t.stop_and_print();
        if util::dump_arrays() { print_array(m, n, &C); }
    }
}

#[test]
fn check() {
    run::<10, 12>();
}
