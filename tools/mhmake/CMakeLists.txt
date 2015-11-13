cmake_minimum_required(VERSION 2.8)

IF(CMAKE_BUILD_TYPE MATCHES "Release")
SET(PROGRAM_NAME mhmake)
ELSE(CMAKE_BUILD_TYPE MATCHES "Release")
SET(PROGRAM_NAME mhmake_dbg)
ENDIF(CMAKE_BUILD_TYPE MATCHES "Release")

PROJECT(${PROGRAM_NAME})

SET( CMAKE_CXX_FLAGS_DEBUG "-g -D _DEBUG")

ADD_CUSTOM_COMMAND(
   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mhmakelexer.cpp ${CMAKE_CURRENT_BINARY_DIR}/FlexLexer.h
   COMMAND flex++ -S${CMAKE_CURRENT_SOURCE_DIR}/src/flex.skl -o${CMAKE_CURRENT_BINARY_DIR}/mhmakelexer.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/mhmakelexer.l
   COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/mhmakelexer.cpp  ${CMAKE_CURRENT_BINARY_DIR}/temp1234.456
   COMMAND echo '\#include \"stdafx.h\"' > ${CMAKE_CURRENT_BINARY_DIR}/mhmakelexer.cpp
   COMMAND cat ${CMAKE_CURRENT_BINARY_DIR}/temp1234.456 >> ${CMAKE_CURRENT_BINARY_DIR}/mhmakelexer.cpp
   COMMAND rm ${CMAKE_CURRENT_BINARY_DIR}/temp1234.456
   COMMAND cp ${CMAKE_CURRENT_SOURCE_DIR}/src/flexlexer.h ${CMAKE_CURRENT_BINARY_DIR}/FlexLexer.h

   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/mhmakelexer.l ${CMAKE_CURRENT_SOURCE_DIR}/src/flex.skl ${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.hpp ${CMAKE_CURRENT_SOURCE_DIR}/src/flexlexer.h
)
ADD_CUSTOM_COMMAND(
   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.cpp ${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.hpp ${CMAKE_CURRENT_BINARY_DIR}/location.hh  ${CMAKE_CURRENT_BINARY_DIR}/position.hh
   COMMAND BISON_PKGDATADIR=${CMAKE_CURRENT_SOURCE_DIR}/src/bisondata bison -d -ra -S${CMAKE_CURRENT_SOURCE_DIR}/src/bisondata/lalr1.cc -o${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/mhmakeparser.y
   COMMAND mv ${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.cpp temp1234.456
   COMMAND echo '\#include \"stdafx.h\"' > ${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.cpp
   COMMAND cat temp1234.456 >> ${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.cpp
   COMMAND rm temp1234.456

   DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/mhmakeparser.y ${CMAKE_CURRENT_SOURCE_DIR}/src/bisondata/lalr1.cc
)
INCLUDE_DIRECTORIES(src ${CMAKE_CURRENT_BINARY_DIR})
LINK_LIBRARIES(libpopt.a)

ADD_EXECUTABLE(${PROGRAM_NAME}
               ${CMAKE_CURRENT_BINARY_DIR}/mhmakeparser.cpp
               ${CMAKE_CURRENT_BINARY_DIR}/mhmakelexer.cpp
               src/mhmake.cpp
               src/mhmakefileparser.cpp
               src/util.cpp
               src/functions.cpp
               src/fileinfo.cpp
               src/rule.cpp
               src/md5.cpp
               src/build.cpp
               src/curdir.cpp
               src/commandqueue.cpp
              )

INSTALL_TARGETS( /bin ${PROGRAM_NAME} )
