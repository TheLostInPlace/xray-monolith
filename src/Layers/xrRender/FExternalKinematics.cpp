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

	// --- geometry children: one FExternalVisual per glTF material (multi-material) -----
	// Several materials need several render batches (one shader each), so build one child visual per
	// material, each loading only its material's primitives. The skeleton below stays a single rigid
	// root and all children render under it. A single-material model makes exactly one child
	// (filter -1 = merge everything), identical to the original behaviour.
	xr_vector<FExternalVisual::MatInfo> mats;
	FExternalVisual::GetMaterialIndices(full_path, mats);

	// --- skeleton source: glTF skin (multi-bone) vs none (1-bone rigid) ----------------
	// A skinned glTF builds a real bone hierarchy; a non-skinned one keeps the existing 1-bone rigid
	// root (so every static model loads exactly as before). The engine hard-caps skeletons at 64 bones
	// (visimask/hidden_bones are u64); a larger skin is rejected -> static fallback (Phase A limit).
	FExternalVisual::ExtSkinData skin;
	bool is_skinned = FExternalVisual::GetSkinData(full_path, skin) && skin.valid();
	if (is_skinned && skin.bones.size() > 64)
	{
		Msg("! [gltf] skin '%s' has %u bones (>64) -- rendering static for now", full_path,
			(u32)skin.bones.size());
		is_skinned = false;
	}

	Fbox total;
	total.invalidate();

	// skinned children (kept so we can recompute bounds from the bind pose after the skeleton is built)
	xr_vector<FExternalSkinned*> skinned_kids;

	auto add_child = [&](int material_filter, FExternalVisual::EExtPass pass) -> bool
	{
		FExternalVisual* geom = xr_new<FExternalVisual>();
		geom->Type = MT_EXTERNAL_STATIC;
		if (!geom->LoadExternal(short_name, full_path, material_filter, pass))
		{
			xr_delete(geom); // selected no geometry / no map for this overlay (or failed) -> skip
			return false;
		}
		children.push_back(geom);
		const Fbox& gb = geom->getVisData().box;
		total.modify(gb.min);
		total.modify(gb.max);
		return true;
	};

	// Skinned child (deforms with the skeleton). Phase A builds only the lit batch -- emissive/metal/blend
	// overlays on skinned geometry come later. SetParent wires the child to read our bone matrices.
	auto add_skinned_child = [&](int material_filter) -> bool
	{
		FExternalSkinned* geom = xr_new<FExternalSkinned>();
		geom->Type = MT_EXTERNAL_SKINNED;
		if (!geom->LoadExternal(short_name, full_path, material_filter, skin))
		{
			xr_delete(geom);
			return false;
		}
		geom->SetParent(this); // CSkeletonX::_Render reads LL_GetTransform_R from this kinematics
		children.push_back(geom);
		skinned_kids.push_back(geom);
		const Fbox& gb = geom->getVisData().box;
		total.modify(gb.min);
		total.modify(gb.max);
		return true;
	};

	if (is_skinned)
	{
		// one skinned (lit) child per material, under the multi-bone skeleton built below
		if (mats.size() <= 1)
		{
			if (!add_skinned_child(-1))
				return false;
		}
		else
		{
			for (const FExternalVisual::MatInfo& m : mats)
				add_skinned_child((m.index < 0) ? -2 : m.index);
			if (children.empty())
				return false;
		}
	}
	else if (mats.size() <= 1)
	{
		if (!mats.empty() && mats[0].blend)
		{
			// transparent: a single forward blended child (NO deferred lit child -- can't go in G-buffer)
			if (!add_child(-1, FExternalVisual::ext_blend))
				return false;                             // bones not yet allocated -> object is dtor-safe for the caller
		}
		else
		{
			if (!add_child(-1, FExternalVisual::ext_lit)) // lit batch (merge all)
				return false;
			if (!mats.empty() && mats[0].emissive)
				add_child(-1, FExternalVisual::ext_emissive); // forward additive emissive overlay (same geometry)
			if (!mats.empty() && mats[0].metallic)
				add_child(-1, FExternalVisual::ext_metal);    // forward additive colored-reflection overlay
		}
	}
	else
	{
		// one child per material; map the "no material" group (-1) to the -2 filter so it loads only
		// its own primitives instead of re-merging everything (which -1 means inside LoadExternal).
		// BLEND materials -> one forward blended child; others -> lit child + emissive/metal overlays.
		for (const FExternalVisual::MatInfo& m : mats)
		{
			const int f = (m.index < 0) ? -2 : m.index;
			if (m.blend)
			{
				add_child(f, FExternalVisual::ext_blend); // transparent: forward blended only
			}
			else
			{
				add_child(f, FExternalVisual::ext_lit);
				if (m.emissive)
					add_child(f, FExternalVisual::ext_emissive);
				if (m.metallic)
					add_child(f, FExternalVisual::ext_metal);
			}
		}
		if (children.empty())
			return false;
	}

	// bounds for this (parent) visual = union of all material children
	vis.box.set(total.min, total.max);
	{
		Fvector c, half;
		c.add(total.min, total.max).mul(0.5f);
		half.sub(total.max, total.min).mul(0.5f);
		vis.sphere.set(c, half.magnitude());
	}

	// --- skeleton ---------------------------------------------------------------------
	bone_map_N = xr_new<accel>();
	bone_map_P = xr_new<accel>();
	bones = xr_new<vecBones>();
	bone_instances = nullptr;
	bones_size = 0;
	hidden_bones.zero();
	visimask.zero();

	// model-space bbox -> center + halfsize (clamped non-zero for physics / visibility); used as the
	// collision/OBB box of the root (rigid) or skeleton-root (skinned) bone.
	const Fbox& bb = vis.box;
	Fvector c, h;
	c.add(bb.min, bb.max).mul(0.5f);
	h.sub(bb.max, bb.min).mul(0.5f);
	if (h.x < EPS_L) h.x = EPS_L;
	if (h.y < EPS_L) h.y = EPS_L;
	if (h.z < EPS_L) h.z = EPS_L;

	if (is_skinned)
	{
		// --- multi-bone skeleton from the glTF skin -----------------------------------
		// One CBoneData per joint (kept in skin.joints order so glTF JOINTS_0 indexes bones directly).
		// bind_transform = local bind (X-Ray world = parent_world * bind_transform); CalculateM2B then
		// gives m2b = inverse(world bind), so mRenderTransform = identity at bind pose (undeformed).
		const u16 nb = (u16)skin.bones.size();
		bones->reserve(nb);
		for (u16 i = 0; i < nb; ++i)
		{
			const FExternalVisual::ExtBone& sb = skin.bones[i];
			CBoneData* Bi = CreateBoneData(i);
			Bi->name = sb.name;
			Bi->child_faces.resize(children.size());
			Bi->bind_transform = sb.bind_local;
			Bi->IK_data.Reset(); // jtNone (no breakable -> LL_Validate is a no-op)
			Bi->mass = 1.f;
			Bi->center_of_mass.set(0.f, 0.f, 0.f);
			Bi->game_mtl_name = "default_object";
			Bi->shape.Reset();
			Bi->shape.type = SBoneShape::stNone; // per-bone collision not needed for rendering (Phase A)
			Bi->shape.flags.zero();
			Bi->obb.m_rotate.identity();         // minimal valid OBB at origin (picking/wallmarks)
			Bi->obb.m_translate.set(0.f, 0.f, 0.f);
			Bi->obb.m_halfsize.set(EPS_L, EPS_L, EPS_L);
			bones->push_back(Bi);
			bone_map_N->push_back(mk_pair(Bi->name, u32(i)));
			bone_map_P->push_back(mk_pair(Bi->name, u32(i)));
			visimask.set(u64(1) << i, TRUE);
		}

		// parent links + children vectors (drive the recursive Bone_Calculate; array order irrelevant)
		for (u16 i = 0; i < nb; ++i)
		{
			const int p = skin.bones[i].parent;
			if (p < 0)
				(*bones)[i]->SetParentID(BI_NONE);
			else
			{
				(*bones)[i]->SetParentID((u16)p);
				(*bones)[p]->children.push_back((*bones)[i]);
			}
		}
		iRoot = (u16)skin.root;

		// bone_map must be sorted for LL_BoneID's binary search (by name, then by shared_str pointer)
		std::sort(bone_map_N->begin(), bone_map_N->end(),
			[](const std::pair<shared_str, u32>& A, const std::pair<shared_str, u32>& B)
			{ return xr_strcmp(A.first, B.first) < 0; });
		std::sort(bone_map_P->begin(), bone_map_P->end(),
			[](const std::pair<shared_str, u32>& A, const std::pair<shared_str, u32>& B)
			{ return A.first._get() < B.first._get(); });

		// give the root bone the model bbox collision shape (physics/pick) so the object behaves like
		// the 1-bone rigid case for spawning; child bones stay shapeless.
		CBoneData* Broot = (*bones)[iRoot];
		Broot->mass = 10.f;
		Broot->center_of_mass.set(c);
		Broot->shape.type = SBoneShape::stBox;
		Broot->shape.box.m_rotate.identity();
		Broot->shape.box.m_translate.set(c);
		Broot->shape.box.m_halfsize.set(h);
		Broot->obb.m_rotate.identity();
		Broot->obb.m_translate.set(c);
		Broot->obb.m_halfsize.set(h);
	}
	else
	{
		// --- 1-bone rigid root (non-skinned: identical to the original static behaviour) ----------
		CBoneData* B = CreateBoneData(0);
		B->name = shared_str("$external_root$");
		B->child_faces.resize(children.size());
		bones->push_back(B);
		bone_map_N->push_back(mk_pair(B->name, u32(0)));
		bone_map_P->push_back(mk_pair(B->name, u32(0)));
		visimask.set(u64(1) << 0, TRUE); // bone 0 visible (CalculateBones VERIFYs at least one visible)

		iRoot = 0;
		B->SetParentID(BI_NONE);

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
	}

	// model->bone conversion matrices
	if (is_skinned)
	{
		// Use the glTF inverseBindMatrices directly as m2b (authoritative; carries any skeleton-root /
		// bind-shape offset). CalculateM2B's hierarchy inverse would drop that offset and mis-orient or
		// deform skins like CesiumMan. mTransform (from bind_transform) still gives the joint world, so
		// mRenderTransform = jointWorld * IBM = the correct glTF skin matrix.
		for (u16 i = 0; i < (u16)skin.bones.size(); ++i)
			(*bones)[i]->m2b_transform = skin.bones[i].inv_bind;
	}
	else
	{
		(*bones)[LL_GetBoneRoot()]->CalculateM2B(Fidentity); // identity bind -> identity m2b (rigid root)
	}

	// per-instance bone transforms (base model needs them; clones make their own in Copy)
	IBoneInstances_Create();

	Update_Callback = NULL;
	wm_frame = u32(-1);
	LL_Validate();
	bones_size = (u16)bones->size();

	// Finalize skinned children now that bones + child_faces exist: AfterLoad sets ChildIDX + Parent and
	// collects per-bone faces (child_faces) for picking. WITHOUT this, ChildIDX stays u16(-1) and the pick
	// path (IK foot collider -> PickBone) reads child_faces[65535] OOB and crashes. Must run after the bone
	// build (LL_GetData / child_faces.resize); static children aren't CSkeletonX so they're skipped.
	if (is_skinned)
	{
		// LL_GetChild does the correct CSkeletonX cross-cast (null for non-skeletal children).
		for (u16 ci = 0; ci < (u16)children.size(); ++ci)
			if (CSkeletonX* c = LL_GetChild(ci))
				c->AfterLoad(this, ci);
	}

	// Report as a rigid skeleton: makes the engine drive physics/collision, render the child via
	// the skeleton-children path, and assign the bone's game material via RegisterModel.
	Type = MT_SKELETON_RIGID;

	CalculateBones_Invalidate();

	// Skinned children read bone render-transforms in their first draw; force one bone evaluation now so
	// the bind pose is valid even before the engine's per-frame calc runs.
	if (is_skinned)
	{
		CalculateBones(TRUE);

		// Recompute bounds + the root physics/pick box from the actual SKINNED bind pose. The raw mesh
		// bbox each child measured can be in a different space than the rendered mesh (e.g. Z-up-authored
		// models whose root node stands them up), which would leave the collision box mis-shaped/mis-placed
		// -> the spawned body tips/floats while the visual stands. Skinning the verts fixes the mismatch.
		Fbox tb;
		tb.invalidate();
		for (FExternalSkinned* kid : skinned_kids)
		{
			Fbox cb;
			kid->CalcPoseBBox(cb);
			if (cb.min.x <= cb.max.x) // valid (invalidate() leaves min>max)
			{
				tb.modify(cb.min);
				tb.modify(cb.max);
			}
		}
		if (tb.min.x <= tb.max.x)
		{
			// diagnostic: the tall axis of the bind pose tells us the rendered orientation (Y = upright)
			Msg("* [gltf] skinned bind bbox '%s': X=%.2f Y=%.2f Z=%.2f (tall axis = up; Y means upright)",
				short_name, tb.max.x - tb.min.x, tb.max.y - tb.min.y, tb.max.z - tb.min.z);

			vis.box.set(tb.min, tb.max);
			Fvector bc, bh;
			bc.add(tb.min, tb.max).mul(0.5f);
			bh.sub(tb.max, tb.min).mul(0.5f);
			vis.sphere.set(bc, bh.magnitude());
			if (bh.x < EPS_L) bh.x = EPS_L;
			if (bh.y < EPS_L) bh.y = EPS_L;
			if (bh.z < EPS_L) bh.z = EPS_L;
			CBoneData* Broot = (*bones)[iRoot];
			Broot->center_of_mass.set(bc);
			Broot->shape.box.m_translate.set(bc);
			Broot->shape.box.m_halfsize.set(bh);
			Broot->obb.m_translate.set(bc);
			Broot->obb.m_halfsize.set(bh);
		}
	}

	return true;
}
