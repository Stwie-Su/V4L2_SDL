# CMake 面试笔记

> 范围：常见工程用法 + 高频面试点。跳过 CPack、复杂 export、策略细节等偏门内容。
>
> 一句话：**CMake 不是编译器，它负责生成 Makefile / Ninja 文件，再由真正的构建工具去编译链接。**

```text
CMakeLists.txt --配置/生成--> build/Makefile 或 build.ninja --构建--> 可执行文件 / 库
```

---

## 1. 最小可用模板

```cmake
cmake_minimum_required(VERSION 3.16)
project(demo LANGUAGES C CXX)

add_executable(app src/main.cpp)

target_compile_features(app PRIVATE cxx_std_17)
target_include_directories(app PRIVATE include)
target_compile_definitions(app PRIVATE APP_VERSION="1.0")
target_link_libraries(app PRIVATE mylib)
```

| 命令 | 作用 |
|---|---|
| `cmake_minimum_required` | 声明最低 CMake 版本，同时设置策略兼容性 |
| `project()` | 定义工程名、启用语言（C/CXX）、检查编译器 |
| `add_executable()` | 定义可执行目标 |
| `add_library()` | 定义库目标 |
| `target_*()` | 给某个目标设置头文件、宏、选项、链接库 |
| `find_package()` | 查找外部依赖 |
| `add_subdirectory()` | 加入子目录工程 |
| `install()` | 定义安装规则 |
| `enable_testing()` / `add_test()` | 注册 CTest 测试 |

---

## 2. 构建流程

### 四个常用命令

```bash
cmake -S . -B build                 # 配置 + 生成构建文件
cmake --build build -j              # 编译链接
ctest --test-dir build              # 跑测试（如果有）
cmake --install build --prefix /opt # 安装（如果写了 install 规则）
```

### 配置阶段 vs 构建阶段

- **配置（configure/generate）**：读 `CMakeLists.txt`，找编译器、找依赖、写 `CMakeCache.txt` 和 Makefile/Ninja 文件。
- **构建（build）**：调用 Make/Ninja，真正执行编译和链接。
- 改 `CMakeLists.txt` 后一般不用手动重新配置，`cmake --build` 会检测到并自动重跑 CMake。

### out-of-source build

源码和构建产物分开：

```text
project/
├── CMakeLists.txt
├── src/
└── build/          ← 所有生成物都在这里，可整个删除
```

好处：源码目录干净、可以同时有 `build-debug/` 和 `build-release/`、清理方便。

### CMakeCache.txt

缓存里存编译器路径、依赖路径、`option()` 的值等。

常见坑：

- 工程目录被移动后，缓存里的旧路径会导致配置失败 → `rm -rf build`；
- 改了 `option()` 的默认值，旧缓存不会自动更新 → 删缓存，或 `set(... CACHE ... FORCE)`；
- CMake 3.24+ 可以用 `cmake --fresh` 清缓存重配。

### 生成器（Generator）

```bash
cmake -G Ninja -B build
cmake -G "Unix Makefiles" -B build
```

Linux 默认是 Unix Makefiles；Ninja 通常更快。生成器分两类：

- **单配置**：Unix Makefiles、Ninja。用 `CMAKE_BUILD_TYPE` 决定 Debug/Release。
- **多配置**：Visual Studio、Xcode、Ninja Multi-Config。构建时用 `--config Release` 指定，`CMAKE_BUILD_TYPE` 不起作用。

---

## 3. Target 和 Modern CMake

### target 是什么

一个 target 就是一个最终产物：可执行文件、静态库、共享库，或者只有使用要求的接口库。

```cmake
add_executable(app src/main.c)
add_library(mylib STATIC src/mylib.c)
add_library(header_only INTERFACE)
```

**Modern CMake 的核心思想：一切设置都挂在 target 上，而不是全局目录上。**

### 新旧写法对照

| 旧写法（目录级，不推荐） | 新写法（目标级） |
|---|---|
| `include_directories(include)` | `target_include_directories(app PRIVATE include)` |
| `add_definitions(-DFOO)` | `target_compile_definitions(app PRIVATE FOO)` |
| `add_compile_options(-Wall)` | `target_compile_options(app PRIVATE -Wall)` |
| `link_libraries(foo)` | `target_link_libraries(app PRIVATE foo)` |
| `link_directories(lib)` | 优先用完整库路径或 imported target |

旧写法会污染它之后定义的所有 target；新写法只影响指定目标，而且能表达“依赖要不要传给使用者”。

### PRIVATE / PUBLIC / INTERFACE（高频）

| 关键字 | 自己编译时用 | 传给依赖自己的 target |
|---|---|---|
| `PRIVATE` | 是 | 否 |
| `PUBLIC` | 是 | 是 |
| `INTERFACE` | 否 | 是 |

用于 `target_include_directories`、`target_compile_definitions`、`target_compile_options`、`target_link_libraries`。

例子：

```cmake
add_library(video STATIC src/video.c)

# public 头文件里会 include 这个目录，所以使用者也需要 -I
target_include_directories(video PUBLIC include)

# 只有 video.c 自己用的内部头文件
target_include_directories(video PRIVATE src)

# public 头文件里出现了 SDL 类型，使用者也需要 SDL 的头文件和链接
target_link_libraries(video PUBLIC SDL2::SDL2)

# 只有 video.c 用到数学库
target_link_libraries(video PRIVATE m)
```

判断口诀：

- **只给自己用 → PRIVATE**
- **自己用，别人也要用 → PUBLIC**
- **自己不用，只给别人用 → INTERFACE**（典型是 header-only 库）

### 静态库的一个细节

静态库本身不进行链接，它只是 `.o` 的打包。最终可执行文件链接时，仍然需要静态库所依赖的库。CMake 会记录这类依赖，但 `PRIVATE` 依赖的头文件路径和宏**不会**传给使用者。

### header-only 库

```cmake
add_library(mylib INTERFACE)
target_include_directories(mylib INTERFACE include)
target_compile_features(mylib INTERFACE cxx_std_17)
```

`INTERFACE` 库没有源文件、不产生构建产物，只携带“使用要求”。

---

## 4. 查找依赖

### find_package 的两种模式

```cmake
find_package(SDL2 REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
```

| 模式 | 查找什么 | 例子 |
|---|---|---|
| Module 模式 | `Find<包名>.cmake` | 系统自带的 `FindZLIB.cmake` |
| Config 模式 | `<包名>Config.cmake` / `<包名>-config.cmake` | 库自己安装的配置文件 |

基本签名默认先找 Module，再找 Config。想强制用 Config 可以写 `find_package(fmt CONFIG REQUIRED)`。

常用参数：

- `REQUIRED`：找不到就报错停止；
- `QUIET`：找不到不打印；
- `COMPONENTS`：只找指定组件。

### imported target

现代库通常提供带命名空间的 imported target，例如：

```cmake
target_link_libraries(app PRIVATE SDL2::SDL2)
target_link_libraries(app PRIVATE fmt::fmt)
target_link_libraries(app PRIVATE Qt6::Widgets)
```

它把“头文件路径 + 编译选项 + 链接库 + 传递依赖”打包在一起，比手动使用 `${SDL2_INCLUDE_DIRS}`、`${SDL2_LIBRARIES}` 更可靠。

### pkg-config

很多 Linux 库没有 CMake Config，但有 `.pc` 文件：

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

target_link_libraries(app PRIVATE PkgConfig::SDL2)
```

`pkg_check_modules` 会生成：

- `SDL2_FOUND`
- `SDL2_INCLUDE_DIRS`
- `SDL2_CFLAGS_OTHER`
- `SDL2_LIBRARIES`

加 `IMPORTED_TARGET` 后会生成 `PkgConfig::SDL2`，推荐这种写法。

---

## 5. 编译选项和构建类型

### 构建类型

单配置生成器常用：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

| 类型 | 典型 GCC 参数 | 用途 |
|---|---|---|
| `Debug` | `-g` | 调试，不优化 |
| `Release` | `-O3 -DNDEBUG` | 发布，追求性能 |
| `RelWithDebInfo` | `-O2 -g -DNDEBUG` | 优化 + 调试信息 |
| `MinSizeRel` | `-Os -DNDEBUG` | 最小体积 |

`-DNDEBUG` 会让标准库的 `assert()` 失效。

多配置生成器：

```bash
cmake --build build --config Release
```

不要用 `CMAKE_BUILD_TYPE` 控制多配置生成器。

### 常用 target 命令

```cmake
target_include_directories(app PRIVATE include)          # -I
target_compile_definitions(app PRIVATE FOO=1)            # -DFOO=1
target_compile_options(app PRIVATE -Wall -Wextra)        # 原样传给编译器
target_compile_features(app PRIVATE c_std_11)            # 要求语言特性
```

C/C++ 标准两种写法：

```cmake
# 全局/目录级（老写法）
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 目标级（更现代）
target_compile_features(app PRIVATE c_std_11)
```

### 生成器表达式

只在特定配置下加选项：

```cmake
target_compile_options(app PRIVATE $<$<CONFIG:Debug>:-g3>)
```

`$<...>` 在生成阶段才展开，常用于 Debug/Release 差异化、`$<BUILD_INTERFACE:...>` 和 `$<INSTALL_INTERFACE:...>`。面试一般知道“按配置/按条件展开”即可，不用背太多。

---

## 6. 变量和作用域

### 普通变量

```cmake
set(SRC main.c util.c)
add_executable(app ${SRC})
```

- 列表本质是分号分隔的字符串：`main.c;util.c`；
- 引用变量用 `${}`；
- `if()` 里可以直接写变量名，不写 `${}`，但为了清晰建议写 `${}`（字符串比较时尤其要注意）。

### 缓存变量

```cmake
set(MY_OPTION ON CACHE BOOL "开关说明")
option(MY_OPTION "开关说明" ON)
```

- 缓存变量会写进 `CMakeCache.txt`；
- 命令行 `-DMY_OPTION=ON` 创建的就是缓存变量；
- `option()` 本质上就是创建一个 BOOL 缓存变量；
- 已经存在的缓存变量，普通 `set(... CACHE ...)` 不会覆盖，除非加 `FORCE`。

### 作用域

- 变量有目录作用域，子目录继承父目录的变量；
- `add_subdirectory()` 会创建新的目录作用域，子目录改变量默认不影响父目录；
- 函数 `function()` 有独立作用域；
- 宏 `macro()` 是文本替换，没有独立作用域；
- 想向父作用域传值：`set(VAR value PARENT_SCOPE)`。

### include vs add_subdirectory

| 命令 | 行为 |
|---|---|
| `include(xxx.cmake)` | 把文件内容直接包含进当前作用域，不创建子工程 |
| `add_subdirectory(dir)` | 处理子目录的 `CMakeLists.txt`，创建新的目录作用域，构建其中的 target |

### 常见目录变量

| 变量 | 含义 |
|---|---|
| `CMAKE_SOURCE_DIR` | 最顶层源码目录 |
| `CMAKE_BINARY_DIR` | 最顶层构建目录 |
| `CMAKE_CURRENT_SOURCE_DIR` | 当前 `CMakeLists.txt` 所在源码目录 |
| `CMAKE_CURRENT_BINARY_DIR` | 当前 CMakeLists 对应的构建目录 |
| `PROJECT_SOURCE_DIR` | 最近一次 `project()` 所在源码目录 |
| `PROJECT_BINARY_DIR` | 最近一次 `project()` 对应的构建目录 |

多级工程里，`CMAKE_SOURCE_DIR` 和 `PROJECT_SOURCE_DIR` 可能不同，这是常见考点。

---

## 7. 静态库 vs 共享库

```cmake
add_library(mylib STATIC src/mylib.c)   # libmylib.a
add_library(mylib SHARED src/mylib.c)   # libmylib.so
add_library(mylib INTERFACE)            # 无产物，只有使用要求
```

| 类型 | 链接时 | 运行时 | 特点 |
|---|---|---|---|
| 静态库 `.a` | 代码被复制进可执行文件 | 不依赖库文件 | 体积大、部署简单、更新需重编 |
| 共享库 `.so` | 只记录依赖 | 需要能找到 `.so` | 体积小、可共享、可单独升级 |

- `add_library(mylib ...)` 不写类型时，由 `BUILD_SHARED_LIBS` 决定：`ON` 默认共享，否则静态。
- 共享库通常需要位置无关代码 `-fPIC`，CMake 会自动加；静态库如果要被链进共享库，需要 `POSITION_INDEPENDENT_CODE ON`。
- 运行时找不到 `.so` 时，用 `ldd ./app` 查看依赖，再检查 `RPATH`、`LD_LIBRARY_PATH` 或系统库路径。

### 链接顺序和 C/C++ 混用

- GNU ld 对静态库通常要求“**使用者在前，被使用者在后**”：`-lA -lB` 且 A 依赖 B；
- CMake 一般会自动处理依赖顺序和重复库；
- C++ 调用 C 库时，C 头文件要加 `extern "C"`，否则会因 C++ 名称修饰导致 `undefined reference`。

---

## 8. 工程组织

### 子目录

```cmake
# 顶层
add_subdirectory(src)
add_subdirectory(lib)
```

每个子目录一个 `CMakeLists.txt`，各自定义 target。target 名字全局唯一。

### 别名

```cmake
add_library(mylib src/mylib.c)
add_library(mylib::mylib ALIAS mylib)
```

别名让目标看起来像 imported target，便于统一写法。

### 不要用 file(GLOB) 收集源文件

```cmake
file(GLOB SRC "src/*.c")   # 不推荐
```

原因：新增文件时 CMake 不会自动重新配置，容易“明明加了文件却没编进去”。推荐显式列出源文件；`CONFIGURE_DEPENDS` 能缓解，但有额外扫描开销。

### compile_commands.json

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

生成 `build/compile_commands.json`，给 clangd、VS Code、CLion 等工具提供真实的编译命令和头文件路径。Makefile 和 Ninja 生成器支持。

---

## 9. 安装和测试（了解即可）

### 安装

```cmake
include(GNUInstallDirs)

install(TARGETS app
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS mylib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
```

```bash
cmake --install build --prefix /opt/myapp
```

`CMAKE_INSTALL_PREFIX` 默认通常是 `/usr/local`。

`install(EXPORT ...)` 用于生成 `<包>Targets.cmake`，让别的 CMake 工程能 `find_package(你的库)`。面试知道“install 是安装，export 是导出供其他工程 find_package 使用”即可。

### 测试

```cmake
enable_testing()
add_test(NAME basic COMMAND app --self-test)
```

```bash
ctest --test-dir build --output-on-failure
```

---

## 10. 常见坑

1. **改不了缓存里的旧值**：`option()` 第二次配置不会用新默认值，删 `build/` 或 `cmake --fresh`。
2. **工程移动后配置失败**：`CMakeCache.txt` 里是旧绝对路径，删缓存重配。
3. **头文件找不到**：忘了 `target_include_directories`，或者 `PRIVATE/PUBLIC` 用错导致使用者拿不到。
4. **undefined reference**：忘了 `target_link_libraries`、链接顺序不对，或 C/C++ 混用没写 `extern "C"`。
5. **改了 `.h` 没重编**：正常 CMake 会跟踪头文件依赖；如果是外部生成的依赖没被跟踪，检查构建规则。
6. **多配置生成器里设 `CMAKE_BUILD_TYPE` 无效**：改用 `--config`。
7. **用 `file(GLOB)` 后新增文件没被编译**：重新配置或改成显式列表。
8. **全局 `include_directories` 污染所有目标**：改用 `target_include_directories`。
9. **静态库的依赖没传到最终程序**：检查 `target_link_libraries` 的 `PUBLIC/PRIVATE`，以及最终链接命令。
10. **运行时找不到 `.so`**：`ldd` 查看，再处理 RPATH / `LD_LIBRARY_PATH` / 安装路径。

---

## 11. 高频面试问答

**Q1：CMake 和 Make 的区别？**

Make 是实际执行编译的工具，需要手写 Makefile；CMake 是更高层的构建系统生成器，读 `CMakeLists.txt`，生成 Makefile 或 Ninja 文件，再调用它们。CMake 跨平台、跨生成器，依赖管理更强。

**Q2：configure 和 build 有什么区别？**

configure 阶段读 CMakeLists、找编译器和依赖、生成构建文件；build 阶段才真正编译链接。配置只做一次（或 CMakeLists 变化时重做），构建可以反复增量执行。

**Q3：PRIVATE、PUBLIC、INTERFACE 的区别？**

PRIVATE 只给自己用；PUBLIC 自己用也传给使用者；INTERFACE 自己不用、只传给使用者。用于头文件路径、宏、编译选项和链接库。

**Q4：为什么推荐 target_* 而不是全局命令？**

作用域清晰、不会污染其他目标、能表达依赖传递关系，适合大型工程和库的复用。

**Q5：find_package 的 Module 和 Config 模式？**

Module 模式找 `FindXXX.cmake`，通常由 CMake 或工程自己提供；Config 模式找库安装的 `XXXConfig.cmake`。默认先 Module 后 Config，可以显式写 `CONFIG` 强制。

**Q6：imported target 是什么？**

由 `find_package` 或 `pkg_check_modules(IMPORTED_TARGET)` 提供的、代表外部库的目标，例如 `SDL2::SDL2`。它把头文件、编译选项、链接库和传递依赖打包好，直接 `target_link_libraries` 即可。

**Q7：怎么设置 C/C++ 标准？**

老写法：`CMAKE_C_STANDARD` / `CMAKE_CXX_STANDARD` 加 `..._STANDARD_REQUIRED`；新写法：`target_compile_features(app PRIVATE cxx_std_17)`。目标级写法更利于库的传播。

**Q8：Debug 和 Release 有什么区别？**

Debug 通常 `-g` 无优化，便于调试；Release 通常 `-O3 -DNDEBUG`，追求性能并关闭 `assert`。单配置生成器用 `CMAKE_BUILD_TYPE`，多配置生成器用 `--config`。

**Q9：普通变量和缓存变量有什么区别？**

普通变量只在当前配置过程中、当前作用域内有效；缓存变量写入 `CMakeCache.txt`，跨配置保留，命令行 `-D` 设置的就是缓存变量。`option()` 创建 BOOL 缓存变量。

**Q10：add_subdirectory 和 include 的区别？**

`add_subdirectory` 处理一个子工程，创建新的目录作用域并构建其中的 target；`include` 只是把另一个 CMake 文件内容包含进当前作用域，不创建子工程。

**Q11：静态库的依赖为什么要特别关注？**

静态库不自己链接，最终可执行文件链接时仍需要它依赖的库。CMake 会记录链接依赖，但 `PRIVATE` 的头文件路径和宏不会传给使用者；如果公共头文件暴露了依赖，应该用 `PUBLIC`。

**Q12：为什么 file(GLOB) 不推荐？**

新增源文件不会自动触发 CMake 重新配置，容易出现文件没被编译的问题。显式列出源文件更可靠。

**Q13：install 和 export 是做什么的？**

`install` 定义安装规则，把可执行文件、库、头文件复制到安装前缀；`export` 生成供其他 CMake 工程使用的 targets 文件，使别人可以 `find_package` 你的库并链接到 imported target。

**Q14：CMake 工程怎么排查构建问题？**

`cmake --build build --verbose` 看实际命令；`message(STATUS ...)` 打印变量；`cmake --trace-expand` 跟踪执行；`cmake -LAH -N build` 查看缓存变量；配置错误优先怀疑缓存和依赖路径。

**Q15：交叉编译怎么做？**

用 toolchain 文件指定目标平台编译器、sysroot、查找根路径：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchains/arm.cmake
```

---

## 12. 对照本工程

| `CMakeLists.txt` 写法 | 对应知识点 |
|---|---|
| `project(v4l2_camera C)` | 只启用 C 语言 |
| `CMAKE_C_STANDARD 11` | C 标准设置 |
| `CMAKE_BUILD_TYPE` 默认 Release | 单配置生成器的构建类型 |
| `add_compile_options(-Wall -Wextra)` | 全局警告选项 |
| `pkg_check_modules(SDL2 REQUIRED sdl2)` | pkg-config 查找依赖 |
| `add_executable(v4l2_probe ...)` | 独立可执行目标，不链接 SDL |
| `add_executable(v4l2_display ...)` | 多源文件目标 |
| `target_include_directories(v4l2_display PRIVATE src ...)` | 目标级头文件路径 |
| `target_link_libraries(... SDL2 m)` | 链接 SDL2 和数学库 |
| `set_source_files_properties(... COMPILE_OPTIONS "-w")` | 只给第三方源文件关警告 |
| `CMAKE_EXPORT_COMPILE_COMMANDS ON` | 生成 clangd 用的编译数据库 |

本工程没有测试、安装规则和 CI；构建是否成功、警告是否干净，就是最低验证标准。
