extern "C" void printfI(int);

__attribute__((always_inline)) inline int square(int x) {
  int result = x * x;
  return result;
}

__attribute__((always_inline)) inline int dsquare(int x, int y) {
  int dsq = square(x);
  dsq += square(y);
  return dsq;
}

int main() {
  int I = dsquare(5, 12);
  printfI(I);
  return I;
}
