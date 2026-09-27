# Le prototype en natif, sur Dawn (vcpkg) :
#   cmake -S prototypes/webgpu/native -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake_minimum_required(VERSION 3.28)
project(levain_webgpu_spike CXX)
find_package(Dawn CONFIG REQUIRED)
add_executable(cube_native ../main.cpp)
target_compile_features(cube_native PRIVATE cxx_std_23)
target_link_libraries(cube_native PRIVATE dawn::webgpu_dawn)
