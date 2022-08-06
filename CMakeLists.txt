cmake_minimum_required(VERSION 2.6)

PROJECT(Keylayout2kle)

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

IF(NOT WIN32)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -pedantic -Wno-unused-function")
    SET(ENV{LANG} "EN")
ELSE(NOT WIN32)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D _CRT_SECURE_NO_WARNINGS /D NOMINMAX /Zc:preprocessor /wd4244 /wd4267 /wd4305 /wd4307 /wd5105 /wd26485 /wd26812")
	SET(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:windows")
	SET(VCPKG_PATH "D:/Programmes/vcpkg")
	SET(CMAKE_INCLUDE_PATH ${VCPKG_PATH}"/installed/x64-windows/include")
	SET(CMAKE_LIBRARY_PATH ${VCPKG_PATH}"/installed/x64-windows/lib")
	SET(CMAKE_TOOLCHAIN_FILE ${VCPKG_PATH}"/scripts/buildsystems/vcpkg.cmake")
ENDIF(NOT WIN32)

FILE(
    GLOB_RECURSE
    src_files
    src/*
)

FILE(
    GLOB_RECURSE
    header_files
    include/*.hpp
)

ADD_EXECUTABLE(keylayout2kle ${src_files})
SET(CURRENT_TARGETS keylayout2kle)




FIND_PACKAGE(TinyXML2 REQUIRED)
FIND_PACKAGE(ICU REQUIRED data uc)

FOREACH(CURRENT_TARGET ${CURRENT_TARGETS})

    TARGET_LINK_LIBRARIES(${CURRENT_TARGET} libtinyxml2.so)
    TARGET_LINK_LIBRARIES(${CURRENT_TARGET} ${ICU_LIBRARIES})

    SET_PROPERTY(TARGET ${CURRENT_TARGET} PROPERTY INCLUDE_DIRECTORIES
      ${CMAKE_SOURCE_DIR}/include/
      ${TINYXML2_INCLUDE_DIR}
      ${ICU_INCLUDE_DIR}
    )

ENDFOREACH(CURRENT_TARGET)

