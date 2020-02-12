#ifndef LLDB_CDLC_VARIABLES_H_
#define LLDB_CDLC_VARIABLES_H_

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include <functional>
#include <memory>

namespace lldb_private {
class CompilerType;
} // namespace lldb_private

namespace lldb {
namespace cdlc {
struct MemoryLocation {
  std::string Type;
  llvm::StringRef ModuleId;
  int32_t AddressSpace;
  int64_t Offset;
};

class VariablePrinter {
public:
  using StringSlice = std::pair<llvm::Value *, llvm::Value *>;
  VariablePrinter();
  VariablePrinter(const VariablePrinter &) = delete;
  VariablePrinter(VariablePrinter &&) = delete;

  llvm::Expected<std::unique_ptr<llvm::Module>>
  generateModule(llvm::StringRef VariableName,
                 const lldb_private::CompilerType &VariableType,
                 const MemoryLocation &VariableLocation);
  std::unique_ptr<llvm::MemoryBuffer> generateCode(llvm::Module &M);

private:
  std::unique_ptr<llvm::Module> loadRuntimeModule();

  llvm::Expected<StringSlice>
  formatVariable(llvm::IRBuilder<> &Builder, llvm::Value *Buffer,
                 llvm::Value *Size, llvm::StringRef Name,
                 const lldb_private::CompilerType &VariableType,
                 const MemoryLocation &Variable);
  llvm::Expected<StringSlice>
  formatPrimitive(llvm::IRBuilder<> &Builder, llvm::Value *Buffer,
                  llvm::Value *Size, llvm::StringRef Name,
                  const lldb_private::CompilerType &VariableType,
                  const MemoryLocation &Variable);
  llvm::Expected<StringSlice>
  formatAggregate(llvm::IRBuilder<> &Builder, llvm::Value *Buffer,
                  llvm::Value *Size, llvm::StringRef Name,
                  const lldb_private::CompilerType &VariableType,
                  const MemoryLocation &Variable);
  llvm::Expected<StringSlice> formatArray(
      llvm::IRBuilder<> &Builder, llvm::Value *Buffer, llvm::Value *Size,
      llvm::StringRef Name, const lldb_private::CompilerType &ElementType,
      const MemoryLocation &Variable, uint64_t ArraySize, bool Incomplete);

  llvm::LLVMContext MainContext;
  std::map<llvm::StringRef, std::function<llvm::FunctionCallee(llvm::Module &)>>
      PrimitiveFormatters;
};
} // namespace cdlc
} // namespace lldb

#endif // LLDB_CDLC_VARIABLES_H_
