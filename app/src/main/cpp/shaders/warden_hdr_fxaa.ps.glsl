// PLAN 9.2: the FXAA pass's HDR10 variant - warden_fxaa.glsli, its result encoded by
// warden_hdr_output.glsli. Its own main is renamed and called first, so the pass itself
// is the same code as the SDR one.
#version 460
#extension GL_GOOGLE_include_directive : require

#define main warden_sdr_main
#include "warden_fxaa.glsli"
#undef main

#include "warden_hdr_output.glsli"

void main() {
  warden_sdr_main();
  xe_fxaa_color.rgb = WardenHdrEncode(xe_fxaa_color.rgb);
}
