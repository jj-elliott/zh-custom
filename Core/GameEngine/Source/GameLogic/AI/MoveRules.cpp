/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 zh-pipeline
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// zh-pipeline @feature 2026-11-16 Unit movement rules level, see MoveRules.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameLogic/MoveRules.h"

namespace MoveRules
{
	static Int s_level = LATEST;

	Int getLevel()
	{
		return s_level;
	}

	void setLevel( Int level )
	{
		if( level < RETAIL )
			level = RETAIL;
		if( level > LATEST )
			level = LATEST;
		s_level = level;
	}

	MoveOrderHook moveOrderHook = nullptr;

	static char s_benchRecordFile[512] = "";
	static char s_benchRunFile[512] = "";

	void setBenchRecordFile( const char *path )
	{
		strncpy(s_benchRecordFile, path, sizeof(s_benchRecordFile) - 1);
	}

	void setBenchRunFile( const char *path )
	{
		strncpy(s_benchRunFile, path, sizeof(s_benchRunFile) - 1);
	}

	const char *benchRecordFile() { return s_benchRecordFile; }
	const char *benchRunFile() { return s_benchRunFile; }
}

namespace MoveTune
{
	struct Override
	{
		char name[48];
		Real value;
	};
	static const Int MAX_OVERRIDES = 64;
	static Override s_overrides[MAX_OVERRIDES];
	static Int s_numOverrides = 0;
	static Bool s_list = false;

	void parse( const char *spec )
	{
		// "name=value,name=value"
		const char *p = spec;
		while (*p != 0)
		{
			const char *end = p;
			while (*end != 0 && *end != ',')
				++end;
			const char *eq = p;
			while (eq < end && *eq != '=')
				++eq;
			if (eq < end && s_numOverrides < MAX_OVERRIDES)
			{
				Override& o = s_overrides[s_numOverrides];
				size_t len = (size_t)(eq - p);
				if (len > sizeof(o.name) - 1)
					len = sizeof(o.name) - 1;
				memcpy(o.name, p, len);
				o.name[len] = 0;
				o.value = (Real)atof(eq + 1);
				++s_numOverrides;
			}
			else if ((end - p) == 4 && strncmp(p, "list", 4) == 0)
			{
				s_list = true;
			}
			p = (*end == ',') ? end + 1 : end;
		}
	}

	void apply( const char *name, Real& value )
	{
		for (Int i = 0; i < s_numOverrides; ++i)
		{
			if (strcmp(s_overrides[i].name, name) == 0)
				value = s_overrides[i].value;
		}
		if (s_list)
		{
			printf("MoveTune: %s=%g\n", name, value);
			fflush(stdout);
		}
	}

	void apply( const char *name, Int& value )
	{
		Real v = (Real)value;
		apply(name, v);
		value = (Int)(v + (v >= 0.0f ? 0.5f : -0.5f));
	}

	void apply( const char *name, UnsignedInt& value )
	{
		Real v = (Real)value;
		apply(name, v);
		value = (v > 0.0f) ? (UnsignedInt)(v + 0.5f) : 0;
	}
}
