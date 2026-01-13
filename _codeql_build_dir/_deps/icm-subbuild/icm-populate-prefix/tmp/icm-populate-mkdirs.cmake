# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-src")
  file(MAKE_DIRECTORY "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-src")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-build"
  "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-subbuild/icm-populate-prefix"
  "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-subbuild/icm-populate-prefix/tmp"
  "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp"
  "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-subbuild/icm-populate-prefix/src"
  "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/Lightweight/Lightweight/_codeql_build_dir/_deps/icm-subbuild/icm-populate-prefix/src/icm-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
