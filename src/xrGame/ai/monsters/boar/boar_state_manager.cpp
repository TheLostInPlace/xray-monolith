#include "stdafx.h"
#include "boar.h"
#include "boar_state_manager.h"

#include "../control_animation_base.h"
#include "../control_direction_base.h"
#include "../control_movement_base.h"
#include "../control_path_builder_base.h"

#include "../states/monster_state_rest.h"
#include "../states/monster_state_attack.h"
#include "../states/monster_state_panic.h"
#include "../states/monster_state_eat.h"
#include "../states/monster_state_hear_int_sound.h"
#include "../states/monster_state_hear_danger_sound.h"
#include "../states/monster_state_hitted.h"
#include "../states/monster_state_controlled.h"
#include "../states/monster_state_help_sound.h"
#include "../group_states/group_state_panic.h"
#include "../group_states/group_state_hear_danger_sound.h"

extern int g_ai_monster_alt;

CStateManagerBoar::CStateManagerBoar(CAI_Boar* monster) : inherited(monster)
{
	m_pack_fsm_enabled = false;

	add_state(eStateRest, xr_new<CStateMonsterRest<CAI_Boar>>(monster));
	add_state(eStatePanic, xr_new<CStateMonsterPanic<CAI_Boar>>(monster));

	CStateMonsterAttackMoveToHomePoint<CAI_Boar>* move2home =
		xr_new<CStateMonsterAttackMoveToHomePoint<CAI_Boar>>(monster);

	add_state(eStateAttack, xr_new<CStateMonsterAttack<CAI_Boar>>(monster, move2home));
	add_state(eStateEat, xr_new<CStateMonsterEat<CAI_Boar>>(monster));
	add_state(eStateHearInterestingSound, xr_new<CStateMonsterHearInterestingSound<CAI_Boar>>(monster));
	add_state(eStateHearDangerousSound, xr_new<CStateMonsterHearDangerousSound<CAI_Boar>>(monster));
	add_state(eStateHitted, xr_new<CStateMonsterHitted<CAI_Boar>>(monster));
	add_state(eStateControlled, xr_new<CStateMonsterControlled<CAI_Boar>>(monster));
	add_state(eStateHearHelpSound, xr_new<CStateMonsterHearHelpSound<CAI_Boar>>(monster));
}

void CStateManagerBoar::load_optional_states(LPCSTR section)
{
	m_pack_fsm_enabled = !!READ_IF_EXISTS(pSettings, r_bool, section, "pack_fsm_enabled", false);

	if (!m_pack_fsm_enabled) return;

	add_state(eStatePackPanic, xr_new<CStateGroupPanic<CAI_Boar>>(object));
	add_state(eStatePackHearDangerousSound, xr_new<CStateGroupHearDangerousSound<CAI_Boar>>(object));
}

void CStateManagerBoar::execute()
{
	u32 state_id = u32(-1);

	if (!object->is_under_control())
	{
		const CEntityAlive* enemy = object->EnemyMan.get_enemy();

		if (enemy)
		{
			switch (object->EnemyMan.get_danger_type())
			{
			case eStrong: state_id = eStatePanic;
				break;
			case eWeak: state_id = eStateAttack;
				break;
			}
		}
		else if (object->HitMemory.is_hit())
		{
			state_id = eStateHitted;
		}
		else if (check_state(eStateHearHelpSound))
		{
			state_id = eStateHearHelpSound;
		}
		else if (object->hear_interesting_sound)
		{
			state_id = eStateHearInterestingSound;
		}
		else if (object->hear_dangerous_sound)
		{
			state_id = eStateHearDangerousSound;
		}
		else
		{
			if (can_eat()) state_id = eStateEat;
			else state_id = eStateRest;
		}
	}
	else state_id = eStateControlled;

	if (m_pack_fsm_enabled && g_ai_monster_alt)
	{
		if (state_id == eStatePanic) state_id = eStatePackPanic;
		else if (state_id == eStateHearDangerousSound) state_id = eStatePackHearDangerousSound;
	}

	select_state(state_id);

	// выполнить текущее состояние
	get_state_current()->execute();

	prev_substate = current_substate;
}
