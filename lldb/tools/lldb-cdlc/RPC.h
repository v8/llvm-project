#ifndef LLDB_CDLC_RPC_H_
#define LLDB_CDLC_RPC_H_
#include "Modules.h"
#include "Protocol.h"
#include "cdlc.pb.h"

namespace lldb {
namespace cdlc {

struct LLDBLanguageComponentServiceImpl {
  static int runInteractive();

private:
  ModuleCache MDB;
};
} // namespace cdlc
} // namespace lldb
#endif // LLDB_CDLC_RPC_H_
