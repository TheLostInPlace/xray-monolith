// FExternalKinematics.h : kinematic wrapper for an external (GLTF/GLB) static mesh.
//
// Spawnable game objects in X-Ray (physic_object, items, NPCs) require a KINEMATIC visual --
// the collision/physics/object pipeline calls dcast_PKinematics()/ObjectKinematics() and walks
// bones. A pure static FExternalVisual returns null kinematics and cannot be spawned. This class
// wraps the mesh in a minimal 1-bone CKinematics:
//   * the single bone carries a box collision shape built from the mesh bbox (-> box physics),
//   * the geometry is a child FExternalVisual (rendered by the normal skeleton-children path).
// It reports Type = MT_SKELETON_RIGID so the engine treats it as a rigid skeleton (physics,
// collision, child rendering, game-material registration). Built by LoadExternal, never by OGF
// Load. See docs/GLTF_GLB_Integration_Research.md Section 11.
//
//////////////////////////////////////////////////////////////////////
#ifndef FExternalKinematicsH
#define FExternalKinematicsH
#pragma once

#include "SkeletonCustom.h"

class FExternalKinematics : public CKinematics
{
public:
	// OGF entry point -- never used (built via LoadExternal); present only to guard the vtable.
	virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);

	// Parse the GLTF/GLB at `full_path`, build the geometry child + the 1-bone skeleton.
	bool LoadExternal(const char* short_name, const char* full_path);

	FExternalKinematics();
	virtual ~FExternalKinematics();
};

#endif // FExternalKinematicsH
