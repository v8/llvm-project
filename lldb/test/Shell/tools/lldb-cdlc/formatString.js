// RUN: %p/Inputs/lsp-encode %p/Inputs/formatString.json \
// RUN: | %lldbcdlc 2>/dev/null | %d8 %S/tests.js %s \
// RUN: | FileCheck %s

// CHECK-NOT: Didn't consume
// CHECK: Heap base: [[BASE:[0-9]+]]
// CHECK: Reading 8 bytes from offset 1028
// CHECK: Result at: [[BASE]]
// CHECK: Result: {"type":"const char *","name":"String","value":"ABCD"}

// void __getMemory(uint32_t offset, uint32_t size, void* result);
function proxyGetMemory(offset, size, result) {
  print("Reading " + size + " bytes from offset " + offset);
  // Expecting size 4, so "read" 4 bytes from the engine:
  switch (offset) {
  case 1028: // Deref char*;
    Heap[result] = 0;
    Heap[result + 1] = 0;
    Heap[result + 2] = 1;
    Heap[result + 3] = 0;
    Heap[result + 4] = 0;
    Heap[result + 5] = 0;
    Heap[result + 6] = 0;
    Heap[result + 7] = 0;
    break;
  case 65536:
    Heap[result] = 65;
    'A'
    break;
  case 65537:
    Heap[result] = 66;
    'B'
    break;
  case 65538:
    Heap[result] = 67;
    'C'
    break;
  case 65539:
    Heap[result] = 68;
    'D'
    break;
  }
}

const input = [...parseInput(read('-')) ];
print(JSON.stringify(input));
for (const message of input) {
  if (message.result.value) {
    const buf = Uint8Array.from(decodeBase64(message.result.value.code));
    const module = new WebAssembly.Module(buf);
    const [memory, instance] = makeInstance(module, 1024);
    Heap = new Uint8Array(memory.buffer);
    const OutputBase = instance.exports.__heap_base.value;

    print('Heap base: ' + OutputBase)
    print('Result at: ' + instance.exports.wasm_format());
    print('Result: ' + toString(Heap, OutputBase));
  } else {
    print(JSON.stringify(message));
  }
}
