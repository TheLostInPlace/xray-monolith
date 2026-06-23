// cgltf_impl.cpp : the single translation unit that compiles the cgltf implementation.
//
// This file intentionally does NOT include the engine precompiled header (stdafx.h):
// it must be set to "Not Using Precompiled Headers" in every render project that
// builds it (xrRender_R1/R2/R3/R4). cgltf is a self-contained single-header library
// and pulls in only <stdio.h>/<stdlib.h>/<string.h>/<float.h> via its implementation.
//
// See docs/GLTF_GLB_Integration_Research.md (Section 8) and cgltf.h.

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
