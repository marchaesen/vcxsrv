
include (CTest)

include_directories(..)

set(XXLIBS	ws2_32.lib)

set(VCEFLAGS "${VCEFLAGS} -D__PtW32NoCatchWarn")

macro(add_testcase test type def)

    set(lib_test test-${test}${type}${PTW32_VER})
    set(dll_test test-dll-${test}${type}${PTW32_VER})

    set(lib_lib libpthread${type}${PTW32_VER})
    set(dll_lib pthread${type}${PTW32_VER})

    set(c_dep "")
    if(${test} MATCHES "benchtest")
        set(c_dep "benchlib.c")
    endif()

    set(extra "")
    if(${test} MATCHES "openmp1")
        if(MSVC)
            set(extra "/openmp -D_OPENMP")
        endif()
    endif()

    add_executable(${lib_test} ${test}.c ${c_dep})
    target_link_libraries(${lib_test} ${lib_lib} ${XXLIBS})
    target_compile_definitions(${lib_test} PUBLIC "/nologo -D_CONSOLE -D_MBCS -D${def} ${extra}")
    add_dependencies(${lib_test} ${lib_lib})

    add_executable(${dll_test} ${test}.c ${c_dep})
    target_link_libraries(${dll_test} ${dll_lib} ${XXLIBS})
    target_compile_definitions(${dll_test} PUBLIC "/nologo -D_CONSOLE -D_MBCS -D${def} ${extra}")
    add_dependencies(${dll_test} ${dll_lib})

    if(${CMAKE_GENERATOR} MATCHES "Visual Studio")
        install(FILES ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/tests/${lib_test}.exe DESTINATION ${TESTDEST})
        install(FILES ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/tests/${dll_test}.exe DESTINATION ${TESTDEST})
    else()
        install(FILES ${CMAKE_BINARY_DIR}/tests/${lib_test}.exe DESTINATION ${TESTDEST})
        install(FILES ${CMAKE_BINARY_DIR}/tests/${dll_test}.exe DESTINATION ${TESTDEST})
    endif()

    if(${type} MATCHES "VCE")
        set_target_properties(${lib_test} PROPERTIES COMPILE_FLAGS ${VCEFLAGS})
        set_target_properties(${dll_test} PROPERTIES COMPILE_FLAGS ${VCEFLAGS})
    endif()

    add_test(NAME ${lib_test} COMMAND ${lib_test})
    add_test(NAME ${dll_test} COMMAND ${dll_test})

endmacro()


file(GLOB TESTS *.c)

foreach(t ${TESTS})

    get_filename_component(test ${t} NAME)
    string(REPLACE ".c" "" test "${test}")

    # exclusions
    if(${test} STREQUAL "benchlib")
        list(REMOVE_ITEM TESTS ${t})
        continue()
    elseif(${test} STREQUAL "context2")    # SEGFAULT
        continue()
    elseif(${test} STREQUAL "tryentercs2") # SEGFAULT
        continue()
    endif()

    add_testcase(${test} VCE __PTW32_CLEANUP_CXX )
    add_testcase(${test} VSE __PTW32_CLEANUP_SEH )
    add_testcase(${test} VC  __PTW32_CLEANUP_C )

endforeach(t)
