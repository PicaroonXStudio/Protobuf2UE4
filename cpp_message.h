#ifndef GOOGLE_PROTOBUF_COMPILER_CPP_MESSAGE_H__
#define GOOGLE_PROTOBUF_COMPILER_CPP_MESSAGE_H__

#include <memory>
#ifndef _SHARED_PTR_H
#include <google/protobuf/stubs/shared_ptr.h>
#endif
#include <set>
#include <string>
#include "cpp_field.h"
#include "cpp_helpers.h"
#include "cpp_options.h"

namespace google {
	namespace protobuf {
		namespace io {
			class Printer;             // printer.h
		}
	}

	namespace protobuf {
		namespace compiler {
			namespace cpp {

				class EnumGenerator;           // enum.h
				class ExtensionGenerator;      // extension.h
			}
		}
	}
}

using namespace google;
using namespace google::protobuf;
using namespace google::protobuf::compiler;
using namespace google::protobuf::compiler::cpp;

class UEMessageGenerator {
public:
	// See generator.cc for the meaning of dllexport_decl.
	UEMessageGenerator(const Descriptor* descriptor, const Options& options,
		SCCAnalyzer* scc_analyzer);
	~UEMessageGenerator();

	// Generate definitions for this class and all its nested types.
	void GenerateClassDefinition(io::Printer* printer);

	// Generate all non-inline methods for this class.
	void GenerateClassMethods(io::Printer* printer);

	void ToPBMessage_Map(io::Printer* printer, const FieldDescriptor *field);
	void ToPBMessage_MapPair(io::Printer* printer, const FieldDescriptor *field, const std::string part);
	void ToPBMessage_Normal(io::Printer* printer, const FieldDescriptor *field);
	void ToPBMessage_Repeated(io::Printer* printer, const FieldDescriptor *field);
	void ToPBMessage(io::Printer* printer);

	void FromPBMessage_Map(io::Printer* printer, const FieldDescriptor *field);
	void FromPBMessage_MapPair(io::Printer* printer, const FieldDescriptor *field, const std::string part);
	void FromPBMessage_Normal(io::Printer* printer, const FieldDescriptor *field);
	void FromPBMessage_Repeated(io::Printer* printer, const FieldDescriptor *field);
	void FromPBMessage(io::Printer* printer);


	void Flatten(std::vector<UEMessageGenerator*>* list);
	const Descriptor* descriptor_;
	string classname_;
	Options options_;
	FieldGeneratorMap field_generators_;
	// optimized_order_ is the order we layout the message's fields in the class.
	// This is reused to initialize the fields in-order for cache efficiency.
	//
	// optimized_order_ excludes oneof fields and weak fields.
	std::vector<const FieldDescriptor *> optimized_order_;
	std::vector<int> has_bit_indices_;
	int max_has_bit_index_;
	google::protobuf::scoped_array<google::protobuf::scoped_ptr<UEMessageGenerator> > nested_generators_;
	google::protobuf::scoped_array<google::protobuf::scoped_ptr<EnumGenerator> > enum_generators_;
	google::protobuf::scoped_array<google::protobuf::scoped_ptr<ExtensionGenerator> > extension_generators_;
	int num_required_fields_;
	bool use_dependent_base_;
	int num_weak_fields_;
	// table_driven_ indicates the generated message uses table-driven parsing.
	bool table_driven_;

	int index_in_file_messages_;

	SCCAnalyzer* scc_analyzer_;

	friend class FileGenerator;
	GOOGLE_DISALLOW_EVIL_CONSTRUCTORS(UEMessageGenerator);
};

#endif 