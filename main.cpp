#include <google/protobuf/compiler/plugin.h>
#include "cpp_generator.h"

using namespace google::protobuf;
using namespace google::protobuf::compiler;
using namespace google::protobuf::compiler::cpp;

using namespace std;

int main(int argc, char* argv[]) {
	UECppGenerator generator;
	return google::protobuf::compiler::PluginMain(argc, argv, &generator);
}