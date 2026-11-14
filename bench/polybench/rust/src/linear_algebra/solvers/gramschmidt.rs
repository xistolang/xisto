#![allow(non_snake_case)]

use crate::config::linear_algebra::solvers::gramschmidt::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const M: usize, const N: usize>(
    m: usize,
    n: usize,
    A: &mut Array2D<DataType, M, N>,
    R: &mut Array2D<DataType, N, N>,
    Q: &mut Array2D<DataType, M, N>,
) {
    for i in 0..m {
        for j in 0..n {
            A[i][j] = ((((i * j) % m) as DataType / m as DataType) * 100.0) + 10.0;
            Q[i][j] = 0.0;
        }
    }
    for i in 0..n {
        for j in 0..n {
            R[i][j] = 0.0;
        }
    }
}

unsafe fn kernel_gramschmidt<const M: usize, const N: usize>(
    m: usize,
    n: usize,
    A: &mut Array2D<DataType, M, N>,
    R: &mut Array2D<DataType, N, N>,
    Q: &mut Array2D<DataType, M, N>,
) {
    for k in 0..n {
        let mut nrm = 0.0;
        for i in 0..m {
            nrm += A[i][k] * A[i][k];
        }
        R[k][k] = nrm.sqrt();
        for i in 0..m {
            Q[i][k] = A[i][k] / R[k][k];
        }
        for j in (k + 1)..n {
            R[k][j] = 0.0;
            for i in 0..m {
                R[k][j] += Q[i][k] * A[i][j];
            }
            for i in 0..m {
                A[i][j] = A[i][j] - Q[i][k] * R[k][j];
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize, const P2: usize, const P3: usize>(n: usize, m: usize, R: &Array2D<DataType, P0, P1>, Q: &Array2D<DataType, P2, P3>) {
    let mut d = util::Dump::start();
    d.begin("R");
    for i in 0..n {
        for j in 0..n {
            if (i * n + j) % 20 == 0 {
                d.nl();
            }
            d.f64(R[i][j]);
        }
    }
    d.end("R");
    d.begin("Q");
    for i in 0..m {
        for j in 0..n {
            if (i * n + j) % 20 == 0 {
                d.nl();
            }
            d.f64(Q[i][j]);
        }
    }
    d.end("Q");
    d.finish();
}

pub fn run<const M: usize, const N: usize>() {
    let m = M;
    let n = N;

    let mut A = Array2D::<DataType, M, N>::zeroed();
    let mut R = Array2D::<DataType, N, N>::zeroed();
    let mut Q = Array2D::<DataType, M, N>::zeroed();

    unsafe {
        init_array(m, n, &mut A, &mut R, &mut Q);
        let t = util::Timer::start();
        kernel_gramschmidt(m, n, &mut A, &mut R, &mut Q);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, m, &R, &Q); }
    }
}

#[test]
fn check() {
    run::<10, 12>();
}
