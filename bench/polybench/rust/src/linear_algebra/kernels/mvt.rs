#![allow(non_snake_case)]

use crate::config::linear_algebra::kernels::mvt::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize>(
    n: usize,
    x1: &mut Array1D<DataType, N>,
    x2: &mut Array1D<DataType, N>,
    y_1: &mut Array1D<DataType, N>,
    y_2: &mut Array1D<DataType, N>,
    A: &mut Array2D<DataType, N, N>,
) {
    for i in 0..n {
        x1[i] = (i % n) as DataType / n as DataType;
        x2[i] = ((i + 1) % n) as DataType / n as DataType;
        y_1[i] = ((i + 3) % n) as DataType / n as DataType;
        y_2[i] = ((i + 4) % n) as DataType / n as DataType;
        for j in 0..n {
            A[i][j] = (i * j % n) as DataType / n as DataType;
        }
    }
}

unsafe fn kernel_mvt<const N: usize>(
    n: usize,
    x1: &mut Array1D<DataType, N>,
    x2: &mut Array1D<DataType, N>,
    y_1: &Array1D<DataType, N>,
    y_2: &Array1D<DataType, N>,
    A: &Array2D<DataType, N, N>,
) {
    for i in 0..n {
        for j in 0..n {
            x1[i] = x1[i] + A[i][j] * y_1[j];
        }
    }
    for i in 0..n {
        for j in 0..n {
            x2[i] = x2[i] + A[j][i] * y_2[j];
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(n: usize, x1: &Array1D<DataType, P0>, x2: &Array1D<DataType, P1>) {
    let mut d = util::Dump::start();
    d.begin("x1");
    for i in 0..n {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(x1[i]);
    }
    d.end("x1");
    d.begin("x2");
    for i in 0..n {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(x2[i]);
    }
    d.end("x2");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut A = Array2D::<DataType, N, N>::zeroed();
    let mut x1 = Array1D::<DataType, N>::zeroed();
    let mut x2 = Array1D::<DataType, N>::zeroed();
    let mut y_1 = Array1D::<DataType, N>::zeroed();
    let mut y_2 = Array1D::<DataType, N>::zeroed();

    unsafe {
        init_array(n, &mut x1, &mut x2, &mut y_1, &mut y_2, &mut A);
        let t = util::Timer::start();
        kernel_mvt(n, &mut x1, &mut x2, &y_1, &y_2, &A);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &x1, &x2); }
    }
}

#[test]
fn check() {
    run::<20>();
}
