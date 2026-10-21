#pragma once

#include "SoundRender_Core.h"
#include "OpenALDeviceList.h"
#include <AL/efx.h>


#ifdef DEBUG
#	define A_CHK(expr)		{ alGetError(); 		expr; ALenum error=alGetError(); 			VERIFY2(error==AL_NO_ERROR, (LPCSTR)alGetString(error)); }
#	define AC_CHK(expr)		{ alcGetError(pDevice); expr; ALCenum error=alcGetError(pDevice); 	VERIFY2(error==ALC_NO_ERROR,(LPCSTR)alcGetString(pDevice,error)); }
#else
#	define A_CHK(expr)		{ expr; }
#	define AC_CHK(expr)		{ expr; }
#endif

#define FUNCTION_CAST(T, ptr) reinterpret_cast<T>(ptr)

class CSoundRender_CoreA : public CSoundRender_Core
{
	typedef CSoundRender_Core inherited;
	friend class CNotificationClient;
	friend class CSoundRender_TargetA;
	
	ALCdevice* pDevice;
	ALCcontext* pContext;
	ALDeviceList* pDeviceList;

	struct SListener
	{
		Fvector position;
		Fvector prevVelocity;
		Fvector curVelocity;
		Fvector accVelocity;
		Fvector orientation[2];
	};

	SListener Listener;

	/* Filter object functions */
	LPALGENFILTERS alGenFilters{};
	LPALDELETEFILTERS alDeleteFilters{};
	LPALISFILTER alIsFilter{};
	LPALFILTERI alFilteri{};
	LPALFILTERIV alFilteriv{};
	LPALFILTERF alFilterf{};
	LPALFILTERFV alFilterfv{};
	LPALGETFILTERI alGetFilteri{};
	LPALGETFILTERIV alGetFilteriv{};
	LPALGETFILTERF alGetFilterf{};
	LPALGETFILTERFV alGetFilterfv{};
	/* Effect object functions */
	LPALGENEFFECTS alGenEffects{};
	LPALDELETEEFFECTS alDeleteEffects{};
	LPALISEFFECT alIsEffect{};
	LPALEFFECTI alEffecti{};
	LPALEFFECTIV alEffectiv{};
	LPALEFFECTF alEffectf{};
	LPALEFFECTFV alEffectfv{};
	LPALGETEFFECTI alGetEffecti{};
	LPALGETEFFECTIV alGetEffectiv{};
	LPALGETEFFECTF alGetEffectf;
	LPALGETEFFECTFV alGetEffectfv;
	/* Auxiliary Effect Slot object functions */
	LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots{};
	LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots{};
	LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot{};
	LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti{};
	LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv{};
	LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf{};
	LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv{};
	LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti{};
	LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv{};
	LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf{};
	LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv{};

public:
	ALuint effect{};
	ALuint effectfv{};
	ALuint slot{};

	// Grouped raytraced reverb, an effect and slot per group made when the group is first heard
	ALuint group_effect[sdef_reverb_groups]{};
	ALuint group_slot[sdef_reverb_groups]{};
	bool group_ready[sdef_reverb_groups]{};
	bool group_refused{};
	// What each group's slot holds, gliding toward the raytracer's result
	EFXEAXREVERBPROPERTIES group_reverb[sdef_reverb_groups]{};
	float group_gain[sdef_reverb_groups]{};

	virtual void update_listener(const Fvector& P, const Fvector& D, const Fvector& N, float dt);

	// EFX Slots
	void LoadEffect();
	void DestroyEffect();
	bool make_group(int i);
	void commit_group(int i, float gain);

public:
	CSoundRender_CoreA();
	virtual ~CSoundRender_CoreA();

	int load_reverb(ALuint effect, const EFXEAXREVERBPROPERTIES* reverb, float decay_limit = AL_EAXREVERB_MAX_DECAY_TIME);

	virtual void _initialize(int stage);
	virtual void _clear();
	virtual void _restart();
	virtual void refresh_devices() override;
	virtual void default_device_changed() override;
	virtual void switch_device(LPCSTR device_name) override;

	virtual void set_master_volume(float f);

	virtual const Fvector& listener_position() { return Listener.position; }

	// EFX listener
	void set_listener(const CSoundRender_Environment& env);
	void get_listener(CSoundRender_Environment& env);
	void commit();
};

extern CSoundRender_CoreA* SoundRenderA;
