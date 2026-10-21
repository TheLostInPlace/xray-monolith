#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Raytrace.h"
#include "SoundRender_Decay.h"
#include "SoundRender_Environment.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_Source.h"
#include "SoundRender_TargetA.h"

// xrSound.vcxproj defines SOUND_RAYTRACE_SDK when the header is in 3rd party\vaudio
#ifdef SOUND_RAYTRACE_SDK
#	include <vaudio/vaudio.h>
#	include <psapi.h>
#	pragma comment(lib, "vaudionative.lib")
#	pragma comment(lib, "delayimp.lib")
#	pragma comment(lib, "psapi.lib")
#endif

CSoundRender_Raytrace::CSoundRender_Raytrace() :
	world(nullptr), listener(nullptr), ready(false), reverb(reverbs[0]), has_reverb(false), asked(false), held(false), held_latch(false), held_updates(0), room(false), room_serial(0),
	commit_base(0), first_result(false), walked(0.f), still_time(0.f),
	muffle(nullptr), muffle_rays(false), retrace(false), reset_updates(0),
	occluders_open(false), occluder_moves(0),
	ambient(false), ambient_ready(false), ambient_lf(1.f), ambient_hf(1.f), ambient_wait(-1.f), group_sources(0), group_count(0),
	formula(false), model(nullptr), direct_moves(0), direct_tick(0), ray_time(0.f), direct_window(0),
	open_air(false), open_air_window(0), rays(), rays_out(false), ray_lapsed(false), ray_done(false),
	query_next(0), query_generation(0), queries_open(false), query_window(0)
{
	bounds.invalidate();
	std::fill(std::begin(group_heard), std::end(group_heard), false);
}

CSoundRender_Raytrace::~CSoundRender_Raytrace()
{
	destroy();
	for (CSoundRender_RaytraceSource* S : sources)
		xr_delete(S);
	// Sound shutdown waits for every old world still finishing its round
	for (HANDLE h : teardowns)
	{
		if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT)
			Msg("SOUND: Raytrace: waiting for an old world to finish its round");
		WaitForSingleObject(h, INFINITE);
		CloseHandle(h);
	}
}

bool CSoundRender_Raytrace::listener_reverb(EFXEAXREVERBPROPERTIES& props) const
{
	if (!ready.load(std::memory_order_acquire) || !has_reverb)
		return false;
	props = reverb;
	return true;
}

namespace
{
	constexpr u32 snapshot_valid = 1;
	constexpr u32 snapshot_applied = 2;
	constexpr u32 snapshot_hf_limit = 4;

	u32 snapshot_bits(float f)
	{
		u32 u;
		std::memcpy(&u, &f, sizeof(u));
		return u;
	}

	float word_float(u32 u)
	{
		float f;
		std::memcpy(&f, &u, sizeof(f));
		return f;
	}

	// Copies the listener's words, false while no room is published
	bool listener_words(const CSoundRender_RaytraceSnapshot<snapshot_listener_words>& snapshot, u32* w)
	{
		const bool read = snapshot.read([w](const std::atomic<u32>* words)
		{
			for (u32 i = 0; i < snapshot_listener_words; ++i)
				w[i] = words[i].load(std::memory_order_relaxed);
		});
		return read && (w[1] & snapshot_valid);
	}
}

bool CSoundRender_Raytrace::read_acoustics(CSound_acoustics& out) const
{
	if (!asked.load(std::memory_order_relaxed))
		asked.store(true, std::memory_order_relaxed);
	u32 w[snapshot_listener_words];
	if (!ready.load(std::memory_order_acquire) || held.load(std::memory_order_relaxed) || held_latch.load(std::memory_order_acquire) || !listener_words(listener_snapshot, w))
		return false;
	out.serial = w[0];
	out.position.set(word_float(w[2]), word_float(w[3]), word_float(w[4]));
	out.sky = word_float(w[5]);
	out.returned = word_float(w[6]);
	out.decay_lf = word_float(w[7]);
	out.decay_hf = word_float(w[8]);
	out.absorption_lf = word_float(w[9]);
	out.absorption_hf = word_float(w[10]);
	out.roughness = word_float(w[11]);
	out.ambient_lf = word_float(w[12]);
	out.ambient_hf = word_float(w[13]);
	return true;
}

bool CSoundRender_Raytrace::read_reverb(CSound_reverb& out) const
{
	if (!asked.load(std::memory_order_relaxed))
		asked.store(true, std::memory_order_relaxed);
	u32 w[snapshot_listener_words];
	if (!ready.load(std::memory_order_acquire) || held.load(std::memory_order_relaxed) || held_latch.load(std::memory_order_acquire) || !listener_words(listener_snapshot, w))
		return false;
	out.serial = w[0];
	out.applied = w[1] & snapshot_applied;
	out.decay_hf_limit = w[1] & snapshot_hf_limit;
	float* values[] =
	{
		&out.density, &out.diffusion, &out.gain, &out.gain_hf, &out.gain_lf, &out.decay_time, &out.decay_hf_ratio, &out.decay_lf_ratio,
		&out.reflections_gain, &out.reflections_delay, &out.late_reverb_gain, &out.late_reverb_delay, &out.echo_time, &out.echo_depth,
		&out.modulation_time, &out.modulation_depth, &out.air_absorption_gain_hf, &out.hf_reference, &out.lf_reference, &out.room_rolloff_factor
	};
	for (u32 i = 0; i < 20; ++i)
		*values[i] = word_float(w[14 + i]);
	return true;
}

bool CSoundRender_Raytrace::read_muffling(const void* owner, CSound_muffling& out) const
{
	if (!asked.load(std::memory_order_relaxed))
		asked.store(true, std::memory_order_relaxed);
	if (!owner || !ready.load(std::memory_order_acquire) || held.load(std::memory_order_relaxed) || held_latch.load(std::memory_order_acquire))
		return false;
	const u64 key = u64(uintptr_t(owner));
	bool found = false;
	const bool read = owner_snapshot.read([&](const std::atomic<u32>* words)
	{
		found = false;
		const u32 count = std::min(words[0].load(std::memory_order_relaxed), snapshot_owners);
		for (u32 i = 0; i < count; ++i)
		{
			const std::atomic<u32>* e = words + 1 + i * 6;
			if (e[0].load(std::memory_order_relaxed) != u32(key) || e[1].load(std::memory_order_relaxed) != u32(key >> 32))
				continue;
			out.lf = word_float(e[2].load(std::memory_order_relaxed));
			out.hf = word_float(e[3].load(std::memory_order_relaxed));
			out.direct_lf = word_float(e[4].load(std::memory_order_relaxed));
			out.direct_hf = word_float(e[5].load(std::memory_order_relaxed));
			found = true;
			return;
		}
	});
	return read && found;
}

void CSoundRender_Raytrace::hold(bool value)
{
	if (value)
		held_latch.store(true, std::memory_order_relaxed);
	held.store(value, std::memory_order_relaxed);
}

bool CSoundRender_Raytrace::group_reverb(int i, EFXEAXREVERBPROPERTIES& props, float& gain) const
{
	// A group heard at no gain keeps its sources on the listener's slot
	if (!ready.load(std::memory_order_acquire) || !group_heard[i] || group_gain[i] <= 0.f)
		return false;
	props = group_props[i];
	gain = group_gain[i];
	return true;
}

// A busy slot passes the post on to the next ticket, so only a ring with every slot pending turns it away
u32 CSoundRender_Raytrace::post_query(bool sweep, const Fvector& ear, const Fvector& source, const void* skip)
{
	// The generation goes first, so a post that sees the queries open after a release carries the new one
	const u32 generation = query_generation.load(std::memory_order_acquire);
	if (!queries_open.load(std::memory_order_acquire) || !_valid(ear) || !_valid(source))
		return 0;
	for (u32 tries = 0; tries < raytrace_query_slots; ++tries)
	{
		const u32 ticket = query_next.fetch_add(1, std::memory_order_relaxed) + 1;
		if (!ticket)
			continue;
		CSoundRender_RaytraceSlot& S = query_slots[ticket % raytrace_query_slots];
		u64 head = S.head.load(std::memory_order_relaxed);
		const u32 phase = u32(head & 0xff);
		if (phase != query_free && phase != query_answered && phase != query_dropped)
			continue;
		if (!S.head.compare_exchange_strong(head, query_head(ticket, query_writing), std::memory_order_acq_rel))
			continue;
		// Orders the writing head before the request stores, so a reader that sees a new field also sees the head move
		std::atomic_thread_fence(std::memory_order_release);
		S.generation.store(generation, std::memory_order_relaxed);
		S.room.store(sweep, std::memory_order_relaxed);
		for (int k = 0; k < 3; ++k)
		{
			S.ear[k].store(ear[k], std::memory_order_relaxed);
			S.source[k].store(source[k], std::memory_order_relaxed);
		}
		S.skip.store(skip, std::memory_order_relaxed);
		S.head.store(query_head(ticket, query_posted), std::memory_order_release);
		return ticket;
	}
	return 0;
}

// An answer is read between two loads of its head, a head that moved means the slot went to a later ticket or a release
int CSoundRender_Raytrace::query_result(u32 ticket, CSound_query& out) const
{
	if (!ticket)
		return -1;
	const CSoundRender_RaytraceSlot& S = query_slots[ticket % raytrace_query_slots];
	const u64 head = S.head.load(std::memory_order_acquire);
	if (u32(head >> 8) != ticket)
		return -1;
	switch (u32(head & 0xff))
	{
	case query_writing:
	case query_posted:
	case query_taken:
		return S.generation.load(std::memory_order_relaxed) == query_generation.load(std::memory_order_acquire) ? 0 : -1;
	case query_answered:
		break;
	default:
		return -1;
	}
	out.room = S.room.load(std::memory_order_relaxed);
	out.lf = S.lf.load(std::memory_order_relaxed);
	out.hf = S.hf.load(std::memory_order_relaxed);
	out.direct_lf = S.direct_lf.load(std::memory_order_relaxed);
	out.direct_hf = S.direct_hf.load(std::memory_order_relaxed);
	out.through_lf = S.through_lf.load(std::memory_order_relaxed);
	out.through_hf = S.through_hf.load(std::memory_order_relaxed);
	out.crossings = S.crossings.load(std::memory_order_relaxed);
	out.blocked = S.blocked.load(std::memory_order_relaxed);
	out.open = S.open.load(std::memory_order_relaxed);
	out.reflect_lf = S.reflect_lf.load(std::memory_order_relaxed);
	out.reflect_hf = S.reflect_hf.load(std::memory_order_relaxed);
	std::atomic_thread_fence(std::memory_order_acquire);
	return S.head.load(std::memory_order_relaxed) == head ? 1 : -1;
}

#ifdef SOUND_RAYTRACE_SDK

namespace
{
	// Preset names as sound_raytrace.ltx spells them, in VAMaterialType order from VAMaterialBrick
	LPCSTR preset_names[] =
	{
		"brick", "cloth", "concrete", "concrete_polished", "dirt", "glass", "grass", "gravel", "gyprock", "ice", "leaf",
		"marble", "metal", "mud", "rock", "sand", "snow", "tile", "tree", "water", "wood_indoor", "wood_outdoor"
	};
	static_assert(sizeof(preset_names) / sizeof(preset_names[0]) == VAMaterialTypeCount - 1, "preset names out of step with vaudio.h");

	// Custom material ids start at 1000, one per game material index
	constexpr int material_base = 1000;

	// Muffle listener rays, the full example's occlusion and permeation counts
	constexpr int occlusion_rays = 512;
	constexpr int occlusion_bounces = 8;
	constexpr int permeation_rays = 128;
	constexpr int permeation_bounces = 3;

	// Ambient permeation rays at the ambience page's counts, and the energy share that brings a bed to full volume
	constexpr int ambient_permeation_rays = 128;
	constexpr int ambient_permeation_bounces = 3;
	constexpr float ambient_cap = 0.3f;
	// A held bed plays at stock level when the ambient filter is still missing 10 s after the world's first update
	constexpr float ambient_timeout = 10.f;

	// A crossing that keeps less than the permeation floor passes nothing, so walls pass sound up to 80 dB of loss
	constexpr float permeation_floor = 0.0001f;

	// Sources past 1.5 m cast 64 x 64 reverb rays into the grouped reverb, 16 at most, 4 trails refreshed per update
	constexpr int group_rays = 64;
	constexpr int group_bounces = 64;
	constexpr int group_refresh = 4;
	constexpr u32 group_budget = 16;
	constexpr float group_near = 1.5f;
	// A source changes group after three equal readings
	constexpr u32 group_switch = 3;
	// Grouped reverb is fully directional from a pan of 0.8 and all around under 0.6
	constexpr float group_inner = 0.6f;
	constexpr float group_outer = 0.8f;

	// A limp panel passes pressure falling with frequency, squared ratio of the 5000 Hz and 250 Hz references
	constexpr float mass_law_ratio = (5000.f / 250.f) * (5000.f / 250.f);

	// Occlusion energy per ray at clear points 5 m from the listener in open air with world-bounds loss on, at 250 and 5000 Hz
	constexpr float open_air_lf = 0.3979f;
	constexpr float open_air_hf = 0.4773f;

	// The measured open air energy, the share each of 512 listener rays keeps at its first surface, escaped rays keep none
	constexpr u32 open_air_rays = 512;
	// A 60 fps frame casts every 16th ray of the sphere as four batches of 8, so a full sweep takes 64 batches
	constexpr u32 open_air_frame_rays = 32;
	constexpr u32 open_air_batch_rays = 8;
	constexpr u32 open_air_batches = open_air_rays / open_air_batch_rays;
	constexpr u32 open_air_split = open_air_frame_rays / open_air_batch_rays;
	constexpr float open_air_range = 300.f;
	// Surfaces of skipped materials a ray may pass before it counts as escaped
	constexpr u32 open_air_passes = 4;

	// The direct path's Fresnel disc at each filter reference, sampled where the line first meets a solid surface
	constexpr float speed_of_sound = 343.f;
	constexpr float direct_frequency[2] = { 250.f, 5000.f };
	constexpr int direct_samples = 16;
	// A ray stops short of its far end, so a source resting on a surface stays in sight
	constexpr float direct_margin = 0.05f;
	// A blocked path casts its line and up to two legs per sample in each band
	constexpr u32 direct_path_rays = 1 + 2 * 2 * direct_samples;
	// Rays the direct paths may cast in one 60 fps frame, two blocked paths with room for clear ones
	constexpr u32 direct_ray_budget = 192;
	static_assert(direct_ray_budget >= direct_path_rays, "the budget must fit one blocked path");
	// The rays keep their 60 fps rate at any frame rate, each 1/60 s window holds one such frame's budgets
	constexpr float ray_window = 1.f / 60.f;

	// Script queries get one blocked path's rays a window, a room casts the open air sphere in batches within them
	constexpr u32 query_ray_budget = direct_path_rays;
	static_assert(query_ray_budget >= open_air_batch_rays, "the budget must fit one room batch");
	// Posted queries the tick takes at most, oldest first
	constexpr u32 query_takes = 4;
	// A crossing that keeps less than this zeroes its band
	constexpr float query_keep_floor = 1e-4f;
	// The upload's mesh cell, a query's line pays each material once per cell
	constexpr float mesh_split = 2.f;

	// The DLL is probed once per session
	int sdk_state = -1;

	void sdk_log(const char* message)
	{
		Msg("SOUND: Raytrace: sdk: %s", message);
	}

	u64 process_commit()
	{
		PROCESS_MEMORY_COUNTERS_EX pmc{};
		pmc.cb = sizeof(pmc);
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
		return pmc.PrivateUsage;
	}

	float commit_mb(u64 base)
	{
		return float(s64(process_commit()) - s64(base)) / (1024.f * 1024.f);
	}

	VAVector va_vector(const Fvector& v)
	{
		return { v.x, v.y, v.z };
	}

	// The SDK's EAX as it stands, undirected
	void eax_props(EFXEAXREVERBPROPERTIES& R, const VAEAXReverb& eax)
	{
		R.flDensity = eax.density;
		R.flDiffusion = eax.diffusion;
		R.flGain = eax.gain;
		R.flGainHF = eax.gainHF;
		R.flGainLF = eax.gainLF;
		R.flDecayTime = eax.decayTime;
		R.flDecayHFRatio = eax.decayHFRatio;
		R.flDecayLFRatio = eax.decayLFRatio;
		R.flReflectionsGain = eax.reflectionsGain;
		R.flReflectionsDelay = eax.reflectionsDelay;
		R.flLateReverbGain = eax.lateReverbGain;
		R.flLateReverbDelay = eax.lateReverbDelay;
		R.flEchoTime = eax.echoTime;
		R.flEchoDepth = eax.echoDepth;
		R.flModulationTime = eax.modulationTime;
		R.flModulationDepth = eax.modulationDepth;
		R.flAirAbsorptionGainHF = eax.airAbsorptionGainHF;
		R.flHFReference = eax.hfReference;
		R.flLFReference = eax.lfReference;
		R.flRoomRolloffFactor = eax.roomRolloffFactor;
		R.iDecayHFLimit = eax.decayHFLimit;
		for (int i = 0; i < 3; ++i)
			R.flReflectionsPan[i] = R.flLateReverbPan[i] = 0.f;
	}

	// Where the SDK puts a prism's origin, as a share of its size from the centre
	constexpr float prism_origin = 0.f;

	// Fmatrix and VAMatrix share one memory layout, translation in m41 to m43
	VAMatrix va_matrix(const Fmatrix& m, const CSound_occluder_shape& s)
	{
		Fmatrix t = m;
		if (s.type == sos_Box)
		{
			Fvector offset;
			offset.set(s.size).mul(-prism_origin);
			m.transform_dir(offset);
			t.c.add(offset);
		}
		VAMatrix v;
		static_assert(sizeof(v) == sizeof(t), "matrix layouts differ in size");
		std::memcpy(&v, &t, sizeof(v));
		return v;
	}

	int destroy_primitive(void* p, u16 type)
	{
		switch (type)
		{
		case sos_Box: return vaPrismPrimitiveDestroy(static_cast<VAPrismPrimitive*>(p));
		case sos_Cylinder: return vaCylinderPrimitiveDestroy(static_cast<VACylinderPrimitive*>(p));
		default: return vaSpherePrimitiveDestroy(static_cast<VASpherePrimitive*>(p));
		}
	}

	// The sizes a shape type reads, each finite and not below zero
	bool valid_size(const CSound_occluder_shape& s)
	{
		const int used = s.type == sos_Box ? 3 : s.type == sos_Cylinder ? 2 : 1;
		const float* v = &s.size.x;
		return std::all_of(v, v + used, [](float x) { return _valid(x) && x >= 0.f; });
	}

	// The listener's measured open air energy per band, read by the formula on the SDK threads
	std::atomic<float> open_air_reference[2];

	// Energy above the measured open air and the permeated share add as energies, the root gives the amplitude
	float open_air_gain(bool lowFrequency, int occlusionRayCount, int permeationRayCount, int permeationBounceCount, float occlusionEnergy, float permeationEnergy)
	{
		const float e = occlusionRayCount > 0 ? occlusionEnergy / occlusionRayCount : 0.f;
		const float p = permeationRayCount > 0 && permeationBounceCount > 0 ? permeationEnergy / (permeationRayCount * permeationBounceCount) : 0.f;
		return std::min(1.f, std::sqrt(p * p + e / open_air_reference[!lowFrequency].load(std::memory_order_relaxed)));
	}

	// The SDK's ambient sum of each energy share over its cap, the root gives the bed's amplitude
	float ambient_gain(bool lowFrequency, int occlusionRayCount, int permeationRayCount, int permeationBounceCount, float occlusionEnergy, float permeationEnergy)
	{
		float gain = 0.f;
		if (occlusionRayCount > 0)
			gain += occlusionEnergy / (ambient_cap * occlusionRayCount);
		if (permeationRayCount > 0 && permeationBounceCount > 0)
			gain += permeationEnergy / (ambient_cap * permeationRayCount * permeationBounceCount);
		// A NaN share reads as full, as the SDK's default formula takes it
		return std::sqrt(gain < 1.f ? gain : 1.f);
	}

	// The SDK's own fit, for echograms the continuous one cannot span, set once from the first formulas struct
	std::atomic<CustomEAXFormulas_CalculateRT60Func> sdk_rt60{ nullptr };

	// A band's decay from its echogram's first run of filled bins on the analysis thread, kept in the EAX decay range
	float decay_rt60(VACustomEAXFormulas* formulas, float* echogram, int echogramLength)
	{
		float decay = schroeder_t20(echogram, echogramLength, formulas->binDurationMs * 0.001f);
		if (decay <= 0.f)
			decay = sdk_rt60.load(std::memory_order_relaxed)(formulas, echogram, echogramLength);
		return std::clamp(decay, EAX_DECAY_TIME_MIN, EAX_DECAY_TIME_MAX);
	}

	// The inverse pose and world box the direct path tests a shape with
	void direct_shape(CSoundRender_RaytracePrimitive& p)
	{
		p.inverse.invert_b(p.pose);
		const CSound_occluder_shape& s = p.shape;
		Fvector h;
		switch (s.type)
		{
		case sos_Box: h.mul(s.size, 0.5f); break;
		case sos_Cylinder: h.set(s.size.x, s.size.y * 0.5f, s.size.x); break;
		default: h.set(s.size.x, s.size.x, s.size.x); break;
		}
		Fbox local;
		local.min.invert(h);
		local.max.set(h);
		p.box.xform(local, p.pose);
	}

	// Where a segment from a along unit dir enters an occluder shape within len, -1 when it misses
	float shape_hit(const CSound_occluder_shape& s, const Fmatrix& inv, const Fvector& a, const Fvector& dir, float len)
	{
		Fvector o, d;
		inv.transform_tiny(o, a);
		inv.transform_dir(d, dir);
		float t0 = 0.f, t1 = len;
		// The slab between -h and h on one local axis
		auto slab = [&](float oa, float da, float h)
		{
			if (std::abs(da) < EPS_S)
				return std::abs(oa) <= h;
			float ta = (-h - oa) / da, tb = (h - oa) / da;
			if (ta > tb)
				std::swap(ta, tb);
			t0 = std::max(t0, ta);
			t1 = std::min(t1, tb);
			return t0 <= t1;
		};
		// The inside of a radius r around the local origin, over the axes the factor keeps
		auto radial = [&](const Fvector& keep, float r)
		{
			const float qa = d.x * d.x * keep.x + d.y * d.y * keep.y + d.z * d.z * keep.z;
			const float qb = 2.f * (o.x * d.x * keep.x + o.y * d.y * keep.y + o.z * d.z * keep.z);
			const float qc = o.x * o.x * keep.x + o.y * o.y * keep.y + o.z * o.z * keep.z - r * r;
			if (qa < EPS_S)
				return qc <= 0.f;
			const float disc = qb * qb - 4.f * qa * qc;
			if (disc < 0.f)
				return false;
			const float root = std::sqrt(disc);
			t0 = std::max(t0, (-qb - root) / (2.f * qa));
			t1 = std::min(t1, (-qb + root) / (2.f * qa));
			return t0 <= t1;
		};
		bool inside;
		switch (s.type)
		{
		case sos_Box:
			inside = slab(o.x, d.x, s.size.x * 0.5f) && slab(o.y, d.y, s.size.y * 0.5f) && slab(o.z, d.z, s.size.z * 0.5f);
			break;
		case sos_Cylinder:
			inside = slab(o.y, d.y, s.size.y * 0.5f) && radial({ 1.f, 0.f, 1.f }, s.size.x);
			break;
		default:
			inside = radial({ 1.f, 1.f, 1.f }, s.size.x);
			break;
		}
		return inside ? t0 : -1.f;
	}

	bool sdk_probe()
	{
		if (sdk_state < 0)
		{
			sdk_state = 0;
			if (!LoadLibraryExW(L"vaudionative.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR))
			{
				Msg("! SOUND: Raytrace: vaudionative.dll not found beside the exe, raytracing stays off");
				return false;
			}
			int major, minor, patch;
			vaGetVersion(&major, &minor, &patch);
			Msg("SOUND: Raytrace: %d.%d.%d %s", major, minor, patch, vaIsProduction() ? "production" : "dev");
			if (major != VA_VERSION_MAJOR || minor != VA_VERSION_MINOR)
			{
				Msg("! SOUND: Raytrace: built against %d.%d, raytracing stays off", VA_VERSION_MAJOR, VA_VERSION_MINOR);
				return false;
			}
			sdk_state = 1;
		}
		return sdk_state == 1;
	}

	bool succeeded(int result)
	{
		switch (result)
		{
		case VA_SUCCESS:
		case VA_UNCHANGED:
		case VA_STILL_RUNNING:
		case VA_PENDING_REMOVAL:
		case VA_TRUE:
		case VA_FALSE:
			return true;
		}
		return false;
	}

	// An old world still runs its round after the level is gone, so it finishes and frees on its own thread
	struct Teardown
	{
		VAWorld* world;
		VAEmitter* listener;
		xr_vector<VAMeshPrimitive*> meshes;
		xr_vector<VAEmitter*> emitters;
		xr_vector<CSoundRender_RaytracePrimitive> primitives;
	};

	// Every failed teardown call goes on one line, with its result or how many of its objects failed
	void teardown(void* arg)
	{
		Teardown* t = static_cast<Teardown*>(arg);
		string512 failures = "";
		string128 part;
		auto note = [&failures, &part](int result, LPCSTR call)
		{
			if (succeeded(result))
				return;
			xr_sprintf(part, ", %s %d", call, result);
			xr_strcat(failures, part);
		};
		note(vaWorldSetPendingShutdown(t->world, true), "vaWorldSetPendingShutdown");
		note(vaWorldWait(t->world), "vaWorldWait");
		note(vaWorldDestroy(t->world), "vaWorldDestroy");
		if (t->listener)
			note(vaEmitterDestroy(t->listener), "vaEmitterDestroy");
		u32 failed_emitters = 0;
		for (VAEmitter* e : t->emitters)
			failed_emitters += !succeeded(vaEmitterDestroy(e));
		if (failed_emitters)
		{
			xr_sprintf(part, ", vaEmitterDestroy %u of %u muffle emitters", failed_emitters, u32(t->emitters.size()));
			xr_strcat(failures, part);
		}
		u32 failed = 0;
		int last = 0;
		for (VAMeshPrimitive* mesh : t->meshes)
		{
			int result = vaMeshPrimitiveDestroy(mesh);
			if (!succeeded(result))
			{
				++failed;
				last = result;
			}
		}
		if (failed)
		{
			xr_sprintf(part, ", vaMeshPrimitiveDestroy %u of %u meshes, last %d", failed, u32(t->meshes.size()), last);
			xr_strcat(failures, part);
		}
		// Occluder primitives go after their world, live and removed alike
		u32 failed_primitives = 0;
		for (const CSoundRender_RaytracePrimitive& p : t->primitives)
			failed_primitives += !succeeded(destroy_primitive(p.primitive, p.shape.type));
		if (failed_primitives)
		{
			xr_sprintf(part, ", occluder destroy %u of %u primitives", failed_primitives, u32(t->primitives.size()));
			xr_strcat(failures, part);
		}
		if (failures[0])
			Msg("! SOUND: Raytrace: teardown failed%s", failures);
		xr_delete(t);
	}
}

bool CSoundRender_Raytrace::check(int result, LPCSTR call)
{
	if (succeeded(result))
		return true;
	if (std::find(reported.begin(), reported.end(), call) == reported.end())
	{
		reported.push_back(call);
		Msg("! SOUND: Raytrace: %s returned %d", call, result);
	}
	return false;
}

// Runs inside vaWorldUpdate or vaWorldRemoveEmitter under the lock, a source emitter is safe to free from here on
void CSoundRender_Raytrace::emitter_removed(VAEmitter* emitter)
{
	if (CSoundRender_Raytrace* self = static_cast<CSoundRender_Raytrace*>(vaEmitterGetUserData(emitter)))
		self->freed.push_back(emitter);
}

void CSoundRender_Raytrace::create(CDB::MODEL* M, const CSound_material_desc* materials, u32 count)
{
	if (!sdk_probe())
		return;

	xrCriticalSectionGuard guard(lock);
	upload_timer.Start();
	commit_base = process_commit();
	reported.clear();

	world = vaWorldCreate();
	if (!world)
	{
		Msg("! SOUND: Raytrace: vaWorldCreate failed, raytracing stays off");
		return;
	}
	check(vaWorldSetLogCallback(world, sdk_log), "vaWorldSetLogCallback");
	check(vaWorldSetLogMemoryAllocationWarnings(world, false), "vaWorldSetLogMemoryAllocationWarnings");
	// OpenAL Soft filter reference frequencies
	check(vaWorldSetReferenceFrequencyHF(world, 5000.f), "vaWorldSetReferenceFrequencyHF");
	check(vaWorldSetReferenceFrequencyLF(world, 250.f), "vaWorldSetReferenceFrequencyLF");
	check(vaWorldSetMetersPerUnit(world, 1.f), "vaWorldSetMetersPerUnit");
	check(vaWorldSetInverseSpeedOfSound(world, 1.f / 343.f), "vaWorldSetInverseSpeedOfSound");
	check(vaWorldSetCoordinateSystem(world, VACoordinateSystemDefault), "vaWorldSetCoordinateSystem");
	check(vaWorldSetAirAbsorption(world, nullptr), "vaWorldSetAirAbsorption");
	// One effect slot per group beside the listener's
	check(vaWorldSetMaximumGroupedEAXCount(world, sdef_reverb_groups), "vaWorldSetMaximumGroupedEAXCount");
	// Open sky stays open past the box edge
	check(vaWorldSetEmittersOutsideTheWorldAreMuffled(world, false), "vaWorldSetEmittersOutsideTheWorldAreMuffled");
	check(vaWorldSetOcclusionRaysLoseEnergyFromWorldBounds(world, false), "vaWorldSetOcclusionRaysLoseEnergyFromWorldBounds");
	// The world frees its formulas with itself, only the decay fit differs from the SDK's
	if (VACustomEAXFormulas* formulas = vaCustomEAXFormulasCreate())
	{
		// Every struct holds the same SDK fit, so only the first world sets it
		CustomEAXFormulas_CalculateRT60Func unset = nullptr;
		sdk_rt60.compare_exchange_strong(unset, formulas->calculateRt60);
		formulas->calculateRt60 = decay_rt60;
		if (!check(vaWorldSetCustomEAXFormulas(world, formulas), "vaWorldSetCustomEAXFormulas"))
			check(vaCustomEAXFormulasDestroy(formulas), "vaCustomEAXFormulasDestroy");
	}
	else
		check(VA_INVALID_POINTER, "vaCustomEAXFormulasCreate");

	const CDB::TRI* tris = M->get_tris();
	const Fvector* verts = M->get_verts();
	const u32 tri_count = u32(M->get_tris_count());

	xr_vector<u32> per_material(count + 1, 0);
	for (u32 i = 0; i < tri_count; ++i)
		++per_material[std::min<u32>(tris[i].material, count)];

	string_path ltx_name;
	CInifile* ltx = FS.exist(ltx_name, "$game_data$", "materials\\sound_raytrace", ".ltx") ? xr_new<CInifile>(ltx_name, TRUE) : nullptr;
	if (!ltx)
		Msg("! SOUND: Raytrace: materials\\sound_raytrace.ltx not found, every material uses its default");

	// A dynamic material takes the SDK material of its like section, made here when the level has no static triangles of it
	xr_vector<u32> like_of(count, u32(-1));
	xr_vector<u8> wanted(count, 0);
	for (u32 m = 0; m < count; ++m)
	{
		if (!materials[m].dynamic)
			continue;
		LPCSTR name = materials[m].name.c_str();
		u32 target = m;
		if (ltx && ltx->line_exist(name, "like"))
		{
			LPCSTR like = ltx->r_string(name, "like");
			const auto it = std::find_if(materials, materials + count, [like](const CSound_material_desc& d) { return like && !xr_strcmp(d.name.c_str(), like); });
			if (it != materials + count)
				target = u32(it - materials);
			else
				Msg("! SOUND: Raytrace: [%s] like %s names no game material, it gets its own", name, like ? like : "");
		}
		like_of[m] = target;
		wanted[target] = 1;
	}

	// Materials first, a mesh with a missing material is refused
	xr_vector<int> material_id(count, 0);
	u32 used = 0, skipped = per_material[count], unmapped = 0;
	for (u32 m = 0; m < count; ++m)
	{
		if (!per_material[m] && !wanted[m])
			continue;

		LPCSTR name = materials[m].name.c_str();
		LPCSTR section = name;
		if (!ltx || !ltx->section_exist(section))
		{
			section = materials[m].passable ? "default_passable" : "default";
			++unmapped;
		}
		const bool has_section = ltx && ltx->section_exist(section);
		// An empty value reads as NULL and counts as missing
		auto text = [&](LPCSTR key) -> LPCSTR
		{
			if (!has_section || !ltx->line_exist(section, key))
				return nullptr;
			LPCSTR v = ltx->r_string(section, key);
			if (!v || !*v)
			{
				Msg("! SOUND: Raytrace: [%s] %s is empty, the preset stays", section, key);
				return nullptr;
			}
			return v;
		};
		if (text("skip") && ltx->r_bool(section, "skip"))
		{
			skipped += per_material[m];
			continue;
		}

		LPCSTR preset = text("preset");
		if (!preset)
			preset = materials[m].passable ? "leaf" : "concrete";
		int type = VAMaterialConcrete;
		auto it = std::find_if(std::begin(preset_names), std::end(preset_names), [preset](LPCSTR p) { return !xr_strcmp(p, preset); });
		if (it != std::end(preset_names))
			type = VAMaterialBrick + int(it - std::begin(preset_names));
		else
			Msg("! SOUND: Raytrace: unknown preset %s for %s, using concrete", preset, name);

		const int id = material_base + int(m);
		if (!check(vaWorldCreateMaterial(world, id), "vaWorldCreateMaterial"))
			continue;

		// Preset numbers stay the SDK's unless the section overrides them, an override the SDK refuses or a loss above 1 falls back
		auto set = [&](decltype(&vaWorldSetMaterialAbsorptionLF) setter, LPCSTR call, LPCSTR key, float preset_value, float top)
		{
			if (text(key))
			{
				const float v = ltx->r_float(section, key);
				if (v <= top && check(setter(world, id, v), call))
					return;
				Msg("! SOUND: Raytrace: [%s] %s %.3f rejected, the preset stays", section, key, v);
			}
			check(setter(world, id, preset_value), call);
		};
		set(vaWorldSetMaterialAbsorptionLF, "vaWorldSetMaterialAbsorptionLF", "absorption_lf", vaWorldGetMaterialAbsorptionLF(world, type), 1.f);
		set(vaWorldSetMaterialAbsorptionHF, "vaWorldSetMaterialAbsorptionHF", "absorption_hf", vaWorldGetMaterialAbsorptionHF(world, type), 1.f);
		set(vaWorldSetMaterialScattering, "vaWorldSetMaterialScattering", "scattering", vaWorldGetMaterialScattering(world, type), 1.f);
		set(vaWorldSetMaterialTransmissionLF, "vaWorldSetMaterialTransmissionLF", "transmission_lf", vaWorldGetMaterialTransmissionLF(world, type), FLT_MAX);
		set(vaWorldSetMaterialTransmissionHF, "vaWorldSetMaterialTransmissionHF", "transmission_hf", vaWorldGetMaterialTransmissionHF(world, type), FLT_MAX);
		// A crossing keeps the stock occlusion factor at 250 Hz, a panel follows the mass law to 5000 Hz, passable porous layers stay flat
		const float keep_lf = std::clamp(materials[m].occlusion_factor, 0.f, 1.f);
		const float keep_hf = materials[m].passable ? keep_lf : keep_lf > 0.f ? 1.f / std::sqrt(1.f + mass_law_ratio * (1.f / (keep_lf * keep_lf) - 1.f)) : 0.f;
		set(vaWorldSetMaterialFlatTransmissionLF, "vaWorldSetMaterialFlatTransmissionLF", "flat_transmission_lf", 1.f - keep_lf, 1.f);
		set(vaWorldSetMaterialFlatTransmissionHF, "vaWorldSetMaterialFlatTransmissionHF", "flat_transmission_hf", 1.f - keep_hf, 1.f);
		material_id[m] = id;
		++used;
	}
	xr_delete(ltx);
	if (unmapped)
		Msg("SOUND: Raytrace: %u materials unmapped, each uses its default", unmapped);

	// The direct path skips what the upload skipped, and a passable material lets it through
	model = M;
	direct_static.assign(count, 0);
	direct_passable.assign(count, 0);
	for (u32 m = 0; m < count; ++m)
	{
		direct_passable[m] = materials[m].passable;
		direct_static[m] = material_id[m] && !materials[m].passable;
	}

	u32 aliases = 0;
	dynamic_material.assign(count, 0);
	for (u32 m = 0; m < count; ++m)
	{
		if (like_of[m] != u32(-1))
			dynamic_material[m] = material_id[like_of[m]];
		aliases += wanted[m] && !per_material[m] && material_id[m];
	}

	// Triangle ids grouped by material
	xr_vector<u32> first(count + 1, 0);
	for (u32 m = 0; m < count; ++m)
		first[m + 1] = first[m] + (material_id[m] ? per_material[m] : 0);
	xr_vector<u32> order(first[count]);
	{
		xr_vector<u32> fill;
		fill.assign(first.begin(), first.end() - 1);
		for (u32 i = 0; i < tri_count; ++i)
		{
			const u32 m = tris[i].material;
			if (m < count && material_id[m])
				order[fill[m]++] = i;
		}
	}

	// A ray pays a flat crossing once per mesh it touches, so each material gets one mesh per 2 m cell
	struct Cell
	{
		int x, y, z;
		u32 tri;
		bool operator<(const Cell& c) const { return std::tie(x, y, z, tri) < std::tie(c.x, c.y, c.z, c.tri); }
	};
	xr_vector<Cell> cells;
	xr_vector<u32> runs;

	VAMatrix identity{};
	identity.m11 = identity.m22 = identity.m33 = identity.m44 = 1.f;
	Fbox world_box;
	world_box.invalidate();
	xr_vector<VAVector> buffer;
	u32 uploaded = 0;
	float lowest = 1.f;
	// A triangle whose float cross product is zero has no face to reflect off
	u32 degenerate = 0;
	// The SDK reflects off the face CFORM treats as the back, so corners go in reverse order
	constexpr u32 corner_order[3] = { 0, 2, 1 };
	for (u32 m = 0; m < count; ++m)
	{
		if (first[m + 1] == first[m])
			continue;

		// Triangles sorted by the cell of their centroid, each run of one cell becomes a mesh
		runs.assign(1, first[m]);
		cells.clear();
		for (u32 k = first[m]; k < first[m + 1]; ++k)
		{
			const CDB::TRI& T = tris[order[k]];
			Fvector c;
			c.add(verts[T.verts[0]], verts[T.verts[1]]).add(verts[T.verts[2]]).div(3.f * mesh_split);
			cells.push_back({ iFloor(c.x), iFloor(c.y), iFloor(c.z), order[k] });
		}
		std::sort(cells.begin(), cells.end());
		for (u32 i = 0; i < u32(cells.size()); ++i)
		{
			order[first[m] + i] = cells[i].tri;
			if (i && std::tie(cells[i].x, cells[i].y, cells[i].z) != std::tie(cells[i - 1].x, cells[i - 1].y, cells[i - 1].z))
				runs.push_back(first[m] + i);
		}
		runs.push_back(first[m + 1]);

		for (u32 r = 0; r + 1 < u32(runs.size()); ++r)
		{
			const u32 n = runs[r + 1] - runs[r];
			buffer.resize(n * 3);
			Fbox box;
			box.invalidate();
			VAVector* out = buffer.data();
			u32 kept = 0;
			for (u32 k = runs[r]; k < runs[r + 1]; ++k)
			{
				const CDB::TRI& T = tris[order[k]];
				Fvector e1, e2, normal;
				e1.sub(verts[T.verts[corner_order[1]]], verts[T.verts[corner_order[0]]]);
				e2.sub(verts[T.verts[corner_order[2]]], verts[T.verts[corner_order[0]]]);
				normal.crossproduct(e1, e2);
				if (normal.x == 0.f && normal.y == 0.f && normal.z == 0.f)
				{
					++degenerate;
					continue;
				}
				for (u32 v = 0; v < 3; ++v)
				{
					const Fvector& p = verts[T.verts[corner_order[v]]];
					*out++ = va_vector(p);
					box.modify(p);
				}
				++kept;
			}
			if (!kept)
				continue;

			VAMeshPrimitive* mesh = nullptr;
			if (!check(vaMeshPrimitiveCreate(VAMaterialType(material_id[m]), buffer.data(), int(kept * 3), va_vector(box.min), va_vector(box.max), &identity, &mesh), "vaMeshPrimitiveCreate"))
				continue;
			// The level is an open soup, so a crossing costs a flat share
			check(vaMeshPrimitiveSetUseFlatTransmission(mesh, true), "vaMeshPrimitiveSetUseFlatTransmission");
			if (!check(vaWorldAddPrimitive_(world, mesh), "vaWorldAddPrimitive"))
			{
				check(vaMeshPrimitiveDestroy(mesh), "vaMeshPrimitiveDestroy");
				continue;
			}
			meshes.push_back(mesh);
			uploaded += kept;
			lowest = std::min({ lowest, vaWorldGetMaterialAbsorptionLF(world, material_id[m]), vaWorldGetMaterialAbsorptionHF(world, material_id[m]) });
			world_box.merge(box);
		}
	}
	xr_vector<VAVector>().swap(buffer);
	xr_vector<u32>().swap(order);
	xr_vector<Cell>().swap(cells);

	if (meshes.empty())
	{
		Msg("! SOUND: Raytrace: no triangles uploaded, raytracing stays off");
		release();
		return;
	}

	Fvector size, center;
	size.sub(world_box.max, world_box.min);
	world_box.getcenter(center);
	check(vaWorldSetPosition(world, va_vector(world_box.min)), "vaWorldSetPosition");
	check(vaWorldSetSize(world, va_vector(size)), "vaWorldSetSize");
	bounds.set(world_box);
	bounds.grow(EPS_L);

	string_path level_name;
	xr_strcpy(level_name, FS.get_path("$level$")->m_Add);
	if (const size_t len = xr_strlen(level_name); len && level_name[len - 1] == '\\')
		level_name[len - 1] = 0;

	// Weather beds take the ambient filter unless the level's weather cycle is indoors, read from game.ltx as the weather manager reads it
	ambient = true;
	ambient_ready = false;
	ambient_lf = ambient_hf = 1.f;
	ambient_wait = -1.f;
	if (ambient)
	{
		string_path file;
		FS.update_path(file, "$game_config$", "game.ltx");
		CInifile game(file, TRUE, TRUE, FALSE);
		LPCSTR cycle = game.line_exist(level_name, "weathers") ? game.r_string(level_name, "weathers") : nullptr;
		if (!cycle)
			cycle = "[default]";
		if (!xr_strcmp(cycle, "indoor") || !strncmp(cycle, "w_indoor_", 9))
			ambient = false;
	}

	// Reverb only listener at the camera, the Godot listener counts
	listener = vaEmitterCreate();
	if (!listener)
	{
		Msg("! SOUND: Raytrace: vaEmitterCreate failed, raytracing stays off");
		release();
		return;
	}
	// Emitter messages reach the game log beside the world's
	check(vaEmitterSetLogCallback(listener, sdk_log), "vaEmitterSetLogCallback");
	check(vaEmitterSetLogErrorCallback(listener, sdk_log), "vaEmitterSetLogErrorCallback");
	check(vaEmitterSetReverbRayCount(listener, 256), "vaEmitterSetReverbRayCount");
	// Reverb rays run to 30 dB down, so the decay fit sees most of the tail
	check(vaEmitterSetMinimumReverbEnergy(listener, 0.001f), "vaEmitterSetMinimumReverbEnergy");
	check(vaEmitterSetMinimumPermeationEnergy(listener, permeation_floor), "vaEmitterSetMinimumPermeationEnergy");
	check(vaEmitterSetMinimumAmbientPermeationEnergy(listener, permeation_floor), "vaEmitterSetMinimumAmbientPermeationEnergy");
	// Reverb rays bounce until the least absorbing surface takes them under the energy floor
	const double bounces = std::ceil(std::log(double(vaEmitterGetMinimumReverbEnergy(listener))) / std::log1p(-double(lowest)));
	if (bounces < double(INT_MAX))
		check(vaEmitterSetReverbBounceCount(listener, std::max(1, int(bounces))), "vaEmitterSetReverbBounceCount");
	else
		Msg("! SOUND: Raytrace: absorption %.3f never takes reverb rays under the energy floor, the SDK bounce count stays", lowest);
	// The SDK shares returned energy over every bounce, so the cap keeps the reverb level of 64 bounces
	constexpr float reference_bounces = 64.f;
	const float cap = vaEmitterGetReverbEnergyCap(listener) * reference_bounces / vaEmitterGetReverbBounceCount(listener);
	check(vaEmitterSetReverbEnergyCap(listener, std::min(cap, 1.f)), "vaEmitterSetReverbEnergyCap");
	check(vaEmitterSetScatteringSeed(listener, 1), "vaEmitterSetScatteringSeed");
	// A 20 s echogram holds a concrete hall's whole tail, 50 ms bins keep the first arrival sharp for the decay fit
	check(vaEmitterSetMaxEchogramTime(listener, 20000), "vaEmitterSetMaxEchogramTime");
	check(vaEmitterSetEchogramGranularity(listener, 50), "vaEmitterSetEchogramGranularity");
	// Ambient occlusion rays count the energy that escapes to open sky
	check(vaEmitterSetAmbientOcclusionRayCount(listener, 512), "vaEmitterSetAmbientOcclusionRayCount");
	check(vaEmitterSetAmbientOcclusionBounceCount(listener, 8), "vaEmitterSetAmbientOcclusionBounceCount");
	if (ambient)
	{
		// Permeation rays add the energy that crosses walls, and each share caps where a bed plays at full volume
		check(vaEmitterSetAmbientPermeationRayCount(listener, ambient_permeation_rays), "vaEmitterSetAmbientPermeationRayCount");
		check(vaEmitterSetAmbientPermeationBounceCount(listener, ambient_permeation_bounces), "vaEmitterSetAmbientPermeationBounceCount");
		check(vaEmitterSetAmbientOcclusionEnergyCap(listener, ambient_cap), "vaEmitterSetAmbientOcclusionEnergyCap");
		check(vaEmitterSetAmbientPermeationEnergyCap(listener, ambient_cap), "vaEmitterSetAmbientPermeationEnergyCap");
		check(vaEmitterSetAmbientGainFormula(listener, ambient_gain), "vaEmitterSetAmbientGainFormula");
	}
	// A full sweep of the 512 trails every 32 updates, a trail recast once its first hit moves a metre
	check(vaEmitterSetTrailRefreshCount(listener, 16), "vaEmitterSetTrailRefreshCount");
	check(vaEmitterSetRefreshDistanceThreshold(listener, 1.f), "vaEmitterSetRefreshDistanceThreshold");
	check(vaEmitterSetPosition(listener, va_vector(center)), "vaEmitterSetPosition");
	listener_last = still_at = trace_from = center;
	walked = still_time = 0.f;
	check(vaWorldAddEmitter(world, listener), "vaWorldAddEmitter");

	// Muffle listener at the camera, occlusion rays find each source and permeation rays cross walls to it
	muffle = vaEmitterCreate();
	if (muffle)
	{
		check(vaEmitterSetLogCallback(muffle, sdk_log), "vaEmitterSetLogCallback");
		check(vaEmitterSetLogErrorCallback(muffle, sdk_log), "vaEmitterSetLogErrorCallback");
		check(vaEmitterSetScatteringSeed(muffle, 1), "vaEmitterSetScatteringSeed");
		check(vaEmitterSetMinimumPermeationEnergy(muffle, permeation_floor), "vaEmitterSetMinimumPermeationEnergy");
		check(vaEmitterSetMinimumAmbientPermeationEnergy(muffle, permeation_floor), "vaEmitterSetMinimumAmbientPermeationEnergy");
		check(vaEmitterSetPosition(muffle, va_vector(center)), "vaEmitterSetPosition");
		// A full sweep of the 512 trails every 32 updates, a trail recast once its first hit moves a metre
		check(vaEmitterSetTrailRefreshCount(muffle, 16), "vaEmitterSetTrailRefreshCount");
		check(vaEmitterSetRefreshDistanceThreshold(muffle, 1.f), "vaEmitterSetRefreshDistanceThreshold");
		set_muffling();
		// The muffle listener holds every source as a target and hears each group at a gain and from a direction of its own
		check(vaEmitterSetHasRelativeReverb(muffle, true), "vaEmitterSetHasRelativeReverb");
		check(vaEmitterSetRelativeReverbInnerThreshold(muffle, group_inner), "vaEmitterSetRelativeReverbInnerThreshold");
		check(vaEmitterSetRelativeReverbOuterThreshold(muffle, group_outer), "vaEmitterSetRelativeReverbOuterThreshold");
		check(vaWorldAddEmitter(world, muffle), "vaWorldAddEmitter");
	}
	else
		Msg("! SOUND: Raytrace: vaEmitterCreate failed for the muffle listener, sources keep the stock occlusion");
	// Threads and the optional parts of the load, for the summary line
	// Both counts are the SDK's own defaults, read back since the C header documents neither
	string512 parts;
	xr_sprintf(parts, ", %d threads, %d work items", vaWorldGetMaximumConcurrencyLevel(world), vaWorldGetWorkItemCount(world));
	string64 part;
	xr_sprintf(part, ", %d groups", vaWorldGetMaximumGroupedEAXCount(world));
	xr_strcat(parts, part);
	xr_sprintf(part, ", permeation floors listener %.4f ambient %.4f", vaEmitterGetMinimumPermeationEnergy(listener), vaEmitterGetMinimumAmbientPermeationEnergy(listener));
	xr_strcat(parts, part);
	if (muffle)
	{
		xr_sprintf(part, ", muffle %.4f ambient %.4f", vaEmitterGetMinimumPermeationEnergy(muffle), vaEmitterGetMinimumAmbientPermeationEnergy(muffle));
		xr_strcat(parts, part);
	}

	check(vaWorldSetName(world, level_name), "vaWorldSetName");

	// Occluder poses go to the SDK as memcpy'd Fmatrix, so the load checks a turned and moved pose reads the same both ways
	{
		Fmatrix turn, move, pose;
		turn.rotateY(0.5f);
		move.translate(1.f, 2.f, 3.f);
		pose.mul_43(move, turn);
		const VAMatrix va_turn = vaMatrixCreateRotationY(0.5f), va_move = vaMatrixCreateTranslation(1.f, 2.f, 3.f);
		const VAMatrix expected = vaMatrixMultiply(&va_turn, &va_move);
		CSound_occluder_shape probe;
		probe.type = sos_Sphere;
		probe.material = 0;
		probe.size.set(0.f, 0.f, 0.f);
		const VAMatrix sent = va_matrix(pose, probe);
		const float* a = &sent.m11;
		const float* b = &expected.m11;
		const bool layout = std::equal(a, a + 16, b, [](float x, float y) { return std::abs(x - y) <= 1e-5f; });
		if (layout)
		{
			xr_sprintf(part, ", %u alias materials", aliases);
			xr_strcat(parts, part);
		}
		else
			Msg("! SOUND: Raytrace: dynamic occluders stay off, the pose layout differs from the SDK");
		xrCriticalSectionGuard occluder_guard(occluder_lock);
		occluders_open = layout;
	}

	xr_sprintf(part, ", %u zero-area triangles skipped", degenerate);
	xr_strcat(parts, part);

	// The first update sizes the buffers and takes in every mesh, so it runs inside the loading event
	const float upload_ms = upload_timer.GetElapsed_sec() * 1000.f;
	check(vaWorldUpdate(world), "vaWorldUpdate");
	Msg("SOUND: Raytrace: %s: %u triangles, %u skipped, %u meshes, %u materials, box %.0f x %.0f x %.0f, reverb bounces %d at absorption %.3f, energy cap %.4f, %.0f ms, first update %.0f ms, commit %+.0f MB%s",
		level_name, uploaded, skipped, u32(meshes.size()), used, size.x, size.y, size.z, vaEmitterGetReverbBounceCount(listener), lowest,
		vaEmitterGetReverbEnergyCap(listener),
		upload_ms, upload_timer.GetElapsed_sec() * 1000.f - upload_ms, commit_mb(commit_base), parts);

	has_reverb = false;
	room = false;
	first_result = true;
	ready.store(true, std::memory_order_release);
}

void CSoundRender_Raytrace::destroy()
{
	ready.store(false, std::memory_order_release);
	xrCriticalSectionGuard guard(lock);
	release();
	if (asked.load(std::memory_order_relaxed))
		publish(false);
}

bool CSoundRender_Raytrace::running() const
{
	return ready.load(std::memory_order_acquire);
}

void CSoundRender_Raytrace::release()
{
	if (!world)
		return;

	// A batch still on the worker reads CFORM and the occluders, so it ends unread before they go
	ray_task.wait();
	rays_out = false;
	ray_targets.clear();
	close_queries();

	// Finished teardowns give their handles back here
	teardowns.erase(std::remove_if(teardowns.begin(), teardowns.end(), [](HANDLE h)
	{
		if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0)
			return false;
		CloseHandle(h);
		return true;
	}), teardowns.end());

	Teardown* t = xr_new<Teardown>();
	t->world = world;
	t->listener = listener;
	t->meshes.swap(meshes);
	// Sources stay with their engine emitters until released, their SDK side goes with the world
	if (muffle)
		t->emitters.push_back(muffle);
	for (CSoundRender_RaytraceSource*& S : sources)
	{
		if (S->emitter)
			t->emitters.push_back(S->emitter);
		S->emitter = nullptr;
		S->owner = nullptr;
		S->traced = false;
		S->reverb = false;
		S->group = -1;
		if (!S->users)
			xr_delete(S);
	}
	sources.erase(std::remove(sources.begin(), sources.end(), nullptr), sources.end());
	group_sources = 0;
	tails.clear();
	group_count = 0;
	std::fill(std::begin(group_heard), std::end(group_heard), false);
	t->emitters.insert(t->emitters.end(), removed.begin(), removed.end());
	removed.clear();
	freed.clear();
	releases.clear();
	{
		xrCriticalSectionGuard occluder_guard(occluder_lock);
		occluders_open = false;
		commands.clear();
		queued_shapes.clear();
		queued_poses.clear();
		occluder_owners.clear();
	}
	for (CSoundRender_RaytraceOccluder& O : occluders)
	{
		for (const CSoundRender_RaytracePrimitive& p : O.shapes)
		{
			if (p.primitive)
				t->primitives.push_back(p);
		}
	}
	occluders.clear();
	t->primitives.insert(t->primitives.end(), parked.begin(), parked.end());
	parked.clear();
	occluder_moves = 0;
	// The teardown thread frees them after the world, so their removal callbacks stop reaching this object
	for (VAEmitter* e : t->emitters)
		check(vaEmitterSetUserData(e, nullptr), "vaEmitterSetUserData");
	{
		xrCriticalSectionGuard relcase_guard(relcase_lock);
		relcased.clear();
	}
	world = nullptr;
	listener = nullptr;
	muffle = nullptr;
	muffle_rays = false;
	ambient_ready = false;
	reset_updates = 0;
	formula = false;
	open_air = false;
	open_air_absorption.clear();
	direct_moves = 0;
	model = nullptr;

	if (HANDLE h = thread_spawn_ex(teardown, "X-Ray Raytrace Teardown", 0, t))
		teardowns.push_back(h);
	else
		teardown(t);
}

void CSoundRender_Raytrace::frame(const Fvector& P, const Fvector& D, const Fvector& N, float dt)
{
	if (!ready.load(std::memory_order_acquire))
		return;
	xrCriticalSectionTryGuard guard(lock);
	if (!guard.owns_lock() || !world)
		return;

	// The player spawns and sets the camera in the game frame before this update, so a held room waits for one trace from there and its handback
	const bool holding = held.load(std::memory_order_relaxed);
	const bool latched = held_latch.load(std::memory_order_relaxed);
	if (latched || holding)
	{
		room = false;
		held_updates = 2;
	}
	// Scripts keep reading nothing until the cleared room is published
	if (latched)
	{
		if (asked.load(std::memory_order_relaxed))
			publish(false);
		held_latch.store(false, std::memory_order_release);
	}
	take_rays();
	if (muffle && muffle_rays != formula)
	{
		set_formula(!formula);
		retrace = true;
	}
	if (muffle && formula != open_air)
		set_open_air(!open_air);
	drain();
	apply_occluders();
	// An idle source goes once its owner is gone, the listener leaves its range or muffling stops
	for (size_t i = sources.size(); i-- > 0;)
	{
		CSoundRender_RaytraceSource* S = sources[i];
		if (!S->users && (!S->owner || !muffle_rays || P.distance_to(S->position) > S->range))
			remove(S);
	}
	// Each source sits where the first of its emitters in range was last tick, and the lead goes to whichever asks first next
	for (CSoundRender_RaytraceSource* S : sources)
	{
		S->lead = nullptr;
		if (S->emitter && !S->position.similar(S->sent))
		{
			S->sent = S->position;
			check(vaEmitterSetPosition(S->emitter, va_vector(S->position)), "vaEmitterSetPosition");
		}
	}
	if (muffle)
		check(vaEmitterSetPosition(muffle, va_vector(P)), "vaEmitterSetPosition");
	// New ray counts or a jumped source leave a cache that misses permeation, so every trail is cast again
	// Targets added during a trace that started with none can keep part of a result, so the reset waits for a target and muffle rays
	if (retrace && muffle_rays && std::any_of(sources.begin(), sources.end(), [](const CSoundRender_RaytraceSource* S) { return S->emitter; }))
	{
		retrace = false;
		reset_updates = 2;
		check(vaEmitterResetTrails(muffle), "vaEmitterResetTrails");
	}
	check(vaEmitterSetPosition(listener, va_vector(P)), "vaEmitterSetPosition");
	// Refreshed trails keep part of the last room, so a listener that stops for 1 s after 1.5 m casts them all again
	if (!first_result)
	{
		walked += P.distance_to(listener_last);
		if (P.distance_to(still_at) > 0.05f)
		{
			still_at = P;
			still_time = 0.f;
		}
		else if ((still_time += dt) >= 1.f && walked > 1.5f)
		{
			walked = 0.f;
			check(vaEmitterResetTrails(listener), "vaEmitterResetTrails");
		}
	}
	listener_last = P;
	// The wait for the ambient filter counts from this world's first update
	if (ambient && !ambient_ready)
		ambient_wait = ambient_wait < 0.f ? 0.f : ambient_wait + dt;
	const VAResult result = vaWorldUpdate(world);
	if (result == VA_SUCCESS)
	{
		// The update that starts a reset's trace and the one that hands back its results
		if (reset_updates)
			--reset_updates;
		if (held_updates && !holding)
			--held_updates;
		// The ambient filter stays null until the listener's first trace
		if (ambient)
			if (const VALowPassFilter* filter = vaEmitterGetAmbientFilter(listener))
			{
				ambient_lf = filter->gainLF;
				ambient_hf = filter->gainHF;
				ambient_ready = true;
			}
		if (const VAEAXReverb* eax = vaEmitterGetEAX(listener))
		{
			// The listener's own reverb stays undirected, only the grouped reverbs carry pans
			eax_props(reverb, *eax);
			float decay = eax->decayTime, hf_ratio = eax->decayHFRatio, lf_ratio = eax->decayLFRatio;
			// The SDK stops the mid decay at 20 s, so a pinned mid is rebuilt from the measured band times
			const VAProcessedReverb* measured = decay >= EAX_DECAY_TIME_MAX ? vaEmitterGetProcessedReverb(listener) : nullptr;
			if (measured)
			{
				const bool lf_pinned = lf_ratio <= EAX_DECAY_LF_RATIO_MIN || lf_ratio >= EAX_DECAY_LF_RATIO_MAX;
				const float band = lf_pinned ? measured->measuredDecayTimeHF : measured->measuredDecayTimeLF;
				// A band time the SDK could not fit keeps the SDK's own decay for this update
				if (_valid(band) && band > 0.f)
					decay = std::min(band / (lf_pinned ? hf_ratio : lf_ratio), 60.f);
			}
			hf_ratio = bound_decay_ratio(hf_ratio);
			lf_ratio = bound_decay_ratio(lf_ratio);
			reverb.flDecayTime = decay;
			reverb.flDecayHFRatio = hf_ratio;
			reverb.flDecayLFRatio = lf_ratio;
			has_reverb = true;
			first_result = false;
			// The room for scripts comes from the processed reverb and the ambient occlusion rays, read only once a script asks
			if (asked.load(std::memory_order_relaxed) && !held_updates)
				if (const VAProcessedReverb* processed = measured ? measured : vaEmitterGetProcessedReverb(listener))
				{
					room = true;
					++room_serial;
					room_position = trace_from;
					room_sky = vaEmitterGetOutsidePercent(listener);
					room_returned = processed->returnedPercent;
					room_decay_lf = _valid(processed->measuredDecayTimeLF) && processed->measuredDecayTimeLF > 0.f ? processed->measuredDecayTimeLF : -1.f;
					room_decay_hf = _valid(processed->measuredDecayTimeHF) && processed->measuredDecayTimeHF > 0.f ? processed->measuredDecayTimeHF : -1.f;
					room_absorption_lf = processed->materialAbsorptionLF;
					room_absorption_hf = processed->materialAbsorptionHF;
					room_roughness = processed->materialRoughness;
				}
		}
		// The results above came from the trace the last successful update started, this one starts the next from P
		trace_from = P;
		// Pans go from the SDK's world direction to the listener's right, up and forward
		Fvector R;
		R.crossproduct(N, D);
		const VAEAXReverb** grouped = vaWorldGetGroupedEAX(world);
		group_count = grouped ? vaWorldGetGroupedEAXCount(world) : 0;
		for (int i = 0; i < sdef_reverb_groups; ++i)
		{
			const VAEAXReverb* eax = i < group_count ? grouped[i] : nullptr;
			const float* gain = eax && muffle ? vaEAXReverbGetRelativeGain(eax, muffle) : nullptr;
			const VAVector* dir = eax && muffle ? vaEAXReverbGetRelativeDirection(eax, muffle) : nullptr;
			// A group the muffle listener has no gain or direction for yet stays unheard
			group_heard[i] = gain && dir;
			if (!group_heard[i])
				continue;
			EFXEAXREVERBPROPERTIES& G = group_props[i];
			eax_props(G, *eax);
			G.flDecayHFRatio = bound_decay_ratio(G.flDecayHFRatio);
			G.flDecayLFRatio = bound_decay_ratio(G.flDecayLFRatio);
			Fvector d;
			d.set(dir->x, dir->y, dir->z);
			G.flReflectionsPan[0] = G.flLateReverbPan[0] = d.dotproduct(R);
			G.flReflectionsPan[1] = G.flLateReverbPan[1] = d.dotproduct(N);
			G.flReflectionsPan[2] = G.flLateReverbPan[2] = d.dotproduct(D);
			group_gain[i] = std::clamp(*gain, 0.f, 1.f);
		}
		for (CSoundRender_RaytraceSource* S : sources)
		{
			if (!S->reverb)
				continue;
			// The first group is taken at once, a move to another waits for repeated readings
			const int g = vaEmitterGetGroupedEAXIndex(S->emitter);
			if (g == S->group)
				S->group_reads = 0;
			else if (S->group < 0 || (g == S->group_read && ++S->group_reads >= group_switch))
			{
				S->group = g;
				S->group_reads = 0;
			}
			else if (g != S->group_read)
			{
				S->group_read = g;
				S->group_reads = 1;
			}
		}
		for (CSoundRender_RaytraceSource* S : sources)
		{
			if (!S->emitter)
				continue;
			if (S->stale)
			{
				--S->stale;
				continue;
			}
			if (const VALowPassFilter* f = vaEmitterGetTargetFilter(muffle, S->emitter))
			{
				if (formula)
				{
					S->formula_lf = f->gainLF;
					S->formula_hf = f->gainHF;
					S->gain_lf = S->direct_lf + (1.f - S->direct_lf) * S->formula_lf;
					S->gain_hf = S->direct_hf + (1.f - S->direct_hf) * S->formula_hf;
				}
				else
				{
					S->gain_lf = f->gainLF;
					S->gain_hf = f->gainHF;
				}
				S->traced = true;
			}
		}
	}
	else
		check(result, "vaWorldUpdate");

	// A traced source falls due once it or the listener moves a short wavelength, or an occluder moves
	// Due sources take turns oldest first within the window's ray budget, a waiting one keeps its last V
	if (formula)
	{
		// A window's budgets fill with its time and lapse unspent when it closes, as a 60 fps frame's do
		ray_time += dt;
		const float fill = ray_time >= ray_window ? 1.f : ray_time / ray_window;
		const float step = speed_of_sound / direct_frequency[1];
		const bool moved = occluder_moves != direct_moves;
		direct_moves = occluder_moves;
		++direct_tick;
		direct_queue.clear();
		for (CSoundRender_RaytraceSource* S : sources)
		{
			if (!S->emitter || !S->traced)
				continue;
			if (!S->direct_since && (S->direct_due || moved || S->direct_listener.distance_to_sqr(P) > step * step ||
				S->direct_source.distance_to_sqr(S->position) > step * step))
				S->direct_since = direct_tick;
			if (S->direct_since)
				direct_queue.push_back(S);
		}
		std::stable_sort(direct_queue.begin(), direct_queue.end(), [](const CSoundRender_RaytraceSource* a, const CSoundRender_RaytraceSource* b) { return a->direct_since < b->direct_since; });
		// The worker casts what the window allows and the next tick takes the results
		rays.listener = P;
		rays.positions.clear();
		ray_targets.clear();
		for (CSoundRender_RaytraceSource* S : direct_queue)
		{
			rays.positions.push_back(S->position);
			ray_targets.push_back(S);
		}
		rays.window = direct_window;
		rays.allowance = u32(direct_ray_budget * fill);
		rays.open_air = open_air;
		rays.open_air_allowance = u32(open_air_frame_rays * fill);
		rays.open_air_window = open_air_window;
		drain_queries(fill);
		rays_out = true;
		ray_lapsed = false;
		ray_done.store(false, std::memory_order_relaxed);
		ray_task.run([this]
		{
			cast_rays(rays);
			ray_done.store(true, std::memory_order_release);
		});
		// A frame that ends its window keeps up to one window of time over, so frames of about 1/60 s each get a full budget
		if (ray_time >= ray_window)
		{
			ray_time = std::min(ray_time - ray_window, ray_window);
			direct_window = open_air_window = 0;
			query_window = 0;
			ray_lapsed = true;
		}
	}

	// Removed sources are freed once their removal callback says the SDK threads let go of them
	for (VAEmitter* e : freed)
	{
		drop_tail(e);
		if (check(vaEmitterDestroy(e), "vaEmitterDestroy"))
			removed.erase(std::remove(removed.begin(), removed.end(), e), removed.end());
	}
	freed.clear();
	// Removed occluders are freed once an update has taken them out of the world
	if (result == VA_SUCCESS && !parked.empty())
	{
		parked.erase(std::remove_if(parked.begin(), parked.end(), [this](const CSoundRender_RaytracePrimitive& p)
		{
			return check(destroy_primitive(p.primitive, p.shape.type), "occluder destroy");
		}), parked.end());
	}
	// A held tail past twice its slowest decay is spent, keep false acts only at another removal call
	for (size_t i = releases.size(); i-- > 0;)
	{
		VAEmitter* e = releases[i].first;
		const VAEAXReverb* eax = vaEmitterGetEAX(e);
		const VAProcessedReverb* pr = vaEmitterGetProcessedReverb(e);
		const float decay = std::max(eax ? eax->decayTime : 0.f, pr ? std::max(pr->measuredDecayTimeLF, pr->measuredDecayTimeHF) : 0.f);
		const float threshold = std::max(2.f, 2.f * decay);
		const float age = SoundRender->fTimer_Value - releases[i].second;
		if (age <= threshold)
			continue;
		releases.erase(releases.begin() + i);
		const int keep = vaEmitterSetKeepReverbTailAlive(e, false);
		const int removal = vaWorldRemoveEmitter(world, e);
		check(keep, "vaEmitterSetKeepReverbTailAlive");
		check(removal, "vaWorldRemoveEmitter");
	}
	if (asked.load(std::memory_order_relaxed))
		publish(!holding);
}

// Copies the listener's room and every traced owner's gains where scripts read them, a cleared publish leaves them nothing
void CSoundRender_Raytrace::publish(bool valid)
{
	listener_snapshot.write([&](std::atomic<u32>* w)
	{
		u32 i = 0;
		const auto put = [w, &i](float value) { w[i++].store(snapshot_bits(value), std::memory_order_relaxed); };
		w[i++].store(room_serial, std::memory_order_relaxed);
		w[i++].store((valid && room ? snapshot_valid : 0) | (reverb_overwrite ? 0 : snapshot_applied) | (reverb.iDecayHFLimit ? snapshot_hf_limit : 0),
			std::memory_order_relaxed);
		put(room_position.x);
		put(room_position.y);
		put(room_position.z);
		put(room_sky);
		put(room_returned);
		put(room_decay_lf);
		put(room_decay_hf);
		put(room_absorption_lf);
		put(room_absorption_hf);
		put(room_roughness);
		put(ambient && ambient_ready ? ambient_lf : -1.f);
		put(ambient && ambient_ready ? ambient_hf : -1.f);
		for (const float value : { reverb.flDensity, reverb.flDiffusion, reverb.flGain, reverb.flGainHF, reverb.flGainLF, reverb.flDecayTime,
			reverb.flDecayHFRatio, reverb.flDecayLFRatio, reverb.flReflectionsGain, reverb.flReflectionsDelay, reverb.flLateReverbGain,
			reverb.flLateReverbDelay, reverb.flEchoTime, reverb.flEchoDepth, reverb.flModulationTime, reverb.flModulationDepth,
			reverb.flAirAbsorptionGainHF, reverb.flHFReference, reverb.flLFReference, reverb.flRoomRolloffFactor })
			put(value);
	});
	owner_snapshot.write([&](std::atomic<u32>* w)
	{
		u32 count = 0;
		for (const CSoundRender_RaytraceSource* S : sources)
		{
			if (!valid || count == snapshot_owners)
				break;
			// A new or jumped source's gains wait for a direct path from where it now stands, as the engine's do
			if (!S->owner || !S->emitter || !S->direct_ready)
				continue;
			std::atomic<u32>* e = w + 1 + count++ * 6;
			const u64 key = u64(uintptr_t(S->owner));
			e[0].store(u32(key), std::memory_order_relaxed);
			e[1].store(u32(key >> 32), std::memory_order_relaxed);
			e[2].store(snapshot_bits(S->gain_lf), std::memory_order_relaxed);
			e[3].store(snapshot_bits(S->gain_hf), std::memory_order_relaxed);
			e[4].store(snapshot_bits(S->direct_lf), std::memory_order_relaxed);
			e[5].store(snapshot_bits(S->direct_hf), std::memory_order_relaxed);
		}
		w[0].store(count, std::memory_order_relaxed);
	});
}

void CSoundRender_Raytrace::set_muffling()
{
	muffle_rays = true;
	check(vaEmitterSetOcclusionRayCount(muffle, occlusion_rays), "vaEmitterSetOcclusionRayCount");
	check(vaEmitterSetOcclusionBounceCount(muffle, occlusion_bounces), "vaEmitterSetOcclusionBounceCount");
	check(vaEmitterSetPermeationRayCount(muffle, permeation_rays), "vaEmitterSetPermeationRayCount");
	check(vaEmitterSetPermeationBounceCount(muffle, permeation_bounces), "vaEmitterSetPermeationBounceCount");
}

// The formula and world-bounds loss go on and off together, live sources keep their gains until their next result
void CSoundRender_Raytrace::set_formula(bool value)
{
	formula = value;
	check(vaWorldSetOcclusionRaysLoseEnergyFromWorldBounds(world, value), "vaWorldSetOcclusionRaysLoseEnergyFromWorldBounds");
	if (value)
	{
		// The rays start a window ahead, so the first frames keep a full budget
		ray_time = ray_window;
		direct_window = open_air_window = 0;
		// Shapes that moved while the formula was off take their direct path poses now
		for (CSoundRender_RaytraceOccluder& O : occluders)
		{
			for (CSoundRender_RaytracePrimitive& p : O.shapes)
			{
				if (p.added)
					direct_shape(p);
			}
		}
	}
	for (CSoundRender_RaytraceSource* S : sources)
	{
		if (!S->emitter)
			continue;
		formula_target(S, value);
		S->formula_lf = S->gain_lf;
		S->formula_hf = S->gain_hf;
		S->direct_lf = S->direct_hf = 0.f;
		S->direct_due = true;
		S->direct_since = 0;
	}
}

// A source takes the formula, or the SDK's own gain while it is off
void CSoundRender_Raytrace::formula_target(CSoundRender_RaytraceSource* S, bool value)
{
	check(vaEmitterSetGainFormula(S->emitter, value ? open_air_gain : nullptr), "vaEmitterSetGainFormula");
}

// The measured reference starts from the Zaton constants until its first full sweep
void CSoundRender_Raytrace::set_open_air(bool value)
{
	open_air = value;
	rays.open_air_still = rays.open_air_batch = rays.open_air_sweeps = 0;
	rays.open_air_energy.set(open_air_lf, open_air_hf);
	open_air_reference[0].store(open_air_lf, std::memory_order_relaxed);
	open_air_reference[1].store(open_air_hf, std::memory_order_relaxed);
	if (value)
	{
		Fvector2 none;
		none.set(0.f, 0.f);
		rays.open_air_scores.assign(open_air_rays, none);
		// The rays score a surface by the absorption the upload gave it and pass what the upload skipped, read once per level
		if (open_air_absorption.empty())
		{
			open_air_absorption.resize(direct_static.size());
			for (u32 m = 0; m < open_air_absorption.size(); ++m)
			{
				const int id = material_base + int(m);
				if (vaWorldHasMaterial(world, id))
					open_air_absorption[m].set(vaWorldGetMaterialAbsorptionLF(world, id), vaWorldGetMaterialAbsorptionHF(world, id));
				else
					open_air_absorption[m].set(-1.f, -1.f);
			}
		}
	}
}

// Batches of open air rays from the listener while the window's allowance holds one, each ray keeps 1 minus the absorption of its first surface, an escaped ray keeps 0
// The energy is the mean over all 512 rays, so it follows a walking listener as the last 64 batches
void CSoundRender_Raytrace::open_air_sweep(CSoundRender_RaytraceRays& B) const
{
	const Fvector& P = B.listener;
	const CDB::TRI* tris = model->get_tris();
	B.collider.ray_options(CDB::OPT_ONLYNEAREST | CDB::OPT_CULL);
	bool cast = false;
	for (; B.open_air_window + open_air_batch_rays <= B.open_air_allowance; B.open_air_window += open_air_batch_rays)
	{
		// Static CFORM and fixed directions give the same scores again, so a listener at rest after a full sweep casts nothing
		const bool still = P.x == B.open_air_at.x && P.y == B.open_air_at.y && P.z == B.open_air_at.z;
		if (still && B.open_air_still >= open_air_batches)
			break;
		B.open_air_still = still ? B.open_air_still + 1 : 1;
		B.open_air_at = P;
		cast = true;
		// Four batches in a row cast the rays of one 60 fps frame, every 16th of the sphere
		for (u32 i = B.open_air_batch / open_air_split + open_air_batches / open_air_split * (B.open_air_batch % open_air_split); i < open_air_rays; i += open_air_batches)
		{
			// Fibonacci sphere, the same directions every sweep
			const float y = 1.f - 2.f * (i + 0.5f) / open_air_rays, r = std::sqrt(std::max(0.f, 1.f - y * y)), angle = i * 2.39996323f;
			Fvector a = P, dir;
			dir.set(r * std::cos(angle), y, r * std::sin(angle));
			Fvector2& score = B.open_air_scores[i];
			score.set(0.f, 0.f);
			float left = open_air_range;
			for (u32 pass = 0; pass < open_air_passes && left > 0.f; ++pass)
			{
				B.collider.ray_query(model, a, dir, left);
				if (!B.collider.r_count())
					break;
				const CDB::RESULT& R = *B.collider.r_begin();
				const u32 m = tris[R.id].material;
				if (m < open_air_absorption.size() && open_air_absorption[m].x >= 0.f)
				{
					score.set(1.f - open_air_absorption[m].x, 1.f - open_air_absorption[m].y);
					break;
				}
				// A skipped material lets the ray on from just past its surface
				a.mad(dir, R.range + EPS_L);
				left -= R.range + EPS_L;
			}
		}
		if (++B.open_air_batch == open_air_batches)
		{
			B.open_air_batch = 0;
			++B.open_air_sweeps;
		}
	}
	// A sweep that meets no surface keeps the last energy
	if (cast && B.open_air_sweeps)
	{
		Fvector2 sum;
		sum.set(0.f, 0.f);
		for (const Fvector2& s : B.open_air_scores)
			sum.add(s);
		if (sum.x > 0.f && sum.y > 0.f)
			B.open_air_energy.set(sum.x / open_air_rays, sum.y / open_air_rays);
	}
}

// Nearest solid surface from a to b shortened by trim, CFORM and occluders alike, -1 when the segment is clear
// A leg asks only whether it is blocked, so any solid surface it meets answers it
float CSoundRender_Raytrace::direct_hit(CSoundRender_RaytraceRays& B, const Fvector& a, const Fvector& b, float trim, bool leg, CSoundRender_RaytraceWalk* walk) const
{
	Fvector dir;
	dir.sub(b, a);
	const float d = dir.magnitude();
	const float len = d - trim;
	if (len <= 0.f)
		return -1.f;
	// A ray from a point to itself has no direction, so it counts as blocked where it starts
	if (d < EPS_L)
		return 0.f;
	dir.div(d);
	++B.rays;
	float nearest = -1.f;
	const CDB::TRI* tris = model->get_tris();
	// A leg takes the first CFORM hit, and only a passable one sends it back for every hit
	bool every = true;
	if (leg)
	{
		B.collider.ray_options(CDB::OPT_ONLYFIRST);
		B.collider.ray_query(model, a, dir, len);
		if (B.collider.r_count())
		{
			const CDB::RESULT& R = *B.collider.r_begin();
			const u32 m = tris[R.id].material;
			if (m < direct_static.size() && direct_static[m])
				return R.range;
		}
		else
			every = false;
	}
	if (every)
	{
		B.collider.ray_options(0);
		B.collider.ray_query(model, a, dir, len);
		for (int k = 0; k < B.collider.r_count(); ++k)
		{
			const CDB::RESULT& R = B.collider.r_begin()[k];
			const u32 m = tris[R.id].material;
			if (m < direct_static.size() && direct_static[m] && (nearest < 0.f || R.range < nearest))
				nearest = R.range;
		}
		if (leg && nearest >= 0.f)
			return nearest;
		// A query's line keys every solid hit by material and the cell of its triangle, as the upload grouped them into meshes
		if (walk && !leg)
		{
			const Fvector* verts = model->get_verts();
			for (int k = 0; k < B.collider.r_count(); ++k)
			{
				const CDB::TRI& T = tris[B.collider.r_begin()[k].id];
				const u32 m = T.material;
				if (m >= direct_static.size() || !direct_static[m])
					continue;
				Fvector c;
				c.add(verts[T.verts[0]], verts[T.verts[1]]).add(verts[T.verts[2]]).div(3.f * mesh_split);
				walk->cells.push_back(u64(m) << 48 | u64(u16(iFloor(c.x))) << 32 | u64(u16(iFloor(c.y))) << 16 | u64(u16(iFloor(c.z))));
			}
		}
	}
	// Only shapes whose box meets the segment's box take the exact test
	Fbox span;
	span.invalidate();
	span.modify(a);
	span.modify(Fvector().mad(a, dir, len));
	for (const CSoundRender_RaytraceOccluder& O : occluders)
	{
		// A query passes through the shapes of the object it names
		if (walk && O.owner == walk->skip)
			continue;
		for (const CSoundRender_RaytracePrimitive& p : O.shapes)
		{
			if (!p.added || p.shape.material >= direct_passable.size() || direct_passable[p.shape.material] || !span.intersect(p.box))
				continue;
			const float t = shape_hit(p.shape, p.inverse, a, dir, len);
			if (t >= 0.f && leg)
				return t;
			if (t >= 0.f && (nearest < 0.f || t < nearest))
				nearest = t;
			if (t >= 0.f && walk)
				walk->shapes.push_back(p.shape.material);
		}
	}
	return nearest;
}

// The clear share of the Fresnel disc where the line from the ear first meets a surface per band, 1 on a clear line
Fvector2 CSoundRender_Raytrace::direct_path(CSoundRender_RaytraceRays& B, const Fvector& ear, const Fvector& source, CSoundRender_RaytraceWalk* walk) const
{
	const Fvector& P = ear;
	const float x = direct_hit(B, P, source, direct_margin, false, walk);
	float share[2] = { 1.f, 1.f };
	if (x >= 0.f)
	{
		Fvector dir, u, w, centre;
		dir.sub(source, P);
		const float d = dir.magnitude();
		dir.div(d);
		Fvector::generate_orthonormal_basis(dir, u, w);
		centre.mad(P, dir, x);
		for (int b = 0; b < 2; ++b)
		{
			const float r = std::sqrt(speed_of_sound / direct_frequency[b] * x * (d - x) / d);
			int clear = 0;
			for (int i = 0; i < direct_samples; ++i)
			{
				// Equal-area spiral over the disc
				const float rad = r * std::sqrt((i + 0.5f) / direct_samples), angle = i * 2.39996323f;
				Fvector q;
				q.mad(centre, u, rad * std::cos(angle)).mad(w, rad * std::sin(angle));
				// The first leg runs past q, so a sample on or just before the blocking surface counts as blocked
				clear += direct_hit(B, P, q, -direct_margin, true, walk) < 0.f && direct_hit(B, q, source, direct_margin, true, walk) < 0.f;
			}
			share[b] = float(clear) / direct_samples;
		}
	}
	Fvector2 result;
	result.set(share[0], share[1]);
	return result;
}

// Runs on the worker, reading only what the tick leaves alone until it takes the batch back
void CSoundRender_Raytrace::cast_rays(CSoundRender_RaytraceRays& B) const
{
	B.shares.clear();
	B.rays = 0;
	for (const Fvector& source : B.positions)
	{
		if (B.window + B.rays + direct_path_rays > B.allowance)
			break;
		B.shares.push_back(direct_path(B, B.listener, source, nullptr));
	}
	if (B.open_air)
		open_air_sweep(B);
	if (!B.queries.empty())
		cast_queries(B);
}

// The last tick's batch, a path whose source went meanwhile has nobody to take it
void CSoundRender_Raytrace::take_rays()
{
	if (!rays_out)
		return;
	// A finished batch is read without the task group's wait, which runs long even once its chore is done
	if (!ray_done.load(std::memory_order_acquire))
		ray_task.wait();
	rays_out = false;
	for (u32 i = 0; i < rays.shares.size(); ++i)
	{
		CSoundRender_RaytraceSource* S = ray_targets[i];
		if (!S)
			continue;
		const Fvector2& share = rays.shares[i];
		S->direct_due = false;
		S->direct_since = 0;
		S->direct_ready = true;
		S->direct_listener = rays.listener;
		S->direct_source = rays.positions[i];
		S->direct_lf = share.x;
		S->direct_hf = share.y;
		S->gain_lf = share.x + (1.f - share.x) * S->formula_lf;
		S->gain_hf = share.y + (1.f - share.y) * S->formula_hf;
	}
	if (!ray_lapsed)
	{
		direct_window += rays.rays;
		open_air_window = rays.open_air_window;
	}
	if (rays.open_air)
	{
		open_air_reference[0].store(rays.open_air_energy.x, std::memory_order_relaxed);
		open_air_reference[1].store(rays.open_air_energy.y, std::memory_order_relaxed);
	}
	take_queries();
}

// Script queries after the camera's paths, a path reserves a blocked path's rays as the camera's do, a room goes on batch by batch
void CSoundRender_Raytrace::cast_queries(CSoundRender_RaytraceRays& B) const
{
	for (CSoundRender_RaytraceQuery& Q : B.queries)
	{
		if (Q.room)
		{
			while (Q.next < open_air_rays && B.query_window + B.query_rays + open_air_batch_rays <= B.query_allowance)
			{
				cast_room(B, Q);
				B.query_rays += open_air_batch_rays;
			}
			if (Q.next < open_air_rays)
				break;
		}
		else
		{
			if (B.query_window + B.query_rays + direct_path_rays > B.query_allowance)
				break;
			// The camera's count stays its own
			const u32 camera = B.rays;
			cast_path(B, Q);
			B.query_rays += B.rays - camera;
			B.rays = camera;
		}
		Q.done = true;
	}
}

// The Fresnel share and the crossings of one line, each mesh cell and each moving shape it crosses charged once
void CSoundRender_Raytrace::cast_path(CSoundRender_RaytraceRays& B, CSoundRender_RaytraceQuery& Q) const
{
	CSoundRender_RaytraceWalk& W = B.walk;
	W.skip = Q.skip;
	W.cells.clear();
	W.shapes.clear();
	const Fvector2 share = direct_path(B, Q.ear, Q.source, &W);
	std::sort(W.cells.begin(), W.cells.end());
	W.cells.erase(std::unique(W.cells.begin(), W.cells.end()), W.cells.end());
	Fvector2 through;
	through.set(1.f, 1.f);
	auto charge = [&through](const Fvector2& keep)
	{
		through.x = keep.x < query_keep_floor ? 0.f : through.x * keep.x;
		through.y = keep.y < query_keep_floor ? 0.f : through.y * keep.y;
	};
	for (u64 key : W.cells)
		charge(query_keep[u32(key >> 48)]);
	for (u16 m : W.shapes)
		charge(query_shape_keep[m]);
	CSound_query& A = Q.answer;
	A.room = false;
	A.direct_lf = share.x;
	A.direct_hf = share.y;
	A.through_lf = through.x;
	A.through_hf = through.y;
	A.lf = share.x + (1.f - share.x) * through.x;
	A.hf = share.y + (1.f - share.y) * through.y;
	A.crossings = u32(W.cells.size() + W.shapes.size());
	A.blocked = A.crossings > 0;
}

// One batch of the open air sphere from a room query's point, each ray keeps 1 minus the absorption of its first surface as the listener's sweep scores it
void CSoundRender_Raytrace::cast_room(CSoundRender_RaytraceRays& B, CSoundRender_RaytraceQuery& Q) const
{
	const CDB::TRI* tris = model->get_tris();
	B.collider.ray_options(CDB::OPT_ONLYNEAREST | CDB::OPT_CULL);
	for (const u32 end = Q.next + open_air_batch_rays; Q.next < end; ++Q.next)
	{
		const u32 i = Q.next;
		const float y = 1.f - 2.f * (i + 0.5f) / open_air_rays, r = std::sqrt(std::max(0.f, 1.f - y * y)), angle = i * 2.39996323f;
		Fvector a = Q.ear, dir;
		dir.set(r * std::cos(angle), y, r * std::sin(angle));
		bool hit = false;
		float left = open_air_range;
		for (u32 pass = 0; pass < open_air_passes && left > 0.f; ++pass)
		{
			B.collider.ray_query(model, a, dir, left);
			if (!B.collider.r_count())
				break;
			const CDB::RESULT& R = *B.collider.r_begin();
			const u32 m = tris[R.id].material;
			if (m < open_air_absorption.size() && open_air_absorption[m].x >= 0.f)
			{
				Q.reflect.x += 1.f - open_air_absorption[m].x;
				Q.reflect.y += 1.f - open_air_absorption[m].y;
				hit = true;
				break;
			}
			a.mad(dir, R.range + EPS_L);
			left -= R.range + EPS_L;
		}
		Q.escaped += !hit;
	}
	if (Q.next < open_air_rays)
		return;
	CSound_query& A = Q.answer;
	A.room = true;
	A.open = float(Q.escaped) / open_air_rays;
	A.reflect_lf = Q.reflect.x / open_air_rays;
	A.reflect_hf = Q.reflect.y / open_air_rays;
}

// Posted queries join the batch oldest first, at most four a tick, and a post from before the last release is dropped
void CSoundRender_Raytrace::drain_queries(float fill)
{
	if (!queries_open.load(std::memory_order_relaxed))
	{
		query_window = 0;
		queries_open.store(true, std::memory_order_release);
	}
	rays.query_window = query_window;
	rays.query_allowance = u32(query_ray_budget * fill);
	rays.query_rays = 0;
	CSoundRender_RaytraceSlot* posted[raytrace_query_slots];
	u32 count = 0;
	for (CSoundRender_RaytraceSlot& S : query_slots)
	{
		if (u32(S.head.load(std::memory_order_acquire) & 0xff) == query_posted)
			posted[count++] = &S;
	}
	if (!count)
		return;
	std::sort(posted, posted + count, [](const CSoundRender_RaytraceSlot* a, const CSoundRender_RaytraceSlot* b) { return a->head.load(std::memory_order_relaxed) < b->head.load(std::memory_order_relaxed); });
	const u32 generation = query_generation.load(std::memory_order_relaxed);
	for (u32 i = 0, taken = 0; i < count && taken < query_takes; ++i)
	{
		CSoundRender_RaytraceSlot& S = *posted[i];
		const u32 ticket = u32(S.head.load(std::memory_order_relaxed) >> 8);
		if (S.generation.load(std::memory_order_relaxed) != generation)
		{
			S.head.store(query_head(ticket, query_dropped), std::memory_order_release);
			continue;
		}
		CSoundRender_RaytraceQuery& Q = rays.queries.emplace_back();
		Q.ticket = ticket;
		Q.room = S.room.load(std::memory_order_relaxed);
		Q.ear.set(S.ear[0].load(std::memory_order_relaxed), S.ear[1].load(std::memory_order_relaxed), S.ear[2].load(std::memory_order_relaxed));
		Q.source.set(S.source[0].load(std::memory_order_relaxed), S.source[1].load(std::memory_order_relaxed), S.source[2].load(std::memory_order_relaxed));
		Q.skip = S.skip.load(std::memory_order_relaxed);
		Q.done = false;
		Q.next = Q.escaped = 0;
		Q.reflect.set(0.f, 0.f);
		S.head.store(query_head(ticket, query_taken), std::memory_order_relaxed);
		++taken;
		// The walk reads each material's flat share from the world, once per level and only once a path is asked for
		if (!Q.room && query_keep.empty())
		{
			auto keep = [this](int id)
			{
				Fvector2 k;
				if (id && vaWorldHasMaterial(world, id))
					k.set(1.f - vaWorldGetMaterialFlatTransmissionLF(world, id), 1.f - vaWorldGetMaterialFlatTransmissionHF(world, id));
				else
					k.set(1.f, 1.f);
				return k;
			};
			query_keep.resize(direct_static.size());
			query_shape_keep.resize(dynamic_material.size());
			for (u32 m = 0; m < query_keep.size(); ++m)
				query_keep[m] = keep(direct_static[m] ? material_base + int(m) : 0);
			for (u32 m = 0; m < query_shape_keep.size(); ++m)
				query_shape_keep[m] = keep(dynamic_material[m]);
		}
	}
}

// Answers go to their slots, a room still sweeping waits for the next batch
void CSoundRender_Raytrace::take_queries()
{
	if (!ray_lapsed)
		query_window += rays.query_rays;
	if (rays.queries.empty())
		return;
	for (const CSoundRender_RaytraceQuery& Q : rays.queries)
	{
		if (!Q.done)
			continue;
		CSoundRender_RaytraceSlot& S = query_slots[Q.ticket % raytrace_query_slots];
		const CSound_query& A = Q.answer;
		// A reader that sees any of these values also sees the post that took the slot before them
		std::atomic_thread_fence(std::memory_order_release);
		if (A.room)
		{
			S.open.store(A.open, std::memory_order_relaxed);
			S.reflect_lf.store(A.reflect_lf, std::memory_order_relaxed);
			S.reflect_hf.store(A.reflect_hf, std::memory_order_relaxed);
		}
		else
		{
			S.lf.store(A.lf, std::memory_order_relaxed);
			S.hf.store(A.hf, std::memory_order_relaxed);
			S.direct_lf.store(A.direct_lf, std::memory_order_relaxed);
			S.direct_hf.store(A.direct_hf, std::memory_order_relaxed);
			S.through_lf.store(A.through_lf, std::memory_order_relaxed);
			S.through_hf.store(A.through_hf, std::memory_order_relaxed);
			S.crossings.store(A.crossings, std::memory_order_relaxed);
			S.blocked.store(A.blocked, std::memory_order_relaxed);
		}
		S.head.store(query_head(Q.ticket, query_answered), std::memory_order_release);
	}
	rays.queries.erase(std::remove_if(rays.queries.begin(), rays.queries.end(), [](const CSoundRender_RaytraceQuery& Q) { return Q.done; }), rays.queries.end());
}

// A release drops every ticket of the level, so a later read of one gets false
void CSoundRender_Raytrace::close_queries()
{
	queries_open.store(false, std::memory_order_release);
	query_generation.fetch_add(1, std::memory_order_acq_rel);
	for (CSoundRender_RaytraceSlot& S : query_slots)
	{
		u64 head = S.head.load(std::memory_order_acquire);
		const u32 phase = u32(head & 0xff);
		if (phase == query_posted || phase == query_taken || phase == query_answered)
			S.head.compare_exchange_strong(head, query_head(u32(head >> 8), query_dropped), std::memory_order_acq_rel);
	}
	rays.queries.clear();
	query_keep.clear();
	query_shape_keep.clear();
}

// Destroyed objects stop being keys before any source is looked up again
void CSoundRender_Raytrace::drain()
{
	xr_vector<const void*> gone;
	{
		xrCriticalSectionGuard guard(relcase_lock);
		gone.swap(relcased);
	}
	if (gone.empty())
		return;
	for (CSoundRender_RaytraceSource* S : sources)
	{
		if (S->owner && std::find(gone.begin(), gone.end(), S->owner) != gone.end())
			S->owner = nullptr;
	}
}

void CSoundRender_Raytrace::object_released(const void* owner)
{
	if (!ready.load(std::memory_order_acquire))
		return;
	{
		xrCriticalSectionGuard guard(relcase_lock);
		relcased.push_back(owner);
	}
	// Only owners with shapes queue a remove, so a level unload costs one search per object
	xrCriticalSectionGuard occluder_guard(occluder_lock);
	const auto it = std::find(occluder_owners.begin(), occluder_owners.end(), owner);
	if (!occluders_open || it == occluder_owners.end())
		return;
	occluder_owners.erase(it);
	commands.push_back({ CSoundRender_RaytraceCommand::remove, owner, 0, 0 });
}

bool CSoundRender_Raytrace::occluder_add(const void* owner, const CSound_occluder_shape* shapes, u32 count)
{
	if (!ready.load(std::memory_order_acquire) || !owner || !count)
		return false;
	xrCriticalSectionGuard guard(occluder_lock);
	if (!occluders_open)
		return false;
	commands.push_back({ CSoundRender_RaytraceCommand::add, owner, u32(queued_shapes.size()), count });
	queued_shapes.insert(queued_shapes.end(), shapes, shapes + count);
	if (std::find(occluder_owners.begin(), occluder_owners.end(), owner) == occluder_owners.end())
		occluder_owners.push_back(owner);
	return true;
}

void CSoundRender_Raytrace::occluder_move(const void* owner, const Fmatrix* poses, u32 count)
{
	if (!ready.load(std::memory_order_acquire) || !owner || !count)
		return;
	xrCriticalSectionGuard guard(occluder_lock);
	if (!occluders_open)
		return;
	commands.push_back({ CSoundRender_RaytraceCommand::move, owner, u32(queued_poses.size()), count });
	queued_poses.insert(queued_poses.end(), poses, poses + count);
}

// Runs on the tick before the update, a move is dropped when a later remove in the same batch takes its owner
void CSoundRender_Raytrace::apply_occluders()
{
	{
		xrCriticalSectionGuard guard(occluder_lock);
		batch.swap(commands);
		batch_shapes.swap(queued_shapes);
		batch_poses.swap(queued_poses);
	}
	if (batch.empty())
		return;

	removing.clear();
	for (size_t i = batch.size(); i-- > 0;)
	{
		CSoundRender_RaytraceCommand& C = batch[i];
		if (C.kind == CSoundRender_RaytraceCommand::remove)
			removing.push_back(C.owner);
		else if (C.kind == CSoundRender_RaytraceCommand::move && std::find(removing.begin(), removing.end(), C.owner) != removing.end())
			C.count = 0;
		else if (C.kind == CSoundRender_RaytraceCommand::add)
			removing.erase(std::remove(removing.begin(), removing.end(), C.owner), removing.end());
	}

	for (const CSoundRender_RaytraceCommand& C : batch)
	{
		auto it = std::find_if(occluders.begin(), occluders.end(), [&C](const CSoundRender_RaytraceOccluder& O) { return O.owner == C.owner; });
		switch (C.kind)
		{
		case CSoundRender_RaytraceCommand::add:
		{
			// A second add for a live owner replaces its shapes
			if (it != occluders.end())
			{
				retire(*it);
				occluders.erase(it);
			}
			CSoundRender_RaytraceOccluder& O = occluders.emplace_back();
			O.owner = C.owner;
			O.shapes.resize(C.count);
			for (u32 k = 0; k < C.count; ++k)
			{
				CSoundRender_RaytracePrimitive& p = O.shapes[k];
				p.shape = batch_shapes[C.first + k];
				p.added = false;
				p.primitive = nullptr;
				const int id = p.shape.material < dynamic_material.size() ? dynamic_material[p.shape.material] : 0;
				if (!id)
					continue;
				// Only the dev DLL checks sizes, so a NaN, infinite or negative one stays out here
				if (!valid_size(p.shape))
					continue;
				// Every occluder loses its material's flat share on touch, as the static panels do
				switch (p.shape.type)
				{
				case sos_Box:
					if (VAPrismPrimitive* prism = vaPrismPrimitiveCreate())
					{
						check(vaPrismPrimitiveSetSize(prism, va_vector(p.shape.size)), "vaPrismPrimitiveSetSize");
						check(vaPrismPrimitiveSetMaterial(prism, VAMaterialType(id)), "vaPrismPrimitiveSetMaterial");
						check(vaPrismPrimitiveSetUseFlatTransmission(prism, true), "vaPrismPrimitiveSetUseFlatTransmission");
						p.primitive = prism;
					}
					break;
				case sos_Cylinder:
					if (VACylinderPrimitive* cylinder = vaCylinderPrimitiveCreate())
					{
						check(vaCylinderPrimitiveSetRadius(cylinder, p.shape.size.x), "vaCylinderPrimitiveSetRadius");
						check(vaCylinderPrimitiveSetLength(cylinder, p.shape.size.y), "vaCylinderPrimitiveSetLength");
						check(vaCylinderPrimitiveSetMaterial(cylinder, VAMaterialType(id)), "vaCylinderPrimitiveSetMaterial");
						check(vaCylinderPrimitiveSetUseFlatTransmission(cylinder, true), "vaCylinderPrimitiveSetUseFlatTransmission");
						p.primitive = cylinder;
					}
					break;
				case sos_Sphere:
					if (VASpherePrimitive* sphere = vaSpherePrimitiveCreate())
					{
						check(vaSpherePrimitiveSetRadius(sphere, p.shape.size.x), "vaSpherePrimitiveSetRadius");
						check(vaSpherePrimitiveSetMaterial(sphere, VAMaterialType(id)), "vaSpherePrimitiveSetMaterial");
						check(vaSpherePrimitiveSetUseFlatTransmission(sphere, true), "vaSpherePrimitiveSetUseFlatTransmission");
						p.primitive = sphere;
					}
					break;
				default:
					check(VA_INVALID_VALUE, "occluder shape type");
					break;
				}
			}
			break;
		}
		case CSoundRender_RaytraceCommand::move:
		{
			if (it == occluders.end() || !C.count)
				break;
			if (C.count != it->shapes.size())
			{
				check(VA_INVALID_COUNT, "occluder_move shape count");
				break;
			}
			for (u32 k = 0; k < C.count; ++k)
			{
				CSoundRender_RaytracePrimitive& p = it->shapes[k];
				if (!p.primitive)
					continue;
				const VAMatrix pose = va_matrix(batch_poses[C.first + k], p.shape);
				if (vaMatrixHasScale(&pose) || vaMatrixIsNanOrInfinity(&pose))
				{
					check(VA_INVALID_VALUE, "occluder_move pose");
					continue;
				}
				int result;
				switch (p.shape.type)
				{
				case sos_Box: result = vaPrismPrimitiveSetTransform(static_cast<VAPrismPrimitive*>(p.primitive), &pose); break;
				case sos_Cylinder: result = vaCylinderPrimitiveSetTransform(static_cast<VACylinderPrimitive*>(p.primitive), &pose); break;
				default: result = vaSpherePrimitiveSetCenter(static_cast<VASpherePrimitive*>(p.primitive), { pose.m41, pose.m42, pose.m43 }); break;
				}
				if (!check(result, "occluder transform"))
					continue;
				p.pose = batch_poses[C.first + k];
				if (formula)
					direct_shape(p);
				++occluder_moves;
				// A shape joins the world with its first accepted pose
				if (!p.added && check(vaWorldAddPrimitive_(world, p.primitive), "vaWorldAddPrimitive"))
					p.added = true;
			}
			break;
		}
		case CSoundRender_RaytraceCommand::remove:
			if (it != occluders.end())
			{
				retire(*it);
				occluders.erase(it);
			}
			break;
		}
	}
	batch.clear();
	batch_shapes.clear();
	batch_poses.clear();
}

// A shape still in the world leaves it now and is freed after the next successful update, one never added is freed at once
void CSoundRender_Raytrace::retire(CSoundRender_RaytraceOccluder& O)
{
	bool left = false;
	for (const CSoundRender_RaytracePrimitive& p : O.shapes)
	{
		if (!p.primitive)
			continue;
		if (!p.added)
		{
			check(destroy_primitive(p.primitive, p.shape.type), "occluder destroy");
			continue;
		}
		check(vaWorldRemovePrimitive_(world, p.primitive), "vaWorldRemovePrimitive");
		parked.push_back(p);
		left = true;
	}
	// The direct paths retake their lines once a shape leaves the world
	if (left)
		++occluder_moves;
}

// Every emitter of one object shares its source, an ownerless emitter gets its own
bool CSoundRender_Raytrace::acquire(CSoundRender_Emitter* E)
{
	xrCriticalSectionTryGuard guard(lock);
	if (!guard.owns_lock() || !world || !muffle)
		return false;
	drain();

	const void* owner = E->owner_data->g_object;
	CSoundRender_RaytraceSource* S = nullptr;
	if (owner)
	{
		const auto it = std::find_if(sources.begin(), sources.end(), [owner](const CSoundRender_RaytraceSource* s) { return s->owner == owner; });
		if (it != sources.end())
			S = *it;
		// An idle source follows its owner's next sound, and gains from more than the refresh distance away wait for a new trace
		if (S && !S->users && S->emitter && S->position.distance_to(E->p_source.position) > vaEmitterGetRefreshDistanceThreshold(muffle))
		{
			S->position = S->sent = E->p_source.position;
			check(vaEmitterSetPosition(S->emitter, va_vector(S->position)), "vaEmitterSetPosition");
			S->traced = false;
			S->stale = 1;
			retrace = true;
			S->direct_ready = false;
			// A path still on the worker was cast from the old spot, so nobody takes it
			std::replace(ray_targets.begin(), ray_targets.end(), S, static_cast<CSoundRender_RaytraceSource*>(nullptr));
		}
	}
	if (!S)
	{
		VAEmitter* e = vaEmitterCreate();
		if (!e)
		{
			check(VA_INVALID_POINTER, "vaEmitterCreate");
			return false;
		}
		check(vaEmitterSetPosition(e, va_vector(E->p_source.position)), "vaEmitterSetPosition");
		// A source past the box edge is traced from the nearest point on it
		check(vaEmitterSetClampPosition(e, true), "vaEmitterSetClampPosition");
		check(vaEmitterSetMinimumPermeationEnergy(e, permeation_floor), "vaEmitterSetMinimumPermeationEnergy");
		check(vaEmitterSetMinimumAmbientPermeationEnergy(e, permeation_floor), "vaEmitterSetMinimumAmbientPermeationEnergy");
		// A source away from the listener casts its own reverb into a group, its tail stays after removal
		// Interface files kept dry take no group place
		const bool reverb = group_sources < group_budget && listener_last.distance_to(E->p_source.position) > group_near &&
			!E->source()->m_bInterface;
		if (reverb)
		{
			check(vaEmitterSetReverbRayCount(e, group_rays), "vaEmitterSetReverbRayCount");
			check(vaEmitterSetReverbBounceCount(e, group_bounces), "vaEmitterSetReverbBounceCount");
			check(vaEmitterSetAffectsGroupedEAX(e, true), "vaEmitterSetAffectsGroupedEAX");
			check(vaEmitterSetScatteringSeed(e, 1), "vaEmitterSetScatteringSeed");
			check(vaEmitterSetTrailRefreshCount(e, group_refresh), "vaEmitterSetTrailRefreshCount");
			check(vaEmitterSetRefreshDistanceThreshold(e, 1.f), "vaEmitterSetRefreshDistanceThreshold");
			// A 5 s echogram in 100 ms bins
			check(vaEmitterSetMaxEchogramTime(e, 5000), "vaEmitterSetMaxEchogramTime");
			check(vaEmitterSetEchogramGranularity(e, 100), "vaEmitterSetEchogramGranularity");
			check(vaEmitterSetKeepReverbTailAlive(e, true), "vaEmitterSetKeepReverbTailAlive");
		}
		// The first dry and the first reverb source of the session read their floors back
		static bool floor_read[2] = { false, false };
		if (!floor_read[reverb])
		{
			floor_read[reverb] = true;
			Msg("SOUND: Raytrace: %s source permeation floor %.4f ambient %.4f", reverb ? "reverb" : "dry",
				vaEmitterGetMinimumPermeationEnergy(e), vaEmitterGetMinimumAmbientPermeationEnergy(e));
		}
		check(vaEmitterSetLogCallback(e, sdk_log), "vaEmitterSetLogCallback");
		check(vaEmitterSetLogErrorCallback(e, sdk_log), "vaEmitterSetLogErrorCallback");
		check(vaEmitterSetUserData(e, this), "vaEmitterSetUserData");
		check(vaEmitterSetOnRemovedCallback(e, emitter_removed), "vaEmitterSetOnRemovedCallback");
		if (!check(vaWorldAddEmitter(world, e), "vaWorldAddEmitter"))
		{
			check(vaEmitterDestroy(e), "vaEmitterDestroy");
			return false;
		}
		if (!check(vaEmitterAddTarget(muffle, e), "vaEmitterAddTarget"))
		{
			// A source nobody traces plays on the stock ray, its emitter is freed after the removal callback
			removed.push_back(e);
			check(vaWorldRemoveEmitter(world, e), "vaWorldRemoveEmitter");
			return false;
		}
		S = xr_new<CSoundRender_RaytraceSource>();
		*S = { e, owner, nullptr, 0, E->p_source.max_distance, E->p_source.position, E->p_source.position, 1.f, 1.f, false, 0, reverb, -1, -1, 0, 1.f, 1.f, 1.f, 1.f, {}, {}, true, 0, false };
		if (reverb)
			++group_sources;
		if (formula)
			formula_target(S, true);
		// A target added while a reset's trace runs keeps part of that result, so it waits for a reset made with it present
		if (reset_updates)
		{
			S->stale = 1;
			retrace = true;
		}
		sources.push_back(S);
	}
	++S->users;
	E->rt_source = S;
	return true;
}

void CSoundRender_Raytrace::release_source(CSoundRender_Emitter* E)
{
	// The level unload frees idle sources under the lock, so the count drops under it too
	xrCriticalSectionGuard guard(lock);
	CSoundRender_RaytraceSource* S = E->rt_source;
	E->rt_source = nullptr;
	if (S->lead == E)
		S->lead = nullptr;
	// An owner's source waits idle for its next sound, the tick drops it
	if (--S->users || S->owner)
		return;
	remove(S);
}

// A finished tail leaves the list, and the budget if it still held a place there
void CSoundRender_Raytrace::drop_tail(VAEmitter* e)
{
	if (!releases.empty())
		releases.erase(std::remove_if(releases.begin(), releases.end(), [e](const std::pair<VAEmitter*, float>& r) { return r.first == e; }), releases.end());
	const auto it = std::find_if(tails.begin(), tails.end(), [e](const std::pair<VAEmitter*, bool>& t) { return t.first == e; });
	if (it == tails.end())
		return;
	if (it->second)
		--group_sources;
	tails.erase(it);
}

// A source past its range lets go of its target, an owner's idle source goes on the next tick
void CSoundRender_Raytrace::range_release(CSoundRender_Emitter* E)
{
	release_source(E);
}

void CSoundRender_Raytrace::remove(CSoundRender_RaytraceSource* S)
{
	const auto it = std::find(sources.begin(), sources.end(), S);
	if (it == sources.end())
		return;
	sources.erase(it);
	// A path the worker casts for it comes back to nobody
	std::replace(ray_targets.begin(), ray_targets.end(), S, static_cast<CSoundRender_RaytraceSource*>(nullptr));
	// A reverb source's tail plays on after removal, it can keep its budget place until its removal callback
	const bool hold = S->reverb && S->emitter;
	if (S->reverb && !hold)
		--group_sources;
	if (S->emitter)
		detach(S, hold);
	xr_delete(S);
}

// Takes a source's emitter out of the world, the removal callback frees it
void CSoundRender_Raytrace::detach(CSoundRender_RaytraceSource* S, bool hold)
{
	// The removal callback may run inside vaWorldRemoveEmitter
	removed.push_back(S->emitter);
	if (S->reverb)
		tails.emplace_back(S->emitter, hold);
	check(vaEmitterRemoveTarget(muffle, S->emitter), "vaEmitterRemoveTarget");
	// A failed removal brings no callback, so the tail goes now
	const int removal = vaWorldRemoveEmitter(world, S->emitter);
	if (!check(removal, "vaWorldRemoveEmitter"))
	{
		if (S->reverb)
			drop_tail(S->emitter);
	}
	else if (removal == VA_PENDING_REMOVAL && S->reverb)
		releases.emplace_back(S->emitter, SoundRender->fTimer_Value);
}

// Gains for the direct path once the emitter's source is traced, false while the stock ray applies
bool CSoundRender_Raytrace::muffle_gains(CSoundRender_Emitter* E, float& lf, float& hf)
{
	if (!ready.load(std::memory_order_acquire) || !muffle_rays)
	{
		if (E->rt_source)
			release_source(E);
		return E->rt_traced = false;
	}

	// A still ownerless one-shot keeps the values of its single trace
	if (E->rt_traced && !E->rt_source && !E->bMoved)
	{
		lf = std::clamp(E->rt_lf, 0.f, 1.f);
		hf = E->rt_lf > 0.f ? std::clamp(E->rt_hf, 0.f, 1.f) / E->rt_lf : 1.f;
		return true;
	}
	// A source from an unloaded world starts over in this one
	if (E->rt_source && !E->rt_source->emitter)
		release_source(E);
	if (!E->rt_source && !acquire(E))
		return E->rt_traced = false;

	CSoundRender_RaytraceSource* S = E->rt_source;
	S->range = std::max(S->range, E->p_source.max_distance);
	if (!S->lead)
		S->lead = E;
	if (S->lead == E)
		S->position = E->p_source.position;
	// The stock ray holds until the filter and a direct path from the source's current spot are both in
	if (!S->traced || !S->direct_ready)
		return E->rt_traced = false;

	// The SDK's HF gain is absolute, so the filter takes its ratio to LF, above 1 where treble passes better
	E->rt_lf = S->gain_lf;
	E->rt_hf = S->gain_hf;
	E->rt_group = S->group;
	lf = std::clamp(E->rt_lf, 0.f, 1.f);
	hf = E->rt_lf > 0.f ? std::clamp(E->rt_hf, 0.f, 1.f) / E->rt_lf : 1.f;

	const bool looped = E->m_current_state == CSoundRender_Emitter::stPlayingLooped || E->m_current_state == CSoundRender_Emitter::stSimulatingLooped ||
		E->m_current_state == CSoundRender_Emitter::stStartingLooped;
	if (!S->owner && !looped && !E->bMoved)
		release_source(E);
	return E->rt_traced = true;
}

// A weather bed's gains from the listener's ambient filter, false until the filter is ready
bool CSoundRender_Raytrace::ambient_gains(CSoundRender_Emitter* E, float& lf, float& hf)
{
	if (!ready.load(std::memory_order_acquire) || !ambient || !ambient_ready)
	{
		E->rt_ambient.store(false, std::memory_order_relaxed);
		return false;
	}

	lf = std::clamp(ambient_lf, 0.f, 1.f);
	hf = ambient_lf > 0.f ? std::clamp(ambient_hf, 0.f, 1.f) / ambient_lf : 1.f;
	E->rt_ambient.store(true, std::memory_order_relaxed);
	return true;
}

// True while a weather bed waits silent for the ambient filter, until the timeout after the world's first update
bool CSoundRender_Raytrace::ambient_held()
{
	return ready.load(std::memory_order_acquire) && ambient && !ambient_ready && ambient_wait < ambient_timeout;
}

#else

void CSoundRender_Raytrace::create(CDB::MODEL* M, const CSound_material_desc* materials, u32 count)
{
	static bool reported_once = false;
	if (!reported_once)
		Msg("! SOUND: Raytrace: this build has no SDK, raytracing stays off");
	reported_once = true;
}

void CSoundRender_Raytrace::destroy()
{
}

bool CSoundRender_Raytrace::running() const
{
	return false;
}

void CSoundRender_Raytrace::frame(const Fvector& P, const Fvector& D, const Fvector& N, float dt)
{
}

bool CSoundRender_Raytrace::muffle_gains(CSoundRender_Emitter* E, float& lf, float& hf)
{
	return false;
}

bool CSoundRender_Raytrace::ambient_gains(CSoundRender_Emitter* E, float& lf, float& hf)
{
	return false;
}

bool CSoundRender_Raytrace::ambient_held()
{
	return false;
}

void CSoundRender_Raytrace::release_source(CSoundRender_Emitter* E)
{
	E->rt_source = nullptr;
}

void CSoundRender_Raytrace::range_release(CSoundRender_Emitter* E)
{
	E->rt_source = nullptr;
}

void CSoundRender_Raytrace::object_released(const void* owner)
{
}

bool CSoundRender_Raytrace::occluder_add(const void* owner, const CSound_occluder_shape* shapes, u32 count)
{
	return false;
}

void CSoundRender_Raytrace::occluder_move(const void* owner, const Fmatrix* poses, u32 count)
{
}

#endif
