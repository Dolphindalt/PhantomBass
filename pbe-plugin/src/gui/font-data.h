#pragma once

#include <stddef.h>

// TrueType font embedded at build time (see cmake/EmbedFile.cmake).
extern const unsigned char pbe_font_data[];
extern const size_t pbe_font_data_size;
