#pragma once
#include "../monster_state_manager.h"

class CCat;

class CStateManagerCat : public CMonsterStateManager<CCat>
{
	typedef CMonsterStateManager<CCat> inherited;

	u32 m_rot_jump_last_time;
	bool m_pack_fsm_enabled;

public:
	CStateManagerCat(CCat* obj);
	virtual ~CStateManagerCat();

	// registers the pack panic and danger states when the key is on
	void load_optional_states(LPCSTR section);

	virtual void execute();
	virtual void remove_links(CObject* object) { inherited::remove_links(object); }
};
