#![allow(non_snake_case)]

use crate::config::linear_algebra::kernels::_2mm::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const NI: usize, const NJ: usize, const NK: usize, const NL: usize>(
    ni: usize,
    nj: usize,
    nk: usize,
    nl: usize,
    alpha: &mut DataType,
    beta: &mut DataType,
    A: &mut Array2D<DataType, NI, NK>,
    B: &mut Array2D<DataType, NK, NJ>,
    C: &mut Array2D<DataType, NJ, NL>,
    D: &mut Array2D<DataType, NI, NL>,
) {
    *alpha = 1.5;
    *beta = 1.2;
    for i in 0..ni {
        for j in 0..nk {
            A[i][j] = ((i * j + 1) % ni) as DataType / ni as DataType;
        }
    }
    for i in 0..nk {
        for j in 0..nj {
            B[i][j] = (i * (j + 1) % nj) as DataType / nj as DataType;
        }
    }
    for i in 0..nj {
        for j in 0..nl {
            C[i][j] = ((i * (j + 3) + 1) % nl) as DataType / nl as DataType;
        }
    }
    for i in 0..ni {
        for j in 0..nl {
            D[i][j] = (i * (j + 2) % nk) as DataType / nk as DataType;
        }
    }
}

unsafe fn kernel_2mm<const NI: usize, const NJ: usize, const NK: usize, const NL: usize>(
    ni: usize,
    nj: usize,
    nk: usize,
    nl: usize,
    alpha: DataType,
    beta: DataType,
    tmp: &mut Array2D<DataType, NI, NJ>,
    A: &Array2D<DataType, NI, NK>,
    B: &Array2D<DataType, NK, NJ>,
    C: &Array2D<DataType, NJ, NL>,
    D: &mut Array2D<DataType, NI, NL>,
) {
    for i in 0..ni {
        for j in 0..nj {
            tmp[i][j] = 0.0;
            for k in 0..nk {
                tmp[i][j] += alpha * A[i][k] * B[k][j];
            }
        }
    }
    for i in 0..ni {
        for j in 0..nl {
            D[i][j] *= beta;
            for k in 0..nj {
                D[i][j] += tmp[i][k] * C[k][j];
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(ni: usize, nl: usize, D: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("D");
    for i in 0..ni {
        for j in 0..nl {
            if (i * ni + j) % 20 == 0 {
                d.nl();
            }
            d.f64(D[i][j]);
        }
    }
    d.end("D");
    d.finish();
}

pub fn run<const NI: usize, const NJ: usize, const NK: usize, const NL: usize>() {
    let ni = NI;
    let nj = NJ;
    let nk = NK;
    let nl = NL;

    let mut alpha = 0.0;
    let mut beta = 0.0;
    let mut tmp = Array2D::<DataType, NI, NJ>::zeroed();
    let mut A = Array2D::<DataType, NI, NK>::zeroed();
    let mut B = Array2D::<DataType, NK, NJ>::zeroed();
    let mut C = Array2D::<DataType, NJ, NL>::zeroed();
    let mut D = Array2D::<DataType, NI, NL>::zeroed();

    unsafe {
        init_array(
            ni, nj, nk, nl, &mut alpha, &mut beta, &mut A, &mut B, &mut C, &mut D,
        );
        let t = util::Timer::start();
        kernel_2mm(ni, nj, nk, nl, alpha, beta, &mut tmp, &A, &B, &C, &mut D);
        t.stop_and_print();
        if util::dump_arrays() { print_array(ni, nl, &D); }
    }
}

#[test]
fn check() {
    run::<8, 9, 11, 12>();
}
