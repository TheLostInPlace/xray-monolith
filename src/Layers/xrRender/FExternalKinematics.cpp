// FExternalKinematics.cpp : 1-bone kinematic wrapper around an external static mesh.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "../../xrEngine/fmesh.h"
#include "FExternalKinematics.h"
#include "FExternalVisual.h"

FExternalKinematics::FExternalKinematics() : CKinematics()
{
	// CKinematics() does NOT null these -- the dtor/Release would otherwise touch garbage.
	bones = nullptr;
	bone_map_N = nullptr;
	bone_map_P = nullptr;
	bone_instances = nullptr;
	pUserData = nullptr;
	m_lod = nullptr;
	bones_size = 0;
}

FExternalKinematics::~FExternalKinematics()
{
	// bones/bone_map are shared data freed by CKinematics::Release (called by the model pool on
	// the base model). The dtor (base ~CKinematics) frees the per-instance bone_instances. Nothing
	// extra to do here.
}

void FExternalKinematics::Load(LPCSTR N, IReader* /*data*/, u32 /*dwFlags*/)
{
	// External kinematics are built by LoadExternal(), never through the OGF path.
	Msg("! FExternalKinematics::Load() called unexpectedly for [%s] - built via LoadExternal()", N);
}

bool FExternalKinematics::LoadExternal(const char* short_name, const char* full_path)
{
	dbg_name = short_name;
	dbg_id = 1;
	skinning = -1;
	hud = false;

	// --- geometry child: the actual mesh (renders as MT_EXTERNAL_STATIC) -------------
	FExternalVisual* geom = xr_new<FExternalVisual>();
	geom->Type = MT_EXTERNAL_STATIC;
	if (!geom->LoadExternal(short_name, full_path))
	{
		xr_delete(geom); // FExternalVisual dtor releases any GPU buffers it created
		return false;    // bones not yet allocated -> object is dtor-safe for the caller
	}
	children.push_back(geom);

	// bounds for this (parent) visual = the geometry's bounds
	vis = geom->getVisData();

	// --- 1-bone skeleton ------------------------------------------------------------
	bone_map_N = xr_new<accel>();
	bone_map_P = xr_new<accel>();
	bones = xr_new<vecBones>();
	bone_instances = nullptr;
	bones_size = 0;
	hidden_bones.zero();
	visimask.zero();

	CBoneData* B = CreateBoneData(0);
	B->name = shared_str("$external_root$");
	B->child_faces.resize(children.size());
	bones->push_back(B);
	bone_map_N->push_back(mk_pair(B->name, u16(0)));
	bone_map_P->push_back(mk_pair(B->name, u16(0)));
	visimask.set(u64(1) << 0, TRUE); // bone 0 visible (CalculateBones VERIFYs at least one visible)

	iRoot = 0;
	B->SetParentID(BI_NONE);

	// model-space bbox -> center + halfsize (clamped non-zero for physics / visibility)
	const Fbox& bb = vis.box;
	Fvector c, h;
	c.add(bb.min, bb.max).mul(0.5f);
	h.sub(bb.max, bb.min).mul(0.5f);
	if (h.x < EPS_L) h.x = EPS_L;
	if (h.y < EPS_L) h.y = EPS_L;
	if (h.z < EPS_L) h.z = EPS_L;

	B->bind_transform.identity();
	B->IK_data.Reset(); // jtRigid, no joint
	B->mass = 10.f;
	B->center_of_mass.set(c);
	B->game_mtl_name = "default_object"; // known-dynamic; game_mtl_idx assigned by RegisterModel

	// physics collision shape (axis-aligned box from the bbox)
	B->shape.Reset();
	B->shape.type = SBoneShape::stBox;
	B->shape.flags.zero();
	B->shape.box.m_rotate.identity();
	B->shape.box.m_translate.set(c);
	B->shape.box.m_halfsize.set(h);

	// pick / bbox OBB (same axis-aligned box)
	B->obb.m_rotate.identity();
	B->obb.m_translate.set(c);
	B->obb.m_halfsize.set(h);

	// model->bone conversion matrices (identity bind -> identity m2b)
	(*bones)[LL_GetBoneRoot()]->CalculateM2B(Fidentity);

	// per-instance bone transforms (base model needs them; clones make their own in Copy)
	IBoneInstances_Create();

	Update_Callback = NULL;
	wm_frame = u32(-1);
	LL_Validate();
	bones_size = (u16)bones->size();

	// Report as a rigid skeleton: makes the engine drive physics/collision, render the child via
	// the skeleton-children path, and assign the bone's game material via RegisterModel.
	Type = MT_SKELETON_RIGID;

	CalculateBones_Invalidate();
	return true;
}
