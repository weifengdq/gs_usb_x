cmake_minimum_required(VERSION 3.20)

project(gsusb_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)

if (MINGW)
    add_compile_options(-pthread)
    add_link_options(-pthread)
endif()

find_package(Threads REQUIRED)

add_library(gsusb_cpp
    src/gsusb.cpp
    src/libusb_dyn.cpp
)

target_include_directories(gsusb_cpp
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(gsusb_cpp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(gsusb_cpp PRIVATE setupapi Threads::Threads)

find_file(GSUSB_CPP_LIBUSB_DLL
    NAMES libusb-1.0.dll
    PATHS
        $ENV{VIRTUAL_ENV}/Lib/site-packages/libusb_package
        D:/n32/.venv/Lib/site-packages/libusb_package
        d:/n32/.venv/Lib/site-packages/libusb_package
)

add_library(gsusb_cpp_examples_common OBJECT examples/demo_common.cpp)
target_include_directories(gsusb_cpp_examples_common PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gsusb_cpp_examples_common PRIVATE gsusb_cpp Threads::Threads)

function(add_gsusb_example name source)
    add_executable(${name} ${source} $<TARGET_OBJECTS:gsusb_cpp_examples_common>)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    target_link_libraries(${name} PRIVATE gsusb_cpp Threads::Threads)
endfunction()

add_gsusb_example(01_simple_std examples/01_simple_std.cpp)
add_gsusb_example(02_periodic examples/02_periodic.cpp)
add_gsusb_example(03_simple_fd examples/03_simple_fd.cpp)
add_gsusb_example(04_custom_timing examples/04_custom_timing.cpp)
add_gsusb_example(04_multi_std_asyncio examples/04_multi_std_asyncio.cpp)
add_gsusb_example(01_pair_4ch_threading examples/01_pair_4ch_threading.cpp)
add_gsusb_example(02_pair_8ch_threading examples/02_pair_8ch_threading.cpp)
add_gsusb_example(05_pair_4ch_threading examples/05_pair_4ch_threading.cpp)
add_gsusb_example(06_pair_8ch_threading examples/06_pair_8ch_threading.cpp)
add_gsusb_example(07_pair_4ch_asyncio examples/07_pair_4ch_asyncio.cpp)
add_gsusb_example(08_pair_8ch_asyncio examples/08_pair_8ch_asyncio.cpp)
add_gsusb_example(09_multi_custom_timing_threading examples/09_multi_custom_timing_threading.cpp)
add_gsusb_example(10_multi_custom_timing_asyncio examples/10_multi_custom_timing_asyncio.cpp)
add_gsusb_example(11_long_run_stress examples/11_long_run_stress.cpp)