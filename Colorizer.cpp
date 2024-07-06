#include <Common.h>
#include "Colorizer.h"

void Colorizer::OnLoad(std::vector<Common::Line>& aEditorLines)
{
	for (Common::Line& l : aEditorLines)
		for (Common::Glyph& g : l)
			g.mColorIndex = Common::PaletteIndex::CharLiteral;
}

void Colorizer::OnChange(const std::string& aText, int aStartLine, int aStartChar, int aEndLine, int aEndChar, bool aAddition, std::vector<Common::Line>& aEditorLines)
{
	if (aAddition)
	{
		int currentLine = aStartLine;
		int currentChar = aStartChar;
		while (true)
		{
			if (currentLine < aEditorLines.size() && currentChar < aEditorLines[currentLine].size())
				aEditorLines[currentLine][currentChar].mColorIndex = Common::PaletteIndex::Keyword;
	
			currentChar++;
			if (currentChar >= aEditorLines[currentLine].size())
				currentLine++, currentChar = 0;
	
			if (currentLine > aEndLine || currentLine == aEndLine && currentChar >= aEndChar)
				break;
		}
	}
}