#![allow(non_snake_case)]

use crate::config::linear_algebra::blas::gesummv::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize>(
    n: usize,
    alpha: &mut DataType,
    beta: &mut DataType,
    A: &mut Array2D<DataType, N, N>,
    B: &mut Array2D<DataType, N, N>,
    x: &mut Array1D<DataType, N>,
) {
    *alpha = 1.5;
    *beta = 1.2;
    for i in 0..n {
        x[i] = (i % n) as DataType / n as DataType;
        for j in 0..n {
            A[i][j] = ((i * j + 1) % n) as DataType / n as DataType;
            B[i][j] = ((i * j + 2) % n) as DataType / n as DataType;
        }
    }
}

unsafe fn kernel_gesummv<const N: usize>(
    n: usize,
    alpha: DataType,
    beta: DataType,
    A: &Array2D<DataType, N, N>,
    B: &Array2D<DataType, N, N>,
    tmp: &mut Array1D<DataType, N>,
    x: &Array1D<DataType, N>,
    y: &mut Array1D<DataType, N>,
) {
    for i in 0..n {
        tmp[i] = 0.0;
        y[i] = 0.0;
        for j in 0..n {
            tmp[i] = A[i][j] * x[j] + tmp[i];
            y[i] = B[i][j] * x[j] + y[i];
        }
        y[i] = alpha * tmp[i] + beta * y[i];
    }
}

fn print_array<const P0: usize>(n: usize, y: &Array1D<DataType, P0>) {
    let mut d = util::Dump::start();
    d.begin("y");
    for i in 0..n {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(y[i]);
    }
    d.end("y");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut alpha = 0.0;
    let mut beta = 0.0;
    let mut A = Array2D::<DataType, N, N>::zeroed();
    let mut B = Array2D::<DataType, N, N>::zeroed();
    let mut tmp = Array1D::<DataType, N>::zeroed();
    let mut x = Array1D::<DataType, N>::zeroed();
    let mut y = Array1D::<DataType, N>::zeroed();

    unsafe {
        init_array(n, &mut alpha, &mut beta, &mut A, &mut B, &mut x);
        let t = util::Timer::start();
        kernel_gesummv(n, alpha, beta, &A, &B, &mut tmp, &x, &mut y);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &y); }
    }
}

#[test]
fn check() {
    run::<13>();
}
