# This file is part of Project SkyFire https://www.projectskyfire.org.
# See LICENSE.md file for Copyright information
cmake_minimum_required(VERSION 3.16)
project(AnticheatMovementTests LANGUAGES CXX)
enable_testing()
add_executable(movement_monitor_tests MovementMonitorTests.cpp)
target_compile_features(movement_monitor_tests PRIVATE cxx_std_11)
add_test(NAME movement_monitor COMMAND movement_monitor_tests)
