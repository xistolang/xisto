#![allow(non_snake_case)]

use crate::config::stencils::seidel_2d::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize, const TSTEPS: usize>(
    n: usize,
    A: &mut Array2D<DataType, N, N>,
) {
    for i in 0..n {
        for j in 0..n {
            A[i][j] = (i * (j + 2) + 2) as DataType / n as DataType;
        }
    }
}

unsafe fn kernel_seidel_2d<const N: usize, const TSTEPS: usize>(
    tsteps: usize,
    n: usize,
    A: &mut Array2D<DataType, N, N>,
) {
    for _ in 0..tsteps {
        for i in 1..(n - 1) {
            for j in 1..(n - 1) {
                A[i][j] = (A[i - 1][j - 1]
                    + A[i - 1][j]
                    + A[i - 1][j + 1]
                    + A[i][j - 1]
                    + A[i][j]
                    + A[i][j + 1]
                    + A[i + 1][j - 1]
                    + A[i + 1][j]
                    + A[i + 1][j + 1])
                    / 9.0;
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

pub fn run<const N: usize, const TSTEPS: usize>() {
    let n = N;
    let tsteps = TSTEPS;

    let mut A = Array2D::<DataType, N, N>::zeroed();

    unsafe {
        init_array::<N, TSTEPS>(n, &mut A);
        let t = util::Timer::start();
        kernel_seidel_2d::<N, TSTEPS>(tsteps, n, &mut A);
        t.stop_and_print();
        print_array(n, &A);
    }
}

#[test]
fn check() {
    run::<20, 5>();
}
