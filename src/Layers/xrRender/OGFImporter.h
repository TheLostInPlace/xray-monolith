// OGFImporter.h : IModelImporter for the native OGF/OMF format.
//
// Wraps the engine's existing OGF decode verbatim (read OGF_HEADER -> CModelPool::Instance_Create by
// model type -> dxRender_Visual::Load). No behavioural change whatsoever: this is the exact code path
// OGF/OMF content has always run, lifted behind the IModelImporter seam so the glTF path can sit
// beside it. File I/O, RegisterModel and pool registration stay with the caller (Instance_Load).
//
//////////////////////////////////////////////////////////////////////
#pragma once

#include "IModelImporter.h"

class CModelPool;

class OGFImporter : public IModelImporter
{
public:
	explicit OGFImporter(CModelPool& pool) : m_pool(pool) {}

	// OGF decode core only: r_chunk_safe(OGF_HEADER) -> Instance_Create(type) -> Load.
	virtual dxRender_Visual* Import(IReader* data, const char* name, bool assert) override;

private:
	CModelPool& m_pool; // the owning pool, for the type -> visual-class factory (Instance_Create)
};
