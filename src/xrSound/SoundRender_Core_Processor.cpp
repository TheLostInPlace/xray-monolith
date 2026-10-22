#include "stdafx.h"
#pragma hdrstop

#include "cl_intersect.h"
#include "SoundRender_Core.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_TargetA.h"
#include "SoundRender_Source.h"
#include "SoundRender_CoreA.h"
#include "SoundRender_Raytrace.h"

// The effect state set_listener leaves behind, on top of the reverbs[0] seed
static void reverb_from_environment(EFXEAXREVERBPROPERTIES& R, const CSoundRender_Environment& env)
{
	R = reverbs[0];
	R.flDiffusion = env.EnvironmentDiffusion;
	R.flGain = env.Room;
	R.flGainHF = env.RoomHF;
	R.flDecayTime = env.DecayTime;
	R.flDecayHFRatio = env.DecayHFRatio;
	R.flReflectionsGain = env.Reflections;
	R.flReflectionsDelay = env.ReflectionsDelay;
	R.flLateReverbGain = env.Reverb;
	R.flAirAbsorptionGainHF = env.AirAbsorptionHF;
	R.flRoomRolloffFactor = env.RoomRolloffFactor;
	if (env.version == sndenv_ver_extended)
	{
		R.flGainLF = env.RoomLF;
		R.iDecayHFLimit = env.DecayHFLimit;
		R.flEchoTime = env.EchoTime;
		R.flEchoDepth = env.EchoDepth;
		R.flLateReverbDelay = env.ReverbDelay;
		R.flDecayLFRatio = env.DecayLFRatio;
		R.flModulationTime = env.ModulationTime;
		R.flModulationDepth = env.ModulationDepth;
		R.flHFReference = env.HFReference;
		R.flLFReference = env.LFReference;
		R.flDensity = env.Density;
	}
}

// The environment that sends the same effect, so the stock path picks up where the raytracer left it
static void environment_from_reverb(CSoundRender_Environment& env, const EFXEAXREVERBPROPERTIES& R)
{
	env.version = sndenv_ver_extended;
	env.EnvironmentDiffusion = R.flDiffusion;
	env.Room = R.flGain;
	env.RoomHF = R.flGainHF;
	env.RoomLF = R.flGainLF;
	env.DecayTime = R.flDecayTime;
	env.DecayHFRatio = R.flDecayHFRatio;
	env.DecayLFRatio = R.flDecayLFRatio;
	env.DecayHFLimit = R.iDecayHFLimit;
	env.Reflections = R.flReflectionsGain;
	env.ReflectionsDelay = R.flReflectionsDelay;
	env.Reverb = R.flLateReverbGain;
	env.ReverbDelay = R.flLateReverbDelay;
	env.EchoTime = R.flEchoTime;
	env.EchoDepth = R.flEchoDepth;
	env.ModulationTime = R.flModulationTime;
	env.ModulationDepth = R.flModulationDepth;
	env.AirAbsorptionHF = R.flAirAbsorptionGainHF;
	env.HFReference = R.flHFReference;
	env.LFReference = R.flLFReference;
	env.RoomRolloffFactor = R.flRoomRolloffFactor;
	env.Density = R.flDensity;
	env.clamp();
}

// Fields whose change makes OpenAL rebuild the reverb on its second pipeline, air absorption feeds the HF decay under the HF limit
template <typename Move>
static void reverb_rebuild_fields(EFXEAXREVERBPROPERTIES& R, const EFXEAXREVERBPROPERTIES& T, Move move)
{
	move(R.flDensity, T.flDensity);
	move(R.flDiffusion, T.flDiffusion);
	move(R.flDecayTime, T.flDecayTime);
	move(R.flDecayHFRatio, T.flDecayHFRatio);
	move(R.flDecayLFRatio, T.flDecayLFRatio);
	move(R.flModulationTime, T.flModulationTime);
	move(R.flModulationDepth, T.flModulationDepth);
	move(R.flAirAbsorptionGainHF, T.flAirAbsorptionGainHF);
	move(R.flHFReference, T.flHFReference);
	move(R.flLFReference, T.flLFReference);
	R.iDecayHFLimit = T.iDecayHFLimit;
}

// Gains, delays, echo and pans, which OpenAL takes without a rebuild
template <typename Move>
static void reverb_glide_fields(EFXEAXREVERBPROPERTIES& R, const EFXEAXREVERBPROPERTIES& T, Move move)
{
	move(R.flGain, T.flGain);
	move(R.flGainHF, T.flGainHF);
	move(R.flGainLF, T.flGainLF);
	move(R.flReflectionsGain, T.flReflectionsGain);
	move(R.flReflectionsDelay, T.flReflectionsDelay);
	move(R.flLateReverbGain, T.flLateReverbGain);
	move(R.flLateReverbDelay, T.flLateReverbDelay);
	move(R.flEchoTime, T.flEchoTime);
	move(R.flEchoDepth, T.flEchoDepth);
	move(R.flRoomRolloffFactor, T.flRoomRolloffFactor);
	for (int i = 0; i < 3; ++i)
	{
		move(R.flReflectionsPan[i], T.flReflectionsPan[i]);
		move(R.flLateReverbPan[i], T.flLateReverbPan[i]);
	}
}

// The rebuilding fields take the target at once, which changes only with a new raytrace result
static void reverb_step(EFXEAXREVERBPROPERTIES& R, const EFXEAXREVERBPROPERTIES& T, float alpha)
{
	reverb_rebuild_fields(R, T, [](float& a, float b) { a = b; });
	reverb_glide_fields(R, T, [alpha](float& a, float b) { a += (b - a) * alpha; });
}

CSoundRender_Emitter* CSoundRender_Core::i_play(ref_sound* S, BOOL _loop, float delay)
{
	PROF_EVENT("CSoundRender_Core::i_play");
	VERIFY(S->_p->feedback==0);
	CSoundRender_Emitter* E = xr_new<CSoundRender_Emitter>();
	S->_p->feedback = E;
	E->start(S, _loop, delay);
	s_emitters.push_back(E);
	return E;
}

void CSoundRender_Core::update(const Fvector& P, const Fvector& D, const Fvector& N)
{
	PROF_EVENT("CSoundRender_Core::update");
	u32 it;

	if (0 == bReady) return;

	xrCriticalSectionGuard guard(update_lock);

	if (bPendingDeviceListRefresh)
	{
		bPendingDeviceListRefresh = FALSE;
		refresh_devices();
	}
	if (bPendingDefaultDeviceSwitch)
	{
		bPendingDefaultDeviceSwitch = FALSE;
		default_device_changed();
	}

	bLocked = TRUE;
	float new_tm = Timer.GetElapsed_sec();
	fTimer_Delta = new_tm - fTimer_Value;
	//.	float dt					= float(Timer_Delta)/1000.f;
	float dt_sec = fTimer_Delta;
	fTimer_Value = new_tm;

	if (raytrace)
		raytrace->frame(P, D, N, dt_sec, s_emitters);

	s_emitters_u ++;

	// Firstly update emitters, which are now being rendered
	//Msg	("! update: r-emitters");
	for (it = 0; it < s_targets.size(); it++)
	{
		CSoundRender_Target* T = s_targets[it];
		CSoundRender_Emitter* E = T->get_emitter();
		if (E)
		{
			E->update(dt_sec);
			E->marker = s_emitters_u;
			E = T->get_emitter(); // update can stop itself
			if (E) T->priority = E->priority();
			else T->priority = -1;
		}
		else
		{
			T->priority = -1;
		}
	}

	// Update emmitters
	//Msg	("! update: emitters");
	for (it = 0; it < s_emitters.size(); it++)
	{
		CSoundRender_Emitter* pEmitter = s_emitters[it];
		if (pEmitter->marker != s_emitters_u)
		{
			pEmitter->update(dt_sec);
			pEmitter->marker = s_emitters_u;
		}
		if (!pEmitter->isPlaying())
		{
			// Stopped
			xr_delete(pEmitter);
			s_emitters.erase(s_emitters.begin() + it);
			it--;
		}
	}

	// Get currently rendering emitters
	//Msg	("! update: targets");
	s_targets_defer.clear();
	s_targets_pu ++;
	// u32 PU				= s_targets_pu%s_targets.size();
	for (it = 0; it < s_targets.size(); it++)
	{
		CSoundRender_Target* T = s_targets[it];
		if (T->get_emitter())
		{
			// Has emmitter, maybe just not started rendering
			if (T->get_Rendering())
			{
				/*if	(PU == it)*/
				T->fill_parameters();
				T->update();
			}
			else
				s_targets_defer.push_back(T);
		}
	}

	// Commit parameters from pending targets
	if (!s_targets_defer.empty())
	{
		//Msg	("! update: start render - commit");
		s_targets_defer.erase(std::unique(s_targets_defer.begin(), s_targets_defer.end()), s_targets_defer.end());
		for (it = 0; it < s_targets_defer.size(); it++)
			s_targets_defer[it]->fill_parameters();
	}

	// update EFX
	if (m_is_supported)
	{
		// demonized: Interpolate from e_current to 95% of e_target in close to exact time
		constexpr float percent = 0.95f;
		float alpha = 1.0f - std::exp(std::log(1.0f - percent) * dt_sec / snd_efx_environment_change_time);
		clamp(alpha, 0.f, 1.f);

		CSoundRender_CoreA* Core = (CSoundRender_CoreA*)this;
		EFXEAXREVERBPROPERTIES target;
		if (!reverb_overwrite && raytrace && raytrace->listener_reverb(target))
		{
			// Start from what the effect holds now so the switch glides, its ratios bounded under the raised limit
			if (!rt_reverb_active)
			{
				reverb_from_environment(rt_reverb, e_current);
				rt_reverb_active = true;
				rt_reverb.flDecayHFRatio = bound_decay_ratio(rt_reverb.flDecayHFRatio);
				rt_reverb.flDecayLFRatio = bound_decay_ratio(rt_reverb.flDecayLFRatio);
			}
			reverb_step(rt_reverb, target, alpha);
			rt_reverb_step_time = 0.f;
			environment_from_reverb(e_current, rt_reverb);
			Core->load_reverb(Core->effect, &rt_reverb, 60.f);
			commit();

			// Each heard group glides on its own slot like the listener's reverb, a group not heard sends its sources back to the listener's slot
			for (int i = 0; i < sdef_reverb_groups; ++i)
			{
				EFXEAXREVERBPROPERTIES group;
				float gain;
				if (!raytrace->group_reverb(i, group, gain) || !Core->make_group(i))
				{
					Core->group_ready[i] = false;
					continue;
				}
				if (Core->group_ready[i])
				{
					reverb_step(Core->group_reverb[i], group, alpha);
					Core->group_gain[i] += (gain - Core->group_gain[i]) * alpha;
				}
				else
				{
					Core->group_reverb[i] = group;
					Core->group_gain[i] = gain;
				}
				Core->load_reverb(Core->group_effect[i], &Core->group_reverb[i]);
				Core->commit_group(i, Core->group_gain[i]);
				Core->group_ready[i] = true;
			}
		}
		else
		{
			// Grouped sources go back to the listener's slot while the stock path runs
			std::fill(std::begin(Core->group_ready), std::end(Core->group_ready), false);

			if (bListenerMoved)
			{
				bListenerMoved = FALSE;
				e_target_ptr = get_environment(P);
				if (!e_target_ptr)
					e_target_ptr = &e_identity;
			}

			// Bands lifted past 20 s glide down to the level environment under the raised limit and bounded ratios before the stock path takes over
			const float longest = rt_reverb.flDecayTime * std::max({1.f, rt_reverb.flDecayLFRatio, rt_reverb.flDecayHFRatio});
			if (rt_reverb_active && longest > AL_EAXREVERB_MAX_DECAY_TIME + 0.001f)
			{
				// Level environments load unclamped, so the glide aims where the stock path's clamped lerp settles
				CSoundRender_Environment level = *e_target_ptr;
				level.clamp();
				reverb_from_environment(target, level);
				// Every target band stays at or under 20 s so the glide ends
				const float band_cap = AL_EAXREVERB_MAX_DECAY_TIME / target.flDecayTime;
				target.flDecayHFRatio = std::min(bound_decay_ratio(target.flDecayHFRatio), band_cap);
				target.flDecayLFRatio = std::min(bound_decay_ratio(target.flDecayLFRatio), band_cap);
				const EFXEAXREVERBPROPERTIES held = rt_reverb;
				// The rebuilding fields glide in one step a second along the same curve, the rest every frame
				rt_reverb_step_time += dt_sec;
				const bool rebuilt = rt_reverb_step_time >= 1.f;
				if (rebuilt)
				{
					float step_alpha = 1.0f - std::exp(std::log(1.0f - percent) * rt_reverb_step_time / snd_efx_environment_change_time);
					clamp(step_alpha, 0.f, 1.f);
					reverb_rebuild_fields(rt_reverb, target, [step_alpha](float& a, float b) { a += (b - a) * step_alpha; });
					rt_reverb_step_time = 0.f;
				}
				reverb_glide_fields(rt_reverb, target, [alpha](float& a, float b) { a += (b - a) * alpha; });
				// A float lerp that no longer moves the decay or either ratio lands on the target so the glide ends
				if (rebuilt && alpha > 0.f && rt_reverb.flDecayTime == held.flDecayTime && rt_reverb.flDecayLFRatio == held.flDecayLFRatio && rt_reverb.flDecayHFRatio == held.flDecayHFRatio)
					rt_reverb = target;
				environment_from_reverb(e_current, rt_reverb);
				Core->load_reverb(Core->effect, &rt_reverb, 60.f);
				commit();
			}
			else
			{
				// Reseed the effect once so no raytraced field survives into the stock path
				if (rt_reverb_active)
				{
					rt_reverb_active = false;
					Core->load_reverb(Core->effect, &reverbs[0]);
				}

				//Msg("interpolating from e_current to e_target %.2f", std::min(e_current.Reverb, e_target_ptr->Reverb) / std::max(e_current.Reverb, e_target_ptr->Reverb));
				e_current.lerp(e_current, *e_target_ptr, alpha);

				set_listener(e_current);
				commit();
			}
		}
	}

	// update listener
	update_listener(P, D, N, dt_sec);

	// Start rendering of pending targets
	if (!s_targets_defer.empty())
	{
		CSoundRender_CoreA* Core = (CSoundRender_CoreA*)this;
		//Msg	("! update: start render");
		for (it = 0; it < s_targets_defer.size(); it++)
		{
			CSoundRender_TargetA* Ptr = (CSoundRender_TargetA*)s_targets_defer[it];
			if (m_is_supported)
				Ptr->SetSlot(Core->slot);
			Ptr->render();
		}
	}

	// Events
	update_events();

	bLocked = FALSE;
}

static u32 g_saved_event_count = 0;

void CSoundRender_Core::update_events()
{
	PROF_EVENT("CSoundRender_Core::update_events");
	g_saved_event_count = s_events.size();
	for (u32 it = 0; it < s_events.size(); it++)
	{
		event& E = s_events[it];
		Handler(E.first, E.second);
	}
	s_events.clear_not_free();
}

void CSoundRender_Core::statistic(CSound_stats* dest, CSound_stats_ext* ext)
{
	if (dest)
	{
		dest->_rendered = 0;
		for (u32 it = 0; it < s_targets.size(); it++)
		{
			CSoundRender_Target* T = s_targets[it];
			if (T->get_emitter() && T->get_Rendering()) dest->_rendered++;
		}
		dest->_simulated = s_emitters.size();
		dest->_cache_hits = cache._stat_hit;
		dest->_cache_misses = cache._stat_miss;
		dest->_events = g_saved_event_count;
		cache.stats_clear();
	}
	if (ext)
	{
		for (u32 it = 0; it < s_emitters.size(); it++)
		{
			CSoundRender_Emitter* _E = s_emitters[it];
			CSound_stats_ext::SItem _I;
			_I._3D = !_E->b2D;
			_I._rendered = !!_E->target;
			_I.params = _E->p_source;
			_I.volume = _E->smooth_volume;
			if (_E->owner_data)
			{
				_I.name = _E->source()->fname;
				_I.game_object = _E->owner_data->g_object;
				_I.game_type = _E->owner_data->g_type;
				_I.type = _E->owner_data->s_type;
			}
			else
			{
				_I.game_object = 0;
				_I.game_type = 0;
				_I.type = st_Effect;
			}
			ext->append(_I);
		}
	}
}


float CSoundRender_Core::get_occlusion_to(const Fvector& hear_pt, const Fvector& snd_pt, float dispersion)
{
	PROF_EVENT("CSoundRender_Core::get_occlusion_to");
	float occ_value = 1.f;

	if (0 != geom_SOM)
	{
		// Calculate RAY params
		Fvector pos, dir;
		pos.random_dir();
		pos.mul(dispersion);
		pos.add(snd_pt);
		dir.sub(pos, hear_pt);
		float range = dir.magnitude();
		dir.div(range);

#ifdef _EDITOR
		ETOOLS::ray_options		(CDB::OPT_CULL);
		ETOOLS::ray_query		(geom_SOM,hear_pt,dir,range);
		u32 r_cnt				= ETOOLS::r_count();
		CDB::RESULT*	_B 		= ETOOLS::r_begin();
#else
		geom_DB.ray_options(CDB::OPT_CULL);
		geom_DB.ray_query(geom_SOM, hear_pt, dir, range);
		u32 r_cnt = geom_DB.r_count();
		CDB::RESULT* _B = geom_DB.r_begin();
#endif
		if (0 != r_cnt)
		{
			for (u32 k = 0; k < r_cnt; k++)
			{
				CDB::RESULT* R = _B + k;
				occ_value *= *(float*)&R->dummy;
			}
		}
	}
	return occ_value;
}

float CSoundRender_Core::get_occlusion(Fvector& P, float R, Fvector* occ)
{
	PROF_EVENT("CSoundRender_Core::get_occlusion");
	float occ_value = 1.f;

	// Calculate RAY params
	Fvector base = listener_position();
	Fvector pos, dir;
	float range;
	pos.random_dir();
	pos.mul(R);
	pos.add(P);
	dir.sub(pos, base);
	range = dir.magnitude();
	dir.div(range);

	if (0 != geom_MODEL)
	{
		bool bNeedFullTest = true;
		// 1. Check cached polygon
		float _u, _v, _range;
		if (CDB::TestRayTri(base, dir, occ, _u, _v, _range, true))
			if (_range > 0 && _range < range)
			{
				occ_value = psSoundOcclusionScale;
				bNeedFullTest = false;
			}
		// 2. Polygon doesn't picked up - real database query
		if (bNeedFullTest)
		{
#ifdef _EDITOR
			ETOOLS::ray_options		(CDB::OPT_ONLYNEAREST);
			ETOOLS::ray_query		(geom_MODEL,base,dir,range);
			if (0!=ETOOLS::r_count()){ 
				// cache polygon
				const CDB::RESULT*	R = ETOOLS::r_begin			();
#else
			geom_DB.ray_options(CDB::OPT_ONLYNEAREST);
			geom_DB.ray_query(geom_MODEL, base, dir, range);
			if (0 != geom_DB.r_count())
			{
				// cache polygon
				const CDB::RESULT* R = geom_DB.r_begin();
#endif
				const CDB::TRI& T = geom_MODEL->get_tris()[R->id];
				const Fvector* V = geom_MODEL->get_verts();
				occ[0].set(V[T.verts[0]]);
				occ[1].set(V[T.verts[1]]);
				occ[2].set(V[T.verts[2]]);
				occ_value = psSoundOcclusionScale;
			}
		}
	}
	if (0 != geom_SOM)
	{
#ifdef _EDITOR
		ETOOLS::ray_options		(CDB::OPT_CULL);
		ETOOLS::ray_query		(geom_SOM,base,dir,range);
		u32 r_cnt				= ETOOLS::r_count();
        CDB::RESULT*	_B 		= ETOOLS::r_begin();
#else
		geom_DB.ray_options(CDB::OPT_CULL);
		geom_DB.ray_query(geom_SOM, base, dir, range);
		u32 r_cnt = geom_DB.r_count();
		CDB::RESULT* _B = geom_DB.r_begin();
#endif
		if (0 != r_cnt)
		{
			for (u32 k = 0; k < r_cnt; k++)
			{
				CDB::RESULT* R = _B + k;
				occ_value *= *(float*)&R->dummy;
			}
		}
	}
	return occ_value;
}
