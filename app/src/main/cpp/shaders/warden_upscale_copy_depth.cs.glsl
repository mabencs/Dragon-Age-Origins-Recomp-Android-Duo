// PLAN 9.4: the scene's depth, a tile of it, from the depth buffer its draws used - see
// warden_upscale_copy.glsli. The source view is the depth aspect.
#version 460
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

#define WARDEN_COPY_FORMAT r32f
#define WARDEN_COPY_VALUE vec4(texel.r, 0.0, 0.0, 0.0)
#include "warden_upscale_copy.glsli"
