# Converts a SPIR-V binary into a C++ header with a word array.
# Required cache variables: INPUT (binary), OUTPUT (header), VAR (array name).
# Optional cache variable: NAMESPACE (defaults to veyra::vulkan_detail).

file(READ "${INPUT}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])" "0x\\4\\3\\2\\1,\n" words "${hex}")
if(NOT DEFINED NAMESPACE)
 set(NAMESPACE "veyra::vulkan_detail")
endif()
file(WRITE "${OUTPUT}" "#pragma once\n")
file(APPEND "${OUTPUT}" "#include <cstddef>\n")
file(APPEND "${OUTPUT}" "#include <cstdint>\n")
file(APPEND "${OUTPUT}" "namespace ${NAMESPACE} {\n")
file(APPEND "${OUTPUT}" "inline constexpr std::uint32_t ${VAR}[] = {\n${words}};\n")
file(APPEND "${OUTPUT}" "inline constexpr std::size_t ${VAR}Words =\n")
file(APPEND "${OUTPUT}" "    sizeof(${VAR}) / sizeof(std::uint32_t);\n")
file(APPEND "${OUTPUT}" "}\n")
