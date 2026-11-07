// Loops aninhados e ponto flutuante: conta os pontos dentro do conjunto
// de Mandelbrot numa grade size x size.
function mandelbrot(size: int, maxIter: int): int {
  let inside: int = 0;
  for (let py: int = 0; py < size; py++) {
    const ci: number = (2 * py) / size - 1;
    for (let px: int = 0; px < size; px++) {
      const cr: number = (2 * px) / size - 1.5;
      let zr: number = 0;
      let zi: number = 0;
      let i: int = 0;
      while (i < maxIter && zr * zr + zi * zi <= 4) {
        const t: number = zr * zr - zi * zi + cr;
        zi = 2 * zr * zi + ci;
        zr = t;
        i++;
      }
      if (i === maxIter) {
        inside++;
      }
    }
  }
  return inside;
}

const size: int = 4000;
console.log("mandelbrot", size, mandelbrot(size, 50));
