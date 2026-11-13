use crate::config::medley::floyd_warshall::DataType;
use crate::ndarray::{Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const N: usize>(n: usize, path: &mut Array2D<DataType, N, N>) {
    for i in 0..n {
        for j in 0..n {
            path[i][j] = (i * j % 7 + 1) as DataType;
            if (i + j) % 13 == 0 || (i + j) % 7 == 0 || (i + j) % 11 == 0 {
                path[i][j] = 999 as DataType;
            }
        }
    }
}

unsafe fn kernel_floyd_warshall<const N: usize>(n: usize, path: &mut Array2D<DataType, N, N>) {
    for k in 0..n {
        for i in 0..n {
            for j in 0..n {
                path[i][j] = if path[i][j] < path[i][k] + path[k][j] {
                    path[i][j]
                } else {
                    path[i][k] + path[k][j]
                };
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize>(n: usize, path: &Array2D<DataType, P0, P1>) {
    let mut d = util::Dump::start();
    d.begin("path");
    for i in 0..n {
        for j in 0..n {
            if (i * n + j) % 20 == 0 {
                d.nl();
            }
            d.i32(path[i][j]);
        }
    }
    d.end("path");
    d.finish();
}

pub fn run<const N: usize>() {
    let n = N;

    let mut path = Array2D::<DataType, N, N>::zeroed();

    unsafe {
        init_array(n, &mut path);
        let t = util::Timer::start();
        kernel_floyd_warshall(n, &mut path);
        t.stop_and_print();
        print_array(n, &path);
    }
}

#[test]
fn check() {
    run::<28>();
}
