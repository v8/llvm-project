#include "RPC.h"
#include "Util.h"

#include <iterator>

#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"
#include "Plugins/ObjectFile/ELF/ObjectFileELF.h"
#include "Plugins/ObjectFile/wasm/ObjectFileWasm.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"
#include "Plugins/SymbolVendor/wasm/SymbolVendorWasm.h"
#include "google/protobuf/util/json_util.h"
#include "lldb/Host/FileSystem.h"
#include "llvm/Support//CommandLine.h"
#include "llvm/Support/raw_ostream.h"

llvm::cl::opt<llvm::StringRef> Port("port");

int main(int Argc, char **Argv) {
  if (!llvm::cl::ParseCommandLineOptions(Argc, Argv, "Description",
                                         &llvm::errs()))
    return 7;

  lldb::cdlc::DefaultPluginsContext C;
  return lldb::cdlc::LLDBLanguageComponentServiceImpl::runInteractive();
}
