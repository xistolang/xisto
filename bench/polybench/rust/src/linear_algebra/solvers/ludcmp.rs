#![allow(non_snake_case)]

use crate::config::linear_algebra::solvers::ludcmp::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize>(
    n: usize,
    A: &mut Array2D<DataType, N, N>,
    b: &mut Array1D<DataType, N>,
    x: &mut Array1D<DataType, N>,
    y: &mut Array1D<DataType, N>,
) {
    let float_n = n as DataType;

    for i in 0..n {
        x[i] = 0.0;
        y[i] = 0.0;
        b[i] = (i + 1) as DataType / float_n / 2.0 + 4.0;
    }

    for i in 0..n {
        for j in 0..=i {
            A[i][j] = (-(j as isize) % n as isize) as DataType / n as DataType + 1.0;
        }
        for j in (i + 1)..n {
            A[i][j] = 0.0;
        }
        A[i][i] = 1.0;
    }

    A.make_positive_semi_definite();
}

unsafe fn kernel_ludcmp<const N: usize>(
    n: usize,
    A: &mut Array2D<DataType, N, N>,
    b: &Array1D<DataType, N>,
    x: &mut Array1D<DataType, N>,
    y: &mut Array1D<DataType, N>,
) {
    let mut w;
    for i in 0..n {
        for j in 0..i {
            w = A[i][j];
            for k in 0..j {
                w -= A[i][k] * A[k][j];
            }
            A[i][j] = w / A[j][j];
        }
        for j in i..n {
            w = A[i][j];
            for k in 0..i {
                w -= A[i][k] * A[k][j];
            }
            A[i][j] = w;
        }
    }

    for i in 0..n {
        w = b[i];
        for j in 0..i {
            w -= A[i][j] * y[j];
        }
        y[i] = w;
    }

    for i in (0..n).rev() {
        w = y[i];
        for j in (i + 1)..n {
            w -= A[i][j] * x[j];
        }
        x[i] = w / A[i][i];
    }
}

fn print_array<const P0: usize>(n: usize, x: &Array1D<DataType, P0>) {
    let mut d = util::Dump::start();
    d.begin("x");
    for i in 0..n {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(x[i]);
    }
    d.end("x");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut A = Array2D::<DataType, N, N>::zeroed();
    let mut b = Array1D::<DataType, N>::zeroed();
    let mut x = Array1D::<DataType, N>::zeroed();
    let mut y = Array1D::<DataType, N>::zeroed();

    unsafe {
        init_array(n, &mut A, &mut b, &mut x, &mut y);
        let t = util::Timer::start();
        kernel_ludcmp(n, &mut A, &b, &mut x, &mut y);
        t.stop_and_print();
        print_array(n, &x);
    }
}

#[test]
fn check() {
    run::<20>();
}
