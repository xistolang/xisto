#![allow(non_snake_case)]

use crate::config::stencils::jacobi_1d::DataType;
use crate::ndarray::{Array1D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize, const TSTEPS: usize>(
    n: usize,
    A: &mut Array1D<DataType, N>,
    B: &mut Array1D<DataType, N>,
) {
    for i in 0..n {
        A[i] = (i + 2) as DataType / n as DataType;
        B[i] = (i + 3) as DataType / n as DataType;
    }
}

unsafe fn kernel_jacobi_1d<const N: usize, const TSTEPS: usize>(
    tsteps: usize,
    n: usize,
    A: &mut Array1D<DataType, N>,
    B: &mut Array1D<DataType, N>,
) {
    for _ in 0..tsteps {
        for i in 1..(n - 1) {
            B[i] = 0.33333 * (A[i - 1] + A[i] + A[i + 1]);
        }
        for i in 1..(n - 1) {
            A[i] = 0.33333 * (B[i - 1] + B[i] + B[i + 1]);
        }
    }
}

fn print_array<const P0: usize>(n: usize, A: &Array1D<DataType, P0>) {
    let mut d = util::Dump::start();
    d.begin("A");
    for i in 0..n {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(A[i]);
    }
    d.end("A");
    d.finish();
}

pub fn run<const N: usize, const TSTEPS: usize>() {
    let n = N;
    let tsteps = TSTEPS;

    let mut A = Array1D::<DataType, N>::zeroed();
    let mut B = Array1D::<DataType, N>::zeroed();

    unsafe {
        init_array::<N, TSTEPS>(n, &mut A, &mut B);
        let t = util::Timer::start();
        kernel_jacobi_1d::<N, TSTEPS>(tsteps, n, &mut A, &mut B);
        t.stop_and_print();
        print_array(n, &A);
    }
}

#[test]
fn check() {
    run::<20, 5>();
}
