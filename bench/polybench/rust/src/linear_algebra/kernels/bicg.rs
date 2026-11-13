#![allow(non_snake_case)]

use crate::config::linear_algebra::kernels::bicg::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize, const M: usize>(
    n: usize,
    m: usize,
    A: &mut Array2D<DataType, N, M>,
    r: &mut Array1D<DataType, N>,
    p: &mut Array1D<DataType, M>,
) {
    for i in 0..m {
        p[i] = (i % m) as DataType / m as DataType;
    }
    for i in 0..n {
        r[i] = (i % n) as DataType / n as DataType;
        for j in 0..m {
            A[i][j] = (i * (j + 1) % n) as DataType / n as DataType;
        }
    }
}

unsafe fn kernel_bicg<const N: usize, const M: usize>(
    n: usize,
    m: usize,
    A: &Array2D<DataType, N, M>,
    s: &mut Array1D<DataType, M>,
    q: &mut Array1D<DataType, N>,
    p: &Array1D<DataType, M>,
    r: &Array1D<DataType, N>,
) {
    for i in 0..m {
        s[i] = 0.0;
    }
    for i in 0..n {
        q[i] = 0.0;
        for j in 0..m {
            s[j] = s[j] + r[i] * A[i][j];
            q[i] = q[i] + A[i][j] * p[j];
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(m: usize, n: usize, s: &Array1D<DataType, P0>, q: &Array1D<DataType, P1>) {
    let mut d = util::Dump::start();
    d.begin("s");
    for i in 0..m {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(s[i]);
    }
    d.end("s");
    d.begin("q");
    for i in 0..n {
        if i % 20 == 0 {
            d.nl();
        }
        d.f64(q[i]);
    }
    d.end("q");
    d.finish();
}

pub fn run<const N: usize, const M: usize>() {
    let n = N;
    let m = M;

    let mut A = Array2D::<DataType, N, M>::zeroed();
    let mut s = Array1D::<DataType, M>::zeroed();
    let mut q = Array1D::<DataType, N>::zeroed();
    let mut p = Array1D::<DataType, M>::zeroed();
    let mut r = Array1D::<DataType, N>::zeroed();

    unsafe {
        init_array(n, m, &mut A, &mut r, &mut p);
        let t = util::Timer::start();
        kernel_bicg(n, m, &A, &mut s, &mut q, &p, &r);
        t.stop_and_print();
        print_array(m, n, &s, &q);
    }
}

#[test]
fn check() {
    run::<19, 21>();
}
