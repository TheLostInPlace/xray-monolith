// external_common.h : shared per-material constants + helpers for external (glTF/GLB) model shaders.
// All are set per-object in FExternalVisual::Render (RCache.set_c by name). Defaults are written every
// draw, so a shader that includes this and uses ext_uv()/the factors is always given sane values.

#ifndef EXTERNAL_COMMON_H
#define EXTERNAL_COMMON_H

float4 ext_base_color;    // rgb = glTF baseColorFactor (albedo/F0 tint). Render writes (1,1,1,1) default.
float4 ext_mr_factor;     // x = metallicFactor, y = roughnessFactor, z = normalScale. Default (1,1,1,_).
float4 ext_uv_transform;  // KHR_texture_transform: xy = scale, zw = offset. Default (1,1,0,0) = identity.

// Apply KHR_texture_transform (offset + scale; rotation not handled in v1) to a UV before sampling.
float2 ext_uv( float2 uv )
{
	return uv * ext_uv_transform.xy + ext_uv_transform.zw;
}

// How much of a metal's diffuse to actually suppress (1 = pure PBR, no diffuse; 0 = keep all diffuse).
// Pure-PBR metals reflect only the environment (our sky cube), so in a dark scene lit only by the
// flashlight they go black -- a dynamic light can't diffuse-light a diffuse-less surface. Keeping a
// fraction of diffuse lets the flashlight reveal metals. Trades mirror-purity for visibility, which is
// the right call for a dark, flashlight-driven game. Tune here (lower = more visible / less mirror).
#define EXT_METAL_DIFFUSE_KILL 0.6h

#endif
