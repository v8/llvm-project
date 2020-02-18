// RUN: %p/Inputs/lsp-encode %p/Inputs/formatVariable.json \
// RUN: | %lldbcdlc 2>/dev/null | %d8 %S/tests.js %s \
// RUN: | FileCheck %s

// CHECK-NOT: Didn't consume
// CHECK: Got 3 responses
// CHECK: ["global.c"]
// CHECK: {"name":"I","scope":"GLOBAL","type":"int"}

// CHECK: Heap base: [[BASE:[0-9]+]]
// CHECK: Reading 4 bytes from offset 1024
// CHECK: Result at: [[BASE]]
// CHECK: Result: {"type":"int","name":"I","value":"256"}

// void __getMemory(uint32_t offset, uint32_t size, void* result);
function proxyGetMemory(offset, size, result) {
  print("Reading " + size + " bytes from offset " + offset + " into " + result);
  // Expecting size 4, so "read" 4 bytes from the engine:
  Heap[result] = 0;
  Heap[result + 1] = 1;
  Heap[result + 2] = 0;
  Heap[result + 3] = 0;
}


const input = [...parseInput(read('-')) ];
print("Got " + input.length + " responses.");
for (const message of input) {
  if (message.result.value) {
    const buf = Uint8Array.from(decodeBase64(message.result.value.code));
    const module = new WebAssembly.Module(buf);
    const [memory, instance] = makeInstance(module, 32768);
    Heap = new Uint8Array(memory.buffer);
    const OutputBase = instance.exports.__heap_base.value;
    print('Heap base: ' + OutputBase);
    print('Result at: ' + instance.exports.wasm_format());
    print('Result: ' + toString(Heap, OutputBase));
  } else {
    print(JSON.stringify(message));
  }
}
