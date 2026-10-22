#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Core.h"
#include "SoundRender_Source.h"
#include "../xrCore/ScopeLock.hpp"

CSoundRender_Source* CSoundRender_Core::i_create_source(LPCSTR name)
{
	// Search
	string256 id;
	xr_strcpy(id, name);
	strlwr(id);
	if (strext(id)) *strext(id) = 0;
	auto it = s_sources.find(id);
	if (it != s_sources.end())
	{
		return it->second;
	}

	// Load a _new one
	CSoundRender_Source* S = xr_new<CSoundRender_Source>();
	S->load(id);
	s_sources.insert({id, S});
	return S;
}

void CSoundRender_Core::i_destroy_source(CSoundRender_Source* S)
{
	// No actual destroy at all
}

// One file per line relative to sounds\, a trailing * takes every file under that prefix
void CSoundRender_Core::i_load_interface_list()
{
	s_interface_files.clear();
	s_interface_prefixes.clear();

	string_path fn;
	if (!FS.exist(fn, "$game_config$", "misc\\sound\\sound_interface", ".ltx"))
		return;
	CInifile ini(fn, TRUE);
	if (!ini.section_exist("interface"))
		return;
	for (const auto& item : ini.r_section("interface").Data)
	{
		string_path N;
		xr_strcpy(N, item.first.c_str());
		xr_strlwr(N);
		for (char* c = N; *c; ++c)
			if (*c == '/')
				*c = '\\';
		const u32 len = xr_strlen(N);
		if (len && N[len - 1] == '*')
		{
			N[len - 1] = 0;
			s_interface_prefixes.emplace_back(N);
		}
		else
			s_interface_files.emplace(N);
	}
}

bool CSoundRender_Core::i_interface_file(LPCSTR name) const
{
	if (s_interface_files.empty() && s_interface_prefixes.empty())
		return false;
	string_path N;
	xr_strcpy(N, name);
	for (char* c = N; *c; ++c)
		if (*c == '/')
			*c = '\\';
	if (s_interface_files.find(N) != s_interface_files.end())
		return true;
	for (const xr_string& p : s_interface_prefixes)
		if (!strncmp(N, p.c_str(), p.size()))
			return true;
	return false;
}

void CSoundRender_Core::i_create_all_sources()
{
	PROF_EVENT();
	CTimer T;
	T.Start();

	FS_FileSet flist;
	FS.file_list(flist, "$game_sounds$", FS_ListFiles, "*.ogg");
	const size_t sizeBefore = s_sources.size();

	Lock lock;
	const auto processFile = [&](const FS_File& file)
	{
		string256 id;
		xr_strcpy(id, file.name.c_str());

		xr_strlwr(id);
		if (strext(id))
			*strext(id) = 0;

		{
			ScopeLock scope(&lock);
			const auto it = s_sources.find(id);
			if (it != s_sources.end())
				return;
			UNUSED(scope);
		}

		CSoundRender_Source* S = new CSoundRender_Source();
		S->load(id);

		lock.Enter();
		s_sources.insert({ id, S });
		lock.Leave();
	};

	xr_parallel_foreach(flist.begin(), flist.end(), processFile);

	Msg("Finished creating %d sound sources. Duration: %d ms", s_sources.size() - sizeBefore, T.GetElapsed_ms());
}
