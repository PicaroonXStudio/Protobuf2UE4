#include "cpp_file.h"
#include <map>
#include <memory>
#ifndef _SHARED_PTR_H
#include <google/protobuf/stubs/shared_ptr.h>
#endif
#include <set>
#include <vector>

#include <google/protobuf/io/printer.h>
#include <google/protobuf/descriptor.pb.h>

#include "cpp_enum.h"
#include "cpp_service.h"
#include "cpp_extension.h"
#include "cpp_message.h"
#include "cpp_field.h"
#include "cpp_helpers.h"
#include "strutil.h"

namespace google
{
    namespace protobuf
    {
        namespace compiler
        {
            namespace cpp
            {
                namespace
                {
                    // The list of names that are defined as macros on some platforms. We need to
                    // #undef them for the generated code to compile.
                    const char* kMacroNames[] = {"major", "minor"};

                    bool IsMacroName(const string& name)
                    {
                        // Just do a linear search as the number of elements is very small.
                        for (int i = 0; i < GOOGLE_ARRAYSIZE(kMacroNames); ++i)
                        {
                            if (name == kMacroNames[i]) return true;
                        }
                        return false;
                    }

                    void CollectMacroNames(const Descriptor* message, std::vector<string>* names)
                    {
                        for (int i = 0; i < message->field_count(); ++i)
                        {
                            const FieldDescriptor* field = message->field(i);
                            if (IsMacroName(field->name()))
                            {
                                names->push_back(field->name());
                            }
                        }
                        for (int i = 0; i < message->nested_type_count(); ++i)
                        {
                            CollectMacroNames(message->nested_type(i), names);
                        }
                    }

                    void CollectMacroNames(const FileDescriptor* file, std::vector<string>* names)
                    {
                        // Only do this for protobuf's own types. There are some google3 protos using
                        // macros as field names and the generated code compiles after the macro
                        // expansion. Undefing these macros actually breaks such code.
                        if (file->name() != "google/protobuf/compiler/plugin.proto")
                        {
                            return;
                        }
                        for (int i = 0; i < file->message_type_count(); ++i)
                        {
                            CollectMacroNames(file->message_type(i), names);
                        }
                    }
                } // namespace

                // ===================================================================

                UEFileGenerator::UEFileGenerator(const FileDescriptor* file, const Options& options)
                    : file_(file),
                      options_(options),
                      scc_analyzer_(options),
                      message_generators_owner_(
                          new google::protobuf::scoped_ptr<UEMessageGenerator>[file->message_type_count()]),
                      enum_generators_owner_(
                          new google::protobuf::scoped_ptr<EnumGenerator>[file->enum_type_count()])
                {
                    for (int i = 0; i < file->message_type_count(); i++)
                    {
                        message_generators_owner_[i].reset(
                            new UEMessageGenerator(file->message_type(i), options, &scc_analyzer_));
                        message_generators_owner_[i]->Flatten(&message_generators_);
                    }

                    for (int i = 0; i < file->enum_type_count(); i++)
                    {
                        enum_generators_owner_[i].reset(
                            new EnumGenerator(file->enum_type(i), options));
                        enum_generators_.push_back(enum_generators_owner_[i].get());
                    }
                    for (int i = 0; i < enum_generators_.size(); i++)
                    {
                        enum_generators_[i]->index_in_metadata_ = i;
                    }
                }

                UEFileGenerator::~UEFileGenerator()
                {
                }

                void UEFileGenerator::GenerateMacroUndefs(io::Printer* printer)
                {
                    std::vector<string> names_to_undef;
                    CollectMacroNames(file_, &names_to_undef);
                    for (int i = 0; i < names_to_undef.size(); ++i)
                    {
                        printer->Print(
                            "#ifdef $name$\n"
                            "#undef $name$\n"
                            "#endif\n",
                            "name", names_to_undef[i]);
                    }
                }

                void UEFileGenerator::GenerateHeader(io::Printer* printer,
                                                     const string& info_path)
                {
                    string header =
                        MyStripProto(file_->name()) + (options_.proto_h ? ".proto.h" : "_UE.generated.h");

                    GenerateTopHeaderGuard(printer, header);

                    GenerateEnumDefinitions(printer);

                    printer->Print(kThickSeparator);
                    printer->Print("\n");

                    GenerateMessageDefinitions(printer);
                }

                void UEFileGenerator::GenerateSource(io::Printer* printer)
                {
                    const bool use_system_include = IsWellKnownMessage(file_);
                    string header =
                        MyStripProto(file_->name()) + (options_.proto_h ? ".proto.h" : "_UE.h");
                    printer->Print(
                        "// Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
                        "// source: $filename$\n"
                        "\n"
                        "#include $left$$header$$right$\n"
                        "\n\n",
                        "filename", file_->name(),
                        "header", header,
                        "left", use_system_include ? "<" : "\"",
                        "right", use_system_include ? ">" : "\"");


                    for (int i = 0; i < enum_generators_.size(); i++)
                    {
                        enum_generators_[i]->index_in_metadata_ = i;
                    }

                    // Generate classes.
                    for (int i = 0; i < message_generators_.size(); i++)
                    {
                        printer->Print("\n");
                        message_generators_[i]->GenerateClassMethods(printer);
                    }

                    //if (HasGenericServices(file_, options_)) {
                    //  // Generate services.
                    //  for (int i = 0; i < service_generators_.size(); i++) {
                    //    if (i == 0) printer->Print("\n");
                    //    printer->Print(kThickSeparator);
                    //    printer->Print("\n");
                    //    service_generators_[i]->GenerateImplementation(printer);
                    //  }
                    //}

                    //// Define extensions.
                    //for (int i = 0; i < extension_generators_.size(); i++) {
                    //  extension_generators_[i]->GenerateDefinition(printer);
                    //}
                }

                class UEFileGenerator::ForwardDeclarations
                {
                public:
                    ~ForwardDeclarations()
                    {
                        for (std::map<string, ForwardDeclarations*>::iterator
                                 it = namespaces_.begin(),
                                 end = namespaces_.end();
                             it != end; ++it)
                        {
                            delete it->second;
                        }
                        namespaces_.clear();
                    }

                    ForwardDeclarations* AddOrGetNamespace(const string& ns_name)
                    {
                        ForwardDeclarations*& ns = namespaces_[ns_name];
                        if (ns == NULL)
                        {
                            ns = new ForwardDeclarations;
                        }
                        return ns;
                    }

                    std::map<string, const Descriptor*>& classes() { return classes_; }
                    std::map<string, const EnumDescriptor*>& enums() { return enums_; }

                    void Print(io::Printer* printer, const Options& options) const
                    {
                        for (std::map<string, const EnumDescriptor*>::const_iterator
                                 it = enums_.begin(),
                                 end = enums_.end();
                             it != end; ++it)
                        {
                            printer->Print("enum $enumname$ : int;\n", "enumname", it->first);
                            printer->Annotate("enumname", it->second);
                            printer->Print("bool $enumname$_IsValid(int value);\n", "enumname",
                                           it->first);
                        }
                        for (std::map<string, const Descriptor*>::const_iterator
                                 it = classes_.begin(),
                                 end = classes_.end();
                             it != end; ++it)
                        {
                            printer->Print("class $classname$;\n", "classname", it->first);
                            printer->Annotate("classname", it->second);

                            printer->Print(
                                "class $classname$DefaultTypeInternal;\n"
                                "$dllexport_decl$"
                                "extern $classname$DefaultTypeInternal "
                                "_$classname$_default_instance_;\n", // NOLINT
                                "dllexport_decl",
                                options.dllexport_decl.empty() ? "" : options.dllexport_decl + " ",
                                "classname",
                                it->first);
                        }
                        for (std::map<string, ForwardDeclarations*>::const_iterator
                                 it = namespaces_.begin(),
                                 end = namespaces_.end();
                             it != end; ++it)
                        {
                            printer->Print("namespace $nsname$ {\n",
                                           "nsname", it->first);
                            it->second->Print(printer, options);
                            printer->Print("}  // namespace $nsname$\n",
                                           "nsname", it->first);
                        }
                    }


                private:
                    std::map<string, ForwardDeclarations*> namespaces_;
                    std::map<string, const Descriptor*> classes_;
                    std::map<string, const EnumDescriptor*> enums_;
                };

                void UEFileGenerator::GenerateBuildDescriptors(io::Printer* printer)
                {
                    printer->Print("PROTOBUF_CONSTEXPR_VAR ::google::protobuf::internal::ParseTableField\n"
                        "    const TableStruct::entries[] "
                        "GOOGLE_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {\n");
                    printer->Indent();

                    std::vector<size_t> entries;
                    size_t count = 0;
                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  size_t value = message_generators_[i]->GenerateParseOffsets(printer);
                    //  entries.push_back(value);
                    //  count += value;
                    //}

                    //// We need these arrays to exist, and MSVC does not like empty arrays.
                    //if (count == 0) {
                    //  printer->Print("{0, 0, 0, ::google::protobuf::internal::kInvalidMask, 0, 0},\n");
                    //}

                    //printer->Outdent();
                    //printer->Print(
                    //    "};\n"
                    //    "\n"
                    //    "PROTOBUF_CONSTEXPR_VAR ::google::protobuf::internal::AuxillaryParseTableField\n"
                    //    "    const TableStruct::aux[] "
                    //    "GOOGLE_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {\n");
                    //printer->Indent();

                    //std::vector<size_t> aux_entries;
                    //count = 0;
                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  size_t value = message_generators_[i]->GenerateParseAuxTable(printer);
                    //  aux_entries.push_back(value);
                    //  count += value;
                    //}

                    //if (count == 0) {
                    //  printer->Print("::google::protobuf::internal::AuxillaryParseTableField(),\n");
                    //}

                    //printer->Outdent();
                    //printer->Print(
                    //    "};\n"
                    //    "PROTOBUF_CONSTEXPR_VAR ::google::protobuf::internal::ParseTable const\n"
                    //    "    TableStruct::schema[] "
                    //    "GOOGLE_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {\n");
                    //printer->Indent();

                    //size_t offset = 0;
                    //size_t aux_offset = 0;
                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  message_generators_[i]->GenerateParseTable(printer, offset, aux_offset);
                    //  offset += entries[i];
                    //  aux_offset += aux_entries[i];
                    //}

                    //if (message_generators_.empty()) {
                    //  printer->Print("{ NULL, NULL, 0, -1, -1, -1, -1, NULL, false },\n");
                    //}

                    //printer->Outdent();
                    //printer->Print(
                    //    "};\n"
                    //    "\n");

                    //if (!message_generators_.empty() && options_.table_driven_serialization) {
                    //  printer->Print(
                    //      "const ::google::protobuf::internal::FieldMetadata TableStruct::field_metadata[] "
                    //      "= {\n");
                    //  printer->Indent();
                    //  std::vector<int> field_metadata_offsets;
                    //  int idx = 0;
                    //  for (int i = 0; i < message_generators_.size(); i++) {
                    //    field_metadata_offsets.push_back(idx);
                    //    idx += message_generators_[i]->GenerateFieldMetadata(printer);
                    //  }
                    //  field_metadata_offsets.push_back(idx);
                    //  printer->Outdent();
                    //  printer->Print(
                    //      "};\n"
                    //      "const ::google::protobuf::internal::SerializationTable "
                    //      "TableStruct::serialization_table[] = {\n");
                    //  printer->Indent();
                    //  // We rely on the order we layout the tables to match the order we
                    //  // calculate them with FlattenMessagesInFile, so we check here that
                    //  // these match exactly.
                    //  std::vector<const Descriptor*> calculated_order =
                    //      FlattenMessagesInFile(file_);
                    //  GOOGLE_CHECK_EQ(calculated_order.size(), message_generators_.size());
                    //  for (int i = 0; i < message_generators_.size(); i++) {
                    //    GOOGLE_CHECK_EQ(calculated_order[i], message_generators_[i]->descriptor_);
                    //    printer->Print(
                    //        "{$num_fields$, TableStruct::field_metadata + $index$},\n",
                    //        "classname", message_generators_[i]->classname_, "num_fields",
                    //        SimpleItoa(field_metadata_offsets[i + 1] - field_metadata_offsets[i]),
                    //        "index", SimpleItoa(field_metadata_offsets[i]));
                    //  }
                    //  printer->Outdent();
                    //  printer->Print(
                    //      "};\n"
                    //      "\n");
                    //}
                    //if (HasDescriptorMethods(file_, options_)) {
                    //  if (!message_generators_.empty()) {
                    //    printer->Print("const ::google::protobuf::uint32 TableStruct::offsets[] "
                    //                   "GOOGLE_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {\n");
                    //    printer->Indent();
                    //    std::vector<std::pair<size_t, size_t> > pairs;
                    //    for (int i = 0; i < message_generators_.size(); i++) {
                    //      pairs.push_back(message_generators_[i]->GenerateOffsets(printer));
                    //    }
                    //    printer->Outdent();
                    //    printer->Print(
                    //        "};\n"
                    //        "static const ::google::protobuf::internal::MigrationSchema schemas[] "
                    //        "GOOGLE_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {\n");
                    //    printer->Indent();
                    //    {
                    //      int offset = 0;
                    //      for (int i = 0; i < message_generators_.size(); i++) {
                    //        message_generators_[i]->GenerateSchema(printer, offset,
                    //                                               pairs[i].second);
                    //        offset += pairs[i].first;
                    //      }
                    //    }
                    //    printer->Outdent();
                    //    printer->Print(
                    //        "};\n"
                    //        "\nstatic "
                    //        "::google::protobuf::Message const * const file_default_instances[] = {\n");
                    //    printer->Indent();
                    //    for (int i = 0; i < message_generators_.size(); i++) {
                    //      const Descriptor* descriptor = message_generators_[i]->descriptor_;
                    //      printer->Print(
                    //          "reinterpret_cast<const "
                    //          "::google::protobuf::Message*>(&_$classname$_default_instance_),\n",
                    //          "classname", ClassName(descriptor, false));
                    //    }
                    //    printer->Outdent();
                    //    printer->Print(
                    //        "};\n"
                    //        "\n");
                    //  } else {
                    //    // we still need these symbols to exist
                    //    printer->Print(
                    //        // MSVC doesn't like empty arrays, so we add a dummy.
                    //        "const ::google::protobuf::uint32 TableStruct::offsets[1] = {};\n"
                    //        "static const ::google::protobuf::internal::MigrationSchema* schemas = NULL;\n"
                    //        "static const ::google::protobuf::Message* const* "
                    //        "file_default_instances = NULL;\n");
                    //  }

                    //  // ---------------------------------------------------------------

                    //  // protobuf_AssignDescriptorsOnce():  The first time it is called, calls
                    //  // AssignDescriptors().  All later times, waits for the first call to
                    //  // complete and then returns.
                    //  string message_factory = "NULL";
                    //  printer->Print(
                    //      "namespace {\n"
                    //      "\n"
                    //      "void protobuf_AssignDescriptors() {\n"
                    //      // Make sure the file has found its way into the pool.  If a descriptor
                    //      // is requested *during* static init then AddDescriptors() may not have
                    //      // been called yet, so we call it manually.  Note that it's fine if
                    //      // AddDescriptors() is called multiple times.
                    //      "  AddDescriptors();\n"
                    //      "  ::google::protobuf::MessageFactory* factory = $factory$;\n"
                    //      "  AssignDescriptors(\n"
                    //      "      \"$filename$\", schemas, file_default_instances, "
                    //      "TableStruct::offsets, factory,\n"
                    //      "      $metadata$, $enum_descriptors$, $service_descriptors$);\n",
                    //      "filename", file_->name(), "metadata",
                    //      !message_generators_.empty() ? "file_level_metadata" : "NULL",
                    //      "enum_descriptors",
                    //      !enum_generators_.empty() ? "file_level_enum_descriptors" : "NULL",
                    //      "service_descriptors",
                    //      HasGenericServices(file_, options_) && file_->service_count() > 0
                    //          ? "file_level_service_descriptors"
                    //          : "NULL",
                    //      "factory", message_factory);
                    //  // TODO(gerbens) have the compiler include the schemas for map types
                    //  // so that this can go away, and we can potentially use table driven
                    //  // serialization for map types as well.
                    //  for (int i = 0; i < message_generators_.size(); i++) {
                    //    if (!IsMapEntryMessage(message_generators_[i]->descriptor_)) continue;
                    //    printer->Print(
                    //        "file_level_metadata[$index$].reflection = "
                    //        "$parent$::$classname$::CreateReflection(file_level_metadata[$index$]"
                    //        ".descriptor, _$classname$_default_instance_._instance.get_mutable());\n",
                    //        "index", SimpleItoa(i), "parent",
                    //        ClassName(message_generators_[i]->descriptor_->containing_type(),
                    //                  false),
                    //        "classname", ClassName(message_generators_[i]->descriptor_, false));
                    //  }
                    //  printer->Print(
                    //      "}\n"
                    //      "\n"
                    //      "void protobuf_AssignDescriptorsOnce() {\n"
                    //      "  static GOOGLE_PROTOBUF_DECLARE_ONCE(once);\n"
                    //      "  ::google::protobuf::GoogleOnceInit(&once, &protobuf_AssignDescriptors);\n"
                    //      "}\n"
                    //      "\n",
                    //      "filename", file_->name(), "metadata",
                    //      !message_generators_.empty() ? "file_level_metadata" : "NULL",
                    //      "enum_descriptors",
                    //      !enum_generators_.empty() ? "file_level_enum_descriptors" : "NULL",
                    //      "service_descriptors",
                    //      HasGenericServices(file_, options_) && file_->service_count() > 0
                    //          ? "file_level_service_descriptors"
                    //          : "NULL",
                    //      "factory", message_factory);

                    //  // Only here because of useless string reference that we don't want in
                    //  // protobuf_AssignDescriptorsOnce, because that is called from all the
                    //  // GetMetadata member methods.
                    //  printer->Print(
                    //      "void protobuf_RegisterTypes(const ::std::string&) GOOGLE_ATTRIBUTE_COLD;\n"
                    //      "void protobuf_RegisterTypes(const ::std::string&) {\n"
                    //      "  protobuf_AssignDescriptorsOnce();\n");
                    //  printer->Indent();

                    //  // All normal messages can be done generically
                    //  if (!message_generators_.empty()) {
                    //    printer->Print(
                    //      "::google::protobuf::internal::RegisterAllTypes(file_level_metadata, $size$);\n",
                    //      "size", SimpleItoa(message_generators_.size()));
                    //  }

                    //  printer->Outdent();
                    //  printer->Print(
                    //    "}\n"
                    //    "\n"
                    //    "}  // namespace\n");
                    //}

                    //// Now generate the InitDefaultsImpl() function.
                    //printer->Print(
                    //    "void TableStruct::InitDefaultsImpl() {\n"
                    //    "  GOOGLE_PROTOBUF_VERIFY_VERSION;\n\n"
                    //    // Force initialization of primitive values we depend on.
                    //    "  ::google::protobuf::internal::InitProtobufDefaults();\n");

                    //printer->Indent();

                    //// Call the InitDefaults() methods for all of our dependencies, to make
                    //// sure they get added first.
                    //for (int i = 0; i < file_->dependency_count(); i++) {
                    //  const FileDescriptor* dependency = file_->dependency(i);
                    //  // Print the namespace prefix for the dependency.
                    //  string file_namespace = QualifiedFileLevelSymbol(
                    //      dependency->package(), FileLevelNamespace(dependency->name()));
                    //  // Call its AddDescriptors function.
                    //  printer->Print("$file_namespace$::InitDefaults();\n", "file_namespace",
                    //                 file_namespace);
                    //}

                    //// Allocate and initialize default instances.  This can't be done lazily
                    //// since default instances are returned by simple accessors and are used with
                    //// extensions.  Speaking of which, we also register extensions at this time.
                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  message_generators_[i]->GenerateDefaultInstanceAllocator(printer);
                    //}
                    //for (int i = 0; i < extension_generators_.size(); i++) {
                    //  extension_generators_[i]->GenerateRegistration(printer);
                    //}
                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  message_generators_[i]->GenerateDefaultInstanceInitializer(printer);
                    //}
                    //printer->Outdent();
                    //printer->Print(
                    //    "}\n"
                    //    "\n"
                    //    "void InitDefaults() {\n"
                    //    "  static GOOGLE_PROTOBUF_DECLARE_ONCE(once);\n"
                    //    "  ::google::protobuf::GoogleOnceInit(&once, &TableStruct::InitDefaultsImpl);\n"
                    //    "}\n");

                    //// -----------------------------------------------------------------

                    //// Now generate the AddDescriptors() function.
                    //printer->Print(
                    //    "namespace {\n"
                    //    "void AddDescriptorsImpl() {\n"
                    //    "  InitDefaults();\n");

                    //printer->Indent();
                    //if (HasDescriptorMethods(file_, options_)) {
                    //  // Embed the descriptor.  We simply serialize the entire
                    //  // FileDescriptorProto
                    //  // and embed it as a string literal, which is parsed and built into real
                    //  // descriptors at initialization time.
                    //  FileDescriptorProto file_proto;
                    //  file_->CopyTo(&file_proto);
                    //  string file_data;
                    //  file_proto.SerializeToString(&file_data);

                    //  printer->Print("static const char descriptor[] "
                    //                 "GOOGLE_ATTRIBUTE_SECTION_VARIABLE(protodesc_cold) = {\n");
                    //  printer->Indent();

                    //  if (file_data.size() > 66535) {
                    //    // Workaround for MSVC: "Error C1091: compiler limit: string exceeds 65535
                    //    // bytes in length". Declare a static array of characters rather than use
                    //    // a string literal. Only write 25 bytes per line.
                    //    static const int kBytesPerLine = 25;
                    //    for (int i = 0; i < file_data.size();) {
                    //      for (int j = 0; j < kBytesPerLine && i < file_data.size(); ++i, ++j) {
                    //        printer->Print("'$char$', ", "char",
                    //                       CEscape(file_data.substr(i, 1)));
                    //      }
                    //      printer->Print("\n");
                    //    }
                    //  } else {
                    //    // Only write 40 bytes per line.
                    //    static const int kBytesPerLine = 40;
                    //    for (int i = 0; i < file_data.size(); i += kBytesPerLine) {
                    //      printer->Print("  \"$data$\"\n", "data",
                    //                     EscapeTrigraphs(CEscape(
                    //                         file_data.substr(i, kBytesPerLine))));
                    //    }
                    //  }

                    //  printer->Outdent();
                    //  printer->Print("};\n");
                    //  printer->Print(
                    //      "::google::protobuf::DescriptorPool::InternalAddGeneratedFile(\n"
                    //      "    descriptor, $size$);\n",
                    //      "size", SimpleItoa(file_data.size()));

                    //  // Call MessageFactory::InternalRegisterGeneratedFile().
                    //  printer->Print(
                    //    "::google::protobuf::MessageFactory::InternalRegisterGeneratedFile(\n"
                    //    "  \"$filename$\", &protobuf_RegisterTypes);\n",
                    //    "filename", file_->name());
                    //}

                    //// Call the AddDescriptors() methods for all of our dependencies, to make
                    //// sure they get added first.
                    //for (int i = 0; i < file_->dependency_count(); i++) {
                    //  const FileDescriptor* dependency = file_->dependency(i);
                    //  // Print the namespace prefix for the dependency.
                    //  string file_namespace = QualifiedFileLevelSymbol(
                    //      dependency->package(), FileLevelNamespace(dependency->name()));
                    //  // Call its AddDescriptors function.
                    //  printer->Print("$file_namespace$::AddDescriptors();\n", "file_namespace",
                    //                 file_namespace);
                    //}

                    //printer->Outdent();
                    //printer->Print(
                    //    "}\n"
                    //    "} // anonymous namespace\n"
                    //    "\n"
                    //    "void AddDescriptors() {\n"
                    //    "  static GOOGLE_PROTOBUF_DECLARE_ONCE(once);\n"
                    //    "  ::google::protobuf::GoogleOnceInit(&once, &AddDescriptorsImpl);\n"
                    //    "}\n");

                    //if (StaticInitializersForced(file_, options_)) {
                    //  printer->Print(
                    //      "// Force AddDescriptors() to be called at dynamic initialization "
                    //      "time.\n"
                    //      "struct StaticDescriptorInitializer {\n"
                    //      "  StaticDescriptorInitializer() {\n"
                    //      "    AddDescriptors();\n"
                    //      "  }\n"
                    //      "} static_descriptor_initializer;\n");
                    //}
                }

                void UEFileGenerator::GenerateNamespaceOpeners(io::Printer* printer)
                {
                    if (package_parts_.size() > 0) printer->Print("\n");

                    for (int i = 0; i < package_parts_.size(); i++)
                    {
                        printer->Print("namespace $part$ {\n",
                                       "part", package_parts_[i]);
                    }
                }

                void UEFileGenerator::GenerateNamespaceClosers(io::Printer* printer)
                {
                    if (package_parts_.size() > 0) printer->Print("\n");

                    for (int i = package_parts_.size() - 1; i >= 0; i--)
                    {
                        printer->Print("}  // namespace $part$\n",
                                       "part", package_parts_[i]);
                    }
                }

                void UEFileGenerator::GenerateForwardDeclarations(io::Printer* printer)
                {
                    ForwardDeclarations decls;
                    FillForwardDeclarations(&decls);
                    decls.Print(printer, options_);
                }

                void UEFileGenerator::FillForwardDeclarations(ForwardDeclarations* decls)
                {
                    for (int i = 0; i < package_parts_.size(); i++)
                    {
                        decls = decls->AddOrGetNamespace(package_parts_[i]);
                    }
                    // Generate enum definitions.
                    for (int i = 0; i < enum_generators_.size(); i++)
                    {
                        enum_generators_[i]->FillForwardDeclaration(&decls->enums());
                    }
                    // Generate forward declarations of classes.
                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  message_generators_[i]->FillMessageForwardDeclarations(
                    //      &decls->classes());
                    //}
                }

                void UEFileGenerator::GenerateTopHeaderGuard(io::Printer* printer,
                                                             const string& filename_identifier)
                {
                    // Generate top of header.
                    string FileName = file_->name();
                    FileName.replace(FileName.end() - 6, FileName.end(), "");
                    printer->Print(
                        "// Generated by the protocol buffer compiler.  DO NOT EDIT!\n"
                        "// source: $filename$\n"
                        "\n"
                        "#pragma once\n"
                        "#include \"Project_X/Utility/APIServer/Public/APIProtocol.h\"\n"
                        "#include \"$filename_clean$.pb.h\"\n",
                        "filename", file_->name(),
                        "filename_identifier", filename_identifier,
                        "filename_clean", FileName);

                    // Generate enum definitions.
                    for (int i = 0; i < enum_generators_.size(); i++)
                    {
                        enum_generators_[i]->GenerateDefinitionHead(printer);
                    }

                    for (int i = 0; i < file_->dependency_count(); i++)
                    {
                        const FileDescriptor* dep = file_->dependency(i);
                        const char* extension = "_UE.h";
                        // if (!starts_with(dep->name(), "enum_"))
                        // {
                            string dependency = MyStripProto(dep->name()) + extension;
                            printer->Print(
                                "#include \"$dependency$\"\n",
                                "dependency", dependency);
                        //}
                    }
                    if (file_->package().length() > 0)
                    {
                        printer->Print(
                            "#include \"$filename_identifier$\"\n\n"
                            "using namespace  $pakagename$;\n\n",
                            "filename", file_->name(),
                            "pakagename", file_->package(),
                            "filename_identifier", filename_identifier);
                    }
                    else
                    {
                        printer->Print(
                            "#include \"$filename_identifier$\"\n\n",
                            "filename", file_->name(),
                            "pakagename", file_->package(),
                            "filename_identifier", filename_identifier);
                    }

                    printer->Print("\n");
                }

                void UEFileGenerator::GenerateBottomHeaderGuard(
                    io::Printer* printer, const string& filename_identifier)
                {
                }

                void UEFileGenerator::GenerateLibraryIncludes(io::Printer* printer)
                {
                }

                void UEFileGenerator::GenerateMetadataPragma(io::Printer* printer,
                                                             const string& info_path)
                {
                    if (!info_path.empty() && !options_.annotation_pragma_name.empty() &&
                        !options_.annotation_guard_name.empty())
                    {
                        printer->Print(
                            "#ifdef $guard$\n"
                            "#pragma $pragma$ \"$info_path$\"\n"
                            "#endif  // $guard$\n",
                            "guard", options_.annotation_guard_name, "pragma",
                            options_.annotation_pragma_name, "info_path", info_path);
                    }
                }

                void UEFileGenerator::GenerateDependencyIncludes(io::Printer* printer)
                {
                    std::set<string> public_import_names;
                    for (int i = 0; i < file_->public_dependency_count(); i++)
                    {
                        public_import_names.insert(file_->public_dependency(i)->name());
                    }

                    for (int i = 0; i < file_->dependency_count(); i++)
                    {
                        const bool use_system_include = IsWellKnownMessage(file_->dependency(i));
                        const string& name = file_->dependency(i)->name();
                        bool public_import = (public_import_names.count(name) != 0);


                        printer->Print(
                            "#include $left$$dependency$.pb.h$right$$iwyu$\n",
                            "dependency", MyStripProto(name),
                            "iwyu", (public_import) ? "  // IWYU pragma: export" : "",
                            "left", use_system_include ? "<" : "\"",
                            "right", use_system_include ? ">" : "\"");
                    }
                }

                void UEFileGenerator::GenerateGlobalStateFunctionDeclarations(
                    io::Printer* printer)
                {
                }

                void UEFileGenerator::GenerateMessageDefinitions(io::Printer* printer)
                {
                    // Generate class definitions.
                    for (int i = 0; i < message_generators_.size(); i++)
                    {
                        if (i > 0)
                        {
                            printer->Print("\n");
                            printer->Print(kThinSeparator);
                            printer->Print("\n");
                        }
                        message_generators_[i]->GenerateClassDefinition(printer);
                    }
                }

                void UEFileGenerator::GenerateEnumDefinitions(io::Printer* printer)
                {
                    // Generate enum definitions.
                    for (int i = 0; i < enum_generators_.size(); i++)
                    {
                        enum_generators_[i]->GenerateDefinition(printer);
                    }
                }

                void UEFileGenerator::GenerateServiceDefinitions(io::Printer* printer)
                {
                    if (HasGenericServices(file_, options_))
                    {
                        // Generate service definitions.
                        //for (int i = 0; i < service_generators_.size(); i++) {
                        //  if (i > 0) {
                        //    printer->Print("\n");
                        //    printer->Print(kThinSeparator);
                        //    printer->Print("\n");
                        //  }
                        //  //service_generators_[i]->GenerateDeclarations(printer);
                        //}

                        printer->Print("\n");
                        printer->Print(kThickSeparator);
                        printer->Print("\n");
                    }
                }

                void UEFileGenerator::GenerateExtensionIdentifiers(io::Printer* printer)
                {
                    // Declare extension identifiers. These are in global scope and so only
                    // the global scope extensions.
                    for (int i = 0; i < file_->extension_count(); i++)
                    {
                        //extension_generators_owner_[i]->GenerateDeclaration(printer);
                    }
                }

                void UEFileGenerator::GenerateInlineFunctionDefinitions(io::Printer* printer)
                {
                    // An aside about inline functions in .proto.h mode:
                    //
                    // The PROTOBUF_INLINE_NOT_IN_HEADERS symbol controls conditionally
                    // moving much of the inline functions to the .pb.cc file, which can be a
                    // significant performance benefit for compilation time, at the expense
                    // of non-inline function calls.
                    //
                    // However, in .proto.h mode, the definition of the internal dependent
                    // base class must remain in the header, and can never be out-lined. The
                    // dependent base class also needs access to has-bit manipuation
                    // functions, so the has-bit functions must be unconditionally inlined in
                    // proto_h mode.
                    //
                    // This gives us three flavors of functions:
                    //
                    //  1. Functions on the message not used by the internal dependent base
                    //     class: in .proto.h mode, only some functions are defined on the
                    //     message class; others are defined on the dependent base class.
                    //     These are guarded and can be out-lined. These are generated by
                    //     GenerateInlineMethods, and include has_* bit functions in
                    //     non-proto_h mode.
                    //
                    //  2. Functions on the internal dependent base class: these functions
                    //     are dependent on a template parameter, so they always need to
                    //     remain in the header.
                    //
                    //  3. Functions on the message that are used by the dependent base: the
                    //     dependent base class down casts itself to the message
                    //     implementation class to access these functions (the has_* bit
                    //     manipulation functions). Unlike #1, these functions must
                    //     unconditionally remain in the header. These are emitted by
                    //     GenerateDependentInlineMethods, even though they are not actually
                    //     dependent.
                    printer->Print(
                        "#ifdef PROTOBUF_INLINE_NOT_IN_HEADERS\n"
                        "  #define  PROTOBUF_INLINE_NOT_IN_HEADERS 0\n"
                        "#endif  // __GNUC__\n");

                    printer->Print("#if !PROTOBUF_INLINE_NOT_IN_HEADERS\n");
                    // TODO(gerbens) remove pragmas when gcc is no longer used. Current version
                    // of gcc fires a bogus error when compiled with strict-aliasing.
                    printer->Print(
                        "#ifdef __GNUC__\n"
                        "  #pragma GCC diagnostic push\n"
                        "  #pragma GCC diagnostic ignored \"-Wstrict-aliasing\"\n"
                        "#endif  // __GNUC__\n");
                    // Generate class inline methods.
                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  if (i > 0) {
                    //    printer->Print(kThinSeparator);
                    //    printer->Print("\n");
                    //  }
                    //  //message_generators_[i]->GenerateInlineMethods(printer,
                    //  //                                              /* is_inline = */ true);
                    //}
                    printer->Print(
                        "#ifdef __GNUC__\n"
                        "  #pragma GCC diagnostic pop\n"
                        "#endif  // __GNUC__\n");
                    printer->Print("#endif  // !PROTOBUF_INLINE_NOT_IN_HEADERS\n");

                    //for (int i = 0; i < message_generators_.size(); i++) {
                    //  if (i > 0) {
                    //    printer->Print(kThinSeparator);
                    //    printer->Print("\n");
                    //  }
                    //  // Methods of the dependent base class must always be inline in the header.
                    //  //message_generators_[i]->GenerateDependentInlineMethods(printer);
                    //}
                }

                void UEFileGenerator::GenerateProto2NamespaceEnumSpecializations(
                    io::Printer* printer)
                {
                    // Emit GetEnumDescriptor specializations into google::protobuf namespace:
                    if (HasEnumDefinitions(file_))
                    {
                        printer->Print(
                            "\n"
                            "namespace google {\nnamespace protobuf {\n"
                            "\n");
                        for (int i = 0; i < enum_generators_.size(); i++)
                        {
                            enum_generators_[i]->GenerateGetEnumDescriptorSpecializations(printer);
                        }
                        printer->Print(
                            "\n"
                            "}  // namespace protobuf\n}  // namespace google\n");
                    }
                }
            } // namespace cpp
        } // namespace compiler
    } // namespace protobuf
} // namespace google
