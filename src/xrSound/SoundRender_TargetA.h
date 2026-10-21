#ifndef SoundRender_TargetAH
#define SoundRender_TargetAH
#pragma once

#include "soundrender_Target.h"
#include "soundrender_CoreA.h"

class CSoundRender_TargetA : public CSoundRender_Target
{
	typedef CSoundRender_Target inherited;

public:
	// OpenAL
	ALuint pSource;
	ALuint pBuffers[sdef_target_count];
	float cache_gain;
	float cache_pitch;
	ALuint Slot;

	// Treble and bass shelves on the direct path for raytraced muffling, made on first use
	ALuint direct_filter;
	float cache_hf;
	float cache_lf;
	bool direct_attached;

	// A grouped source sends to its group's slot through a gain filter, the direct filter's gain keeps the dry path
	int send_group;
	ALuint send_filter;
	float cache_send;
	float cache_band;
	// The listener's slot hears the source through the direct filter
	bool send_direct;

	ALuint buf_block;
private:
	void fill_block(ALuint BufferID);
	bool skips_reverb() const;
	int grouped() const;
	void leave_group();
public:
	CSoundRender_TargetA();
	virtual ~CSoundRender_TargetA();

	void SetSlot(ALuint NewSlot);
	virtual BOOL _initialize();
	virtual void _destroy();
	virtual void _restart();

	virtual void start(CSoundRender_Emitter* E);
	virtual void render();
	virtual void rewind();
	virtual void stop();
	virtual void update();
	virtual void fill_parameters();
	void source_changed();
};
#endif
