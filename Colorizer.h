#pragma once

#include <Common.h>

namespace Colorizer {
	
	void OnLoad(std::vector<Common::Line>& aEditorLines);
	void OnChange(const std::string& aText,
		int aStartLine,
		int aStartChar,
		int aEndLine,
		int aEndChar,
		bool aAddition,
		std::vector<Common::Line>& aEditorLines);
}