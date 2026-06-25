// OGFImporter.cpp : native OGF/OMF decode behind the IModelImporter seam. See header.
//////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#pragma hdrstop

#include "OGFImporter.h"
#include "ModelPool.h"
#include "../../xrEngine/fmesh.h" // ogf_header / OGF_HEADER
#include "fbasicvisual.h"         // dxRender_Visual

dxRender_Visual* OGFImporter::Import(IReader* data, const char* name, bool /*assert*/)
{
	// Verbatim OGF decode lifted out of CModelPool::Instance_Load -- read the OGF header, construct the
	// concrete visual for its model type, and run the type's own loader. Byte-for-byte the same path OGF
	// content has always taken; the caller still does file I/O, RegisterModel and pool registration.
	ogf_header H;
	data->r_chunk_safe(OGF_HEADER, &H, sizeof(H));
	dxRender_Visual* V = m_pool.Instance_Create(H.type);
	V->Load(name, data, 0);
	return V;
}
