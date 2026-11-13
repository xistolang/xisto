use crate::config::datamining::covariance::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize, const M: usize>(
    n: usize,
    m: usize,
    float_n: &mut DataType,
    data: &mut Array2D<DataType, N, M>,
) {
    *float_n = n as DataType;
    for i in 0..n {
        for j in 0..m {
            data[i][j] = (i * j) as DataType / M as DataType;
        }
    }
}

unsafe fn kernel_covariance<const N: usize, const M: usize>(
    n: usize,
    m: usize,
    float_n: DataType,
    data: &mut Array2D<DataType, N, M>,
    cov: &mut Array2D<DataType, M, M>,
    mean: &mut Array1D<DataType, M>,
) {
    for j in 0..m {
        mean[j] = 0.0;
        for i in 0..n {
            mean[j] += data[i][j];
        }
        mean[j] /= float_n;
    }

    for i in 0..n {
        for j in 0..m {
            data[i][j] -= mean[j];
        }
    }

    for i in 0..m {
        for j in i..m {
            cov[i][j] = 0.0;
            for k in 0..n {
                cov[i][j] += data[k][i] * data[k][j];
            }
            cov[i][j] /= float_n - 1.0;
            cov[j][i] = cov[i][j];
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(m: usize, cov: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("cov");
    for i in 0..m {
        for j in 0..m {
            if (i * m + j) % 20 == 0 {
                d.nl();
            }
            d.f64(cov[i][j]);
        }
    }
    d.end("cov");
    d.finish();
}

pub fn run<const N: usize, const M: usize>() {
    let n = N;
    let m = M;

    let mut float_n = 0.0;
    let mut data = Array2D::<DataType, N, M>::zeroed();
    let mut cov = Array2D::<DataType, M, M>::zeroed();
    let mut mean = Array1D::<DataType, M>::zeroed();

    unsafe {
        init_array(n, m, &mut float_n, &mut data);
        let t = util::Timer::start();
        kernel_covariance(n, m, float_n, &mut data, &mut cov, &mut mean);
        t.stop_and_print();
        print_array(m, &cov);
    }
}

#[test]
fn check() {
    run::<12, 14>();
}
