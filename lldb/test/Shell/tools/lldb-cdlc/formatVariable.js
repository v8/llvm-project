// RUN: %p/Inputs/lsp-encode %p/Inputs/formatVariable.json \
// RUN: | %lldbcdlc 2>/dev/null | node %s | FileCheck %s

// CHECK-NOT: Didn't consume
// CHECK: Got 3 responses
// CHECK: ["global.c"]
// CHECK: {"name":"I","scope":"GLOBAL","type":"int"}

// CHECK: Heap base: [[BASE:[0-9]+]]
// CHECK: Reading 4 bytes from offset 1024
// CHECK: Result at: [[BASE]]
// CHECK: Result: {"type":"int","name":"I","value":"256"}

tests = require('./tests.js')

// void __getMemory(uint32_t offset, uint32_t size, void* result);
function proxyGetMemory(offset, size, result) {
  console.log("Reading " + size + " bytes from offset " + offset + " into " + result);
  // Expecting size 4, so "read" 4 bytes from the engine:
  Heap[result] = 0;
  Heap[result + 1] = 1;
  Heap[result + 2] = 0;
  Heap[result + 3] = 0;
}

(async () => {
  const data = await tests.readStdIn();
  const input = [...tests.parseInput(data) ];
  console.log("Got " + input.length + " responses.");

  for (const message of input) {
    if (message.result.value) {
      const buf = Uint8Array.from(tests.decodeBase64(message.result.value.code));
      const module = new WebAssembly.Module(buf);
      const [memory, instance] = tests.makeInstance(module, 32768, proxyGetMemory);
      Heap = new Uint8Array(memory.buffer);
      const OutputBase = instance.exports.__heap_base;
      console.log('Heap base: ' + OutputBase);
      console.log('Result at: ' + instance.exports.wasm_format());
      console.log('Result: ' + tests.toString(Heap, OutputBase));
    } else {
      console.log(JSON.stringify(message));
    }
  }
})();
