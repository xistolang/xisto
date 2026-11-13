// The parts of PolyBench/C 4.2.1's utilities/polybench.{h,c} the ports use: the dataset, the timer
// (-DPOLYBENCH_TIME) and the dump of the arrays (-DPOLYBENCH_DUMP_ARRAYS).
//
// Command line of every binary: [MINI|SMALL|MEDIUM|LARGE|EXTRALARGE] [time]
//   the dataset (-D<NAME>_DATASET in C; LARGE when absent) and "time" (the kernel's seconds, "%0.6f\n" on
//   stdout, as polybench_timer_print). The dump always goes to stderr (POLYBENCH_DUMP_TARGET), byte for byte
//   what the C prints with -DPOLYBENCH_DUMP_ARRAYS.

use std::io::{BufWriter, Stderr, Write};
use std::time::Instant;

pub const DATASETS: [&str; 5] = ["MINI", "SMALL", "MEDIUM", "LARGE", "EXTRALARGE"];

/// The dataset's position in DATASETS (LARGE unless the command line names another).
pub fn dataset() -> usize {
    for a in std::env::args().skip(1) {
        if let Some(k) = DATASETS.iter().position(|d| *d == a) {
            return k;
        }
    }
    3
}

/// polybench_start_instruments / polybench_stop_instruments / polybench_print_instruments.
pub struct Timer(Instant);

impl Timer {
    pub fn start() -> Timer {
        Timer(Instant::now())
    }

    pub fn stop_and_print(self) {
        let secs = self.0.elapsed().as_secs_f64();
        if std::env::args().skip(1).any(|a| a == "time") {
            println!("{:.6}", secs);
        }
    }
}

/// The dump: POLYBENCH_DUMP_START, _BEGIN(s), the elements, _END(s), _FINISH, on stderr through a buffer.
pub struct Dump(BufWriter<Stderr>);

impl Dump {
    /// POLYBENCH_DUMP_START
    pub fn start() -> Dump {
        let mut d = Dump(BufWriter::with_capacity(1 << 16, std::io::stderr()));
        d.s("==BEGIN DUMP_ARRAYS==\n");
        d
    }

    fn s(&mut self, s: &str) {
        self.0.write_all(s.as_bytes()).unwrap();
    }

    /// POLYBENCH_DUMP_BEGIN(s)
    pub fn begin(&mut self, name: &str) {
        self.s("begin dump: ");
        self.s(name);
    }

    /// POLYBENCH_DUMP_END(s)
    pub fn end(&mut self, name: &str) {
        self.s("\nend   dump: ");
        self.s(name);
        self.s("\n");
    }

    /// POLYBENCH_DUMP_FINISH, and the end of the dump
    pub fn finish(mut self) {
        self.finish_marker();
        self.close();
    }

    /// POLYBENCH_DUMP_FINISH alone (fdtd-2d prints it after its first array, not at the end)
    pub fn finish_marker(&mut self) {
        self.s("==END   DUMP_ARRAYS==\n");
    }

    /// the end of the dump without POLYBENCH_DUMP_FINISH
    pub fn close(mut self) {
        self.0.flush().unwrap();
    }

    /// fprintf(POLYBENCH_DUMP_TARGET, "\n")
    pub fn nl(&mut self) {
        self.s("\n");
    }

    /// "%0.2lf " (double). Rust's {:.2} rounds the exact binary value as printf does (ties to even, "-0.00"
    /// for negative values that round to zero); only NaN is spelled differently ("NaN" against "nan").
    pub fn f64(&mut self, x: f64) {
        if x.is_nan() {
            self.s(if x.is_sign_negative() { "-nan " } else { "nan " });
        } else {
            write!(self.0, "{:.2} ", x).unwrap();
        }
    }

    /// "%0.2f " (float, promoted to double by the varargs call)
    pub fn f32(&mut self, x: f32) {
        self.f64(x as f64);
    }

    /// "%d "
    pub fn i32(&mut self, x: i32) {
        write!(self.0, "{} ", x).unwrap();
    }
}

/// "nodump" on the command line: the timing run, as C built without -DPOLYBENCH_DUMP_ARRAYS (the call stays,
/// so the kernel's results stay live)
pub fn dump_arrays() -> bool {
    !std::env::args().skip(1).any(|a| a == "nodump")
}
