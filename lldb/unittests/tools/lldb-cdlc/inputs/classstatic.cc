extern "C" void printfI(int);

int get() { return 28; }

class MyClass {
public:
  static int Main() {
    I = get();
    printfI(I);
    return I;
  }
private:
  static int I;
};

int MyClass::I = 0;

extern "C" int Main() {
  return MyClass::Main();
}
