#include "cpp_generator.h"

#include <vector>
#include <memory>
#ifndef _SHARED_PTR_H
#include <google/protobuf/stubs/shared_ptr.h>
#endif
#include <utility>

#include "cpp_file.h"
#include "cpp_helpers.h"
#include <google/protobuf/io/printer.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/descriptor.pb.h>

namespace google {
	namespace protobuf {
		namespace compiler {
			namespace cpp {

				UECppGenerator::UECppGenerator() {}
				UECppGenerator::~UECppGenerator() {}

				bool UECppGenerator::Generate(const FileDescriptor* file,
					const string& parameter,
					GeneratorContext* generator_context,
					string* error) const {
					std::vector<std::pair<string, string> > options;
					ParseGeneratorParameter(parameter, &options);

					Options file_options;

					for (int i = 0; i < options.size(); i++) {
						if (options[i].first == "dllexport_decl") {
							file_options.dllexport_decl = options[i].second;
						}
						else if (options[i].first == "safe_boundary_check") {
							file_options.safe_boundary_check = true;
						}
						else if (options[i].first == "annotate_headers") {
							file_options.annotate_headers = true;
						}
						else if (options[i].first == "annotation_pragma_name") {
							file_options.annotation_pragma_name = options[i].second;
						}
						else if (options[i].first == "annotation_guard_name") {
							file_options.annotation_guard_name = options[i].second;
						}
						else if (options[i].first == "lite") {
							file_options.enforce_lite = true;
						}
						else if (options[i].first == "table_driven_parsing") {
							file_options.table_driven_parsing = true;
						}
						else if (options[i].first == "table_driven_serialization") {
							file_options.table_driven_serialization = true;
						}
						else {
							*error = "Unknown generator option: " + options[i].first;
							return false;
						}
					}

					// -----------------------------------------------------------------
					string uebasename = MyStripProto(file->name());

					UEFileGenerator uefile_generator(file, file_options);

					uebasename.append("_UE");
					{
						google::protobuf::scoped_ptr<io::ZeroCopyOutputStream> output(
							generator_context->Open(uebasename + ".h"));
						GeneratedCodeInfo annotations;
						io::AnnotationProtoCollector<GeneratedCodeInfo> annotation_collector(
							&annotations);
						string info_path = uebasename + ".h.meta";
						io::Printer printer(output.get(), '$', file_options.annotate_headers
							? &annotation_collector
							: NULL);
						uefile_generator.GenerateHeader(
							&printer, file_options.annotate_headers ? info_path : "");
						if (file_options.annotate_headers) {
							google::protobuf::scoped_ptr<io::ZeroCopyOutputStream> info_output(generator_context->Open(info_path));
							annotations.SerializeToZeroCopyStream(info_output.get());
						}
					}

					// Generate cc file.
					{
						google::protobuf::scoped_ptr<io::ZeroCopyOutputStream> output(
							generator_context->Open(uebasename + ".cpp"));
						io::Printer printer(output.get(), '$');
						uefile_generator.GenerateSource(&printer);
					}
					return true;
				}

			}  // namespace cpp
		}  // namespace compiler
	}  // namespace protobuf
}  // namespace google
