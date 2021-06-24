#include <map>

#include "cpp_enum.h"
#include "cpp_helpers.h"
#include <google/protobuf/io/printer.h>
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
                    // The GOOGLE_ARRAYSIZE constant is the max enum value plus 1. If the max enum value
                    // is ::google::protobuf::kint32max, GOOGLE_ARRAYSIZE will overflow. In such cases we should omit the
                    // generation of the GOOGLE_ARRAYSIZE constant.
                    bool ShouldGenerateArraySize(const EnumDescriptor* descriptor)
                    {
                        int32 max_value = descriptor->value(0)->number();
                        for (int i = 0; i < descriptor->value_count(); i++)
                        {
                            if (descriptor->value(i)->number() > max_value)
                            {
                                max_value = descriptor->value(i)->number();
                            }
                        }
                        return max_value != ::google::protobuf::kint32max;
                    }
                } // namespace

                EnumGenerator::EnumGenerator(const EnumDescriptor* descriptor,
                                             const Options& options)
                    : descriptor_(descriptor),
                      classname_(ClassName(descriptor, false)),
                      options_(options),
                      generate_array_size_(ShouldGenerateArraySize(descriptor))
                {
                }

                EnumGenerator::~EnumGenerator()
                {
                }

                void EnumGenerator::FillForwardDeclaration(
                    std::map<string, const EnumDescriptor*>* enum_names)
                {
                    if (!options_.proto_h)
                    {
                        return;
                    }
                    (*enum_names)[classname_] = descriptor_;
                }

                void EnumGenerator::GenerateDefinitionHead(io::Printer* printer)
                {
                    std::map<string, string> vars;
                    vars["classname"] = classname_;
                    for (int i = 0; i < descriptor_->value_count(); i++)
                    {
                        SourceLocation Location;
                        descriptor_->value(i)->GetSourceLocation(&Location);
                        string comment = Location.trailing_comments;
                        vector<string> splitResult = split(comment, ",", false);
                        bool foundRep =false;
                        for (string s : splitResult)
                        {
                            s =StringReplace(s, " ","",true);
                            if (starts_with(s, "file="))
                            {
                                s = s.replace(0, 5, "");
                                s = s.replace(s.length()-6, s.length(), "");
                                if( filenames.count(s) == 0)
                                {
                                    vars["repname"] = s;
                                    foundRep = true;
                                    filenames.insert({s, s});
                                }
                            }
                        }
                        if(foundRep)
                        {
                            printer->Print(vars, "#include \"Project_X/Utility/APIServer/Generated/$repname$_UE.h\"\n");    
                        }
                        
                    }
                }

                void EnumGenerator::GenerateDefinition(io::Printer* printer)
                {
                    std::map<string, string> vars;
                    vars["classname"] = classname_;
                    vars["short_name"] = descriptor_->name();
                    vars["enumbase"] = options_.proto_h ? " : int" : "";
                    // These variables are placeholders to pick out the beginning and ends of
                    // identifiers for annotations (when doing so with existing variables would
                    // be ambiguous or impossible). They should never be set to anything but the
                    // empty string.
                    vars["{"] = "";
                    vars["}"] = "";
                    printer->Print("UCLASS()\n"
                        "class PROJECT_X_API UResponseMap : public UObject\n"
                        "{\n GENERATED_BODY() \n public:\n");
                    printer->Print("TMap<int, TSubclassOf<UResponse>> ResponseMap = \n{\n");
                    printer->Annotate("classname", descriptor_);
                   
                    for (int i = 0; i < descriptor_->value_count(); i++)
                    {
                        printer->Indent();
                        vars["name"] = EnumValueName(descriptor_->value(i));
                        SourceLocation Location;
                        descriptor_->value(i)->GetSourceLocation(&Location);
                        vars["nameoption"] = Location.trailing_comments;
                        string comment = Location.trailing_comments;
                        vector<string> splitResult = split(comment, ",", false);
                        bool FoundRef = false;
                        for (string s : splitResult)
                        {
                            if (starts_with(s, "req="))
                            {
                                s = s.replace(0, 4, "");
                                vars["repname"] = s;
                                FoundRef = true;
                            }
                        }
                        if(FoundRef)
                        {
                            if(ends_with(EnumValueName(descriptor_->value(i)), "_PUSH"))
                            {
                                printer->Print(vars, "{$name$, U$repname$Push::StaticClass() }");    
                            }else
                            {
                                printer->Print(vars, "{$name$, U$repname$Resp::StaticClass() }");
                            }
                            
                            printer->Print(",");
                            printer->Print(vars, " //$nameoption$");    
                        }
                    }
                    printer->Print("};\n");
                    
                    printer->Print("};\n");
                }

                void EnumGenerator::
                GenerateGetEnumDescriptorSpecializations(io::Printer* printer)
                {
                    printer->Print(
                        "template <> struct is_proto_enum< $classname$> : ::google::protobuf::internal::true_type "
                        "{};\n",
                        "classname", ClassName(descriptor_, true));
                    if (HasDescriptorMethods(descriptor_->file(), options_))
                    {
                        printer->Print(
                            "template <>\n"
                            "inline const EnumDescriptor* GetEnumDescriptor< $classname$>() {\n"
                            "  return $classname$_descriptor();\n"
                            "}\n",
                            "classname", ClassName(descriptor_, true));
                    }
                }

                void EnumGenerator::GenerateSymbolImports(io::Printer* printer)
                {
                    std::map<string, string> vars;
                    vars["nested_name"] = descriptor_->name();
                    vars["classname"] = classname_;
                    vars["constexpr"] = options_.proto_h ? "constexpr " : "";
                    printer->Print(vars, "typedef $classname$ $nested_name$;\n");

                    for (int j = 0; j < descriptor_->value_count(); j++)
                    {
                        vars["tag"] = EnumValueName(descriptor_->value(j));
                        vars["deprecated_attr"] = descriptor_->value(j)->options().deprecated() ? "GOOGLE_PROTOBUF_DEPRECATED_ATTR " : "";
                        printer->Print(vars,
                                       "$deprecated_attr$static $constexpr$const $nested_name$ $tag$ =\n"
                                       "  $classname$_$tag$;\n");
                    }

                    printer->Print(vars,
                                   "static inline bool $nested_name$_IsValid(int value) {\n"
                                   "  return $classname$_IsValid(value);\n"
                                   "}\n"
                                   "static const $nested_name$ $nested_name$_MIN =\n"
                                   "  $classname$_$nested_name$_MIN;\n"
                                   "static const $nested_name$ $nested_name$_MAX =\n"
                                   "  $classname$_$nested_name$_MAX;\n");
                    if (generate_array_size_)
                    {
                        printer->Print(vars,
                                       "static const int $nested_name$_ARRAYSIZE =\n"
                                       "  $classname$_$nested_name$_ARRAYSIZE;\n");
                    }

                    if (HasDescriptorMethods(descriptor_->file(), options_))
                    {
                        printer->Print(vars,
                                       "static inline const ::google::protobuf::EnumDescriptor*\n"
                                       "$nested_name$_descriptor() {\n"
                                       "  return $classname$_descriptor();\n"
                                       "}\n");
                        printer->Print(vars,
                                       "static inline const ::std::string& "
                                       "$nested_name$_Name($nested_name$ value) {"
                                       "\n"
                                       "  return $classname$_Name(value);\n"
                                       "}\n");
                        printer->Print(vars,
                                       "static inline bool $nested_name$_Parse(const ::std::string& name,\n"
                                       "    $nested_name$* value) {\n"
                                       "  return $classname$_Parse(name, value);\n"
                                       "}\n");
                    }
                }

                void EnumGenerator::GenerateDescriptorInitializer(io::Printer* printer)
                {
                    std::map<string, string> vars;
                    vars["index"] = SimpleItoa(descriptor_->index());
                    vars["index_in_metadata"] = SimpleItoa(index_in_metadata_);

                    if (descriptor_->containing_type() == NULL)
                    {
                        printer->Print(vars,
                                       "file_level_enum_descriptors[$index_in_metadata$] = "
                                       "file->enum_type($index$);\n");
                    }
                    else
                    {
                        vars["parent"] = ClassName(descriptor_->containing_type(), false);
                        printer->Print(vars,
                                       "file_level_enum_descriptors[$index_in_metadata$] = "
                                       "$parent$_descriptor->enum_type($index$);\n");
                    }
                }

                void EnumGenerator::GenerateMethods(io::Printer* printer)
                {
                    std::map<string, string> vars;
                    vars["classname"] = classname_;
                    vars["index_in_metadata"] = SimpleItoa(index_in_metadata_);
                    vars["constexpr"] = options_.proto_h ? "constexpr " : "";
                    vars["file_namespace"] = FileLevelNamespace(descriptor_->file()->name());

                    if (HasDescriptorMethods(descriptor_->file(), options_))
                    {
                        printer->Print(
                            vars,
                            "const ::google::protobuf::EnumDescriptor* $classname$_descriptor() {\n"
                            "  $file_namespace$::protobuf_AssignDescriptorsOnce();\n"
                            "  return "
                            "$file_namespace$::file_level_enum_descriptors[$index_in_metadata$];\n"
                            "}\n");
                    }

                    printer->Print(vars,
                                   "bool $classname$_IsValid(int value) {\n"
                                   "  switch (value) {\n");

                    // Multiple values may have the same number.  Make sure we only cover
                    // each number once by first constructing a set containing all valid
                    // numbers, then printing a case statement for each element.

                    std::set<int> numbers;
                    for (int j = 0; j < descriptor_->value_count(); j++)
                    {
                        const EnumValueDescriptor* value = descriptor_->value(j);
                        numbers.insert(value->number());
                    }

                    for (std::set<int>::iterator iter = numbers.begin();
                         iter != numbers.end(); ++iter)
                    {
                        printer->Print(
                            "    case $number$:\n",
                            "number", Int32ToString(*iter));
                    }

                    printer->Print(vars,
                                   "      return true;\n"
                                   "    default:\n"
                                   "      return false;\n"
                                   "  }\n"
                                   "}\n"
                                   "\n");

                    if (descriptor_->containing_type() != NULL)
                    {
                        // We need to "define" the static constants which were declared in the
                        // header, to give the linker a place to put them.  Or at least the C++
                        // standard says we have to.  MSVC actually insists that we do _not_ define
                        // them again in the .cc file, prior to VC++ 2015.
                        printer->Print("#if !defined(_MSC_VER) || _MSC_VER >= 1900\n");

                        vars["parent"] = ClassName(descriptor_->containing_type(), false);
                        vars["nested_name"] = descriptor_->name();
                        for (int i = 0; i < descriptor_->value_count(); i++)
                        {
                            vars["value"] = EnumValueName(descriptor_->value(i));
                            printer->Print(vars,
                                           "$constexpr$const $classname$ $parent$::$value$;\n");
                        }
                        printer->Print(vars,
                                       "const $classname$ $parent$::$nested_name$_MIN;\n"
                                       "const $classname$ $parent$::$nested_name$_MAX;\n");
                        if (generate_array_size_)
                        {
                            printer->Print(vars,
                                           "const int $parent$::$nested_name$_ARRAYSIZE;\n");
                        }

                        printer->Print("#endif  // !defined(_MSC_VER) || _MSC_VER >= 1900\n");
                    }
                }
            } // namespace cpp
        } // namespace compiler
    } // namespace protobuf
} // namespace google
