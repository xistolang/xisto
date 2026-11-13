#![allow(non_snake_case)]

use crate::config::linear_algebra::blas::gemver::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize>(
    n: usize,
    alpha: &mut DataType,
    beta: &mut DataType,
    A: &mut Array2D<DataType, N, N>,
    u1: &mut Array1D<DataType, N>,
    v1: &mut Array1D<DataType, N>,
    u2: &mut Array1D<DataType, N>,
    v2: &mut Array1D<DataType, N>,
    w: &mut Array1D<DataType, N>,
    x: &mut Array1D<DataType, N>,
    y: &mut Array1D<DataType, N>,
    z: &mut Array1D<DataType, N>,
) {
    *alpha = 1.5;
    *beta = 1.2;

    let float_n = n as DataType;

    for i in 0..n {
        u1[i] = i as DataType;
        u2[i] = ((i + 1) as DataType / float_n) / 2.0;
        v1[i] = ((i + 1) as DataType / float_n) / 4.0;
        v2[i] = ((i + 1) as DataType / float_n) / 6.0;
        y[i] = ((i + 1) as DataType / float_n) / 8.0;
        z[i] = ((i + 1) as DataType / float_n) / 9.0;
        x[i] = 0.0;
        w[i] = 0.0;
        for j in 0..n {
            A[i][j] = (i * j % n) as DataType / n as DataType;
        }
    }
}

unsafe fn kernel_gemver<const N: usize>(
    n: usize,
    alpha: DataType,
    beta: DataType,
    A: &mut Array2D<DataType, N, N>,
    u1: &Array1D<DataType, N>,
    v1: &Array1D<DataType, N>,
    u2: &Array1D<DataType, N>,
    v2: &Array1D<DataType, N>,
    w: &mut Array1D<DataType, N>,
    x: &mut Array1D<DataType, N>,
    y: &Array1D<DataType, N>,
    z: &Array1D<DataType, N>,
) {
    for i in 0..n {
        for j in 0..n {
            A[i][j] = A[i][j] + u1[i] * v1[j] + u2[i] * v2[j];
        }
    }

    for i in 0..n {
        for j in 0..n {
            x[i] = x[i] + beta * A[j][i] * y[j];
        }
    }

    for i in 0..n {
        x[i] = x[i] + z[i];
    }

    for i in 0..n {
        for j in 0..n {
            w[i] = w[i] + alpha * A[i][j] * x[j];
        }
    }
}

fn print_array<const P0: usize>(n: usize, w: &Array1D<DataType, P0>) {
    let mut d = util::Dump::start();
    d.begin("w");
    for i in 0..n {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(w[i]);
    }
    d.end("w");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut alpha = 0.0;
    let mut beta = 0.0;
    let mut A = Array2D::<DataType, N, N>::zeroed();
    let mut u1 = Array1D::<DataType, N>::zeroed();
    let mut v1 = Array1D::<DataType, N>::zeroed();
    let mut u2 = Array1D::<DataType, N>::zeroed();
    let mut v2 = Array1D::<DataType, N>::zeroed();
    let mut w = Array1D::<DataType, N>::zeroed();
    let mut x = Array1D::<DataType, N>::zeroed();
    let mut y = Array1D::<DataType, N>::zeroed();
    let mut z = Array1D::<DataType, N>::zeroed();

    unsafe {
        init_array(
            n, &mut alpha, &mut beta, &mut A, &mut u1, &mut v1, &mut u2, &mut v2, &mut w, &mut x,
            &mut y, &mut z,
        );
        let t = util::Timer::start();
        kernel_gemver(
                n, alpha, beta, &mut A, &u1, &v1, &u2, &v2, &mut w, &mut x, &y, &z,
            );
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &w); }
    }
}

#[test]
fn check() {
    run::<20>();
}
