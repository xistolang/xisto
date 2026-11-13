#![allow(non_snake_case)]

use crate::config::linear_algebra::kernels::doitgen::DataType;
use crate::ndarray::{Array1D, Array2D, Array3D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const NP: usize, const NQ: usize, const NR: usize>(
    nr: usize,
    nq: usize,
    np: usize,
    A: &mut Array3D<DataType, NR, NQ, NP>,
    C4: &mut Array2D<DataType, NP, NP>,
) {
    for i in 0..nr {
        for j in 0..nq {
            for k in 0..np {
                A[i][j][k] = ((i * j + k) % np) as DataType / np as DataType;
            }
        }
    }
    for i in 0..np {
        for j in 0..np {
            C4[i][j] = (i * j % np) as DataType / np as DataType;
        }
    }
}

unsafe fn kernel_doitgen<const NP: usize, const NQ: usize, const NR: usize>(
    nr: usize,
    nq: usize,
    np: usize,
    A: &mut Array3D<DataType, NR, NQ, NP>,
    C4: &Array2D<DataType, NP, NP>,
    sum: &mut Array1D<DataType, NP>,
) {
    for r in 0..nr {
        for q in 0..nq {
            for p in 0..np {
                sum[p] = 0.0;
                for s in 0..np {
                    sum[p] += A[r][q][s] * C4[s][p];
                }
            }
            for p in 0..np {
                A[r][q][p] = sum[p];
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize, const P2: usize>(nr: usize, nq: usize, np: usize, A: &Array3D<DataType, P0, P1, P2>) {
    let mut d = util::Dump::start();
    d.begin("A");
    for i in 0..nr {
        for j in 0..nq {
            for k in 0..np {
                if (i * nq * np + j * np + k) % 20 == 0 {
                    d.nl();
                }
                d.f64(A[i][j][k]);
            }
        }
    }
    d.end("A");
    d.finish();
}

pub fn run<const NP: usize, const NQ: usize, const NR: usize>() {
    let nr = NR;
    let nq = NQ;
    let np = NP;

    let mut A = Array3D::<DataType, NR, NQ, NP>::zeroed();
    let mut sum = Array1D::<DataType, NP>::zeroed();
    let mut C4 = Array2D::<DataType, NP, NP>::zeroed();

    unsafe {
        init_array(nr, nq, np, &mut A, &mut C4);
        let t = util::Timer::start();
        kernel_doitgen(nr, nq, np, &mut A, &C4, &mut sum);
        t.stop_and_print();
        print_array(nr, nq, np, &A);
    }
}

#[test]
fn check() {
    run::<6, 4, 5>();
}
