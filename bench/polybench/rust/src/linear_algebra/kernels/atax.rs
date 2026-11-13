#![allow(non_snake_case)]

use crate::config::linear_algebra::kernels::atax::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const M: usize, const N: usize>(
    m: usize,
    n: usize,
    A: &mut Array2D<DataType, M, N>,
    x: &mut Array1D<DataType, N>,
) {
    let float_n = n as DataType;
    for i in 0..n {
        x[i] = 1.0 + (i as DataType / float_n);
    }
    for i in 0..m {
        for j in 0..n {
            A[i][j] = ((i + j) % n) as DataType / (5 * m) as DataType;
        }
    }
}

unsafe fn kernel_atax<const M: usize, const N: usize>(
    m: usize,
    n: usize,
    A: &Array2D<DataType, M, N>,
    x: &Array1D<DataType, N>,
    y: &mut Array1D<DataType, N>,
    tmp: &mut Array1D<DataType, M>,
) {
    for i in 0..n {
        y[i] = 0.0;
    }
    for i in 0..m {
        tmp[i] = 0.0;
        for j in 0..n {
            tmp[i] = tmp[i] + A[i][j] * x[j];
        }
        for j in 0..n {
            y[j] = y[j] + A[i][j] * tmp[i];
        }
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

pub fn run<const M: usize, const N: usize>() {
    let m = M;
    let n = N;

    let mut A = Array2D::<DataType, M, N>::zeroed();
    let mut x = Array1D::<DataType, N>::zeroed();
    let mut y = Array1D::<DataType, N>::zeroed();
    let mut tmp = Array1D::<DataType, M>::zeroed();

    unsafe {
        init_array(m, n, &mut A, &mut x);
        let t = util::Timer::start();
        kernel_atax(m, n, &A, &x, &mut y, &mut tmp);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &y); }
    }
}

#[test]
fn check() {
    run::<19, 21>();
}
