// RUN: %clang -c -o - -emit-interface-stubs %s | FileCheck %s

// CHECK:      --- !experimental-ifs-v1
// CHECK-NEXT: IfsVersion: 1.0
// CHECK-NEXT: Triple:
// CHECK-NEXT: ObjectFileFormat: ELF
// CHECK-NEXT: Symbols:
// CHECK-NEXT: ...

template<typename T> class C2 { union { T c; }; };
