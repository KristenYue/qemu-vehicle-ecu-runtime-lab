cmake_minimum_required(VERSION 3.20)
project(qemu_vehicle_ecu_runtime_lab VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_library(vecu_core
  cpp/src/application_manifest.cpp
  cpp/src/dependency_graph.cpp
  cpp/src/heartbeat_protocol.cpp
)
target_include_directories(vecu_core PUBLIC cpp/include)

add_executable(vecu_core_tests cpp/tests/test_core.cpp)
target_link_libraries(vecu_core_tests PRIVATE vecu_core)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(vecu_supervisor cpp/src/process_supervisor_linux.cpp)
  target_include_directories(vecu_supervisor PUBLIC cpp/include)
  target_link_libraries(vecu_supervisor PUBLIC vecu_core)

  add_executable(vecu_execution_manager cpp/src/main.cpp)
  target_link_libraries(vecu_execution_manager PRIVATE vecu_supervisor)

  add_executable(vecu_health_service cpp/apps/health_service.cpp)
  add_executable(vecu_vehicle_client cpp/apps/vehicle_client.cpp)
  add_executable(vecu_faultable_service cpp/apps/faultable_service.cpp)
  add_executable(vecu_managed_process_adapter cpp/apps/managed_process_adapter.cpp)
  target_link_libraries(vecu_health_service PRIVATE vecu_core)
  target_link_libraries(vecu_vehicle_client PRIVATE vecu_core)
  target_link_libraries(vecu_faultable_service PRIVATE vecu_core)
  target_link_libraries(vecu_managed_process_adapter PRIVATE vecu_core)
endif()

enable_testing()
add_test(NAME vecu_core_tests COMMAND vecu_core_tests)

