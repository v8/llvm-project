// RUN: %p/Inputs/lsp-encode %p/Inputs/formatCompound.json \
// RUN: | %lldbcdlc 2>/dev/null | node %s | FileCheck %s

// CHECK-NOT: Didn't consume
// CHECK: Heap base: [[BASE:[0-9]+]]
// CHECK: Reading 4 bytes from offset 1032
// CHECK: Reading 4 bytes from offset 1036
// CHECK: Result at: [[BASE]]
// CHECK: Result: {"type":"Pair","name":"P","value":[{"type":"int","name":"X","value":"8"},{"type":"int","name":"Y","value":"12"}]}

tests = require('./tests.js')

// void __getMemory(uint32_t offset, uint32_t size, void* result);
function proxyGetMemory(offset, size, result) {
  console.log("Reading " + size + " bytes from offset " + offset);
  // Expecting size 4, so "read" 4 bytes from the engine:
  Heap[result] = offset % 256;
  Heap[result + 1] = 0;
  Heap[result + 2] = 0;
  Heap[result + 3] = 0;
}

(async () => {
  const data = await tests.readStdIn();
  const input = [...tests.parseInput(data) ];

  for (const message of input) {
    if (message.result.value) {
      const buf = Uint8Array.from(tests.decodeBase64(message.result.value.code));
      const module = new WebAssembly.Module(buf);
      const [memory, instance] = tests.makeInstance(module, 1024, proxyGetMemory);
      Heap = new Uint8Array(memory.buffer);
      const OutputBase = instance.exports.__heap_base;

      console.log('Heap base: ' + OutputBase)
      console.log('Result at: ' + instance.exports.wasm_format());
      console.log('Result: ' + tests.toString(Heap, OutputBase));
    } else {
      console.log(JSON.stringify(message));
    }
  }
})();
