void printfI(int);

float Global = 5;

int get(int a, int b, int c) {
  return Global + a + b + c;
}

int Main() {
  int I = get(1, 2, 3);
  printfI(I);
  return I;
}
