#pragma once

#include <atomic>
#include <AL/efx-presets.h>
#include "SoundRender.h"

struct VAWorld;
struct VAEmitter;
struct VAMeshPrimitive;
struct VAVisualisationData;
class CSoundRender_Emitter;

// One SDK target per owner object, shared by every emitter the object plays within the refresh distance of it
// An owner's target outlives its sounds while the listener stays within range
struct CSoundRender_RaytraceSource
{
	VAEmitter* emitter;
	const void* owner;
	CSoundRender_Emitter* lead;
	u32 users;
	float range;
	Fvector position;
	Fvector sent;
	float gain_lf;
	float gain_hf;
	bool traced;
	// Results still due from before the last jump
	u32 stale;
	// Casts reverb rays into the grouped reverb, its group changes after repeated equal readings
	bool reverb;
	int group;
	int group_read;
	u32 group_reads;
	// The formula's gains and the direct path's Fresnel share per band, composed into the gains
	float formula_lf;
	float formula_hf;
	float direct_lf;
	float direct_hf;
	// Where the listener and the source stood for the last direct path
	Fvector direct_listener;
	Fvector direct_source;
	bool direct_due;
	// The frame its direct path fell due, 0 while the path is current
	u32 direct_since;
	// Set once a direct path cast from where it now stands came back, its gains wait for it
	bool direct_ready;
};

// One shape of a moving object as an SDK primitive, null when its material has none
struct CSoundRender_RaytracePrimitive
{
	void* primitive;
	CSound_occluder_shape shape;
	bool added;
	// The last pose the SDK accepted
	Fmatrix pose;
	// Its inverse and the world box around the shape, kept for the direct path while the formula is on
	Fmatrix inverse;
	Fbox box;
};

struct CSoundRender_RaytraceOccluder
{
	const void* owner;
	xr_vector<CSoundRender_RaytracePrimitive> shapes;
};

// Adds, moves and removes in the order the game sent them, each owning a run of the shape or pose list
struct CSoundRender_RaytraceCommand
{
	enum : u8 { add, move, remove } kind;
	const void* owner;
	u32 first;
	u32 count;
};

// Script point queries share a ring of 16 slots, each head holds the ticket above the phase
constexpr u32 raytrace_query_slots = 16;
enum : u32 { query_free, query_writing, query_posted, query_taken, query_answered, query_dropped };
inline u64 query_head(u32 ticket, u32 phase) { return u64(ticket) << 8 | phase; }

// A poster fills the request under its own ticket, the tick takes it and writes the answer
struct CSoundRender_RaytraceSlot
{
	std::atomic<u64> head{ 0 };
	std::atomic<u32> generation{ 0 };
	std::atomic<bool> room{ false };
	std::atomic<float> ear[3];
	std::atomic<float> source[3];
	std::atomic<const void*> skip{ nullptr };
	std::atomic<float> lf, hf, direct_lf, direct_hf, through_lf, through_hf;
	std::atomic<u32> crossings{ 0 };
	std::atomic<bool> blocked{ false };
	std::atomic<float> open, reflect_lf, reflect_hf;
};

// A query on the worker, a room sweeps its point over several batches
struct CSoundRender_RaytraceQuery
{
	u32 ticket;
	bool room;
	Fvector ear;
	Fvector source;
	const void* skip;
	bool done;
	CSound_query answer;
	u32 next;
	u32 escaped;
	Fvector2 reflect;
};

// The crossings a query's line meets, static ones keyed by material and mesh cell, and the owner whose shapes it passes
struct CSoundRender_RaytraceWalk
{
	const void* skip;
	xr_vector<u64> cells;
	xr_vector<u16> shapes;
};

// One tick's direct and open air rays, cast on the worker with its own collider and taken back at the next tick
struct CSoundRender_RaytraceRays
{
	CDB::COLLIDER collider;
	Fvector listener;
	// Due sources in turn order, each path that fits the window takes its clear share per band
	xr_vector<Fvector> positions;
	xr_vector<Fvector2> shares;
	u32 window;
	u32 allowance;
	u32 rays;
	// Open air batches while the allowance holds one, the sweep carries over from tick to tick
	bool open_air;
	u32 open_air_allowance;
	u32 open_air_window;
	xr_vector<Fvector2> open_air_scores;
	// Where the last batches were cast and how many ran there in a row, a full sweep from one spot holds while the listener stays
	Fvector open_air_at;
	u32 open_air_still;
	u32 open_air_batch;
	u32 open_air_sweeps;
	Fvector2 open_air_energy;
	// Script queries after the camera's paths, under an allowance of their own
	xr_vector<CSoundRender_RaytraceQuery> queries;
	u32 query_window;
	u32 query_allowance;
	u32 query_rays;
	CSoundRender_RaytraceWalk walk;
};

// A decay ratio above 1 shrinks to 2r/(1+r) so the EFX shelf ends its band at r times the mid decay instead of squaring past it
inline float bound_decay_ratio(float r) { return r > 1.f ? 2.f * r / (1.f + r) : r; }

// Two copies of N words, the one writer fills the copy readers are not pointed at and any thread reads without a lock
template <u32 N>
struct CSoundRender_RaytraceSnapshot
{
	struct Copy
	{
		std::atomic<u32> sequence{};
		std::atomic<u32> words[N]{};
	};
	Copy copies[2];
	std::atomic<u32> current{};

	template <typename F>
	void write(F&& fill)
	{
		const u32 next = current.load(std::memory_order_relaxed) ^ 1;
		Copy& c = copies[next];
		const u32 s = c.sequence.load(std::memory_order_relaxed);
		c.sequence.store(s + 1, std::memory_order_relaxed);
		std::atomic_thread_fence(std::memory_order_release);
		fill(c.words);
		c.sequence.store(s + 2, std::memory_order_release);
		current.store(next, std::memory_order_release);
	}

	// False when the writer kept the copies busy through three tries
	template <typename F>
	bool read(F&& visit) const
	{
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			const Copy& c = copies[current.load(std::memory_order_acquire)];
			const u32 s = c.sequence.load(std::memory_order_acquire);
			if (s & 1)
				continue;
			visit(c.words);
			std::atomic_thread_fence(std::memory_order_acquire);
			if (c.sequence.load(std::memory_order_relaxed) == s)
				return true;
		}
		return false;
	}
};

// The listener's serial, flags, position, room, ambient filter and 20 reverb values, then a count and 6 words per owner
constexpr u32 snapshot_listener_words = 34;
constexpr u32 snapshot_owners = 256;
constexpr u32 snapshot_owner_words = 1 + snapshot_owners * 6;

// Every Vercidium Audio call lives here, the SDK is optional at build and run time
class CSoundRender_Raytrace
{
	VAWorld* world;
	VAEmitter* listener;
	VAEmitter* muffle;
	xr_vector<VAMeshPrimitive*> meshes;
	xr_vector<CSoundRender_RaytraceSource*> sources;
	xr_vector<VAEmitter*> removed;
	xr_vector<VAEmitter*> freed;
	xr_vector<const void*> relcased;
	xrCriticalSection relcase_lock;
	bool muffle_rays;
	bool retrace;
	// Successful updates until the last reset's trace hands back its results
	u32 reset_updates;
	xr_vector<HANDLE> teardowns;
	xr_vector<LPCSTR> reported;
	xrCriticalSection lock;
	// Releases that found the lock taken, with the emitter each came from, the next tick lets them go
	xr_vector<std::pair<CSoundRender_RaytraceSource*, const CSoundRender_Emitter*>> late_releases;
	xrCriticalSection late_lock;
	std::atomic<bool> ready;
	Fbox bounds;

	// Moving occluders, the game fills the queue and the tick applies it
	xr_vector<int> dynamic_material;
	xr_vector<CSoundRender_RaytraceOccluder> occluders;
	xr_vector<CSoundRender_RaytracePrimitive> parked;
	xr_vector<CSoundRender_RaytraceCommand> commands;
	xr_vector<CSound_occluder_shape> queued_shapes;
	xr_vector<Fmatrix> queued_poses;
	xr_vector<CSoundRender_RaytraceCommand> batch;
	xr_vector<CSound_occluder_shape> batch_shapes;
	xr_vector<Fmatrix> batch_poses;
	xr_vector<const void*> occluder_owners;
	xr_vector<const void*> removing;
	xrCriticalSection occluder_lock;
	bool occluders_open;
	u32 occluder_moves;

	// The listener's ambient filter for weather beds, off on a level whose weather cycle is indoors
	bool ambient;
	bool ambient_ready;
	float ambient_lf;
	float ambient_hf;
	// Seconds since the world's first update while the filter is missing
	float ambient_wait;

	// Grouped reverb heard from the listener, sources away from it cast reverb rays and join a group
	u32 group_sources;
	// Removed reverb sources whose tail still plays, and whether each still holds its place in the budget
	xr_vector<std::pair<VAEmitter*, bool>> tails;
	// Held tails under the late release and the sound clock at their removal
	xr_vector<std::pair<VAEmitter*, float>> releases;
	int group_count;
	EFXEAXREVERBPROPERTIES group_props[sdef_reverb_groups];
	float group_gain[sdef_reverb_groups];
	bool group_heard[sdef_reverb_groups];

	// Gain formula with world-bounds loss, and the direct path traced through CFORM and the occluders
	bool formula;
	CDB::MODEL* model;
	xr_vector<u8> direct_static;
	xr_vector<u8> direct_passable;
	// Occluder moves the direct paths last saw
	u32 direct_moves;
	// Frames counted for the due order, and the sources due this frame
	u32 direct_tick;
	xr_vector<CSoundRender_RaytraceSource*> direct_queue;
	// Seconds into the current ray window and the direct rays cast in it
	float ray_time;
	u32 direct_window;

	// The listener's open air energy measured from CFORM rays in place of the Zaton constants, in batches paced by the ray window
	bool open_air;
	// Absorption per game material as the upload set it, read on the first switch on, below 0 where the upload skipped the material
	xr_vector<Fvector2> open_air_absorption;
	u32 open_air_window;

	// Script point queries, posted from any thread and taken by the tick while the direct path runs
	CSoundRender_RaytraceSlot query_slots[raytrace_query_slots];
	std::atomic<u32> query_next;
	// Bumped at every release, a post from before it is dropped
	std::atomic<u32> query_generation;
	std::atomic<bool> queries_open;
	u32 query_window;
	// The share each crossing keeps per game material, static and moving, read from the world on a level's first path query
	xr_vector<Fvector2> query_keep;
	xr_vector<Fvector2> query_shape_keep;

	// The worker's batch, the tick fills it before the launch and reads it once the worker is done, with the sources its paths belong to
	xr_task_group ray_task;
	CSoundRender_RaytraceRays rays;
	xr_vector<CSoundRender_RaytraceSource*> ray_targets;
	// True from the launch until a tick takes the batch back
	bool rays_out;
	// The window closed after the launch, so the batch's counts lapse with it
	bool ray_lapsed;
	// Set by the worker once its batch is written
	std::atomic<bool> ray_done;

	EFXEAXREVERBPROPERTIES reverb;
	bool has_reverb;
	// The listener's room is captured and published for scripts once one of them reads it, the serial counts captures since startup
	mutable std::atomic<bool> asked;
	// Set from a game load's start until the player spawns, then the updates a trace from the player needs before a capture
	std::atomic<bool> held;
	// Set by every hold so an update that misses the lock still clears the room on the next one
	std::atomic<bool> held_latch;
	u32 held_updates;
	// Where the trace in flight started
	Fvector trace_from;
	bool room;
	u32 room_serial;
	Fvector room_position;
	float room_sky;
	float room_returned;
	float room_decay_lf;
	float room_decay_hf;
	float room_absorption_lf;
	float room_absorption_hf;
	float room_roughness;
	CSoundRender_RaytraceSnapshot<snapshot_listener_words> listener_snapshot;
	CSoundRender_RaytraceSnapshot<snapshot_owner_words> owner_snapshot;
	u64 commit_base;
	CTimer upload_timer;
	// The level's world builds on the task pool from a copy of the materials, wait_create takes it in with its first update
	xr_task_group load_task;
	xr_vector<CSound_material_desc> load_materials;
	// sound_raytrace.ltx, the level's name and its weather cycle, read before the launch since the task touches no file
	xr_map<xr_string, xr_map<xr_string, shared_str>> load_ltx;
	string_path load_level;
	bool load_indoor;
	bool built;
	float upload_ms;
	string512 summary_head;
	string512 summary_tail;
	bool first_result;
	// Listener travel since its last full recast, and where it last stood still
	Fvector listener_last;
	Fvector still_at;
	float walked;
	float still_time;

	// The listener's visualisation rays for snd_raytrace_draw, the SDK callback fills them and the render copies them under their lock
	xr_vector<CSound_ray_segment> visual;
	xrCriticalSection visual_lock;
	bool visual_reported;
	bool drawing;
	// The thread that runs vaWorldUpdate while drawing, logged once against the callback's
	std::atomic<DWORD> update_thread;

	// Script reads set monitor_asked and ray requests monitor_requested, the tick takes both with one load
	mutable std::atomic<u32> monitor{ 0 };
	// Seconds the tick keeps capturing after the last read, and a request a missed lock left for the next tick
	float monitor_left;
	bool monitor_rays;
	// Counts scripts asked for per family, 0 for the shipped one, and the counts the tick applied
	std::atomic<int> rays_wanted[srf_count]{};
	int rays_used[srf_count];
	// The smoothed tick time while capturing
	float tick_ms;
	// The last capture, the tick fills its own list and swaps it in under the lock
	xr_vector<CSound_source_info> monitor_fill;
	xr_vector<CSound_source_info> monitor_sources;
	CSound_rays monitor_counts{};
	bool monitor_valid;
	mutable xrCriticalSection monitor_lock;

	bool check(int result, LPCSTR call);
	void build(CDB::MODEL* M, const CSound_material_desc* materials, u32 count);
	void release();
	void set_muffling();
	void set_formula(bool value);
	void formula_target(CSoundRender_RaytraceSource* S, bool value);
	float direct_hit(CSoundRender_RaytraceRays& B, const Fvector& a, const Fvector& b, float trim, bool leg, CSoundRender_RaytraceWalk* walk) const;
	Fvector2 direct_path(CSoundRender_RaytraceRays& B, const Fvector& ear, const Fvector& source, CSoundRender_RaytraceWalk* walk) const;
	void set_open_air(bool value);
	void open_air_sweep(CSoundRender_RaytraceRays& B) const;
	void cast_rays(CSoundRender_RaytraceRays& B) const;
	void take_rays();
	void cast_queries(CSoundRender_RaytraceRays& B) const;
	void cast_path(CSoundRender_RaytraceRays& B, CSoundRender_RaytraceQuery& Q) const;
	void cast_room(CSoundRender_RaytraceRays& B, CSoundRender_RaytraceQuery& Q) const;
	void drain_queries(float fill);
	void take_queries();
	void close_queries();
	void drain();
	bool acquire(CSoundRender_Emitter* E);
	void remove(CSoundRender_RaytraceSource* S);
	void let_go(CSoundRender_RaytraceSource* S, const CSoundRender_Emitter* E);
	void take_late_releases();
	void drop_tail(VAEmitter* e);
	void detach(CSoundRender_RaytraceSource* S, bool hold);
	void apply_occluders();
	void retire(CSoundRender_RaytraceOccluder& O);
	void publish(bool valid);
	static void emitter_removed(VAEmitter* emitter);
	void set_drawing(bool value);
	static void visualise(VAEmitter* emitter, VAVisualisationData* data, int count);
	// The tick itself, false when it found the lock taken or no world
	bool tick(const Fvector& P, const Fvector& D, const Fvector& N, float dt, const xr_vector<CSoundRender_Emitter*>* watch);
	void apply_rays();
	void reset_rays();
	void capture(const Fvector& P, const xr_vector<CSoundRender_Emitter*>& emitters);

public:
	CSoundRender_Raytrace();
	~CSoundRender_Raytrace();

	// Starts the level's world on the task pool, the level load calls wait_create before anything needs it
	void create(CDB::MODEL* M, const CSound_material_desc* materials, u32 count);
	void wait_create();
	void destroy();
	// True from a finished level create until destroy
	bool running() const;
	void frame(const Fvector& P, const Fvector& D, const Fvector& N, float dt, const xr_vector<CSoundRender_Emitter*>& emitters);
	bool listener_reverb(EFXEAXREVERBPROPERTIES& props) const;
	// The last published room, reverb and object gains, read from any thread without the lock
	bool read_acoustics(CSound_acoustics& out) const;
	bool read_reverb(CSound_reverb& out) const;
	bool read_muffling(const void* owner, CSound_muffling& out) const;
	// The last captured sounds and ray counts, read from any thread, and a ray count queued for the tick
	bool read_sources(xr_vector<CSound_source_info>& out) const;
	bool read_rays(CSound_rays& out) const;
	bool request_rays(int family, int count);
	void hold(bool value);
	bool group_reverb(int i, EFXEAXREVERBPROPERTIES& props, float& gain) const;
	bool muffle_gains(CSoundRender_Emitter* E, float& lf, float& hf);
	bool ambient_gains(CSoundRender_Emitter* E, float& lf, float& hf);
	bool ambient_held();
	void release_source(CSoundRender_Emitter* E);
	void range_release(CSoundRender_Emitter* E);
	void object_released(const void* owner);
	bool occluder_add(const void* owner, const CSound_occluder_shape* shapes, u32 count);
	void occluder_move(const void* owner, const Fmatrix* poses, u32 count);
	// Any thread may post and read, a ticket of 0 means the ring or the raytracer took none
	u32 post_query(bool sweep, const Fvector& ear, const Fvector& source, const void* skip);
	int query_result(u32 ticket, CSound_query& out) const;
	bool visualisation(xr_vector<CSound_ray_segment>& segments, u32& bounces);
};
