#include "stdafx.h"
#pragma hdrstop

#include "../xrEngine/xrLevel.h"

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_Raytrace.h"

#include "NotificationClient.h"

#include <AL/efx.h>

float psSpeedOfSound = 1.f;
int psSoundTargets = 1024;
Flags32 psSoundFlags = {ss_Hardware | ss_EFX};
Flags32 psSoundRaytraceFlags = {0};
float psSoundOcclusionScale = 0.5f;
float psSoundCull = 0.01f;
float psSoundRolloff = 0.75f;
u32 psSoundModel = 0;
float psSoundVEffects = 1.0f;
float psSoundVFactor = 1.0f;
float psSoundMaxDistanceMultiplier = 1.0f;
float psSoundMaxAIDistanceMultiplier = 1.0f;

float psSoundVMusic = 1.f;
float psSoundVMusicFactor = 1.f;
float psSoundVCategory[sc_count] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
int psSoundCacheSizeMB = 256;

float snd_efx_environment_change_time = 1.66f;

CSoundRender_Core* SoundRender = nullptr;
CSound_manager_interface* Sound = nullptr;

CSoundRender_Core::CSoundRender_Core()
{
	bPresent = FALSE;
	bUserEnvironment = FALSE;
	geom_MODEL = NULL;
	geom_ENV = NULL;
	geom_SOM = NULL;
	s_environment = NULL;
	Handler = NULL;
	s_targets_pu = 0;
	s_emitters_u = 0;
	e_current.set_identity();
	e_identity.set_identity();
	e_target_ptr = &e_identity;
	raytrace = nullptr;
	rt_reverb = reverbs[0];
	rt_reverb_active = false;
	rt_reverb_step_time = 0.f;
	bListenerMoved = FALSE;
	bReady = FALSE;
	bLocked = FALSE;
	fTimer_Value = Timer.GetElapsed_sec();
	fTimer_Delta = 0.0f;
	m_iPauseCounter = 1;
	pSysNotification = xr_new<CNotificationClient>();
}

CSoundRender_Core::~CSoundRender_Core()
{
#ifdef _EDITOR
	ETOOLS::destroy_model		(geom_ENV);
	ETOOLS::destroy_model		(geom_SOM);
#else
	xr_delete(geom_ENV);
	xr_delete(geom_SOM);
	xr_delete(pSysNotification);
#endif
}

void CSoundRender_Core::_initialize(int stage)
{
	Timer.Start();

	// load environment
	env_load();
	i_load_file_lists();

	bPresent = TRUE;

	// Cache
	cache_bytes_per_line = (sdef_target_block / 8) * 276400 / 1000;
	cache.initialize(psSoundCacheSizeMB * 1024, cache_bytes_per_line);

	bReady = TRUE;

	if (Core.ParamsData.test(ECoreParams::prefetch_sounds))
	{
		i_create_all_sources();
	}
}

extern xr_vector<u8> g_target_temp_data;
extern xr_vector<u8> g_target_temp_data_16;

void CSoundRender_Core::_clear()
{
	bReady = FALSE;
	cache.destroy();
	env_unload();

	// remove sources
	for (auto& kv : s_sources)
		xr_delete(kv.second);

	s_sources.clear();

	// remove emmiters
	for (u32 eit = 0; eit < s_emitters.size(); eit++)
		xr_delete(s_emitters[eit]);
	s_emitters.clear();

	g_target_temp_data.clear();
	g_target_temp_data_16.clear();

	xr_delete(raytrace);
}

void CSoundRender_Core::stop_emitters()
{
	for (u32 eit = 0; eit < s_emitters.size(); eit++)
		s_emitters[eit]->stop(FALSE);
}

void CSoundRender_Core::restart_emitters()
{
	for (u32 eit = 0; eit < s_emitters.size(); eit++)
		if (s_emitters[eit]->target)
			i_start(s_emitters[eit]);
}

int CSoundRender_Core::pause_emitters(bool val)
{
	m_iPauseCounter += val ? +1 : -1;
	VERIFY(m_iPauseCounter>=0);

	for (u32 it = 0; it < s_emitters.size(); it++)
		((CSoundRender_Emitter*)s_emitters[it])->pause(val, val ? m_iPauseCounter : m_iPauseCounter + 1);

	return m_iPauseCounter;
}

void CSoundRender_Core::env_load()
{
	// Load environment
	string_path fn;
	if (FS.exist(fn, "$game_data$",SNDENV_FILENAME))
	{
		s_environment = xr_new<SoundEnvironment_LIB>();
		s_environment->Load(fn);
	}

	// Load geometry

	// Assosiate geometry
}

void CSoundRender_Core::env_unload()
{
	// Unload 
	if (s_environment)
		s_environment->Unload();
	xr_delete(s_environment);

	// Unload geometry
}

void CSoundRender_Core::_restart()
{
	cache.destroy();
	cache.initialize(psSoundCacheSizeMB * 1024, cache_bytes_per_line);
	env_apply();
}

void CSoundRender_Core::set_handler(sound_event* E)
{
	Handler = E;
}

void CSoundRender_Core::set_geometry_occ(CDB::MODEL* M)
{
	geom_MODEL = M;
}

void CSoundRender_Core::set_geometry_acoustics(CDB::MODEL* M, const CSound_material_desc* materials, u32 count)
{
	VERIFY(!bLocked);
	if (raytrace)
		raytrace->destroy();
	if (!M || !psSoundFlags.test(ss_Raytrace))
		return;

	if (!bPresent || !m_is_supported)
	{
		Msg("! SOUND: Raytrace: needs EFX on a live device, raytracing stays off");
		return;
	}
	if (!raytrace)
		raytrace = xr_new<CSoundRender_Raytrace>();
	raytrace->create(M, materials, count);
}

void CSoundRender_Core::wait_geometry_acoustics()
{
	if (raytrace)
		raytrace->wait_create();
}

bool CSoundRender_Core::occluder_add(const void* owner, const CSound_occluder_shape* shapes, u32 count)
{
	return raytrace && raytrace->occluder_add(owner, shapes, count);
}

void CSoundRender_Core::occluder_move(const void* owner, const Fmatrix* poses, u32 count)
{
	if (raytrace)
		raytrace->occluder_move(owner, poses, count);
}

u32 CSoundRender_Core::query_path(const Fvector& ear, const Fvector& source, const void* skip)
{
	return raytrace ? raytrace->post_query(false, ear, source, skip) : 0;
}

u32 CSoundRender_Core::query_room(const Fvector& point)
{
	return raytrace ? raytrace->post_query(true, point, point, nullptr) : 0;
}

int CSoundRender_Core::query_result(u32 ticket, CSound_query& out) const
{
	return raytrace ? raytrace->query_result(ticket, out) : -1;
}

bool CSoundRender_Core::raytracing() const
{
	return raytrace && raytrace->running();
}

bool CSoundRender_Core::raytrace_rays(xr_vector<CSound_ray_segment>& segments, u32& bounces)
{
	return raytrace && raytrace->visualisation(segments, bounces);
}

bool CSoundRender_Core::get_listener_acoustics(CSound_acoustics& out) const
{
	return raytrace && raytrace->read_acoustics(out);
}

bool CSoundRender_Core::get_listener_reverb(CSound_reverb& out) const
{
	return raytrace && raytrace->read_reverb(out);
}

bool CSoundRender_Core::get_object_muffling(const CObject* O, CSound_muffling& out) const
{
	return raytrace && raytrace->read_muffling(O, out);
}

bool CSoundRender_Core::get_sources(xr_vector<CSound_source_info>& out) const
{
	return raytrace && raytrace->read_sources(out);
}

bool CSoundRender_Core::get_rays(CSound_rays& out) const
{
	return raytrace && raytrace->read_rays(out);
}

bool CSoundRender_Core::set_rays(int family, int count)
{
	return raytrace && raytrace->request_rays(family, count);
}

void CSoundRender_Core::hold_acoustics(bool hold)
{
	if (raytrace)
		raytrace->hold(hold);
}

bool CSoundRender_Core::get_muffling(CSoundRender_Emitter* E, float& lf, float& hf)
{
	return raytrace && raytrace->muffle_gains(E, lf, hf);
}

void CSoundRender_Core::release_muffling(CSoundRender_Emitter* E)
{
	raytrace->release_source(E);
}

void CSoundRender_Core::cull_muffling(CSoundRender_Emitter* E)
{
	raytrace->range_release(E);
}

bool CSoundRender_Core::get_ambience(CSoundRender_Emitter* E, float& lf, float& hf)
{
	return raytrace && raytrace->ambient_gains(E, lf, hf);
}

bool CSoundRender_Core::hold_ambience()
{
	return raytrace && raytrace->ambient_held();
}

void CSoundRender_Core::set_geometry_som(IReader* I)
{
#ifdef _EDITOR
	ETOOLS::destroy_model	(geom_SOM);
#else
	xr_delete(geom_SOM);
#endif
	if (0 == I) return;

	// check version
	R_ASSERT(I->find_chunk(0));
	u32 version = I->r_u32();
	VERIFY2(version==0, "Invalid SOM version");
	// load geometry	
	IReader* geom = I->open_chunk(1);
	VERIFY2(geom, "Corrupted SOM file");
	// Load tris and merge them
	struct SOM_poly
	{
		Fvector3 v1;
		Fvector3 v2;
		Fvector3 v3;
		u32 b2sided;
		float occ;
	};
	// Create AABB-tree
#ifdef _EDITOR
	CDB::Collector*	CL			= ETOOLS::create_collector();
	while (!geom->eof()){
		SOM_poly				P;
		geom->r					(&P,sizeof(P));
        ETOOLS::collector_add_face_pd		(CL,P.v1,P.v2,P.v3,*(u32*)&P.occ,0.01f);
		if (P.b2sided)
			ETOOLS::collector_add_face_pd	(CL,P.v3,P.v2,P.v1,*(u32*)&P.occ,0.01f);
	}
	geom_SOM					= ETOOLS::create_model_cl(CL);
    ETOOLS::destroy_collector	(CL);
#else
	CDB::Collector CL;
	while (!geom->eof())
	{
		SOM_poly P;
		geom->r(&P, sizeof(P));
		CL.add_face_packed_D(P.v1, P.v2, P.v3, *(u32*)&P.occ, 0.01f);
		if (P.b2sided)
			CL.add_face_packed_D(P.v3, P.v2, P.v1, *(u32*)&P.occ, 0.01f);
	}
	geom_SOM = xr_new<CDB::MODEL>();
	geom_SOM->build(CL.getV(), int(CL.getVS()), CL.getT(), int(CL.getTS()));
#endif

	geom->close();
}

void CSoundRender_Core::set_geometry_env(IReader* I)
{
#ifdef _EDITOR
	ETOOLS::destroy_model	(geom_ENV);
#else
	xr_delete(geom_ENV);
#endif
	if (0 == I) return;
	if (0 == s_environment) return;

	// Assosiate names
	xr_vector<u16> ids;
	IReader* names = I->open_chunk(0);
	while (!names->eof())
	{
		string256 n;
		names->r_stringZ(n, sizeof(n));
		int id = s_environment->GetID(n);
		R_ASSERT(id>=0);
		ids.push_back(u16(id));
	}
	names->close();

	// Load geometry
	IReader* geom_ch = I->open_chunk(1);

	u8* _data = (u8*)xr_malloc(geom_ch->length());

	Memory.mem_copy(_data, geom_ch->pointer(), geom_ch->length());

	IReader* geom = xr_new<IReader>(_data, geom_ch->length(), 0);

	hdrCFORM H;
	geom->r(&H, sizeof(hdrCFORM));
	Fvector* verts = (Fvector*)geom->pointer();
	CDB::TRI* tris = (CDB::TRI*)(verts + H.vertcount);
	for (u32 it = 0; it < H.facecount; it++)
	{
		CDB::TRI* T = tris + it;
		u16 id_front = (u16)((T->dummy & 0x0000ffff) >> 0); //	front face
		u16 id_back = (u16)((T->dummy & 0xffff0000) >> 16); //	back face
		R_ASSERT(id_front<(u16)ids.size());
		R_ASSERT(id_back<(u16)ids.size());
		T->dummy = u32(ids[id_back] << 16) | u32(ids[id_front]);
	}
#ifdef _EDITOR
	geom_ENV			= ETOOLS::create_model(verts, H.vertcount, tris, H.facecount);
	env_apply			();
#else
	geom_ENV = xr_new<CDB::MODEL>();
	geom_ENV->build(verts, H.vertcount, tris, H.facecount);
#endif
	geom_ch->close();
	geom->close();
	xr_free(_data);
}

void CSoundRender_Core::create(ref_sound& S, const char* fName, esound_type sound_type, int game_type)
{
	if (!bPresent) return;
	S._p = xr_new<ref_sound_data>(fName, sound_type, game_type);
}

void CSoundRender_Core::attach_tail(ref_sound& S, const char* fName)
{
	if (!bPresent) return;
	string_path fn;
	xr_strcpy(fn, fName);
	if (strext(fn)) *strext(fn) = 0;
	if (S._p->fn_attached[0].size() && S._p->fn_attached[1].size())
	{
#ifdef DEBUG
		Msg("! 2 file already in queue [%s][%s]",S._p->fn_attached[0].c_str(),S._p->fn_attached[1].c_str());
#endif // #ifdef DEBUG
		return;
	}

	u32 idx = S._p->fn_attached[0].size() ? 1 : 0;

	S._p->fn_attached[idx] = fn;

	CSoundRender_Source* s = SoundRender->i_create_source(fn);
	S._p->dwBytesTotal += s->bytes_total();
	S._p->fTimeTotal += s->length_sec();
	if (S._feedback())
		((CSoundRender_Emitter*)S._feedback())->fTimeToStop += s->length_sec();

	SoundRender->i_destroy_source(s);
}

void CSoundRender_Core::clone(ref_sound& S, const ref_sound& from, esound_type sound_type, int game_type)
{
	if (!bPresent) return;
	S._p = xr_new<ref_sound_data>();
	S._p->handle = from._p->handle;
	S._p->dwBytesTotal = from._p->dwBytesTotal;
	S._p->fTimeTotal = from._p->fTimeTotal;
	S._p->fn_attached[0] = from._p->fn_attached[0];
	S._p->fn_attached[1] = from._p->fn_attached[1];
	S._p->g_type = (game_type == sg_SourceType) ? S._p->handle->game_type() : game_type;
	S._p->s_type = sound_type;
	S._p->s_category = from._p->s_category;
}


void CSoundRender_Core::play(ref_sound& S, CObject* O, u32 flags, float delay)
{
	if (!bPresent || (0==S._handle())) return;
	S._p->g_object = O;
	if (S._feedback()) ((CSoundRender_Emitter*)S._feedback())->rewind();
	else i_play(&S, flags & sm_Looped, delay);

	if ((flags & sm_2D) || (S._handle()->channels_num() == 2))
		S._feedback()->switch_to_2D();

	if (flags & sm_Intro)
	{
		S._feedback()->switch_to_Intro();
	}
	// A file named in the bed list plays as a bed whatever the caller's flags
	((CSoundRender_Emitter*)S._feedback())->bBed = !!(flags & sm_Bed) || ((CSoundRender_Source*)S._handle())->m_bBed;
	((CSoundRender_Emitter*)S._feedback())->bLayer = !!(flags & sm_Layer);
}

void CSoundRender_Core::play_no_feedback(ref_sound& S, CObject* O, u32 flags, float delay, Fvector* pos, float* vol, float* freq, Fvector2* range)
{
	if (!bPresent || (0 == S._handle())) return;
	ref_sound_data_ptr orig = S._p;
	S._p = xr_new<ref_sound_data>();
	S._p->handle = orig->handle;
	S._p->g_type = orig->g_type;
	S._p->s_category = orig->s_category;
	S._p->g_object = O;
	S._p->dwBytesTotal = orig->dwBytesTotal;
	S._p->fTimeTotal = orig->fTimeTotal;
	S._p->fn_attached[0] = orig->fn_attached[0];
	S._p->fn_attached[1] = orig->fn_attached[1];

	i_play(&S, flags & sm_Looped, delay);

	if (flags & sm_2D || S._handle()->channels_num() == 2)
		S._feedback()->switch_to_2D();

	if (flags & sm_Intro)
	{
		S._feedback()->switch_to_Intro();
	}

	((CSoundRender_Emitter*)S._feedback())->bLayer = !!(flags & sm_Layer);
	if (pos) S._feedback()->set_position(*pos);
	if (freq) S._feedback()->set_frequency(*freq);
	if (range) S._feedback()->set_range((*range)[0], (*range)[1]);
	if (vol) S._feedback()->set_volume(*vol);
	S._p = orig;
}

void CSoundRender_Core::play_at_pos(ref_sound& S, CObject* O, const Fvector &pos, u32 flags, float delay)
{
	if (!bPresent || (0 == S._handle())) return;
	S._p->g_object = O;
	if (S._feedback()) ((CSoundRender_Emitter*)S._feedback())->rewind();
	else i_play(&S, flags & sm_Looped, delay);

	S._feedback()->set_position(pos);
	if ((flags & sm_2D) || (S._handle()->channels_num() == 2))
		S._feedback()->switch_to_2D();

	if (flags & sm_Intro)
	{
		S._feedback()->switch_to_Intro();
	}
	((CSoundRender_Emitter*)S._feedback())->bBed = !!(flags & sm_Bed) || ((CSoundRender_Source*)S._handle())->m_bBed;
	((CSoundRender_Emitter*)S._feedback())->bLayer = !!(flags & sm_Layer);
}

void CSoundRender_Core::destroy(ref_sound& S)
{
	// A Lua finalizer destroys sounds on the parallel GC task while the update runs
	xrCriticalSectionGuard guard(update_lock);
	if (S._feedback())
	{
		CSoundRender_Emitter* E = (CSoundRender_Emitter*)S._feedback();
		E->stop(FALSE);
	}
	S._p = 0;
}

void CSoundRender_Core::_create_data(ref_sound_data& S, LPCSTR fName, esound_type sound_type, int game_type)
{
	string_path fn;
	xr_strcpy(fn, fName);
	if (strext(fn)) *strext(fn) = 0;
	S.handle = (CSound_source*)SoundRender->i_create_source(fn);
	S.g_type = (game_type == sg_SourceType) ? S.handle->game_type() : game_type;
	S.s_type = sound_type;
	S.s_category = sc_None;
	S.feedback = 0;
	S.g_object = 0;
	S.g_userdata = 0;
	S.dwBytesTotal = S.handle->bytes_total();
	S.fTimeTotal = S.handle->length_sec();
}

void CSoundRender_Core::_destroy_data(ref_sound_data& S)
{
	if (S.feedback)
	{
		CSoundRender_Emitter* E = (CSoundRender_Emitter*)S.feedback;
		E->stop(FALSE);
	}
	R_ASSERT(0==S.feedback);
	SoundRender->i_destroy_source((CSoundRender_Source*)S.handle);

	S.handle = NULL;
}

CSoundRender_Environment* CSoundRender_Core::get_environment(const Fvector& P)
{
	PROF_EVENT();
	static CSoundRender_Environment identity;

	if (bUserEnvironment)
	{
		return &s_user_environment;
	}
	else
	{
		if (geom_ENV)
		{
			Fvector dir = {0, -1, 0};
#ifdef _EDITOR
			ETOOLS::ray_options		(CDB::OPT_ONLYNEAREST);
			ETOOLS::ray_query		(geom_ENV,P,dir,1000.f);
			if (ETOOLS::r_count()){
				CDB::RESULT*		r	= ETOOLS::r_begin();
#else
			geom_DB.ray_options(CDB::OPT_ONLYNEAREST);
			geom_DB.ray_query(geom_ENV, P, dir, 1000.f);
			if (geom_DB.r_count())
			{
				CDB::RESULT* r = geom_DB.r_begin();
#endif
				CDB::TRI* T = geom_ENV->get_tris() + r->id;
				Fvector* V = geom_ENV->get_verts();
				Fvector tri_norm;
				tri_norm.mknormal(V[T->verts[0]], V[T->verts[1]], V[T->verts[2]]);
				float dot = dir.dotproduct(tri_norm);
				if (dot < 0)
				{
					u16 id_front = (u16)((T->dummy & 0x0000ffff) >> 0); //	front face
					return s_environment->Get(id_front);
				}
				else
				{
					u16 id_back = (u16)((T->dummy & 0xffff0000) >> 16); //	back face
					return s_environment->Get(id_back);
				}
			}
			else
			{
				identity.set_identity();
				return &identity;
			}
		}
		else
		{
			identity.set_identity();
			return &identity;
		}
	}
}

void CSoundRender_Core::env_apply()
{
	bListenerMoved = TRUE;
}

void CSoundRender_Core::update_listener(const Fvector& P, const Fvector& D, const Fvector& N, float dt)
{
}

void CSoundRender_Core::object_relcase(CObject* obj)
{
	if (obj) {
		for (u32 eit = 0; eit < s_emitters.size(); eit++) {
			if (s_emitters[eit] && s_emitters[eit]->owner_data && (obj == s_emitters[eit]->owner_data->g_object))
				s_emitters[eit]->owner_data->g_object = nullptr;
		}
		if (raytrace)
			raytrace->object_released(obj);
	}
}

#ifdef _EDITOR
void CSoundRender_Core::set_user_env(CSound_environment* E)
{
	if ((0 == E) && !bUserEnvironment) return;

	if (E)
	{
		s_user_environment	= *((CSoundRender_Environment*)E);
		bUserEnvironment	= TRUE;
	}
	else 
	{
		bUserEnvironment	= FALSE;
	}
	env_apply			();
}

void CSoundRender_Core::refresh_env_library()
{
	env_unload			();
	env_load			();
	env_apply			();
}
void CSoundRender_Core::refresh_sources()
{
	for (u32 eit=0; eit<s_emitters.size(); eit++)
    	s_emitters[eit]->stop(FALSE);
	for (const auto& kv : s_sources)
	{
		CSoundRender_Source* s = kv.second;
    	s->unload		();
		s->load			(*s->fname);
    }
}
void CSoundRender_Core::set_environment_size	(CSound_environment* src_env, CSound_environment** dst_env)
{
	
}
void CSoundRender_Core::set_environment	(u32 id, CSound_environment** dst_env)
{
	
}
#endif
