#pragma once
#include "../monster_state_manager.h"

class CChimera;

class CStateManagerChimera : public CMonsterStateManager<CChimera>
{
private:
	typedef CMonsterStateManager<CChimera> inherited;

	bool m_threaten_enabled;
	bool m_hitted_enabled;

public:
	CStateManagerChimera(CChimera* obj);
	virtual ~CStateManagerChimera();

	// registers the optional threaten and hitted states when their keys are on
	void load_optional_states(LPCSTR section);

	virtual void execute();
	virtual void remove_links(CObject* object) { inherited::remove_links(object); }
};
