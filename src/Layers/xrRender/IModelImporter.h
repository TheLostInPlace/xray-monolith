// IModelImporter.h : format-agnostic model-import seam.
//
// Both the native OGF/OMF path and the external (glTF/GLB) path converge on this interface: each
// importer decodes an already-open stream into the engine's NATIVE in-memory visual (a
// dxRender_Visual, which may be a CKinematics for skeletal content). CModelPool::Instance_Load owns
// file resolution, format routing and pool/registration; an importer owns only the decode. Everything
// downstream (renderer, animator, physics, attach system, SSFX) keeps consuming today's structures and
// never learns the source format.
//
//////////////////////////////////////////////////////////////////////
#pragma once

class dxRender_Visual;
class IReader;

class IModelImporter
{
public:
	// Decode `data` (an already-open stream, positioned at its start) into a native visual.
	// `name`   logical visual name -- passed to dxRender_Visual::Load and used for diagnostics.
	// `assert` mirrors the model-pool flag (fatal vs. soft-fail on an unloadable asset).
	// Returns the loaded visual, or nullptr on a soft failure.
	virtual dxRender_Visual* Import(IReader* data, const char* name, bool assert) = 0;
	virtual ~IModelImporter() {}
};
