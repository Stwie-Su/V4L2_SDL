cmake_minimum_required(VERSION 3.10)
project(v4l2_camera C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# ---- v4l2_probe：相机能力探测工具（只用 V4L2，不依赖 SDL）----
add_executable(v4l2_probe src/v4l2_probe.c)

# ---- v4l2_display：mmap 采集 + MJPG 解码 + SDL2 实时显示 ----
add_executable(v4l2_display
  src/main_display.c
  src/v4l2_capture.c
  src/stb_image_impl.c
)
target_include_directories(v4l2_display PRIVATE src ${SDL2_INCLUDE_DIRS})
target_compile_options(v4l2_display PRIVATE ${SDL2_CFLAGS_OTHER})
target_link_libraries(v4l2_display PRIVATE ${SDL2_LIBRARIES} m)

# stb_image 是第三方单文件库，压掉它自身的编译警告（只管我们的代码干净）
set_source_files_properties(src/stb_image_impl.c PROPERTIES COMPILE_OPTIONS "-w")
