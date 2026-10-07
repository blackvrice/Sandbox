// stb 구현을 이 번역 단위 하나에만 넣는다 (external/stb). 우리 경고 정책 밖 — sbx_warnings 를 걸지 않는다 (CMake).
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define STBI_ONLY_PNG
#define STBI_NO_STDIO // 파일은 foundation/io 로 읽는다 (Windows 한글 경로)
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>
