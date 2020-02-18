// RUN: %p/Inputs/lsp-encode %p/Inputs/formatCompound.json \
// RUN: | %lldbcdlc 2>/dev/null | %d8 %S/tests.js %s \
// RUN: | FileCheck %s

// CHECK-NOT: Didn't consume
// CHECK: Heap base: [[BASE:[0-9]+]]
// CHECK: Reading 4 bytes from offset 1032
// CHECK: Reading 4 bytes from offset 1036
// CHECK: Result at: [[BASE]]
// CHECK: Result: {"type":"Pair","name":"P","value":[{"type":"int","name":"X","value":"8"},{"type":"int","name":"Y","value":"12"}]}

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

    print('Heap base: ' + OutputBase)
    print('Result at: ' + instance.exports.wasm_format());
    print('Result: ' + toString(Heap, OutputBase));
  } else {
    print(JSON.stringify(message));
  }
}
