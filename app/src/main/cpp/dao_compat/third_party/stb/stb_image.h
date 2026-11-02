// The two zlib decoders of stb_image that DAORecomp's ERF reader (ui/erf.cpp) calls,
// over the platform's zlib here (dao_convert_android.cc) - PLAN 10.13.

#pragma once

// A zlib stream (with its header) into `buffer`; the bytes written, or -1.
int stbi_zlib_decode_buffer(char* buffer, int len, const char* ibuffer, int ilen);
// A raw deflate stream (no header) into `buffer`; the bytes written, or -1.
int stbi_zlib_decode_noheader_buffer(char* buffer, int len, const char* ibuffer, int ilen);
