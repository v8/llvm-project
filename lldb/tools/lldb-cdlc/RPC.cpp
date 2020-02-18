#include "RPC.h"
#include "Logger.h"
#include "Modules.h"
#include "Protocol.h"
#include "Transport.h"
#include "cdlc.pb.h"

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>
#include <grpcpp/impl/codegen/sync_stream.h>
#include <memory>
#include <type_traits>

namespace {
namespace iterable_trait_impl {
template <typename T>
static auto f(int)
    -> decltype(std::begin(std::declval<T>()) != std::end(std::declval<T>()),
                std::true_type()) {}
template <typename T> static std::false_type f(...) {}
}; // namespace iterable_trait_impl
} // namespace

namespace lldb {
namespace cdlc {

bool operator<(const protocol::Variable &A, const protocol::Variable &B) {
  return (A.name() < B.name() || A.scope() < B.scope() || A.type() < B.type());
}
bool operator==(const protocol::Variable &A, const protocol::Variable &B) {
  return (A.name() == B.name() || A.scope() == B.scope() ||
          A.type() == B.type());
}

template <typename T>
using is_iterable = decltype(iterable_trait_impl::f<T>(0));

llvm::raw_ostream &operator<<(llvm::raw_ostream &O,
                              const google::protobuf::Message &M) {
  google::protobuf::util::JsonPrintOptions Options;
  Options.add_whitespace = true;
  std::string S;
  google::protobuf::util::MessageToJsonString(M, &S);
  return O << S;
}

/*static*/ int
LLDBLanguageComponentServiceImpl::run(llvm::StringRef ListeningPort) {
  grpc::ServerBuilder SB;
  LLDBLanguageComponentServiceImpl Service;
  SB.AddListeningPort(ListeningPort.str(), grpc::InsecureServerCredentials());
  SB.RegisterService(&Service);
  std::unique_ptr<grpc::Server> Server(SB.BuildAndStart());
  llvm::errs() << "Listening on " << ListeningPort << "\n";
  Server->Wait();
  return 0;
}

static std::unique_ptr<protocol::Error> makeError(protocol::Error_Code EC,
                                                  llvm::Twine Message) {
  auto E = std::make_unique<protocol::Error>();
  E->set_code(EC);
  E->set_message(Message.str());
  return E;
}

static std::unique_ptr<protocol::Error>
makeNotFoundError(llvm::StringRef ModuleId) {
  return makeError(protocol::Error_Code::Error_Code_NOT_FOUND,
                   "Module with id '" + ModuleId + "' not found");
}

template <typename ResponseT>
static ResponseT &setError(ResponseT &Response,
                           std::unique_ptr<protocol::Error> Error) {
  Response.set_allocated_error(Error.release());
  return Response;
}

template <typename ResponseT>
static ResponseT &setError(ResponseT &Response, llvm::Error Error) {
  llvm::handleAllErrors(std::move(Error), [&](const llvm::StringError &E) {
    setError(Response,
             makeError(protocol::Error_Code::Error_Code_INTERNAL_ERROR,
                       E.getMessage()));
  });
  return Response;
}

static protocol::AddRawModuleResponse
doAddRawModule(ModuleCache &Modules,
               const protocol::AddRawModuleRequest &Request) {
  protocol::AddRawModuleResponse Response;
  if (Modules.deleteModule(Request.rawmoduleid()))
    llvm::errs() << "Deleted dupicate module " << Request.rawmoduleid() << "\n";
  auto *M = [&]() -> const WasmModule * {
    auto SymbolsFile = [&]() -> llvm::Optional<llvm::StringRef> {
      if (Request.symbols().empty())
        return llvm::None;
      return {Request.symbols()};
    }();

    if (!Request.rawmodule().code().empty())
      return Modules.getModuleFromCode(Request.rawmoduleid(),
                                       Request.rawmodule().code(), SymbolsFile);

    if (!Request.rawmodule().url().empty())
      return Modules.getModuleFromUrl(Request.rawmoduleid(),
                                      Request.rawmodule().url(), SymbolsFile);
    return nullptr;
  }();

  if (!M)
    return setError(Response, makeNotFoundError(Request.rawmoduleid()));

  for (auto &SourceFile : M->getSourceScripts())
    Response.add_sources(SourceFile);

  return Response;
}

static protocol::SourceLocationToRawLocationResponse
doSourceLocationToRawLocation(ModuleCache &MC,
                              const protocol::SourceLocation &Loc) {

  protocol::SourceLocationToRawLocationResponse Response;
  auto Module = MC.findModule(Loc.rawmoduleid());
  if (!Module)
    return setError(Response, makeNotFoundError(Loc.sourcefile()));

  for (auto Offset : Module->getOffsetFromSourceLocation(
           {Loc.sourcefile(), static_cast<uint32_t>(Loc.linenumber() + 1),
            static_cast<uint16_t>(Loc.columnnumber() + 1)})) {
    auto *RawLoc = Response.add_rawlocation();
    RawLoc->set_rawmoduleid(Module->id().str());
    RawLoc->set_codeoffset(Offset);
  }
  return Response;
}

static protocol::RawLocationToSourceLocationResponse
doRawLocationToSourceLocation(ModuleCache &MC,
                              const protocol::RawLocation &Loc) {
  protocol::RawLocationToSourceLocationResponse Response;
  auto *Mod = MC.findModule(Loc.rawmoduleid());
  if (!Mod)
    return setError(Response, makeNotFoundError(Loc.rawmoduleid()));
  for (auto &SourceLoc : Mod->getSourceLocationFromOffset(Loc.codeoffset())) {
    auto *ProtoLoc = Response.add_sourcelocation();
    ProtoLoc->set_sourcefile(SourceLoc.File);
    ProtoLoc->set_rawmoduleid(Loc.rawmoduleid());
    ProtoLoc->set_linenumber(SourceLoc.Line - 1);
    ProtoLoc->set_columnnumber(SourceLoc.Column - 1);
  }
  return Response;
}

static protocol::Variable_Scope toProtocolScope(ValueType Scope) {
  switch (Scope) {
  case eValueTypeVariableGlobal:
    return protocol::Variable_Scope_GLOBAL;
  case eValueTypeVariableArgument:
    return protocol::Variable_Scope_PARAMETER;
  case eValueTypeVariableLocal:
  case eValueTypeVariableStatic:
    return protocol::Variable_Scope_LOCAL;
  case eValueTypeInvalid:
  case eValueTypeRegister:
  case eValueTypeRegisterSet:
  case eValueTypeConstResult:
  case eValueTypeVariableThreadLocal:
    llvm::errs() << "Got variable scope " << Scope << "\n";
    llvm_unreachable("Unhandled variable scope");
  }
}

protocol::ListVariablesInScopeResponse
extracted(const protocol::RawLocation &Loc,
          protocol::ListVariablesInScopeResponse &Response) {
  return setError(Response, makeNotFoundError(Loc.rawmoduleid()));
}
static protocol::ListVariablesInScopeResponse
doListVariables(ModuleCache &MC, const protocol::RawLocation &Loc) {
  protocol::ListVariablesInScopeResponse Response;

  auto *Mod = MC.findModule(Loc.rawmoduleid());
  if (!Mod)
    return extracted(Loc, Response);

  for (auto &Variable : Mod->getVariablesInScope(Loc.codeoffset())) {
    auto *ProtoVar = Response.add_variable();
    ProtoVar->set_scope(toProtocolScope(Variable.Scope));
    ProtoVar->set_name(Variable.Name);
    ProtoVar->set_type(Variable.Type);
  }
  return Response;
}

static protocol::EvaluateVariableResponse
doEvaluateVariables(ModuleCache &MC,
                    const protocol::EvaluateVariableRequest &Request) {
  protocol::EvaluateVariableResponse Response;
  auto *Mod = MC.findModule(Request.location().rawmoduleid());
  if (!Mod)
    return setError(Response,
                    makeNotFoundError(Request.location().rawmoduleid()));

  auto FormatScript = Mod->getVariableFormatScript(
      Request.name(), Request.location().codeoffset());
  if (!FormatScript)
    return setError(Response, FormatScript.takeError());

  Response.set_allocated_value(new protocol::RawModule());
  *Response.mutable_value()->mutable_code() = *FormatScript;
  return Response;
}

template <typename T>
static llvm::Expected<T> parse(llvm::json::Value Message) {
  T Result;
  std::string JSONString;
  {
    llvm::raw_string_ostream OS(JSONString);
    OS << Message;
  }

  auto Stat = google::protobuf::util::JsonStringToMessage(JSONString, &Result);
  if (!Stat.ok()) {
    llvm::errs() << __PRETTY_FUNCTION__ << ": Failed to decode json.\n";
    return llvm::make_error<llvm::StringError>(Stat.error_message().as_string(),
                                               llvm::inconvertibleErrorCode());
  }
  return Result;
}

static void reportError(llvm::raw_ostream &OS, llvm::Error E) {
  llvm::handleAllErrors(std::move(E), [&](const llvm::StringError &E) {
    OS << "{\"error\":"
       << *makeError(protocol::Error_Code_PROTOCOL_ERROR, E.getMessage())
       << "}";
  });
}

namespace {
class JSONRPCHandler : public clang::clangd::Transport::MessageHandler {
public:
  JSONRPCHandler(ModuleCache &MC, clang::clangd::Transport &TransportLayer)
      : TransportLayer(TransportLayer), MC(MC) {}

protected:
  bool onNotify(llvm::StringRef Method, llvm::json::Value Params) override {
    // llvm::errs() << "Notify " << Method << ": " << Params << "\n";
    return Method != "quit";
  }

  bool onCall(llvm::StringRef Method, llvm::json::Value Params,
              llvm::json::Value ID) override {

    std::string Response;
    {
      llvm::raw_string_ostream OS(Response);

      if (Method == "addRawModule") {
        auto M = parse<protocol::AddRawModuleRequest>(std::move(Params));
        if (M)
          OS << doAddRawModule(MC, *M);
        else
          reportError(OS, M.takeError());
      } else if (Method == "sourceLocationToRawLocation") {
        auto M = parse<protocol::SourceLocation>(std::move(Params));
        if (M)
          OS << doSourceLocationToRawLocation(MC, *M);
        else
          reportError(OS, M.takeError());
      } else if (Method == "rawLocationToSourceLocation") {
        auto M = parse<protocol::RawLocation>(std::move(Params));
        if (M)
          OS << doRawLocationToSourceLocation(MC, *M);
        else
          reportError(OS, M.takeError());
      } else if (Method == "listVariablesInScope") {
        auto M = parse<protocol::RawLocation>(std::move(Params));
        if (M)
          OS << doListVariables(MC, *M);
        else
          reportError(OS, M.takeError());
      } else if (Method == "evaluateVariable") {
        auto M = parse<protocol::EvaluateVariableRequest>(std::move(Params));
        if (M)
          OS << doEvaluateVariables(MC, *M);
        else
          reportError(OS, M.takeError());
      } else if (Method == "quit") {
        return false;
      } else {
        OS << *makeError(protocol::Error_Code_PROTOCOL_ERROR,
                         "Unknown protocol method '" + Method + "'");
      }
    }
    llvm::errs() << "Sending Response '" << Response << "'\n";
    TransportLayer.reply(std::move(ID), llvm::json::parse(Response));
    return true;
  }
  bool onReply(llvm::json::Value ID,
               llvm::Expected<llvm::json::Value> Result) override {
    // llvm::errs() << "Reply: " << ID << "\n";
    return true;
  }

  clang::clangd::Transport &TransportLayer;
  ModuleCache &MC;
};
} // namespace

/*static*/ int LLDBLanguageComponentServiceImpl::runInteractive() {
  LLDBLanguageComponentServiceImpl Service;

  llvm::errs() << "Running interactive listener\n";
  clang::clangd::StreamLogger Logger(llvm::errs(),
                                     clang::clangd::Logger::Debug);
  clang::clangd::LoggingSession S(Logger);

  auto TransportLayer =
      clang::clangd::newJSONTransport(stdin, llvm::outs(), nullptr, false);
  JSONRPCHandler Handler(Service.MDB, *TransportLayer);
  auto Status = TransportLayer->loop(Handler);
  if (Status) {
    llvm::errs() << Status << "\n";
    consumeError(std::move(Status));
    return 1;
  }
  return 0;
}

grpc::Status LLDBLanguageComponentServiceImpl::addRawModule(
    grpc::ServerContext *Context, const protocol::AddRawModuleRequest *Request,
    protocol::AddRawModuleResponse *Response) {
  *Response = doAddRawModule(MDB, *Request);
  return grpc::Status::OK;
}

grpc::Status LLDBLanguageComponentServiceImpl::sourceLocationToRawLocation(
    grpc::ServerContext *Context, const protocol::SourceLocation *Location,
    protocol::SourceLocationToRawLocationResponse *Response) {
  *Response = doSourceLocationToRawLocation(MDB, *Location);
  return grpc::Status::OK;
}

grpc::Status LLDBLanguageComponentServiceImpl::rawLocationToSourceLocation(
    grpc::ServerContext *Context, const protocol::RawLocation *Location,
    protocol::RawLocationToSourceLocationResponse *Response) {
  *Response = doRawLocationToSourceLocation(MDB, *Location);
  return grpc::Status::OK;
}

grpc::Status LLDBLanguageComponentServiceImpl::listVariablesInScope(
    grpc::ServerContext *Context, const protocol::RawLocation *Query,
    protocol::ListVariablesInScopeResponse *Response) {
  *Response = doListVariables(MDB, *Query);
  return grpc::Status::OK;
}

grpc::Status LLDBLanguageComponentServiceImpl::evaluateVariable(
    grpc::ServerContext *Context,
    const protocol::EvaluateVariableRequest *Query,
    protocol::EvaluateVariableResponse *Response) {
  *Response = doEvaluateVariables(MDB, *Query);
  return grpc::Status::OK;
}

} // namespace cdlc
} // namespace lldb
