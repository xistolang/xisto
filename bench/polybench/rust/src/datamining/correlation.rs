use crate::config::datamining::correlation::DataType;
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
            data[i][j] = (i * j) as DataType / M as DataType + i as DataType;
        }
    }
}

unsafe fn kernel_correlation<const N: usize, const M: usize>(
    n: usize,
    m: usize,
    float_n: DataType,
    data: &mut Array2D<DataType, N, M>,
    corr: &mut Array2D<DataType, M, M>,
    mean: &mut Array1D<DataType, M>,
    stddev: &mut Array1D<DataType, M>,
) {
    let eps = 0.1;

    for j in 0..m {
        mean[j] = 0.0;
        for i in 0..n {
            mean[j] += data[i][j];
        }
        mean[j] /= float_n;
    }

    for j in 0..m {
        stddev[j] = 0.0;
        for i in 0..n {
            stddev[j] += (data[i][j] - mean[j]) * (data[i][j] - mean[j]);
        }
        stddev[j] /= float_n;
        stddev[j] = stddev[j].sqrt();
        stddev[j] = if stddev[j] <= eps { 1.0 } else { stddev[j] };
    }

    for i in 0..n {
        for j in 0..m {
            data[i][j] -= mean[j];
            data[i][j] /= float_n.sqrt() * stddev[j];
        }
    }

    for i in 0..(m - 1) {
        corr[i][i] = 1.0;
        for j in (i + 1)..m {
            corr[i][j] = 0.0;
            for k in 0..n {
                corr[i][j] += data[k][i] * data[k][j];
            }
            corr[j][i] = corr[i][j];
        }
    }
    corr[m - 1][m - 1] = 1.0;
}

fn print_array<const P0: usize, const P1: usize>(m: usize, corr: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("corr");
    for i in 0..m {
        for j in 0..m {
            if (i * m + j) % 20 == 0 {
                d.nl();
            }
            d.f64(corr[i][j]);
        }
    }
    d.end("corr");
    d.finish();
}

pub fn run<const N: usize, const M: usize>() {
    let n = N;
    let m = M;

    let mut float_n = 0.0;
    let mut data = Array2D::<DataType, N, M>::zeroed();
    let mut corr = Array2D::<DataType, M, M>::zeroed();
    let mut mean = Array1D::<DataType, M>::zeroed();
    let mut stddev = Array1D::<DataType, M>::zeroed();

    unsafe {
        init_array(n, m, &mut float_n, &mut data);
        let t = util::Timer::start();
        kernel_correlation(n, m, float_n, &mut data, &mut corr, &mut mean, &mut stddev);
        t.stop_and_print();
        print_array(m, &corr);
    }
}

#[test]
fn check() {
    run::<12, 14>();
}
