/*
 * stb_image_impl.c —— stb_image 单文件库的"实现编译单元"
 *
 * stb_image 只有一个头文件：函数声明和实现都写在 stb_image.h 里。
 * 惯例做法：只让一个 .c 文件来编译它的实现（本文件），
 * 其他文件正常 #include "stb_image.h" 用声明即可。
 *
 * 两个裁剪开关（减小编译体积、加快编译）：
 *   STBI_NO_STDIO  —— 不需要从文件加载（我们只从内存解码）
 *   STBI_ONLY_JPEG —— 只保留 JPEG 解码（MJPG 就是 JPEG，其他格式全裁掉）
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#include "stb_image.h"
