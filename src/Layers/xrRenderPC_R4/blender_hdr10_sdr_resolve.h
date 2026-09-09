#pragma once

class CBlender_hdr10_sdr_resolve : public IBlender
{
public:
    virtual		LPCSTR		getComment()	{ return "HDR10 SDR Resolve"; }
    virtual		BOOL		canBeDetailed()	{ return FALSE; }
    virtual		BOOL		canBeLMAPped()	{ return FALSE; }

    virtual		void		Compile(CBlender_Compile& C);

    CBlender_hdr10_sdr_resolve();
    virtual ~CBlender_hdr10_sdr_resolve();
};
