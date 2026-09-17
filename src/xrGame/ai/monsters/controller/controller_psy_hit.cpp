#include "stdafx.h"
#include "controller_psy_hit.h"
#include "../BaseMonster/base_monster.h"
#include "controller.h"
#include "../control_animation_base.h"
#include "../control_direction_base.h"
#include "../control_movement_base.h"
#include "../../../level.h"
#include "../../../actor.h"
#include "../../../ActorEffector.h"
#include "../../../../xrEngine/CameraBase.h"
#include "../../../CharacterPhysicsSupport.h"
#include "../../../level_debug.h"
#include "../../../ActorCondition.h"
#include "../../../HudManager.h"

void CControllerPsyHit::load(LPCSTR section)
{
	m_min_tube_dist = pSettings->r_float(section, "tube_condition_min_distance");
}

void CControllerPsyHit::reinit()
{
	inherited::reinit();

	IKinematicsAnimated* skel = smart_cast<IKinematicsAnimated *>(m_object->Visual());
	m_stage[0] = skel->ID_Cycle_Safe("psy_attack_0");
	VERIFY(m_stage[0]);
	m_stage[1] = skel->ID_Cycle_Safe("psy_attack_1");
	VERIFY(m_stage[1]);
	m_stage[2] = skel->ID_Cycle_Safe("psy_attack_2");
	VERIFY(m_stage[2]);
	m_stage[3] = skel->ID_Cycle_Safe("psy_attack_3");
	VERIFY(m_stage[3]);
	m_current_index = 0;

	m_time_last_tube = 0;
	m_sound_state = eNone;
	m_at_actor = false;
	m_tube_target_id = u16(-1);
}


bool CControllerPsyHit::tube_ready() const
{
	u32 tube_condition_min_delay = 5000;
	if (CController* controller = smart_cast<CController*>(m_object))
		tube_condition_min_delay = controller->m_tube_condition_min_delay;

	return m_time_last_tube + tube_condition_min_delay < time();
}

bool CControllerPsyHit::check_start_conditions()
{
	if (is_active())
		return false;

	if (m_man->is_captured_pure())
		return false;

	if (target()->cast_actor() && Actor()->Cameras().GetCamEffector(eCEControllerPsyHit))
		return false;

	if (!see_enemy())
		return false;

	if (!tube_ready())
		return false;

	if (m_object->Position().distance_to(target()->Position()) < m_min_tube_dist)
		return false;

	return true;
}

void CControllerPsyHit::activate()
{
	m_man->capture_pure(this);
	m_man->subscribe(this, ControlCom::eventAnimationEnd);

	m_man->path_stop(this);
	m_man->move_stop(this);

	//////////////////////////////////////////////////////////////////////////
	// set direction
	SControlDirectionData* ctrl_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir);
	VERIFY(ctrl_dir);
	ctrl_dir->heading.target_speed = 3.f;
	ctrl_dir->heading.target_angle = m_man->direction().angle_to_target(target()->Position());

	//////////////////////////////////////////////////////////////////////////
	m_current_index = 0;

	// a fresh tube owns nothing and has committed to nothing until it installs
	m_at_actor = false;
	m_tube_target_id = u16(-1);

	play_anim();

	m_blocked = false;

	set_sound_state(ePrepare);
}

void CControllerPsyHit::deactivate()
{
	m_man->release_pure(this);
	m_man->unsubscribe(this, ControlCom::eventAnimationEnd);

	if (m_blocked)
	{
		NET_Packet P;

		Actor()->u_EventGen(P, GEG_PLAYER_WEAPON_HIDE_STATE, Actor()->ID());
		P.w_u16(INV_STATE_BLOCK_ALL);
		P.w_u8(u8(false));
		Actor()->u_EventSend(P);
	}

	set_sound_state(eNone);
}

void CControllerPsyHit::on_event(ControlCom::EEventType type, ControlCom::IEventData* data)
{
	if (type == ControlCom::eventAnimationEnd)
	{
		if (m_current_index < 3)
		{
			m_current_index++;
			play_anim();

			switch (m_current_index)
			{
			case 1: death_glide_start();
				break;
			case 2: hit();
				break;
			case 3: death_glide_end();
				break;
			}
		}
		else
		{
			m_man->deactivate(this);
			return;
		}
	}
}

void CControllerPsyHit::play_anim()
{
	SControlAnimationData* ctrl_anim = (SControlAnimationData*)m_man->data(this, ControlCom::eControlAnimation);
	VERIFY(ctrl_anim);

	ctrl_anim->global.set_motion(m_stage[m_current_index]);
	ctrl_anim->global.actual = false;
}

namespace detail
{
	bool check_actor_visibility(const Fvector trace_from,
	                            const Fvector trace_to,
	                            CObject* object)
	{
		const float dist = trace_from.distance_to(trace_to);
		Fvector trace_dir;
		trace_dir.sub(trace_to, trace_from);

		//DBG().level_info(this).add_item	(trace_from,trace_to,D3DCOLOR_XRGB(0,150,150));


		collide::rq_result l_rq;
		l_rq.O = NULL;
		Level().ObjectSpace.RayPick(trace_from,
		                            trace_dir,
		                            dist,
		                            collide::rqtBoth,
		                            l_rq,
		                            object);

		return l_rq.O == Actor() || (l_rq.range >= dist - 0.1f);
	}
} // namespace detail

extern CActor* g_actor;

bool CControllerPsyHit::see_enemy()
{
	return m_object->EnemyMan.see_enemy_now(target());
	// 	using namespace detail;
	// 	Fvector const self_head = get_head_position(m_object);
	// 	Fvector actor_center;
	// 	Actor()->Center(actor_center);
	// 	Fvector self_center;
	// 	m_object->Center(self_center);
	// 
	// 	if ( check_actor_visibility(self_head, get_head_position(Actor()), m_object) &&
	// 		 check_actor_visibility(self_head, actor_center, m_object) &&
	// 		 check_actor_visibility(self_center, get_head_position(Actor()), m_object) &&
	// 		 check_actor_visibility(self_center, actor_center, m_object) )
	// 	{
	// 		return true;
	// 	}
	// 
	// 	return false;
}

bool CControllerPsyHit::check_conditions_final()
{
	if (!m_object->g_Alive())
		return false;

	if (!g_actor)
		return false;

	// 	if (m_object->EnemyMan.get_enemy() != Actor())	
	// 		return false;

	if (!m_object->EnemyMan.is_enemy(target()))
		return false;

	if (!target()->g_Alive())
		return false;

	if (m_object->Position().distance_to_xz(target()->Position()) < m_min_tube_dist - 2)
		return false;

	return see_enemy();
}

CEntityAlive* CControllerPsyHit::tube_target()
{
	if (m_tube_target_id == u16(-1))
		return nullptr;

	CEntityAlive* const e = smart_cast<CEntityAlive*>(Level().Objects.net_Find(m_tube_target_id));
	return (e && e->g_Alive()) ? e : nullptr;
}

void CControllerPsyHit::death_glide_start()
{
	if (!check_conditions_final())
	{
		m_man->deactivate(this);
		return;
	}

	CEntityAlive* const tgt = target();
	const bool at_actor = tgt && tgt->cast_actor() != nullptr;

	// another tube already owns the actor, a second one would stack effectors
	if (at_actor && Actor()->Cameras().GetCamEffector(eCEControllerPsyHit))
	{
		m_man->deactivate(this);
		return;
	}

	// the tube commits to this target, the hit and the abort both read the latch
	m_tube_target_id = tgt ? tgt->ID() : u16(-1);

	if (at_actor)
	{
		// set once the actor side is really going up, stop() undoes exactly what this installed
		m_at_actor = true;

		HUD().SetRenderable(false);

		if (CController* controller = smart_cast<CController*>(m_object))
		{
			controller->CControlledActor::install();
			controller->CControlledActor::dont_need_turn();
		}
	}

    // demonized: replace m_object->Position() with position of eye bone
    IKinematics* k = m_object->Visual() ? m_object->Visual()->dcast_PKinematics() : nullptr;
    u16 bone_id = BI_NONE;
    if (k)
    {
        bone_id = k->LL_BoneID("eye_left");
        if (bone_id == BI_NONE)
            bone_id = k->LL_BoneID("eye_right");
        if (bone_id == BI_NONE)
            bone_id = k->LL_BoneID("bip01_head");
        if (bone_id == BI_NONE)
            k = nullptr; // Use m_object position
    }

    Fmatrix m;
    Fvector target_pos = k ? m.mul_43(m_object->XFORM(), k->LL_GetTransform(bone_id)).c : m_object->Position();
	target_pos.y += k ? 0.f : 1.4f;

	Fvector dir;
	Fvector src_pos;
	if (at_actor)
	{
		src_pos = Actor()->cam_Active()->vPosition;
		dir.sub(target_pos, src_pos);
	}
	else
		dir.sub(target_pos, tgt->Position());

	float dist = dir.magnitude();
	dir.normalize();

    // demonized: disable actor_psy_immunity affecting camera behaviour of controller attack, very buggy
    // Finetune target_pos a bit
	//float const actor_psy_immunity = Actor()->conditions().GetHitImmunity(ALife::eHitTypeTelepatic);
    //target_pos.mad(src_pos, dir, 0.01f + actor_psy_immunity * (dist - 4.8f));
    //float const base_fov = g_fov;
    //float const dest_fov = g_fov - (g_fov - 10.f) * actor_psy_immunity;

	target_pos.mad(target_pos, dir, -_min(3.5f, dist * 0.75f));
	float const base_fov = g_fov;
	float const dest_fov = 10.f;

	if (at_actor)
	{
		Actor()->Cameras().AddCamEffector(xr_new<CControllerPsyHitCamEffector>(eCEControllerPsyHit, src_pos, target_pos,
		                                                                       m_man->animation().motion_time(
			                                                                       m_stage[1], m_object->Visual()),
		                                                                       base_fov, dest_fov));
	}

	smart_cast<CController *>(m_object)->draw_fire_particles();

	if (at_actor)
	{
		dir.sub(src_pos, target_pos);
		dir.normalize();
		float h, p;
		dir.getHP(h, p);
		dir.setHP(h, p + PI_DIV_3);
		Actor()->character_physics_support()->movement()->ApplyImpulse(dir, Actor()->GetMass() * 530.f);
	}

	set_sound_state(eStart);

	if (at_actor)
	{
		NET_Packet P;
		Actor()->u_EventGen(P, GEG_PLAYER_WEAPON_HIDE_STATE, Actor()->ID());
		P.w_u16(INV_STATE_BLOCK_ALL);
		P.w_u8(u8(true));
		Actor()->u_EventSend(P);

		m_blocked = true;
	}

	//////////////////////////////////////////////////////////////////////////
	// set direction
	SControlDirectionData* ctrl_dir = (SControlDirectionData*)m_man->data(this, ControlCom::eControlDir);
	VERIFY(ctrl_dir);
	ctrl_dir->heading.target_speed = 3.f;
	ctrl_dir->heading.target_angle = m_man->direction().angle_to_target(tgt->Position());

	//////////////////////////////////////////////////////////////////////////
}

void CControllerPsyHit::death_glide_end()
{
	CController* monster = smart_cast<CController *>(m_object);
	monster->draw_fire_particles();

	CEntityAlive* const tgt = tube_target();

	if (tgt && tgt->cast_actor())
	{
		monster->m_sound_tube_hit_left.play_at_pos(Actor(), Fvector().set(-1.f, 0.f, 1.f), sm_2D);
		monster->m_sound_tube_hit_right.play_at_pos(Actor(), Fvector().set(1.f, 0.f, 1.f), sm_2D);
	}

	if (tgt)
		m_object->Hit_Psy(tgt, monster->m_tube_damage);

	m_time_last_tube = Device.dwTimeGlobal;
	stop();
}

void CControllerPsyHit::update_frame()
{
	// experimental, drop the tube as soon as its target is gone instead of finishing it
	if (g_ai_monster_alt && g_ai_monster_tube_abort_lost_target && m_tube_target_id != u16(-1) && !tube_target())
	{
		stop();
		m_man->deactivate(this);
		return;
	}

	//if (m_sound_state == eStart) {
	//	CController *monster = smart_cast<CController *>(m_object);
	//	if (!monster->m_sound_tube_start._feedback()) {
	//		m_sound_state = ePull;
	//		monster->m_sound_tube_pull.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
	//	}
	//}
}

void CControllerPsyHit::set_sound_state(ESoundState state)
{
	CController* monster = smart_cast<CController *>(m_object);
	CEntityAlive* const tgt = target();
	const bool at_actor = tgt && tgt->cast_actor() != nullptr;
	if (state == ePrepare)
	{
		if (at_actor)
			monster->m_sound_tube_prepare.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
		else
			monster->m_sound_tube_prepare.play_at_pos(m_object, m_object->Position());
	}
	else if (state == eStart)
	{
		if (monster->m_sound_tube_prepare._feedback()) monster->m_sound_tube_prepare.stop();

		if (at_actor)
		{
			monster->m_sound_tube_start.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
			monster->m_sound_tube_pull.play_at_pos(Actor(), Fvector().set(0.f, 0.f, 0.f), sm_2D);
		}
		else
		{
			monster->m_sound_tube_start.play_at_pos(m_object, m_object->Position());
			monster->m_sound_tube_pull.play_at_pos(m_object, m_object->Position());
		}
	}
	else if (state == eHit)
	{
		if (monster->m_sound_tube_start._feedback()) monster->m_sound_tube_start.stop();
		if (monster->m_sound_tube_pull._feedback()) monster->m_sound_tube_pull.stop();

		//monster->m_sound_tube_hit_left.play_at_pos(Actor(), Fvector().set(-1.f, 0.f, 1.f), sm_2D);
		//monster->m_sound_tube_hit_right.play_at_pos(Actor(), Fvector().set(1.f, 0.f, 1.f), sm_2D);
	}
	else if (state == eNone)
	{
		if (monster->m_sound_tube_start._feedback()) monster->m_sound_tube_start.stop();
		if (monster->m_sound_tube_pull._feedback()) monster->m_sound_tube_pull.stop();
		if (monster->m_sound_tube_prepare._feedback()) monster->m_sound_tube_prepare.stop();
	}

	m_sound_state = state;
}

void CControllerPsyHit::hit()
{
	//CController *monster	= smart_cast<CController *>(m_object);

	set_sound_state(eHit);
	//m_object->Hit_Psy		(Actor(), monster->m_tube_damage);
}

void CControllerPsyHit::stop()
{
	if (m_at_actor && (Actor()->Cameras().GetCamEffector(eCEControllerPsyHit) || m_blocked))
	{
		HUD().SetRenderable(true);

		// Stop camera effector
		CEffectorCam* ce = Actor()->Cameras().GetCamEffector(eCEControllerPsyHit);
		if (ce)
			Actor()->Cameras().RemoveCamEffector(eCEControllerPsyHit);
	}

	if (CController* controller = smart_cast<CController*>(m_object))
		if (controller->CControlledActor::is_controlling())
			controller->CControlledActor::release();
}

void CControllerPsyHit::on_death()
{
	if (!is_active())
		return;

	stop();

	m_man->deactivate(this);
}
