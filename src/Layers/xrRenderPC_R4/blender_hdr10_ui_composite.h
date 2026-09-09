#pragma once

class CBlender_hdr10_ui_composite : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 UI Composite"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_ui_composite();
    virtual ~CBlender_hdr10_ui_composite();
};
