#![allow(non_snake_case)]

use crate::config::linear_algebra::blas::gemm::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const NI: usize, const NJ: usize, const NK: usize>(
    ni: usize,
    nj: usize,
    nk: usize,
    alpha: &mut DataType,
    beta: &mut DataType,
    C: &mut Array2D<DataType, NI, NJ>,
    A: &mut Array2D<DataType, NI, NK>,
    B: &mut Array2D<DataType, NK, NJ>,
) {
    *alpha = 1.5;
    *beta = 1.2;
    for i in 0..ni {
        for j in 0..nj {
            C[i][j] = ((i * j + 1) % ni) as DataType / ni as DataType;
        }
    }
    for i in 0..ni {
        for j in 0..nk {
            A[i][j] = (i * (j + 1) % nk) as DataType / nk as DataType;
        }
    }
    for i in 0..nk {
        for j in 0..nj {
            B[i][j] = (i * (j + 2) % nj) as DataType / nj as DataType;
        }
    }
}

unsafe fn kernel_gemm<const NI: usize, const NJ: usize, const NK: usize>(
    ni: usize,
    nj: usize,
    nk: usize,
    alpha: DataType,
    beta: DataType,
    C: &mut Array2D<DataType, NI, NJ>,
    A: &Array2D<DataType, NI, NK>,
    B: &Array2D<DataType, NK, NJ>,
) {
    for i in 0..ni {
        for j in 0..nj {
            C[i][j] *= beta;
        }
        for k in 0..nk {
            for j in 0..nj {
                C[i][j] += alpha * A[i][k] * B[k][j];
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(ni: usize, nj: usize, C: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("C");
    for i in 0..ni {
        for j in 0..nj {
            if (i * ni + j) % 20 == 0 {
                d.nl();
            }
            d.f64(C[i][j]);
        }
    }
    d.end("C");
    d.finish();
}

pub fn run<const NI: usize, const NJ: usize, const NK: usize>() {
    let ni = NI;
    let nj = NJ;
    let nk = NK;

    let mut alpha = 0.0;
    let mut beta = 0.0;
    let mut C = Array2D::<DataType, NI, NJ>::zeroed();
    let mut A = Array2D::<DataType, NI, NK>::zeroed();
    let mut B = Array2D::<DataType, NK, NJ>::zeroed();

    unsafe {
        init_array(ni, nj, nk, &mut alpha, &mut beta, &mut C, &mut A, &mut B);
        let t = util::Timer::start();
        kernel_gemm(ni, nj, nk, alpha, beta, &mut C, &A, &B);
        t.stop_and_print();
        print_array(ni, nj, &C);
    }
}

#[test]
fn check() {
    run::<10, 11, 12>();
}
