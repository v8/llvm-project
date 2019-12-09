#ifndef LLDB_CDLC_RPC_H_
#define LLDB_CDLC_RPC_H_
#include "Modules.h"
#include "Protocol.h"
#include "cdlc.grpc.pb.h"
#include "cdlc.pb.h"

namespace lldb {
namespace cdlc {

struct LLDBLanguageComponentServiceImpl final
    : public lldb::cdlc::protocol::DebuggingPlugin::Service {
  static int run(llvm::StringRef ListeningPort);
  static int runInteractive();

  grpc::Status addRawModule(grpc::ServerContext *Context,
                            const protocol::AddRawModuleRequest *,
                            protocol::AddRawModuleResponse *) override;

  grpc::Status sourceLocationToRawLocation(
      grpc::ServerContext *Context, const protocol::SourceLocation *,
      protocol::SourceLocationToRawLocationResponse *) override;

  grpc::Status rawLocationToSourceLocation(
      grpc::ServerContext *Context, const protocol::RawLocation *,
      protocol::RawLocationToSourceLocationResponse *) override;

  grpc::Status
  listVariablesInScope(grpc::ServerContext *Context, const protocol::RawLocation *,
                       protocol::ListVariablesInScopeResponse *) override;

  grpc::Status evaluateVariable(grpc::ServerContext *Context,
                                const protocol::EvaluateVariableRequest *,
                                protocol::EvaluateVariableResponse *) override;

private:
  ModuleCache MDB;
};
} // namespace cdlc
} // namespace lldb
#endif // LLDB_CDLC_RPC_H_
