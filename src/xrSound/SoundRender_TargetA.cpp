#include "stdafx.h"
#pragma hdrstop

#include "soundrender_TargetA.h"
#include "soundrender_emitter.h"
#include "soundrender_source.h"

xr_vector<u8> g_target_temp_data;
xr_vector<u8> g_target_temp_data_16;

// The 5 kHz shelf gain for air at listener distance d is (1 + d / air_shelf_range) raised to air_shelf_power
constexpr float air_shelf_power = -1.30027f;
constexpr float air_shelf_range = 75.f;

CSoundRender_TargetA::CSoundRender_TargetA(): CSoundRender_Target()
{
	cache_gain = 0.f;
	cache_pitch = 1.f;
	pSource = 0;
	Slot = u32(-1);
	direct_filter = 0;
	cache_hf = 1.f;
	cache_lf = 1.f;
	direct_attached = false;
	send_group = -1;
	send_filter = 0;
	cache_send = 1.f;
	cache_band = 1.f;
	send_direct = false;
	cache_air = 1.f;
}

CSoundRender_TargetA::~CSoundRender_TargetA()
{
}

void CSoundRender_TargetA::SetSlot(ALuint NewSlot)
{
	Slot = NewSlot;
}

BOOL CSoundRender_TargetA::_initialize()
{
	inherited::_initialize();
	// initialize buffer
	A_CHK(alGenBuffers (sdef_target_count, pBuffers));
	alGenSources(1, &pSource);
	ALenum error = alGetError();
	if (AL_NO_ERROR == error)
	{
		A_CHK(alSourcei (pSource, AL_LOOPING, AL_FALSE));
		A_CHK(alSourcef (pSource, AL_MIN_GAIN, 0.f));
		A_CHK(alSourcef (pSource, AL_MAX_GAIN, 1.f));
		A_CHK(alSourcef (pSource, AL_GAIN, cache_gain));
		A_CHK(alSourcef (pSource, AL_PITCH, cache_pitch));
		return TRUE;
	}
	else
	{
		Msg("! sound: OpenAL: Can't create source. Error: %s.", (LPCSTR)alGetString(error));
		return FALSE;
	}
}

void CSoundRender_TargetA::_destroy()
{
	// clean up target
	if (alIsSource(pSource))
		alDeleteSources(1, &pSource);
	if (direct_filter)
	{
		A_CHK(SoundRenderA->alDeleteFilters(1, &direct_filter));
		direct_filter = 0;
	}
	direct_attached = false;
	send_direct = false;
	if (send_filter)
	{
		A_CHK(SoundRenderA->alDeleteFilters(1, &send_filter));
		send_filter = 0;
	}
	if (send_group >= 0)
		leave_group();
	A_CHK(alDeleteBuffers (sdef_target_count, pBuffers));
	inherited::_destroy();
}

void CSoundRender_TargetA::_restart()
{
	_destroy();
	_initialize();
}

void CSoundRender_TargetA::start(CSoundRender_Emitter* E)
{
	inherited::start(E);

	// Calc storage
	buf_block = sdef_target_block * E->source()->m_wformat.nAvgBytesPerSec / 1000;
	g_target_temp_data.resize(buf_block);
	g_target_temp_data_16.resize(buf_block * 2);
}

void CSoundRender_TargetA::render()
{
	for (u32 buf_idx = 0; buf_idx < sdef_target_count; buf_idx++)
		fill_block(pBuffers[buf_idx]);

	A_CHK(alSourceQueueBuffers(pSource, sdef_target_count, pBuffers));
	if (Slot != u32(-1) && !skips_reverb())
	{
		// The direct filter fill_parameters left goes to the slot too
		send_direct = direct_attached;
		if (send_direct)
			send_through_direct();
		else
			A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, Slot, 0, AL_FILTER_NULL));
	}
	// demonized: explicitly disable effects by sending sounds to null slot, ie. not sending
	else
	{
		send_direct = false;
		A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, NULL));
	}
	// The send above replaced any group's
	if (send_group >= 0)
		leave_group();
	A_CHK(alSourcePlay(pSource));

	inherited::render();
}

void CSoundRender_TargetA::stop()
{
	if (rendering)
	{
		A_CHK(alSourceStop(pSource));
		A_CHK(alSourcei (pSource, AL_BUFFER, NULL));
		A_CHK(alSourcei (pSource, AL_SOURCE_RELATIVE, TRUE));
	}
	// A stopped source sends nothing until its next render resets the send
	if (send_group >= 0)
		leave_group();
	inherited::stop();
}

// Intro sounds, and interface files under raytraced acoustics, skip the listener's reverb
bool CSoundRender_TargetA::skips_reverb() const
{
	return m_pEmitter->bIntro || (SoundRender->raytracing() && m_pEmitter->source()->m_bInterface);
}

// The group a playing 3D source sends to, -1 for the listener's slot
int CSoundRender_TargetA::grouped() const
{
	if (!rendering || Slot == u32(-1) || m_pEmitter->b2D || skips_reverb() || !m_pEmitter->rt_traced)
		return -1;
	const int g = m_pEmitter->rt_group;
	return g >= 0 && g < sdef_reverb_groups && SoundRenderA->group_ready[g] ? g : -1;
}

void CSoundRender_TargetA::leave_group()
{
	send_group = -1;
}

// The listener's slot copies the direct filter without the air, which only the dry path carries
void CSoundRender_TargetA::send_through_direct()
{
	if (cache_air != 1.f)
		A_CHK(SoundRenderA->alFilterf(direct_filter, AL_BANDPASS_GAINHF, std::min(cache_hf, 1.f)));
	A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, Slot, 0, direct_filter));
	if (cache_air != 1.f)
		A_CHK(SoundRenderA->alFilterf(direct_filter, AL_BANDPASS_GAINHF, std::min(cache_hf, 1.f) * cache_air));
}

void CSoundRender_TargetA::rewind()
{
	inherited::rewind();

	A_CHK(alSourceStop(pSource));
	A_CHK(alSourcei (pSource, AL_BUFFER, NULL));
	for (u32 buf_idx = 0; buf_idx < sdef_target_count; buf_idx++)
		fill_block(pBuffers[buf_idx]);
	A_CHK(alSourceQueueBuffers (pSource, sdef_target_count, pBuffers));
	A_CHK(alSourcePlay (pSource));
}

void CSoundRender_TargetA::update()
{
	inherited::update();

	ALint processed;
	// Get status
	A_CHK(alGetSourcei(pSource, AL_BUFFERS_PROCESSED, &processed));

	if (processed > 0)
	{
		while (processed)
		{
			ALuint BufferID;
			A_CHK(alSourceUnqueueBuffers(pSource, 1, &BufferID));
			fill_block(BufferID);
			A_CHK(alSourceQueueBuffers(pSource, 1, &BufferID));
			--processed;
		}

		ALint state;
		A_CHK(alGetSourcei(pSource, AL_SOURCE_STATE, &state));
		if (state == AL_STOPPED)
		A_CHK(alSourcePlay(pSource));
	}
	else
	{
		// processed == 0
		// check play status -- if stopped then queue is not being filled fast enough
		ALint state;
		A_CHK(alGetSourcei(pSource, AL_SOURCE_STATE, &state));
		if (state != AL_PLAYING)
		{
			//			Log		("Queuing underrun detected.");
			A_CHK(alSourcePlay(pSource));
		}
	}
}

void CSoundRender_TargetA::fill_parameters()
{
	CSoundRender_Emitter* SE = m_pEmitter;
	VERIFY(SE);

	inherited::fill_parameters();

	// 3D params
	VERIFY2(m_pEmitter, SE->source()->file_name());
	A_CHK(alSourcef (pSource, AL_REFERENCE_DISTANCE, m_pEmitter->p_source.min_distance));

	VERIFY2(m_pEmitter, SE->source()->file_name());
	A_CHK(alSourcef (pSource, AL_MAX_DISTANCE, m_pEmitter->p_source.max_distance));

	VERIFY2(m_pEmitter, SE->source()->file_name ());
	A_CHK(alSource3f(pSource, AL_POSITION, m_pEmitter->p_source.position.x,m_pEmitter->p_source.position.y,-m_pEmitter->
		p_source.position.z));

	VERIFY2(m_pEmitter, SE->source()->file_name());
	A_CHK(alSource3f(pSource, AL_VELOCITY, m_pEmitter->p_source.velocity.x, m_pEmitter->p_source.velocity.y, -m_pEmitter->p_source.velocity.z));

	VERIFY2(m_pEmitter, SE->source()->file_name());
	A_CHK(alSourcei (pSource, AL_SOURCE_RELATIVE, m_pEmitter->b2D));

	A_CHK(alSourcef (pSource, AL_ROLLOFF_FACTOR, psSoundRolloff));

	// While raytracing the dry path loses treble over the listener distance as air does, the far layers of a weapon sound already carry that loss
	float air = 1.f;
	if (SoundRender->raytracing() && !m_pEmitter->b2D && !m_pEmitter->bLayer)
	{
		const float distance = SoundRender->listener_position().distance_to(m_pEmitter->p_source.position);
		air = std::pow(1.f + distance / air_shelf_range, air_shelf_power);
	}

	VERIFY2(m_pEmitter, SE->source()->file_name());
	// Treble that passes better than bass lifts the gain by the ratio and the filter takes the bass down instead
	const float _hf = m_pEmitter->occluder_hf;
	float _gain = m_pEmitter->smooth_volume * std::max(_hf, 1.f);
	clamp(_gain, EPS_S, 1.f);
	// A grouped source plays at the louder of its dry and unoccluded levels, the direct and send filters take each path down to its own
	const int group = grouped();
	const float dry = _gain;
	const float wet = group >= 0 ? std::clamp(m_pEmitter->smooth_volume / std::max(m_pEmitter->occluder_volume, EPS_S), EPS_S, 1.f) : 0.f;
	if (group >= 0)
		_gain = std::max(dry, wet);
	if (!fsimilar(_gain, cache_gain, 0.01f))
	{
		cache_gain = _gain;
		A_CHK(alSourcef (pSource, AL_GAIN, _gain));
	}

	// Raytraced and air treble loss, the source copies the filter when it is attached
	bool direct_set = false;
	if (m_pEmitter->rt_traced || _hf != 1.f || air != 1.f)
	{
		if (!direct_filter)
		{
			A_CHK(SoundRenderA->alGenFilters(1, &direct_filter));
			A_CHK(SoundRenderA->alFilteri(direct_filter, AL_FILTER_TYPE, AL_FILTER_BANDPASS));
			A_CHK(SoundRenderA->alFilterf(direct_filter, AL_BANDPASS_GAIN, 1.f));
		}
		// The bass comes back to the volume from the gain actually sent, so only the treble lift gives way to the clamp
		const float _dry = group >= 0 ? dry : cache_gain;
		const float _lf = _hf > 1.f && _dry > 0.f ? std::min(m_pEmitter->smooth_volume / _dry, 1.f) : 1.f;
		// A grouped source's dry path keeps its share of the gain sent
		const float _band = group >= 0 && cache_gain > 0.f ? std::min(dry / cache_gain, 1.f) : 1.f;
		const bool band_moved = _band == 1.f ? cache_band != 1.f : _abs(_band - cache_band) > 0.01f * cache_band;
		if (!direct_attached || !fsimilar(_hf, cache_hf, 0.01f) || _abs(_lf - cache_lf) > 0.01f * cache_lf || band_moved || _abs(air - cache_air) > 0.01f * cache_air)
		{
			cache_hf = _hf;
			cache_lf = _lf;
			cache_air = air;
			direct_attached = true;
			if (_band != cache_band)
			{
				cache_band = _band;
				A_CHK(SoundRenderA->alFilterf(direct_filter, AL_BANDPASS_GAIN, _band));
			}
			A_CHK(SoundRenderA->alFilterf(direct_filter, AL_BANDPASS_GAINHF, std::min(_hf, 1.f) * air));
			A_CHK(SoundRenderA->alFilterf(direct_filter, AL_BANDPASS_GAINLF, _lf));
			A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, direct_filter));
			direct_set = true;
		}
	}
	else if (direct_attached)
	{
		direct_attached = false;
		A_CHK(alSourcei(pSource, AL_DIRECT_FILTER, AL_FILTER_NULL));
	}

	// The send follows the group with a flat gain filter, the reverb hears the source unfiltered at its unoccluded level
	if (group >= 0)
	{
		const float _send = cache_gain > 0.f ? std::min(wet / cache_gain, 1.f) : 1.f;
		if (group != send_group || _abs(_send - cache_send) > 0.01f * cache_send)
		{
			if (!send_filter)
			{
				A_CHK(SoundRenderA->alGenFilters(1, &send_filter));
				A_CHK(SoundRenderA->alFilteri(send_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS));
				A_CHK(SoundRenderA->alFilterf(send_filter, AL_LOWPASS_GAINHF, 1.f));
			}
			send_group = group;
			send_direct = false;
			cache_send = _send;
			A_CHK(SoundRenderA->alFilterf(send_filter, AL_LOWPASS_GAIN, _send));
			A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, SoundRenderA->group_slot[group], 0, send_filter));
		}
	}
	else if (send_group >= 0)
	{
		A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, Slot, 0, AL_FILTER_NULL));
		leave_group();
	}

	// A source on the listener's slot sends through its direct filter, attached again whenever the filter changes since an attach copies it
	if (group < 0 && Slot != u32(-1) && !skips_reverb())
	{
		const bool direct = direct_attached;
		if (direct && (!send_direct || direct_set))
		{
			send_through_direct();
			send_direct = true;
		}
		else if (!direct && send_direct)
		{
			A_CHK(alSource3i(pSource, AL_AUXILIARY_SEND_FILTER, Slot, 0, AL_FILTER_NULL));
			send_direct = false;
		}
	}

	VERIFY2(m_pEmitter, SE->source()->file_name());
	float _pitch = m_pEmitter->p_source.freq;
	clamp(_pitch, EPS_L, 2.f);

	if (!fsimilar(cache_pitch, _pitch * psSpeedOfSound))
	{
		cache_pitch = _pitch * psSpeedOfSound;

		// Only update time to stop for non-looped sounds
		if (!m_pEmitter->iPaused && (m_pEmitter->m_current_state == CSoundRender_Emitter::stStarting || m_pEmitter->m_current_state == CSoundRender_Emitter::stPlaying || m_pEmitter->m_current_state == CSoundRender_Emitter::stSimulating))
			m_pEmitter->fTimeToStop = SoundRender->fTimer_Value + ((m_pEmitter->get_length_sec() - (SoundRender->fTimer_Value - m_pEmitter->fTimeStarted)) / cache_pitch);

		A_CHK(alSourcef(pSource, AL_PITCH, cache_pitch));
	}
	VERIFY2(m_pEmitter, SE->source()->file_name());
}

void CSoundRender_TargetA::fill_block(ALuint BufferID)
{
	R_ASSERT(m_pEmitter);
	ALuint format = (m_pEmitter->source()->m_wformat.nChannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	if (format == AL_FORMAT_MONO16)
	{
		m_pEmitter->fill_block(&g_target_temp_data.front(), g_target_temp_data.size());
		A_CHK(alBufferData(BufferID, format, &g_target_temp_data.front(), g_target_temp_data.size(), m_pEmitter->source()->m_wformat.nSamplesPerSec));
	}
	else
	{
		m_pEmitter->fill_block(&g_target_temp_data_16.front(), g_target_temp_data_16.size());
		A_CHK(alBufferData(BufferID, format, &g_target_temp_data_16.front(), g_target_temp_data_16.size(), m_pEmitter->source()->m_wformat.nSamplesPerSec));
	}
}

void CSoundRender_TargetA::source_changed()
{
	dettach();
	attach();
}
