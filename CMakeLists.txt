cmake_minimum_required(VERSION 3.14)
project(torcc)

include(CMakeDependentOption)
include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(torcc STATIC
  src/torcc_comm.cpp
  src/torcc_queue.cpp
  src/torcc_server.cpp
  src/torcc_thread.cpp
  src/torcc.cpp
  include/torcc.h
  include/torcc_base.h
  include/torcc_config.h
  include/torcc_internal.h
  include/torcc_mpi_internal.h
  include/torcc_queue.h
  include/util.h
)

set_property(TARGET torcc PROPERTY CXX_STANDARD 20)
target_compile_features(torcc PRIVATE cxx_std_20)
target_compile_options(torcc PRIVATE
  -fno-exceptions
  -fno-unwind-tables
  -fno-asynchronous-unwind-tables
  -Wno-sign-compare
  -Wno-unused-function
  -Wunused-variable)

# Build torcpp with -flto if -DTORCC_LTO=ON
option(TORCC_LTO "Build torcc with link-time optimization enabled")
if(TORCC_LTO)
  set_property(TARGET torcc PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

option(TORCC_USE_ASAN "Build torcc with AddressSanitizer" OFF)
if(TORCC_USE_ASAN)
  target_compile_options(torcc PRIVATE -fsanitize=address -fsanitize=undefined)
  target_link_options(torcc PRIVATE -fsanitize=address -fsanitize=undefined)
endif()

option(TORCC_USE_TSAN "Build torcc with ThreadSanitizer" OFF)
if(TORCC_USE_TSAN)
  target_compile_options(torcc PRIVATE -fsanitize=thread)
  target_link_options(torcc PRIVATE -fsanitize=thread)
endif()

target_compile_options(torcc PRIVATE -pthread)
target_link_options(torcc PRIVATE -pthread)

# configure_file(include/config.h.in config.h)

# find_package(MPI REQUIRED)

# Include directories if needed
target_include_directories(torcc PUBLIC include)

# Link external libraries if needed
# target_link_libraries(torcc PRIVATE MPI::MPI_CXX)

# Set target properties if needed
# target_compile_definitions(YourLibraryName PRIVATE SOME_DEFINE=1)

# Optionally, install the library and headers
install(TARGETS torcc DESTINATION build/lib)
install(FILES torc.h DESTINATION build/include/torcc)
