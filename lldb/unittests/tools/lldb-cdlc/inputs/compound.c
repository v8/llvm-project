struct Pair {
  int X, Y;
};

struct Pair get() {
  struct Pair P = {4, 5};
  return P;
}

void printfI(int);
struct Pair P;
int Main() {
  P = get();
  printfI(P.X);
  printfI(P.Y);
  return 0;
}
