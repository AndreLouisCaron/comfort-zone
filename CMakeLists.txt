# Copyright (c) 2012, Andre Caron (andre.l.caron@gmail.com)
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Custom options:
# - FIO_TRACE (ON/OFF): controls debug output.

# Custom targets:
# - help: builds API documentation in HTML format using Doxygen.  Add the
#         "-DPROJECT_VERSION=..." flag to the CMake command when publishing
#         the docuentation for a specific version.  This target is only
#         available if building the library as a standalone project.  It
#         is not available if added to a dependent project using the
#         "find_package(cfcgi)" directive.
#

cmake_minimum_required(VERSION 2.6)

project(fio)

option(FIO_TRACE "Enable detail hub & engine tracing." OFF)

if (NOT PROJECT_VERSION)
  set(PROJECT_VERSION "(dev)")
endif()

# Configure environment.
set(CMAKE_MODULE_PATH
  ${CMAKE_CURRENT_SOURCE_DIR}/site
)
find_package(MSVCExtras)
msvc_configure_runtime()
msvc_enable_se_handling()

# All dependencies are fetched as Git submodules.
macro(add_submodule name)
  set(${name}_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/${name}
  )
  find_package(${name} ${ARGN})
endmacro()

if(MSVC)
  # C and C++ runtime libraries are safe to use.
  # Don't use non-portable and inconvenient APIs
  # unless we have a reason to.
  add_definitions(
    -D_SCL_SECURE_NO_WARNINGS
    -D_CRT_SECURE_NO_WARNINGS
  )
  # Configure Windows SDK headers.
  add_definitions(
    -D_WIN32_WINNT=0x601
    -DSECURITY_WIN32
  )
endif()

# Put all libraries and executables in the build folder root.
set(LIBRARY_OUTPUT_PATH    ${PROJECT_BINARY_DIR})
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})

# Resolve compiled libraries directly.
link_directories(${LIBRARY_OUTPUT_PATH})

# Build dependencies.
add_submodule(w32
  COMPONENTS
    w32
    w32.dbg
    w32.io
    w32.ipc
    w32.fs
    w32.mt
    w32.net
    w32.net.ipv4
    w32.net.tcp
    w32.tp
)

# Resolve library headers.
include_directories(${w32_include_dir})

# Build main library.
if (FIO_TRACE)
  add_definitions(
    -DFIO_DEBUG
  )
endif()
add_subdirectory(code)

# Exclude all but the core libraries when built as a dependency.
if(${PROJECT_NAME} STREQUAL ${CMAKE_PROJECT_NAME})

  # Build demo projects.
  add_subdirectory(demo)

  # Build documentation.
  find_program(DOT_PATH
    NAMES "dot"
    PATH_SUFFIXES "bin"
    DOC "Path to \"dot\", for diagrams in documentation."
  )
  set(HAVE_DOT "NO")
  if (DOT_PATH)
    set(HAVE_DOT "YES")
  endif()
  configure_file(
    "${PROJECT_SOURCE_DIR}/Doxyfile"
    "${PROJECT_BINARY_DIR}/Doxyfile" @ONLY
  )
  add_custom_target(help
    WORKING_DIRECTORY
      "${PROJECT_SOURCE_DIR}"
    COMMAND
      "doxygen" "${PROJECT_BINARY_DIR}/Doxyfile"
    COMMENT
      "Building HTML documentation."
    VERBATIM
  )

endif()
