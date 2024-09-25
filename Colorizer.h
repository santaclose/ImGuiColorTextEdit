#pragma once

#include <Common.h>
#include <unordered_map>

struct Colorizer {

	virtual void OnLoad(std::vector<Common::Line>& aEditorLines) = 0;
	virtual void OnChange(const std::string& aText,
		int aStartLine,
		int aStartChar,
		int aEndLine,
		int aEndChar,
		bool aAddition,
		std::vector<Common::Line>& aEditorLines) = 0;

protected:
	std::unordered_map<int, int> mMultilineCommentStart;
	std::unordered_map<int, int> mMultilineCommentEnd;
};

#define DECLARE_LANGUAGE_COLORIZER(language) struct Colorizer##language : public Colorizer {void OnLoad(std::vector<Common::Line>& aEditorLines);void OnChange(const std::string& aText,int aStartLine,int aStartChar,int aEndLine,int aEndChar,bool aAddition,std::vector<Common::Line>& aEditorLines);};

DECLARE_LANGUAGE_COLORIZER(C)