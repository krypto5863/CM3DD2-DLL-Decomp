/* Implementation unit for stb_image (public domain / MIT). Only its zlib decoder is
 * used, for compressed archive members; image formats other than PNG are disabled. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_JPEG
#define STBI_NO_BMP
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_LINEAR
#include "../third_party/stb/stb_image.h"
