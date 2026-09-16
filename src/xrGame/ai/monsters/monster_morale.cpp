#include "stdafx.h"
#include "monster_morale.h"
#include "BaseMonster/base_monster.h"

void CMonsterMorale::init_external(CBaseMonster* obj)
{
	m_object = obj;
}

void CMonsterMorale::load(LPCSTR section)
{
	m_hit_quant = pSettings->r_float(section, "Morale_Hit_Quant");
	m_attack_success_quant = pSettings->r_float(section, "Morale_Attack_Success_Quant");
	m_v_taking_heart = pSettings->r_float(section, "Morale_Take_Heart_Speed");
	m_v_despondent = pSettings->r_float(section, "Morale_Despondent_Speed");
	m_v_stable = pSettings->r_float(section, "Morale_Stable_Speed");
	m_despondent_threshold = pSettings->r_float(section, "Morale_Despondent_Threashold");

	// state time defaults to the seconds the state speed takes to cross the morale range
	float despondent_default = fis_zero(m_v_despondent) ? 0.f : 1.f / m_v_despondent;
	float take_heart_default = fis_zero(m_v_taking_heart) ? 0.f : 1.f / m_v_taking_heart;

	m_fsm_enabled = !!READ_IF_EXISTS(pSettings, r_bool, section, "morale_fsm_enabled", false);
	m_take_heart_threshold = READ_IF_EXISTS(pSettings, r_float, section, "morale_take_heart_threshold",
	                                        m_despondent_threshold);
	m_despondent_time = READ_IF_EXISTS(pSettings, r_float, section, "morale_despondent_time", despondent_default);
	m_take_heart_time = READ_IF_EXISTS(pSettings, r_float, section, "morale_take_heart_time", take_heart_default);
}

void CMonsterMorale::reinit()
{
	m_state = eStable;
	m_morale = 1.0f;
	m_state_time_left = 0.f;
}

void CMonsterMorale::on_hit()
{
	change(-m_hit_quant);

	if (m_fsm_enabled && g_ai_monster_alt && (m_state != eDespondent) && (m_morale < m_despondent_threshold))
	{
		set_despondent();
		m_state_time_left = m_despondent_time;
		Msg("[MMOR] %s despondent morale=%.2f", m_object->cNameSect().c_str(), m_morale);
	}
}

void CMonsterMorale::on_attack_success()
{
	change(m_attack_success_quant);

	if (m_fsm_enabled && g_ai_monster_alt && (m_state != eTakeHeart) && (m_morale > m_take_heart_threshold))
	{
		set_take_heart();
		m_state_time_left = m_take_heart_time;
		Msg("[MMOR] %s take heart morale=%.2f", m_object->cNameSect().c_str(), m_morale);
	}
}

void CMonsterMorale::update_schedule(u32 dt)
{
	if (m_state != eStable)
	{
		m_state_time_left -= float(dt) / 1000.f;
		if (m_state_time_left <= 0.f)
		{
			set_normal_state();
			Msg("[MMOR] %s stable morale=%.2f", m_object->cNameSect().c_str(), m_morale);
		}
	}

	float cur_v = 1.f;

	switch (m_state)
	{
	case eStable: cur_v = m_v_stable;
		break;
	case eTakeHeart: cur_v = m_v_taking_heart;
		break;
	case eDespondent: cur_v = -m_v_despondent;
		break;
	}

	m_morale += cur_v * dt / 1000;

	clamp(m_morale, 0.f, 1.f);
}
