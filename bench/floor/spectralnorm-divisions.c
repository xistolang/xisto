/* spectral-norm n=5500 (n a multiple of 4): only the 40*n*n double divisions 1/d(i,j) the problem asks for
   (10 iterations x 2 x (A u, A^T v)), each with its real denominator, summed so nothing is dropped. The
   denominators are carried along the row by two exact additions per element (as the fastest programs do), in
   vectors of 4 doubles. One thread. A lower bound on the CPU of any program that does the divisions asked for. */
#include <stdio.h>
#include <stdlib.h>
typedef double v4 __attribute__((ext_vector_type(4)));
int main(int argc, char **argv) {
  long n = argc > 1 ? atol(argv[1]) : 5500;
  double total = 0;
  for (int rep = 0; rep < 20; rep++)
    for (int t = 0; t < 2; t++)
      for (long i = 0; i < n; i++) {
        v4 d, dl, s = 0;
        for (int l = 0; l < 4; l++) {          /* j = l: d(i,l) and its step to j = l + 4 */
          double k = (double)(i + l);
          d[l] = k * (k + 1) / 2 + (t ? l : i) + 1;
          dl[l] = 4 * k + 10 + (t ? 4 : 0);    /* d(j+4) - d(j) = 4k + 10 (+4 for the transposed index) */
        }
        for (long j = 0; j < n; j += 4) { s += 1.0 / d; d += dl; dl += 16; }
        total += s[0] + s[1] + s[2] + s[3];
      }
  printf("%.9f\n", total);
  return 0;
}
