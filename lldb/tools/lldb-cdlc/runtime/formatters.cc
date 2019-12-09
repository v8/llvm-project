#include <cstdint>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>
#include <utility>

#ifndef NDEBUG
#define DEBUG(identifier, value) __debug((identifier), (value))
#else
#define DEBUG()
#endif

extern "C" {
void __getMemory(uint32_t offset, uint32_t size, void *result);

#ifndef NDEBUG
void __debug(uint32_t flag, uint32_t value);
#endif
}

namespace {
template <typename T> class Slice {
public:
  Slice() : Array(nullptr), Length(0) {}
  Slice(T *Array, uint32_t Length) : Array(Array), Length(Length) {
    DEBUG(0, reinterpret_cast<intptr_t>(Array));
    DEBUG(10, Length);
    DEBUG(7, Array == nullptr);
  }

  Slice &operator++() {
    if (empty())
      return *this;
    Array++;
    DEBUG(1, reinterpret_cast<intptr_t>(Array));
    Length--;
    DEBUG(11, Length);
    return *this;
  }

  Slice operator++(int) {
    if (empty())
      return *this;
    Slice Temp = *this;
    Array++;
    DEBUG(1, reinterpret_cast<intptr_t>(Array));
    Length--;
    DEBUG(11, Length);
    return Temp;
  }

  Slice operator-(uint32_t Count) {
    if (Count >= Length)
      return {};
    return {Array, Length - Count};
  }

  void reverse() {
    T *First = Array;
    T *Last = Array + Length - 1;
    while (First < Last) {
      T C = *First;
      *First = *Last;
      *Last = C;
      ++First;
      --Last;
    }
  }

  T &operator[](uint32_t Idx) { return Array[Idx]; }
  T &operator*() { return *Array; }
  T *begin() const { return Array; }
  T *end() const { return Array + Length; }
  uint32_t length() const { return Length; }
  bool empty() const {
    DEBUG(15, Length);
    DEBUG(15, Length == 0);
    return Length == 0;
  }

private:
  T *Array;
  uint32_t Length;
};

using StringSlice = Slice<char>;
using ConstStringSlice = Slice<const char>;

template <typename T> ConstStringSlice getTypename() {
  const char *Typename = __PRETTY_FUNCTION__;
  uint32_t PrefixLen = sizeof("(anonymous namespace)::ConstStringSlice "
                              "(anonymous namespace)::getTypename() [T = ") -
                       1;
  uint32_t Len = sizeof(__PRETTY_FUNCTION__) - 2 - PrefixLen;
  return ConstStringSlice(Typename + PrefixLen, Len);
}

template <typename T> struct TypeName {};

template <typename T> struct Value {
  Value(T *Value) : Val(Value) {}
  void emitInto(StringSlice &Output) const;

  T *Val;
};

void emitValueInto(Value<const char *> V, StringSlice &Output) {
  // Copy the string in Input into Output, including the null-termintor.
  // Output points to the null-terminator when this function returns.
  for (const char *C = *V.Val;; ++C) {
    if (Output.empty()) {
      Output = {};
      return; // ENOSPC;
    }
    __getMemory(static_cast<uint32_t>(reinterpret_cast<intptr_t>(C)), 1,
                Output.begin());
    if (*Output.begin() == 0)
      break;
    ++Output;
  }
}

template <typename T, typename = std::enable_if_t<!std::is_pointer<T>::value &&
                                                  std::is_integral<T>::value>>
void emitValueInto(Value<T> V, StringSlice &Output) {
  if (Output.empty())
    return; // ENOSPC;

  const char *Digits = "0123456789";
  T D = *V.Val;
  int Neg = D < 0;
  if (D == 0) {
    Output[0] = '0';
    ++Output;
    return;
  }

  StringSlice I = Output;
  while (D != 0 && !I.empty()) {
    int X = D % 10;
    if (X < 0)
      X *= -1;
    *I.begin() = Digits[X];
    ++I;
    D /= 10;
  }
  if (D != 0 || (Neg && I.empty())) {
    Output = {};
    return; // ENOSPC
  }

  if (Neg) {
    *I.begin() = '-';
    ++I;
  }
  DEBUG(4, reinterpret_cast<intptr_t>(Output.begin()));
  DEBUG(4, Output.length());
  DEBUG(4, I.length());
  (Output - I.length()).reverse();
  Output = I;
}

template <typename T> void Value<T>::emitInto(StringSlice &Output) const {
  emitValueInto(*this, Output);
}

class Printer {
public:
  Printer(char *Output, uint32_t Size)
      : ScratchPad(Output, Size), OutputWindow(Output, Size) {}

  uint32_t length() const {
    DEBUG(17, ScratchPad.length());
    DEBUG(17, OutputWindow.length());
    return ScratchPad.length() - OutputWindow.length();
  }
  bool valid() const { return !OutputWindow.empty(); }
  operator bool() const { return valid(); }

private:
  StringSlice ScratchPad;
  StringSlice OutputWindow;

  friend Printer &operator<<(Printer &P, std::nullptr_t) {
    if (P.valid())
      *P.OutputWindow = '\0';
    return P;
  }

  friend Printer &operator<<(Printer &P, const char *Msg) {
    for (const char *C = Msg; *C != 0; ++C) {
      if (P.OutputWindow.empty())
        break;
      *P.OutputWindow.begin() = *C;
      ++P.OutputWindow;
    }
    return P;
  }

  friend Printer &operator<<(Printer &P, ConstStringSlice Msg) {
    for (char C : Msg) {
      if (P.OutputWindow.empty())
        break;
      *P.OutputWindow.begin() = C;
      ++P.OutputWindow;
    }
    return P;
  }

  template <typename T>
  friend Printer &operator<<(Printer &P, TypeName<T> Msg) {
    return P << getTypename<T>();
  }

  template <typename T> friend Printer &operator<<(Printer &P, Value<T> Msg) {
    Msg.emitInto(P.OutputWindow);
    return P;
  }
};

std::nullptr_t Fin = nullptr;

int errorOrLen(Printer &P) {
  if (P.valid())
    return P.length();
  return -ENOSPC;
}

template <typename T>
int formatValue(T *Value, const char *Variable, char *Result, uint32_t Size) {
  if (Size < 2)
    return -ENOSPC;

  Printer P(Result, Size);
  P << "{\"type\":\"" << TypeName<T>() << "\",\"name\":\"" << Variable
    << "\",\"value\":\"" << ::Value<T>(Value) << "\"}" << Fin;

  return errorOrLen(P);
}
} // namespace

extern "C" {
int format_begin_array(const char *Variable, const char *Type, char *Result,
                       uint32_t Size) {
  Printer P(Result, Size);
  P << "{\"type\":\"" << Type << "\",\"name\":\"" << Variable
    << "\",\"value\":[" << Fin;
  return errorOrLen(P);
}
int format_end_array(char *Result, uint32_t Size) {
  Printer P(Result, Size);
  P << "]}" << Fin;
  return errorOrLen(P);
}
int format_sep(char *Result, uint32_t Size) {
  Printer P(Result, Size);
  P << "," << Fin;
  return errorOrLen(P);
}
int format_int64_t(int64_t *Value, const char *Variable, char *Result,
                   uint32_t Size) {
  return formatValue(Value, Variable, Result, Size);
}
int format_int32_t(int32_t *Value, const char *Variable, char *Result,
                   uint32_t Size) {
  return formatValue(Value, Variable, Result, Size);
}
int format_int(int32_t *Value, const char *Variable, char *Result,
               uint32_t Size) {
  return formatValue(Value, Variable, Result, Size);
}
int format_int8_t(int8_t *Value, const char *Variable, char *Result,
                  uint32_t Size) {
  return formatValue(Value, Variable, Result, Size);
}
int format_string(const char **Value, const char *Variable, char *Result,
                  uint32_t Size) {
  return formatValue(Value, Variable, Result, Size);
}
}
