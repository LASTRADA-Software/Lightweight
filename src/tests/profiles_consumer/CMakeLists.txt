# SPDX-License-Identifier: Apache-2.0
#
# Standalone consumer of the *installed* Lightweight package. It links only Lightweight::Profiles,
# which proves the public header is self-contained and nothing private leaks through the package.
# Built by cmake/tests/ProfilesPackageTest.cmake (CTest `ProfilesPackageTest`).

cmake_minimum_required(VERSION 3.25)
project(ProfilesConsumer CXX)

find_package(Lightweight CONFIG REQUIRED)

add_executable(profiles_consumer main.cpp)
target_link_libraries(profiles_consumer PRIVATE Lightweight::Profiles)
