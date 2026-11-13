#![allow(non_snake_case)]

use crate::config::linear_algebra::solvers::lu::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize>(n: usize, A: &mut Array2D<DataType, N, N>) {
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

unsafe fn kernel_lu<const N: usize>(n: usize, A: &mut Array2D<DataType, N, N>) {
    for i in 0..n {
        for j in 0..i {
            for k in 0..j {
                A[i][j] -= A[i][k] * A[k][j];
            }
            A[i][j] /= A[j][j];
        }
        for j in i..n {
            for k in 0..i {
                A[i][j] -= A[i][k] * A[k][j];
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(n: usize, A: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("A");
    for i in 0..n {
        for j in 0..n {
            if (i * n + j) % 20 == 0 {
                d.nl();
            }
            d.f64(A[i][j]);
        }
    }
    d.end("A");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut A = Array2D::<DataType, N, N>::zeroed();

    unsafe {
        init_array(n, &mut A);
        let t = util::Timer::start();
        kernel_lu(n, &mut A);
        t.stop_and_print();
        print_array(n, &A);
    }
}

#[test]
fn check() {
    run::<20>();
}
