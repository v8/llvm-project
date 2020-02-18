// vim: ft=javascript

function getContentLength(input) {
  while (input.length > 0) {
    const eol = input.indexOf('\r\n');
    const content_line = eol >= 0 ? input.substring(0, eol) : input;
    input = eol >= 0 ? input.substring(eol + 2) : '';
    if (!content_line.startsWith('Content-Length: '))
      continue;
    const content_length = Number(content_line.trim().substring('Content-Length: '.length));
    return {content_length: content_length, remaining: input};
  }
  return {content_length: -1, remaining: input};
}

function consumeHeaders(input) {
  while (input.length > 0) {
    const eol = input.indexOf('\r\n');
    if (eol < 0)
      return input;
    if (eol == 0)
      return input.substring(2);
    input = input.substring(eol + 2);
  }
  return input;
}

function* parseInput(input) {
  while (input.length > 0) {
    const {content_length, remaining} = getContentLength(input);
    if (content_length < 0) {
      print('Didn\'t consume ' + input.length + ' bytes at the end of input');
      break;
    }
    input = consumeHeaders(remaining);
    const payload = input.substring(0, content_length);
    yield JSON.parse(payload);
    input = input.substring(content_length);
  }
}

function* decodeBase64(input) {
  if (input.length % 4 > 0)
    throw 'String has wrong length';
  while (input.length > 0) {
    const [x, y, z] = decodeBase64Piece(input);
    input = input.substr(4);
    yield x;
    if (y >= 0)
      yield y;
    if (z >= 0)
      yield z;
  }

  function decodeBase64Piece(input) {
    const Base = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    const a = Base.indexOf(input[0]);
    const b = Base.indexOf(input[1]);
    const c = Base.indexOf(input[2]);
    const d = Base.indexOf(input[3]);
    const x = (a << 2) | (b >> 4);
    const y = (0xFF & (b << 4)) | (c >> 2);
    const z = (0xFF & (c << 6)) | d;
    return [x, y, z];
  }
}

function toString(array, begin) {
  var Res = '';
  while (begin < array.length && array[begin] != 0) {
    Res += String.fromCharCode(array[begin]);
    ++begin;
  }
  return Res;
}

function dead(element, index, array) {
  return element === 0xDE;
}

function dump(wasm_memory) {
  for (const i in wasm_memory) {
    if (wasm_memory[i] != 0) {
      if (wasm_memory[i] >= 33 && wasm_memory[i] < 127)
        print(i + ': ' + String.fromCharCode(wasm_memory[i]));
      else
        print(i + ': 0x' + wasm_memory[i].toString(16));
    }
  }
}

function getMethods(obj) {
  var result = [];
  for (var id in obj) {
    print(typeof (obj[id]) + ': ' + id);
    try {
      if (typeof (obj[id]) == 'function') {
        result.push(id + ': ' + obj[id].toString());
      }
    } catch (err) {
      result.push(id + ': inaccessible');
    }
  }
  return result;
}

function getMemory(module) {
  for (const imported of WebAssembly.Module.imports(module))
    if (imported.kind === 'memory')
      return imported;

  return null;
}

function sbrk(memorySize, increment) {
  if (increment === 0) return memorySize;
  return -1;
}

function makeInstance(module, memory_base) {
  const re = /smaller than initial ([0-9]+),/;
  const imports = {
    env: {
      __getMemory: proxyGetMemory,
      __debug: (x, y) => {}// print('flag#' + x + ': ' + y)
    }
  };

  const memory = getMemory(module);
  if (!memory)
    throw new Error('No memory export in module');

  try {
    imports.env[memory.name] = new WebAssembly.Memory({initial: 1, maximum: 1});
    imports.env["sbrk"] = sbrk.bind(this, 1*1<<16);
    return [imports.env[memory.name], new WebAssembly.Instance(module, imports)];
  } catch (err) {
    if (err.name !== 'LinkError')
      throw err;
    print(err.message);
    const size = Number(err.message.match(re)[1]);
    imports.env["sbrk"] = sbrk.bind(this, size*1<<16);
    imports.env[memory.name] = new WebAssembly.Memory({initial: size, maximum: size});
    print('Setting memory size to expected ' + size);
    return [imports.env[memory.name], new WebAssembly.Instance(module, imports)];
  }
}
