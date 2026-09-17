#pragma once
#include "ai_monster_defs.h"

struct SMonsterSettings
{
	// float speed factors

	float m_fDistToCorpse;
	float m_fDamagedThreshold; // порог здоровья, ниже которого устанавливается флаг m_bDamaged

	// -------------------------------------------------------

	u32 m_dwIdleSndDelay;
	u32 m_dwEatSndDelay;
	u32 m_dwAttackSndDelay;

	u32 m_dwDistantIdleSndDelay;
	float m_fDistantIdleSndRange;

	// -------------------------------------------------------

	u32 m_dwDayTimeBegin;
	u32 m_dwDayTimeEnd;
	float satiety_threshold;
	float satiety_decay_per_sec;
	bool anomaly_detect_always;
	float night_eye_range_mult;
	float night_hear_mult;
	float rain_eye_range_mult;
	float rain_hear_mult;
	float dark_eye_range_mult;

	// -----------------------------------------------------------

	float m_fSoundThreshold;

	float m_fEatFreq;
	float m_fEatSlice;
	float m_fEatSliceWeight;

	u8 m_legs_number;
	SAttackEffector m_attack_effector;

	float m_max_hear_dist;

	float m_run_attack_path_dist;
	float m_run_attack_start_dist;
};
