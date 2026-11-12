# cmake -DINPUT=<file.spv> -DOUTPUT=<file.h> -DNAME=<array> -P GeniEmbedSpirv.cmake
#
# Writes a SPIR-V binary as `const uint32_t NAME[] = {...};`. SPIR-V words are stored
# little-endian, so each group of four bytes is reversed into one word.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" length)
math(EXPR remainder "${length} % 8")
if(length EQUAL 0 OR NOT remainder EQUAL 0)
  message(FATAL_ERROR "${INPUT} is not a SPIR-V binary (size is not a multiple of 4 bytes)")
endif()

string(REGEX MATCHALL "([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])" groups "${hex}")
set(words "")
set(column 0)
foreach(group IN LISTS groups)
  string(REGEX REPLACE "(..)(..)(..)(..)" "0x\\4\\3\\2\\1," word "${group}")
  string(APPEND words "${word}")
  math(EXPR column "${column} + 1")
  if(column EQUAL 8)
    string(APPEND words "\n    ")
    set(column 0)
  endif()
endforeach()

file(WRITE "${OUTPUT}.tmp"
  "// Generated from ${INPUT} by cmake/GeniEmbedSpirv.cmake. Do not edit.\n"
  "#pragma once\n"
  "const uint32_t ${NAME}[] = {\n    ${words}\n};\n"
)
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
