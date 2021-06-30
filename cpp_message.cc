#include "cpp_message.h"

#include <algorithm>
#include <google/protobuf/stubs/hash.h>
#include <map>
#include <memory>
#ifndef _SHARED_PTR_H
#include <google/protobuf/stubs/shared_ptr.h>
#endif
#include <utility>
#include <vector>

#include "cpp_enum.h"
#include "cpp_extension.h"
#include "cpp_field.h"
#include "cpp_helpers.h"
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/printer.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/generated_message_table_driven.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/map_entry_lite.h>
#include <google/protobuf/wire_format.h>
#include "strutil.h"
#include "substitute.h"

// ===================================================================
UEMessageGenerator::UEMessageGenerator(const Descriptor* descriptor, const Options& options, SCCAnalyzer* scc_analyzer) :
    descriptor_(descriptor),
    classname_(ClassName(descriptor, false)),
    packagename_(descriptor->file()->package()),
    options_(options),
    field_generators_(descriptor, options),
    max_has_bit_index_(0),
    nested_generators_(new google::protobuf::scoped_ptr<UEMessageGenerator>[descriptor->nested_type_count()]),
    enum_generators_(new google::protobuf::scoped_ptr<EnumGenerator>[descriptor->enum_type_count()]),
    extension_generators_(new google::protobuf::scoped_ptr<ExtensionGenerator>[descriptor->extension_count()]),
    use_dependent_base_(false),
    num_weak_fields_(0),
    scc_analyzer_(scc_analyzer)
{
    for (int i = 0; i < descriptor_->field_count(); i++)
    {
        const FieldDescriptor* field = descriptor_->field(i);
        if (field->options().weak())
        {
            num_weak_fields_++;
        }
        else if (!field->containing_oneof())
        {
            optimized_order_.push_back(field);
        }
    }

    if (HasFieldPresence(descriptor_->file()))
    {
        // We use -1 as a sentinel.
        has_bit_indices_.resize(descriptor_->field_count(), -1);
        for (int i = 0; i < optimized_order_.size(); i++)
        {
            const FieldDescriptor* field = optimized_order_[i];
            // Skip fields that do not have has bits.
            if (field->is_repeated())
            {
                continue;
            }

            has_bit_indices_[field->index()] = max_has_bit_index_++;
        }
    }

    for (int i = 0; i < descriptor->nested_type_count(); i++)
    {
        nested_generators_[i].reset(new UEMessageGenerator(descriptor->nested_type(i),
                                                           options, scc_analyzer));
    }

    for (int i = 0; i < descriptor->enum_type_count(); i++)
    {
        enum_generators_[i].reset(
            new EnumGenerator(descriptor->enum_type(i), options));
    }

    for (int i = 0; i < descriptor->extension_count(); i++)
    {
        extension_generators_[i].reset(
            new ExtensionGenerator(descriptor->extension(i), options));
    }

    num_required_fields_ = 0;
    for (int i = 0; i < descriptor->field_count(); i++)
    {
        if (descriptor->field(i)->is_required())
        {
            ++num_required_fields_;
        }
        if (options.proto_h && IsFieldDependent(descriptor->field(i)))
        {
            use_dependent_base_ = true;
        }
    }
    if (options.proto_h && descriptor->oneof_decl_count() > 0)
    {
        // Always make oneofs dependent.
        use_dependent_base_ = true;
    }
}

UEMessageGenerator::~UEMessageGenerator()
{
}

void UEMessageGenerator::GenerateClassDefinition(io::Printer* printer)
{
    if (IsMapEntryMessage(descriptor_)) return;

    std::map<string, string> vars;
    vars["classname"] = classname_;
    vars["full_name"] = descriptor_->full_name();
    vars["field_count"] = SimpleItoa(descriptor_->field_count());
    vars["oneof_decl_count"] = SimpleItoa(descriptor_->oneof_decl_count());
    vars["dllexport"] = "ZEN_API";


    {
        if (ends_with(classname_, "Req"))
        {
            vars["superclass"] = "URequest";
            vars["append"] = "virtual void Pack() override;\nvirtual CMD GetCmd() override;";
            printer->Print(vars,
                           "UCLASS(Blueprintable)\n"
                           "class U$classname$ : public $superclass$ "
                           "{\n"
                           "GENERATED_BODY()\n");

            printer->Print(" public:\n");

            printer->Indent();

            printer->Print(vars, "$append$\n");
            // Generate private members.
            printer->Outdent();
            printer->Indent();

            printer->Print("\n");

            // Emit some private and static members
            for (int i = 0; i < optimized_order_.size(); ++i)
            {
                const FieldDescriptor* field = optimized_order_[i];
                const FieldGenerator& generator = field_generators_.get(field);
                generator.GenerateStaticMembers(printer);
                generator.GeneratePrivateMembers(printer);
            }

            printer->Outdent();
            printer->Print("};\n");
        }
        else if (ends_with(classname_, "Resp") || ends_with(classname_, "Push"))
        {
            vars["superclass"] = "UResponse";
            vars["classname"] = classname_;
            vars["append"] = "void Unpack(const std::string& data) override;\n"
                "virtual void Generic_GetDataStruct(void* OutData);\n";
            printer->Print(vars,
                           "USTRUCT(BlueprintType)\n"
                           "struct F$classname$Struct : public FResponseDataBase "
                           "{\n"
                           "GENERATED_USTRUCT_BODY()\n");
            printer->Print("\n");
            printer->Indent();
            printer->Print(vars, "void UnPack($classname$& pbMessage);\n");

            printer->Print("\n");
            // Emit some private and static members
            for (int i = 0; i < optimized_order_.size(); ++i)
            {
                const FieldDescriptor* field = optimized_order_[i];
                const FieldGenerator& generator = field_generators_.get(field);
                generator.GenerateStaticMembers(printer);
                generator.GeneratePrivateMembers(printer);
            }
            printer->Outdent();
            printer->Print("};");
            printer->Print("\n");
            printer->Print(vars,
                           "UCLASS(Blueprintable)\n"
                           "class U$classname$ : public $superclass$ "
                           "{\n"
                           "GENERATED_BODY()\n");

            printer->Print("public:\n");

            printer->Indent();
            printer->Print(vars, "$append$\n");
            printer->Outdent();
            printer->Print("protected:\n");
            printer->Indent();
            printer->Print(vars, "F$classname$Struct Data;\n");
            // Generate private members.
            printer->Outdent();
            // printer->Print("\n");
            //
            // // Emit some private and static members
            // for (int i = 0; i < optimized_order_.size(); ++i) {
            // 	const FieldDescriptor* field = optimized_order_[i];
            // 	const FieldGenerator& generator = field_generators_.get(field);
            // 	generator.GenerateStaticMembers(printer);
            // 	generator.GeneratePrivateMembers(printer);
            // }
            printer->Print("};");
        }

        else
        {
            printer->Print(vars,
                           "USTRUCT(BlueprintType)\n"
                           "struct F$classname$"
                           "{\n"
                           "GENERATED_USTRUCT_BODY()\n");

            printer->Print(" public:\n");
            printer->Indent();

            printer->Print(vars, "void FromPB(const $classname$& pbMessage);\n");
            printer->Print(vars, "void ToPB($classname$& pbMessage) const;\n");
            printer->Print("\n");
            // Emit some private and static members
            for (int i = 0; i < optimized_order_.size(); ++i)
            {
                const FieldDescriptor* field = optimized_order_[i];
                const FieldGenerator& generator = field_generators_.get(field);
                generator.GenerateStaticMembers(printer);
                generator.GeneratePrivateMembers(printer);
            }

            printer->Outdent();
            printer->Print("};\n");
        }
    }
}

void UEMessageGenerator::ToPBMessage_Normal(io::Printer* printer, const FieldDescriptor* field)
{
    switch (field->cpp_type())
    {
    case FieldDescriptor::CPPTYPE_MESSAGE:
        printer->Print(
            "$field_type$* element = new $field_type$;\n"
            "$field_name$.ToPB(*element);\n"
            "pbMessage.set_allocated_$lowercase_name$(element);\n"
            , "field_name", FieldName(field)
            , "field_type", ClassName(field->message_type(), false)
            , "lowercase_name", field->lowercase_name());
        break;
    case FieldDescriptor::CPPTYPE_STRING:
        printer->Print(
            "pbMessage.set_$lowercase_name$(TCHAR_TO_UTF8(*$field_name$));\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name());
        break;
    case FieldDescriptor::CPPTYPE_INT32:
    case FieldDescriptor::CPPTYPE_INT64:
    case FieldDescriptor::CPPTYPE_UINT64:
    case FieldDescriptor::CPPTYPE_UINT32:
    case FieldDescriptor::CPPTYPE_FLOAT:
    case FieldDescriptor::CPPTYPE_ENUM:
    case FieldDescriptor::CPPTYPE_DOUBLE:
    case FieldDescriptor::CPPTYPE_BOOL:
        printer->Print(
            "pbMessage.set_$lowercase_name$($field_name$);\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name());
        break;
    default:
        break;
    }
}

void UEMessageGenerator::ToPBMessage_Repeated(io::Printer* printer, const FieldDescriptor* field)
{
    switch (field->cpp_type())
    {
    case FieldDescriptor::CPPTYPE_MESSAGE:
        printer->Print(
            "for (auto element : $field_name$) {\n"
            "$field_type$ *_$field_type$ = pbMessage.add_$lowercase_name$();\n"
            "element.ToPB(*_$field_type$);\n"
            "}\n"
            , "field_name", FieldName(field)
            , "field_type", ClassName(field->message_type(), false)
            , "lowercase_name", field->lowercase_name());
        break;
    case FieldDescriptor::CPPTYPE_STRING:
        printer->Print(
            "for (auto element : $field_name$) {\n"
            "pbMessage.add_$lowercase_name$(TCHAR_TO_UTF8(*element));\n"
            "}\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name());
        break;
    case FieldDescriptor::CPPTYPE_INT32:
    case FieldDescriptor::CPPTYPE_INT64:
    case FieldDescriptor::CPPTYPE_UINT64:
    case FieldDescriptor::CPPTYPE_UINT32:
    case FieldDescriptor::CPPTYPE_FLOAT:
    case FieldDescriptor::CPPTYPE_DOUBLE:
    case FieldDescriptor::CPPTYPE_BOOL:
    case FieldDescriptor::CPPTYPE_ENUM:
        printer->Print(
            "for (auto element : $field_name$) {\n"
            "pbMessage.add_$lowercase_name$(element);\n"
            "}\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name());
        break;
    default:
        break;
    }
}

void UEMessageGenerator::ToPBMessage_Map(io::Printer* printer, const FieldDescriptor* field)
{
    const FieldDescriptor* keyDescriptor =
        field->message_type()->FindFieldByName("key");
    const FieldDescriptor* valDescriptor =
        field->message_type()->FindFieldByName("value");

    printer->Print(
        "for (auto& element : $field_name$) {\n"
        , "field_name", FieldName(field)
    );

    ToPBMessage_MapPair(printer, keyDescriptor, "Key");
    ToPBMessage_MapPair(printer, valDescriptor, "Value");

    printer->Print(
        "(*pbMessage.mutable_$field_name$())[key] = value;\n"
        "}\n\n", "field_name", FieldName(field)
    );
}

void UEMessageGenerator::ToPBMessage_MapPair(io::Printer* printer, const FieldDescriptor* field, const std::string part)
{
    switch (field->cpp_type())
    {
    case FieldDescriptor::CPPTYPE_MESSAGE:
        printer->Print(
            "$field_type$ $field_name$;\n"
            "element.$field_part$.ToPB($field_name$);\n"
            , "field_name", FieldName(field)
            , "field_part", part
            , "field_type", ClassName(field->message_type(), false));
        break;
    case FieldDescriptor::CPPTYPE_STRING:
        printer->Print(
            "std::string $field_name$ = TCHAR_TO_UTF8(*element.$field_part$);\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_INT32:
        printer->Print(
            "int32 $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_BOOL:
        printer->Print(
            "bool $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    default:
        break;
    }
}

void UEMessageGenerator::ToPBMessage(io::Printer* printer)
{
    for (int i = 0; i < descriptor_->field_count(); i++)
    {
        const FieldDescriptor* field = descriptor_->field(i);
        if (field->is_map())
        {
            ToPBMessage_Map(printer, field);
        }
        else if (field->is_repeated())
        {
            ToPBMessage_Repeated(printer, field);
        }
        else
        {
            ToPBMessage_Normal(printer, field);
        }
    }
}

void UEMessageGenerator::FromPBMessage_Normal(io::Printer* printer, const FieldDescriptor* field)
{
    switch (field->cpp_type())
    {
    case FieldDescriptor::CPPTYPE_MESSAGE:
        printer->Print(
            "if (pbMessage.has_$lowercase_name$()) {\n"
            "Dolphin::Protocol::$field_type$ data = pbMessage.$lowercase_name$();\n"
            "	$field_name$.FromPB(data);\n"
            "}\n"
            , "field_name", FieldName(field)
            , "field_type", ClassName(field->message_type(), false)
            , "lowercase_name", field->lowercase_name());

        break;

    case FieldDescriptor::CPPTYPE_STRING:
        printer->Print(
            "$field_name$ = UTF8_TO_TCHAR(pbMessage.$lowercase_name$().c_str());\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name());
        break;
    case FieldDescriptor::CPPTYPE_INT32:
    case FieldDescriptor::CPPTYPE_INT64:
    case FieldDescriptor::CPPTYPE_UINT64:
    case FieldDescriptor::CPPTYPE_UINT32:
    case FieldDescriptor::CPPTYPE_FLOAT:
    case FieldDescriptor::CPPTYPE_DOUBLE:
    case FieldDescriptor::CPPTYPE_BOOL:
        printer->Print(
            "$field_name$ = pbMessage.$lowercase_name$();\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name());
        break;
    case FieldDescriptor::CPPTYPE_ENUM:
        printer->Print(
            "$field_name$ = static_cast<E$EnumName$>(pbMessage.$lowercase_name$());\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name()
            , "EnumName", ClassName(field->enum_type(), false));
    default:
        break;
    }
}

void UEMessageGenerator::FromPBMessage_Repeated(io::Printer* printer, const FieldDescriptor* field)
{
    printer->Print(
        "for (auto element : pbMessage.$lowercase_name$()) {\n"
        , "lowercase_name", field->lowercase_name());


    switch (field->cpp_type())
    {
    case FieldDescriptor::CPPTYPE_MESSAGE:
        printer->Print(
            "F$field_type$ _$field_type$;\n"
            "_$field_type$.FromPB(element);\n"
            "$field_name$.Add(_$field_type$);\n"
            , "field_name", FieldName(field)
            , "field_type", ClassName(field->message_type(), false)
            , "lowercase_name", field->lowercase_name());
        break;
    case FieldDescriptor::CPPTYPE_STRING:
        printer->Print(
            "$field_type$ _$field_type$ = UTF8_TO_TCHAR(element.c_str());\n"
            "$field_name$.Add(_$field_type$);\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name()
            , "field_type", PrimitiveTypeName(field->cpp_type()));
        break;
    case FieldDescriptor::CPPTYPE_INT32:
    case FieldDescriptor::CPPTYPE_INT64:
    case FieldDescriptor::CPPTYPE_UINT64:
    case FieldDescriptor::CPPTYPE_UINT32:
    case FieldDescriptor::CPPTYPE_FLOAT:
    case FieldDescriptor::CPPTYPE_DOUBLE:
    case FieldDescriptor::CPPTYPE_BOOL:
    case FieldDescriptor::CPPTYPE_ENUM:
        printer->Print(
            "$field_name$.Add(element);\n"
            , "field_name", FieldName(field)
            , "lowercase_name", field->lowercase_name());
        break;
    default:
        break;
    }

    printer->Print("}\n");
}

void UEMessageGenerator::FromPBMessage_Map(io::Printer* printer, const FieldDescriptor* field)
{
    const FieldDescriptor* keyDescriptor =
        field->message_type()->FindFieldByName("key");
    const FieldDescriptor* valDescriptor =
        field->message_type()->FindFieldByName("value");

    printer->Print(
        "for (auto& element : pbMessage.$lowercase_name$()) {\n"
        , "lowercase_name", field->lowercase_name());

    FromPBMessage_MapPair(printer, keyDescriptor, "first");
    FromPBMessage_MapPair(printer, valDescriptor, "second");

    printer->Print(
        "$field_name$.Add(key,value);\n"
        "}\n\n", "field_name", FieldName(field)
    );
}

void UEMessageGenerator::FromPBMessage_MapPair(io::Printer* printer, const FieldDescriptor* field, const std::string part)
{
    switch (field->cpp_type())
    {
    case FieldDescriptor::CPPTYPE_MESSAGE:
        printer->Print(
            "F$field_type$ $field_name$;\n"
            "$field_name$.FromPB(element.$field_part$);\n"
            , "field_name", FieldName(field)
            , "field_part", part
            , "field_type", ClassName(field->message_type(), false));
        break;
    case FieldDescriptor::CPPTYPE_STRING:
        printer->Print(
            "$field_type$ $field_name$(element.$field_part$.c_str());\n"
            , "field_name", FieldName(field),
            "field_part", part
            , "field_type", PrimitiveTypeName(field->cpp_type()));
        break;
    case FieldDescriptor::CPPTYPE_INT32:
        printer->Print(
            "int32 $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_INT64:
        printer->Print(
            "int64 $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_FLOAT:
        printer->Print(
            "float $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_DOUBLE:
        printer->Print(
            "double $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_UINT32:
        printer->Print(
            "uint32 $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_UINT64:
        printer->Print(
            "uint64 $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    case FieldDescriptor::CPPTYPE_BOOL:
        printer->Print(
            "bool $field_name$ = element.$field_part$;\n"
            , "field_name", FieldName(field),
            "field_part", part);
        break;
    default:
        break;
    }
}

void UEMessageGenerator::FromPBMessage(io::Printer* printer)
{
    for (int i = 0; i < descriptor_->field_count(); i++)
    {
        const FieldDescriptor* field = descriptor_->field(i);
        if (field->is_map())
        {
            FromPBMessage_Map(printer, field);
        }
        else if (field->is_repeated())
        {
            FromPBMessage_Repeated(printer, field);
        }
        else
        {
            FromPBMessage_Normal(printer, field);
        }
    }
}

void UEMessageGenerator::Flatten(std::vector<UEMessageGenerator*>* list)
{
    for (int i = 0; i < descriptor_->nested_type_count(); i++)
    {
        nested_generators_[i]->Flatten(list);
    }
    index_in_file_messages_ = list->size();
    list->push_back(this);
}

void UEMessageGenerator::GenerateClassMethods(io::Printer* printer)
{
    if (ends_with(classname_, "Req"))
    {
        //TODO 动态加载 模块和方法
        string className = classname_;
        className.replace(className.end() - 3, className.end(), "");


        string fileName = descriptor_->file()->name();
        fileName.replace(fileName.end() - 6, fileName.end(), "");
        fileName.replace(0, 4, "");

        if (starts_with(ToUpper(className), ToUpper(fileName)))
        {
            className.replace(0, fileName.length(), "");
        }
        printer->Print(
            "void U$classname$::Pack() {\n"
            "    $classname$ pbMessage;\n",
            "classname", classname_
        );
        printer->Indent();
        ToPBMessage(printer);
        printer->Outdent();
        printer->Print(
            "    mMessage = &pbMessage;\n"
            "    URequest::Pack();\n"
            "}\n"
            "\n",
            "classname", classname_);


        printer->Print(
            "CMD U$classname$::GetCmd()\n"
            "{\n"
            "    return NO_$uppercase_filename$_$uppercase_classname$;\n"
            "}\n",
            "classname", classname_,
            "uppercase_filename", ToUpper(fileName),
            "uppercase_classname", ToUpper(className)
        );
    }
    else if (ends_with(classname_, "Resp") || ends_with(classname_, "Push"))
    {
        printer->Print(
            "void F$classname$Struct::UnPack($classname$& pbMessage) {\n",
            "classname", classname_);
        printer->Indent();
        FromPBMessage(printer);
        printer->Outdent();
        printer->Print(
            "}\n"
            "\n",
            "classname", classname_);

        printer->Print(
            "void U$classname$::Unpack(const std::string& data) {\n", "classname", classname_);
        printer->Indent();
        printer->Print(
            "$classname$ pbMessage;\n"
            "pbMessage.ParseFromString(data);\n\n"
            "Data.UnPack(pbMessage);\n",
            "classname", classname_);
        printer->Outdent();
        printer->Print(
            "}\n"
            "\n",
            "classname", classname_);

        printer->Print(
            "void U$classname$::Generic_GetDataStruct(void* OutData) {\n"
            "const UScriptStruct* StructType = F$classname$Struct::StaticStruct();\n"
            "if (StructType != nullptr)\n"
            "{\n"
            "StructType->CopyScriptStruct(OutData, &Data);\n"
            "}\n",
            "classname", classname_);

        printer->Print(
            "}\n"
            "\n",
            "classname", classname_);
    }
    else
    {
        printer->Print(
            "void F$classname$::FromPB(const $classname$& pbMessage) {\n",
            "classname", classname_);
        printer->Indent();
        FromPBMessage(printer);
        printer->Outdent();
        printer->Print("}\n\n");

        printer->Print(
            "void F$classname$::ToPB($classname$& pbMessage) const {\n",
            "classname", classname_);

        ToPBMessage(printer);

        printer->Print("}\n\n");
    }
}
