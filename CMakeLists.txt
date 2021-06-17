cmake_minimum_required (VERSION 2.6)
project (protoc-gen-ue4)

set(CMAKE_CXX_FLAGS "-DPROTOBUF_USE_DLLS")

file(GLOB helloworld_SRC
    "*.h"
    "*.cc"
)
add_executable(protoc-gen-ue4 main.cpp ${helloworld_SRC})

include(FindProtobuf)
find_package(Protobuf REQUIRED)

include_directories(${PROTOBUF_INCLUDE_DIR})
target_link_libraries(protoc-gen-ue4
    ${PROTOBUF_LIBRARY}
    ${PROTOBUF_PROTOC_LIBRARY}
)
