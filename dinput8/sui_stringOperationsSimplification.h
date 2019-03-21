#pragma once
#include <string>

static bool SuiString_EndsWith(std::string source, std::string endsWith)
{
	if (&source != NULL && &endsWith != NULL && endsWith.length() <= source.length())
	{
		int startPos = source.length() - endsWith.length();
		for (unsigned int i = 0; i < endsWith.length(); i++)
		{
			char compL = source[startPos + i];
			char compR = endsWith[i];
			if (source[startPos + i] != endsWith[i])
			{
				return false;
			}
		}
		return true;
	}
	else
		return false;
}
