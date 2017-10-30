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
	options_(options),
	field_generators_(descriptor, options),
	max_has_bit_index_(0),
	nested_generators_(new google::protobuf::scoped_ptr<UEMessageGenerator>[descriptor->nested_type_count()]),
	enum_generators_(new google::protobuf::scoped_ptr<EnumGenerator>[descriptor->enum_type_count()]),
	extension_generators_(new google::protobuf::scoped_ptr<ExtensionGenerator>[descriptor->extension_count()]),
	use_dependent_base_(false),
	num_weak_fields_(0),
	scc_analyzer_(scc_analyzer) {

	for (int i = 0; i < descriptor_->field_count(); i++) {
		const FieldDescriptor* field = descriptor_->field(i);
		if (field->options().weak()) {
			num_weak_fields_++;
		}
		else if (!field->containing_oneof()) {
			optimized_order_.push_back(field);
		}
	}

	if (HasFieldPresence(descriptor_->file())) {
		// We use -1 as a sentinel.
		has_bit_indices_.resize(descriptor_->field_count(), -1);
		for (int i = 0; i < optimized_order_.size(); i++) {
			const FieldDescriptor* field = optimized_order_[i];
			// Skip fields that do not have has bits.
			if (field->is_repeated()) {
				continue;
			}

			has_bit_indices_[field->index()] = max_has_bit_index_++;
		}
	}

	for (int i = 0; i < descriptor->nested_type_count(); i++) {
		nested_generators_[i].reset(new UEMessageGenerator(descriptor->nested_type(i),
			options, scc_analyzer));
	}

	for (int i = 0; i < descriptor->enum_type_count(); i++) {
		enum_generators_[i].reset(
			new EnumGenerator(descriptor->enum_type(i), options));
	}

	for (int i = 0; i < descriptor->extension_count(); i++) {
		extension_generators_[i].reset(
			new ExtensionGenerator(descriptor->extension(i), options));
	}

	num_required_fields_ = 0;
	for (int i = 0; i < descriptor->field_count(); i++) {
		if (descriptor->field(i)->is_required()) {
			++num_required_fields_;
		}
		if (options.proto_h && IsFieldDependent(descriptor->field(i))) {
			use_dependent_base_ = true;
		}
	}
	if (options.proto_h && descriptor->oneof_decl_count() > 0) {
		// Always make oneofs dependent.
		use_dependent_base_ = true;
	}

}

UEMessageGenerator::~UEMessageGenerator() {}

void UEMessageGenerator::GenerateClassDefinition(io::Printer* printer) {
	if (IsMapEntryMessage(descriptor_)) return;

	std::map<string, string> vars;
	vars["classname"] = classname_;
	vars["full_name"] = descriptor_->full_name();
	vars["field_count"] = SimpleItoa(descriptor_->field_count());
	vars["oneof_decl_count"] = SimpleItoa(descriptor_->oneof_decl_count());
	vars["dllexport"] = "CLOUD_API";

	if (ends_with(classname_, "Data"))
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
	}
	else
	{
		if (ends_with(classname_, "Request"))
		{
			vars["superclass"] = "UDolphinRequest";
			vars["append"] = "void Pack() override;";
		}
		else if (ends_with(classname_, "Response"))
		{
			vars["superclass"] = "UDolphinResponse";
			vars["append"] = "void Unpack(OriginalMessage *message) override;";
		}
		else
		{
			return;
		}

		printer->Print(vars,
			"UCLASS(Blueprintable)\n"
			"class $dllexport$ U$classname$ : public $superclass$ "
			"{\n"
			"GENERATED_BODY()\n");

		printer->Print(" public:\n");

		printer->Indent();

		printer->Print(vars, "$append$\n");
	}
	// Generate private members.
	printer->Outdent();
	printer->Indent();

	printer->Print("\n");

	// Emit some private and static members
	for (int i = 0; i < optimized_order_.size(); ++i) {
		const FieldDescriptor* field = optimized_order_[i];
		const FieldGenerator& generator = field_generators_.get(field);
		generator.GenerateStaticMembers(printer);
		generator.GeneratePrivateMembers(printer);
	}

	printer->Outdent();
	printer->Print("};");
}

void UEMessageGenerator::ToPBMessage_Normal(io::Printer* printer, const FieldDescriptor *field)
{
	switch (field->cpp_type())
	{
	case FieldDescriptor::CPPTYPE_MESSAGE:
		printer->Print(
			"$field_type$ element;\n"
			"$field_name$.ToPB(element);\n"
			"pbMessage.set_allocated_$lowercase_name$(&element);\n"
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

void UEMessageGenerator::ToPBMessage_Repeated(io::Printer* printer, const FieldDescriptor *field)
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
	case FieldDescriptor::CPPTYPE_BOOL:
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

void UEMessageGenerator::ToPBMessage_Map(io::Printer* printer, const FieldDescriptor *field)
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

void UEMessageGenerator::ToPBMessage_MapPair(io::Printer* printer, const FieldDescriptor *field, const std::string part)
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
	for (int i = 0; i < descriptor_->field_count(); i++) {
		const FieldDescriptor *field = descriptor_->field(i);
		if (field->is_map())
		{
			ToPBMessage_Map(printer, field);
		}
		else if (field->is_repeated()) {
			ToPBMessage_Repeated(printer, field);
		}
		else {
			ToPBMessage_Normal(printer, field);
		}
	}
}

void UEMessageGenerator::FromPBMessage_Normal(io::Printer* printer, const FieldDescriptor *field)
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
			"$field_name$ = $field_type$(pbMessage.$lowercase_name$().c_str());\n"
			, "field_name", FieldName(field)
			, "lowercase_name", field->lowercase_name()
			, "field_type", PrimitiveTypeName(field->cpp_type()));
		break;
	case FieldDescriptor::CPPTYPE_INT32:
	case FieldDescriptor::CPPTYPE_BOOL:
		printer->Print(
			"$field_name$ = pbMessage.$lowercase_name$();\n"
			, "field_name", FieldName(field)
			, "lowercase_name", field->lowercase_name());
		break;
	default:
		break;
	}
}

void UEMessageGenerator::FromPBMessage_Repeated(io::Printer* printer, const FieldDescriptor *field)
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
			"$field_type$ _$field_type$ = $field_type$(element.c_str());\n"
			"$field_name$.Add(_$field_type$);\n"
			, "field_name", FieldName(field)
			, "lowercase_name", field->lowercase_name()
			, "field_type", PrimitiveTypeName(field->cpp_type()));
		break;
	case FieldDescriptor::CPPTYPE_INT32:
	case FieldDescriptor::CPPTYPE_BOOL:
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

void UEMessageGenerator::FromPBMessage_Map(io::Printer * printer, const FieldDescriptor * field)
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

void UEMessageGenerator::FromPBMessage_MapPair(io::Printer* printer, const FieldDescriptor *field, const std::string part)
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
	for (int i = 0; i < descriptor_->field_count(); i++) {
		const FieldDescriptor *field = descriptor_->field(i);
		if (field->is_map())
		{
			FromPBMessage_Map(printer, field);
		}
		else if (field->is_repeated()) {
			FromPBMessage_Repeated(printer, field);
		}
		else {
			FromPBMessage_Normal(printer, field);
		}
	}
}

void UEMessageGenerator::Flatten(std::vector<UEMessageGenerator*>* list) {
	for (int i = 0; i < descriptor_->nested_type_count(); i++) {
		nested_generators_[i]->Flatten(list);
	}
	index_in_file_messages_ = list->size();
	list->push_back(this);
}

void UEMessageGenerator::GenerateClassMethods(io::Printer* printer) {

	if (ends_with(classname_, "Data"))
	{
		printer->Print(
			"void F$classname$::FromPB(const $classname$& pbMessage) {\n",
			"classname", classname_);

		FromPBMessage(printer);

		printer->Print("}\n\n");

		printer->Print(
			"void F$classname$::ToPB($classname$& pbMessage) const {\n",
			"classname", classname_);

		ToPBMessage(printer);

		printer->Print("}\n\n");
	}
	else if (ends_with(classname_, "Request"))
	{
		//TODO 动态加载 模块和方法
		string className = classname_;
		className.replace(className.end() - 7, className.end(), "");

		string module;
		string protocol;

		const vector<string> words = split(className, "_");

		if (words.size() < 2)
		{
			return;
		}

		module = words.at(0);
		protocol = words.at(1);

		printer->Print(
			"void U$classname$::Pack() {\n"
			"mProtocolModule = (uint8)EProtocolModule::$module$;\n"
			"mSpecificProtocol = (uint8)E$module$Protocol::$protocol$;\n"
			"Dolphin::Protocol::$classname$ pbMessage;\n",
			"classname", classname_,
			"module", module,
			"protocol", protocol
		);

		ToPBMessage(printer);

		printer->Print(
			"mMessage = &pbMessage;\n"
			"UDolphinRequest::Pack();\n"
			"}\n"
			"\n",
			"classname", classname_);
	}
	else if (ends_with(classname_, "Response"))
	{
		printer->Print(
			"void U$classname$::Unpack(OriginalMessage *message) {\n"
			"protocolNo = (uint8)message->ProtocolModule << 8 | message->SpecificProtocol;\n"
			"Dolphin::Protocol::AllResponse response;\n"
			"response.ParseFromArray(message->Buffer, message->BufferSize);\n"
			"responseData.state = response.state();\n"
			"responseData.msg = FString(response.msg().c_str());\n"
			"Dolphin::Protocol::$classname$ pbMessage;\n"
			"size_t len = response.result().length();\n"
			"pbMessage.ParseFromArray(response.result().c_str(), len);\n\n",
			"classname", classname_);

		FromPBMessage(printer);

		printer->Print(
			"}\n"
			"\n",
			"classname", classname_);
	}
}