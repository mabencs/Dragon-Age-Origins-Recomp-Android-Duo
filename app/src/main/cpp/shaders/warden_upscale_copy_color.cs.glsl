// PLAN 9.4: the image before the UI, from the render target the last full-screen pass
// reading the scene drew into - see warden_upscale_copy.glsli.
#version 460
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

#define WARDEN_COPY_FORMAT rgba8
#define WARDEN_COPY_VALUE vec4(texel.rgb, 1.0)
#include "warden_upscale_copy.glsli"
