#![allow(non_snake_case)]

use crate::config::linear_algebra::solvers::trisolv::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize>(
    n: usize,
    L: &mut Array2D<DataType, N, N>,
    x: &mut Array1D<DataType, N>,
    b: &mut Array1D<DataType, N>,
) {
    for i in 0..n {
        x[i] = -999.0;
        b[i] = i as DataType;
        for j in 0..=i {
            L[i][j] = (i + n - j + 1) as DataType * 2.0 / n as DataType;
        }
    }
}

unsafe fn kernel_trisolv<const N: usize>(
    n: usize,
    L: &Array2D<DataType, N, N>,
    x: &mut Array1D<DataType, N>,
    b: &Array1D<DataType, N>,
) {
    for i in 0..n {
        x[i] = b[i];
        for j in 0..i {
            x[i] -= L[i][j] * x[j];
        }
        x[i] = x[i] / L[i][i];
    }
}

fn print_array<const P0: usize>(n: usize, x: &Array1D<DataType, P0>) {
    let mut d = util::Dump::start();
    d.begin("x");
    for i in 0..n {
        // as trisolv.c: the element, then the line break
        d.f64(x[i]);
        if i % 20 == 0 {
            d.nl();
        }
    }
    d.end("x");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut L = Array2D::<DataType, N, N>::zeroed();
    let mut x = Array1D::<DataType, N>::zeroed();
    let mut b = Array1D::<DataType, N>::zeroed();

    unsafe {
        init_array(n, &mut L, &mut x, &mut b);
        let t = util::Timer::start();
        kernel_trisolv(n, &L, &mut x, &b);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &x); }
    }
}

#[test]
fn check() {
    run::<20>();
}
