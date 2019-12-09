void printfI(int);

const char *String;

const char *get() { return "abc"; }

int Main() {
  String = get();
  printfI(String[0]);
  return 0;
}
