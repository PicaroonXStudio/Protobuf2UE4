#ifndef GOOGLE_PROTOBUF_COMPILER_CPP_FILE_H__
#define GOOGLE_PROTOBUF_COMPILER_CPP_FILE_H__

#include <memory>
#ifndef _SHARED_PTR_H
#include <google/protobuf/stubs/shared_ptr.h>
#endif
#include <string>
#include <vector>
#include <google/protobuf/stubs/common.h>
#include "cpp_field.h"
#include "cpp_helpers.h"
#include "cpp_options.h"
#include "cpp_extension.h"
#include "cpp_message.h"

namespace google {
namespace protobuf {
  class FileDescriptor;        // descriptor.h
  namespace io {
    class Printer;             // printer.h
  }
}

namespace protobuf {
namespace compiler {
namespace cpp {

class EnumGenerator;           // enum.h
//class MessageGenerator;        // message.h
//class ServiceGenerator;        // service.h
//class ExtensionGenerator;      // extension.h

class UEFileGenerator {
 public:
  // See generator.cc for the meaning of dllexport_decl.
  UEFileGenerator(const FileDescriptor* file, const Options& options);
  ~UEFileGenerator();

  // info_path, if non-empty, should be the path (relative to printer's output)
  // to the metadata file describing this proto header.
  void GenerateProtoHeader(io::Printer* printer,
                           const string& info_path);
  // info_path, if non-empty, should be the path (relative to printer's output)
  // to the metadata file describing this PB header.
  void GenerateHeader(io::Printer* printer,
                        const string& info_path);
  void GenerateSource(io::Printer* printer);

 private:
  // Internal type used by GenerateForwardDeclarations (defined in file.cc).
  class ForwardDeclarations;

  // Generate the BuildDescriptors() procedure, which builds all descriptors
  // for types defined in the file.
  void GenerateBuildDescriptors(io::Printer* printer);

  void GenerateNamespaceOpeners(io::Printer* printer);
  void GenerateNamespaceClosers(io::Printer* printer);

  // For other imports, generates their forward-declarations.
  void GenerateForwardDeclarations(io::Printer* printer);

  // Internal helper used by GenerateForwardDeclarations: fills 'decls'
  // with all necessary forward-declarations for this file and its
  // transient depednencies.
  void FillForwardDeclarations(ForwardDeclarations* decls);

  // Generates top or bottom of a header file.
  void GenerateTopHeaderGuard(io::Printer* printer,
                              const string& filename_identifier);
  void GenerateBottomHeaderGuard(io::Printer* printer,
                                 const string& filename_identifier);

  // Generates #include directives.
  void GenerateLibraryIncludes(io::Printer* printer);
  void GenerateDependencyIncludes(io::Printer* printer);

  // Generate a pragma to pull in metadata using the given info_path (if
  // non-empty). info_path should be relative to printer's output.
  void GenerateMetadataPragma(io::Printer* printer, const string& info_path);

  // Generates a couple of different pieces before definitions:
  void GenerateGlobalStateFunctionDeclarations(io::Printer* printer);

  // Generates types for classes.
  void GenerateMessageDefinitions(io::Printer* printer);

  void GenerateEnumDefinitions(io::Printer* printer);

  // Generates generic service definitions.
  void GenerateServiceDefinitions(io::Printer* printer);

  // Generates extension identifiers.
  void GenerateExtensionIdentifiers(io::Printer* printer);

  // Generates inline function defintions.
  void GenerateInlineFunctionDefinitions(io::Printer* printer);

  void GenerateProto2NamespaceEnumSpecializations(io::Printer* printer);

  // Sometimes the names we use in a .proto file happen to be defined as macros
  // on some platforms (e.g., macro/minor used in plugin.proto are defined as
  // macros in sys/types.h on FreeBSD and a few other platforms). To make the
  // generated code compile on these platforms, we either have to undef the
  // macro for these few platforms, or rename the field name for all platforms.
  // Since these names are part of protobuf public API, renaming is generally
  // a breaking change so we prefer the #undef approach.
  void GenerateMacroUndefs(io::Printer* printer);

  const FileDescriptor* file_;
  const Options options_;

  SCCAnalyzer scc_analyzer_;

  // Contains the post-order walk of all the messages (and child messages) in
  // this file. If you need a pre-order walk just reverse iterate.
  std::vector<UEMessageGenerator*> message_generators_;
  std::vector<EnumGenerator*> enum_generators_;
  //std::vector<ServiceGenerator*> service_generators_;
  //td::vector<ExtensionGenerator*> extension_generators_;

  // These members are just for owning (and thus proper deleting). Some of the
  // message_ and enum_generators above are owned by child messages.
  google::protobuf::scoped_array<google::protobuf::scoped_ptr<UEMessageGenerator> >  message_generators_owner_;
  google::protobuf::scoped_array<google::protobuf::scoped_ptr<EnumGenerator> > enum_generators_owner_;
  /*google::protobuf::scoped_array<google::protobuf::scoped_ptr<ServiceGenerator> >
	  service_generators_owner_;*/
  google::protobuf::scoped_array<google::protobuf::scoped_ptr<ExtensionGenerator> >
      extension_generators_owner_;

  // E.g. if the package is foo.bar, package_parts_ is {"foo", "bar"}.
  std::vector<string> package_parts_;

  GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(UEFileGenerator);
};

}  // namespace cpp
}  // namespace compiler
}  // namespace protobuf

}  // namespace google
#endif  // GOOGLE_PROTOBUF_COMPILER_CPP_FILE_H__
