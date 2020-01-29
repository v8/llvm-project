// RUN: %p/Inputs/lsp-encode %p/Inputs/formatArray.json \
// RUN: | %lldbcdlc 2>/dev/null | %d8 %S/tests.js %s \
// RUN: | FileCheck %s

// CHECK-NOT: Didn't consume
// CHECK: Reading 4 bytes from offset 1024
// CHECK: Reading 4 bytes from offset 1028
// CHECK: Reading 4 bytes from offset 1032
// CHECK: Reading 4 bytes from offset 1036
// CHECK: Result: {"type":"int","name":"A","value":[{"type":"int","name":"A[0]","value":"0"},{"type":"int","name":"A[1]","value":"4"},{"type":"int","name":"A[2]","value":"8"},{"type":"int","name":"A[3]","value":"12"}]}

// void __getMemory(uint32_t offset, uint32_t size, void* result);
function proxyGetMemory(offset, size, result) {
  print("Reading " + size + " bytes from offset " + offset);
  // Expecting size 4, so "read" 4 bytes from the engine:
  Heap[result] = offset % 256;
  Heap[result + 1] = 0;
  Heap[result + 2] = 0;
  Heap[result + 3] = 0;
}

const input = [...parseInput(read('-')) ];
for (const message of input) {
  if (message.result.value) {
    const buf = Uint8Array.from(decodeBase64(message.result.value.code));
    const module = new WebAssembly.Module(buf);
    const [memory, instance] = makeInstance(module, 1024);
    Heap = new Uint8Array(memory.buffer);
    const OutputBase = instance.exports.__heap_base.value;

    instance.exports.wasm_format(OutputBase, 256);
    print("Result: " + toString(Heap, OutputBase));
  } else {
    print(JSON.stringify(message));
  }
}
