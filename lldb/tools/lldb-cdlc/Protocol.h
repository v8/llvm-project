#ifndef LLDB_CDLC_PROTOCOL_H_
#define LLDB_CDLC_PROTOCOL_H_

#include "grpc++//grpc++.h"
#include "cdlc.pb.h"

namespace lldb {
namespace cdlc {
enum class WasmAddressSpace { Memory, Local, Global };
} // namespace cdlc
} // namespace lldb

#endif // LLDB_CDLC_PROTOCOL_H_
