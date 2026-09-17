#pragma once
#include "../monster_state_manager.h"

class CAI_PseudoDog;

class CStateManagerPseudodog : public CMonsterStateManager<CAI_PseudoDog>
{
	typedef CMonsterStateManager<CAI_PseudoDog> inherited;

	bool m_pack_fsm_enabled;

public:

	CStateManagerPseudodog(CAI_PseudoDog* monster);

	// registers the pack panic and danger states when the key is on
	void load_optional_states(LPCSTR section);

	virtual void execute();
	virtual void remove_links(CObject* object) { inherited::remove_links(object); }
};
