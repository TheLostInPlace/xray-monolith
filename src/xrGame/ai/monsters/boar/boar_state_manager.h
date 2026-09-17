#pragma once
#include "../monster_state_manager.h"

class CAI_Boar;

class CStateManagerBoar : public CMonsterStateManager<CAI_Boar>
{
	typedef CMonsterStateManager<CAI_Boar> inherited;

	bool m_pack_fsm_enabled;

public:

	CStateManagerBoar(CAI_Boar* monster);

	// registers the pack panic and danger states when the key is on
	void load_optional_states(LPCSTR section);

	virtual void execute();
	virtual void remove_links(CObject* object) { inherited::remove_links(object); }
};
