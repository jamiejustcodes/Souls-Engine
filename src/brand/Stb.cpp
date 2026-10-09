// PNG decoding is a cold startup dependency, compiled under its upstream policy.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>
