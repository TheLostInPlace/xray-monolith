#pragma once

class CBaseMonster;

class CMonsterMorale
{
	// external parameters
	float m_hit_quant;
	float m_attack_success_quant;
	float m_team_mate_die;
	float m_v_taking_heart;
	float m_v_despondent;
	float m_v_stable;
	float m_despondent_threshold;

	// optional retreat state machine, off unless the section turns it on
	bool m_fsm_enabled;
	float m_take_heart_threshold;
	float m_despondent_time;
	float m_take_heart_time;
	float m_state_time_left;

	CBaseMonster* m_object;

	enum EState
	{
		eStable,
		eTakeHeart,
		eDespondent
	};

	EState m_state;

	float m_morale;

public:
	CMonsterMorale()
	{
	}

	~CMonsterMorale()
	{
	}

	void init_external(CBaseMonster* obj);
	void load(LPCSTR section);
	void reinit();

	void on_hit();
	void on_attack_success();

	void update_schedule(u32 dt);

	IC void set_despondent();
	IC void set_take_heart();
	IC void set_normal_state();

	IC bool is_despondent();

	IC float get_morale();

private:
	IC void change(float value);
};

#include "monster_morale_inline.h"
