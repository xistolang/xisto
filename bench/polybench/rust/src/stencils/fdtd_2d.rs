use crate::config::stencils::fdtd_2d::DataType;
use crate::ndarray::{Array1D, Array2D, ArrayAlloc};
use crate::util;

unsafe fn init_array<const NX: usize, const NY: usize, const TMAX: usize>(
    tmax: usize,
    nx: usize,
    ny: usize,
    ex: &mut Array2D<DataType, NX, NY>,
    ey: &mut Array2D<DataType, NX, NY>,
    hz: &mut Array2D<DataType, NX, NY>,
    fict: &mut Array1D<DataType, TMAX>,
) {
    for i in 0..tmax {
        fict[i] = i as DataType;
    }
    for i in 0..nx {
        for j in 0..ny {
            ex[i][j] = (i * (j + 1)) as DataType / nx as DataType;
            ey[i][j] = (i * (j + 2)) as DataType / ny as DataType;
            hz[i][j] = (i * (j + 3)) as DataType / nx as DataType;
        }
    }
}

unsafe fn kernel_fdtd_2d<const NX: usize, const NY: usize, const TMAX: usize>(
    tmax: usize,
    nx: usize,
    ny: usize,
    ex: &mut Array2D<DataType, NX, NY>,
    ey: &mut Array2D<DataType, NX, NY>,
    hz: &mut Array2D<DataType, NX, NY>,
    fict: &Array1D<DataType, TMAX>,
) {
    for t in 0..tmax {
        for j in 0..ny {
            ey[0][j] = fict[t];
        }
        for i in 1..nx {
            for j in 0..ny {
                ey[i][j] = ey[i][j] - 0.5 * (hz[i][j] - hz[i - 1][j]);
            }
        }
        for i in 0..nx {
            for j in 1..ny {
                ex[i][j] = ex[i][j] - 0.5 * (hz[i][j] - hz[i][j - 1]);
            }
        }
        for i in 0..(nx - 1) {
            for j in 0..(ny - 1) {
                hz[i][j] = hz[i][j] - 0.7 * (ex[i][j + 1] - ex[i][j] + ey[i + 1][j] - ey[i][j]);
            }
        }
    }
}

fn print_array<const P0: usize, const P1: usize, const P2: usize, const P3: usize, const P4: usize, const P5: usize>(nx: usize, ny: usize, ex: &Array2D<DataType, P0, P1>, ey: &Array2D<DataType, P2, P3>, hz: &Array2D<DataType, P4, P5>) {
    let mut d = util::Dump::start();
    d.begin("ex");
    for i in 0..nx {
        for j in 0..ny {
            if (i * nx + j) % 20 == 0 {
                d.nl();
            }
            d.f64(ex[i][j]);
        }
    }
    d.end("ex");
    // as fdtd-2d.c: POLYBENCH_DUMP_FINISH after ex, none at the end
    d.finish_marker();
    d.begin("ey");
    for i in 0..nx {
        for j in 0..ny {
            if (i * nx + j) % 20 == 0 {
                d.nl();
            }
            d.f64(ey[i][j]);
        }
    }
    d.end("ey");
    d.begin("hz");
    for i in 0..nx {
        for j in 0..ny {
            if (i * nx + j) % 20 == 0 {
                d.nl();
            }
            d.f64(hz[i][j]);
        }
    }
    d.end("hz");
    d.close();
}

pub fn run<const NX: usize, const NY: usize, const TMAX: usize>() {
    let tmax = TMAX;
    let nx = NX;
    let ny = NY;

    let mut ex = Array2D::<DataType, NX, NY>::zeroed();
    let mut ey = Array2D::<DataType, NX, NY>::zeroed();
    let mut hz = Array2D::<DataType, NX, NY>::zeroed();
    let mut fict = Array1D::<DataType, TMAX>::zeroed();

    unsafe {
        init_array(tmax, nx, ny, &mut ex, &mut ey, &mut hz, &mut fict);
        let t = util::Timer::start();
        kernel_fdtd_2d(tmax, nx, ny, &mut ex, &mut ey, &mut hz, &fict);
        t.stop_and_print();
        print_array(nx, ny, &ex, &ey, &hz);
    }
}

#[test]
fn check() {
    run::<10, 12, 5>();
}
