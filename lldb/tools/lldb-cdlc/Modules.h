#ifndef LLDB_CDLC_MODULES_H_
#define LLDB_CDLC_MODULES_H_
#include "Variables.h"

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"

namespace lldb {
namespace cdlc {

using Binary = std::string;

struct SourceLocation {
  SourceLocation(llvm::StringRef File, uint32_t Line, uint16_t Column)
      : File(File), Line(Line), Column(Column) {}

  std::string File;
  uint32_t Line;
  uint16_t Column;
};

struct Variable {
  Variable(llvm::StringRef Name, ValueType Scope, llvm::StringRef Type)
      : Name(Name), Scope(Scope), Type(Type) {}

  std::string Name;
  ValueType Scope;
  std::string Type;
};

class WasmModule {
  friend class ModuleCache;
  lldb::ModuleSP Module;
  std::string Id;
  llvm::Optional<llvm::sys::fs::TempFile> TempModule;
  WasmModule(llvm::StringRef Id) : Id(Id) {}

public:
  ~WasmModule();
  WasmModule(const WasmModule &) = delete;
  WasmModule &operator=(const WasmModule &) = delete;
  WasmModule(WasmModule &&) = default;
  WasmModule &operator=(WasmModule &&) = default;

  static std::shared_ptr<WasmModule>
  createFromUrl(llvm::StringRef Id, llvm::StringRef Url,
                llvm::Optional<llvm::StringRef> SymbolsFile = llvm::None);

  static std::shared_ptr<WasmModule>
  createFromCode(llvm::StringRef Id, llvm::StringRef ByteCode,
                 llvm::Optional<llvm::StringRef> SymbolsFile = llvm::None);

  bool valid() const;
  llvm::StringRef id() const { return Id; }

  llvm::SmallVector<std::string, 1> getSourceScripts() const;
  llvm::SmallVector<SourceLocation, 1>
  getSourceLocationFromOffset(lldb::addr_t Offset) const;
  llvm::SmallVector<lldb::addr_t, 1>
  getOffsetFromSourceLocation(const SourceLocation &SourceLoc) const;
  llvm::SmallVector<MemoryLocation, 1>
  getVariablesInScope(const SourceLocation &SourceLoc) const;
  llvm::SmallVector<Variable, 1> getVariablesInScope(lldb::addr_t Offset) const;
  llvm::Expected<Binary>
  getVariableFormatScript(llvm::StringRef Name, lldb::addr_t FrameOffset) const;
  llvm::Expected<Binary>
  getVariableFormatScript(llvm::StringRef Name,
                          const MemoryLocation &Loc) const;
};

class ModuleCache {
  std::map<std::string, std::shared_ptr<WasmModule>> Modules;
  std::map<llvm::SmallString<32>, std::shared_ptr<WasmModule>> ModuleHashes;

public:
  const WasmModule *
  getModuleFromUrl(llvm::StringRef ID, llvm::StringRef Url,
                   llvm::Optional<llvm::StringRef> SymbolsFile = llvm::None);
  const WasmModule *
  getModuleFromCode(llvm::StringRef ID, llvm::StringRef ByteCode,
                    llvm::Optional<llvm::StringRef> SymbolsFile = llvm::None);

  const WasmModule *findModule(llvm::StringRef ID) const;
  bool deleteModule(llvm::StringRef ID);
  llvm::SmallVector<const WasmModule *, 1>
  findModulesContainingSourceScript(llvm::StringRef File) const;
};
} // namespace cdlc
} // namespace lldb
#endif // LLDB_CDLC_MODULES_H_
