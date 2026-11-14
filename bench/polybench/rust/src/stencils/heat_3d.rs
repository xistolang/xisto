#![allow(non_snake_case)]

use crate::config::stencils::heat_3d::DataType;
use crate::ndarray::{Array3D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize, const TSTEPS: usize>(
    n: usize,
    A: &mut Array3D<DataType, N, N, N>,
    B: &mut Array3D<DataType, N, N, N>,
) {
    for i in 0..n {
        for j in 0..n {
            for k in 0..n {
                B[i][j][k] = (i + j + (n - k)) as DataType * (10 as DataType) / n as DataType;
                A[i][j][k] = B[i][j][k];
            }
        }
    }
}

unsafe fn kernel_heat_3d<const N: usize, const TSTEPS: usize>(
    tsteps: usize,
    n: usize,
    A: &mut Array3D<DataType, N, N, N>,
    B: &mut Array3D<DataType, N, N, N>,
) {
    for _ in 1..=tsteps {
        for i in 1..(n - 1) {
            for j in 1..(n - 1) {
                for k in 1..(n - 1) {
                    B[i][j][k] = 0.125 * (A[i + 1][j][k] - 2.0 * A[i][j][k] + A[i - 1][j][k])
                        + 0.125 * (A[i][j + 1][k] - 2.0 * A[i][j][k] + A[i][j - 1][k])
                        + 0.125 * (A[i][j][k + 1] - 2.0 * A[i][j][k] + A[i][j][k - 1])
                        + A[i][j][k];
                }
            }
        }
        for i in 1..(n - 1) {
            for j in 1..(n - 1) {
                for k in 1..(n - 1) {
                    A[i][j][k] = 0.125 * (B[i + 1][j][k] - 2.0 * B[i][j][k] + B[i - 1][j][k])
                        + 0.125 * (B[i][j + 1][k] - 2.0 * B[i][j][k] + B[i][j - 1][k])
                        + 0.125 * (B[i][j][k + 1] - 2.0 * B[i][j][k] + B[i][j][k - 1])
                        + B[i][j][k];
                }
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize, const P2: usize>(n: usize, A: &Array3D<DataType, P0, P1, P2>) {
    let mut d = util::Dump::start();
    d.begin("A");
    for i in 0..n {
        for j in 0..n {
            for k in 0..n {
                if (i * n * n + j * n + k) % 20 == 0 {
                    d.nl();
                }
                d.f64(A[i][j][k]);
            }
        }
    }
    d.end("A");
    d.finish();
}

pub fn run<const N: usize, const TSTEPS: usize>() {
    let n = N;
    let tsteps = TSTEPS;

    let mut A = Array3D::<DataType, N, N, N>::zeroed();
    let mut B = Array3D::<DataType, N, N, N>::zeroed();

    unsafe {
        init_array::<N, TSTEPS>(n, &mut A, &mut B);
        let t = util::Timer::start();
        kernel_heat_3d::<N, TSTEPS>(tsteps, n, &mut A, &mut B);
        t.stop_and_print();
        if util::dump_arrays() { print_array(n, &A); }
    }
}

#[test]
fn check() {
    run::<12, 5>();
}
