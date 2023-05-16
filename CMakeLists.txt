#
# Copyright (c) 2022-2023, NLnet Labs. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#
cmake_minimum_required(VERSION 3.10)
project(
  simdzone
  LANGUAGES C
  VERSION 0.1.0
  DESCRIPTION "Fast and standards compliant DNS zone parser")

set(CMAKE_C_STANDARD 99)
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/Modules)

if(CMAKE_VERSION VERSION_LESS 3.12)
  # GENERATE_EXPORT_HEADER requires a C++ compiler up to version 3.12
  enable_language(CXX)
endif()

if(CMAKE_VERSION VERSION_LESS 3.24)
  # COMPILE_WARNING_AS_ERROR property was added in version 3.24. Mimic the
  # option in prior versions.
  option(CMAKE_COMPILE_WARNING_AS_ERROR "Treat compiler warnings as errors." OFF)
  if(CMAKE_COMPILE_WARNING_AS_ERROR)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU" OR CMAKE_C_COMPILER_ID MATCHES "Clang")
      add_compile_options(-Werror)
    elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
      add_compile_options(/WX)
    endif()
  endif()
endif()

# Set a default build type if none was specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE
      STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

# Building the testing tree is enabled by including CTest, but as it is not
# strictly required to build the product itself, switch to off by default.
option(BUILD_TESTING "Build the testing tree." OFF)
option(BUILD_DOCUMENTATION "Build documentation." OFF)

include(CheckIncludeFile)
include(CheckCCompilerFlag)
include(GenerateExportHeader)
include(GNUInstallDirs)
include(CTest)

if(CMAKE_C_COMPILER_ID MATCHES "Clang")
  add_compile_options(
    -Wall -Wextra -Wconversion -Wunused -Wmissing-prototypes
    -Winfinite-recursion -Wassign-enum -Wcomma -Wdocumentation
    -Wstrict-prototypes -Wconditional-uninitialized -Wshadow)
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    add_compile_options(-Xclang -fcolor-diagnostics)
  endif()
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(
    -Wall -Wextra -pedantic)
  if(CMAKE_GENERATOR STREQUAL "Ninja")
    add_compile_options(-fdiagnostics-color=always)
  endif()
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(/W3)
endif()

set(ANALYZER "" CACHE STRING "Analyzer to enable on the build.")
if(ANALYZER)
  # GCC and Visual Studio offer builtin analyzers. Clang supports static
  # analysis through separate tools, e.g. Clang-Tidy, which can be used in
  # conjunction with other compilers too. Specifying -DANALYZER=on enables
  # the builtin analyzer for the compiler, enabling clang-tidy in case of
  # Clang. Specifying -DANALYZER=clang-tidy always enables clang-tidy.
  string(REPLACE " " "" ANALYZER "${ANALYZER}")
  string(TOLOWER "${ANALYZER}" ANALYZER)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang" AND ANALYZER STREQUAL "on")
    set(ANALYZER "clang-tidy")
  endif()

  if(ANALYZER STREQUAL "clang-tidy")
    # Clang-Tidy is an extensible tool that offers more than static analysis.
    # https://clang.llvm.org/extra/clang-tidy/checks/list.html
    message(STATUS "Enabling analyzer: clang-tidy")
    set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.strcpy,-clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling")
    if(CMAKE_COMPILE_WARNING_AS_ERROR)
      set(CMAKE_C_CLANG_TIDY "${CMAKE_C_CLANG_TIDY};--warnings-as-errors=*")
    endif()
  elseif(ANALYZER STREQUAL "on")
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
      if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL "10")
        message(STATUS "Enabling analyzer: GCC")
        # -Wanalyzer-malloc-leak generates lots of false positives
        add_compile_options(-fanalyzer -Wno-analyzer-malloc-leak)
      endif()
    endif()
  endif()
endif()

set(SANITIZER "" CACHE STRING "Sanitizers to enable on the build.")
if(SANITIZER)
  string(REGEX REPLACE " " "" SANITIZER "${SANITIZER}")
  string(REGEX REPLACE "[,;]+" ";" SANITIZER "${SANITIZER}")
  foreach(san ${SANITIZER})
    if(san STREQUAL "address")
      add_compile_options("-fno-omit-frame-pointer")
      add_link_options("-fno-omit-frame-pointer")
    endif()
    if(san AND NOT san STREQUAL "none")
      message(STATUS "Enabling sanitizer: ${san}")
      add_compile_options("-fsanitize=${san}")
      add_link_options("-fsanitize=${san}")
    endif()
  endforeach()
endif()

add_executable(zone-bench src/zone.c src/bench.c src/log.c)

set_source_files_properties(src/bench.c PROPERTIES COMPILE_FLAGS "-march=haswell")

target_compile_options(zone-bench PRIVATE -fjump-tables)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  target_compile_options(zone-bench PRIVATE --param=case-values-threshold=1)
endif()
target_include_directories(
  zone-bench PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                     $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
             PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
                     $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>)
