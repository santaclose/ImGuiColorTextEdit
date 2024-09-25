#include <boost/regex.hpp>

#include "Common.h"
#include "Colorizer.h"

#define C_MULTILINE_COMMENT_START "/*"
#define C_MULTILINE_COMMENT_END "*/"
#define C_ONELINE_COMMENT "//"

//namespace Colorizer
//{
//	////static std::string matchingBuffer;
//	//void OnLoadC(std::vector<Common::Line>& aEditorLines)
//	//{
//	//	for (Common::Line& line : aEditorLines)
//	//	{
//	//		//matchingBuffer.resize(line.size());
//	//		for (size_t j = 0; j < line.size(); ++j)
//	//		{
//	//			//matchingBuffer[j] = line[j].mChar;
//	//			line[j].mColorIndex = Common::PaletteIndex::Default;
//	//		}
//	//	}
//	//}
//}


struct ColorizerRule
{
	boost::regex pattern;
	Common::PaletteIndex colorIndex;
	bool needsTokenization;
};


static std::vector<ColorizerRule> C_PATTERN_PALETTE_INDEX_PAIRS = {

	{boost::regex(R"##([\[\]\{\}\!\%\^\&\*\(\)\-\+\=\~\|\<\>\?\/\;\,\.\:])##", boost::regex_constants::optimize), Common::PaletteIndex::Punctuation, false},
	{boost::regex(R"##([+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?)##", boost::regex_constants::optimize), Common::PaletteIndex::Number, false},
	{boost::regex(R"##([+-]?[0-9]+[Uu]?[lL]?[lL]?)##", boost::regex_constants::optimize), Common::PaletteIndex::Number, false},
	{boost::regex(R"##(0[0-7]+[Uu]?[lL]?[lL]?)##", boost::regex_constants::optimize), Common::PaletteIndex::Number, false},
	{boost::regex(R"##(0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?)##", boost::regex_constants::optimize), Common::PaletteIndex::Number, false},
	{boost::regex(R"##([_a-zA-Z]\w*)##", boost::regex_constants::optimize), Common::PaletteIndex::Identifier, true},
	{boost::regex(R"##(nullptr|NULL|true|false|auto|break|case|char|const|continue|default|do|double|bool|else|enum|extern|float|for|goto|if|inline|int|long|register|restrict|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while)##", boost::regex_constants::optimize), Common::PaletteIndex::Keyword, true},
	{boost::regex(R"##([ \t]*#[ \t]*[a-zA-Z_]+)##", boost::regex_constants::optimize), Common::PaletteIndex::Preprocessor, false},
	{boost::regex(R"##('[^']+')##", boost::regex_constants::optimize), Common::PaletteIndex::CharLiteral, false},
	{boost::regex(R"##(L?\"(\\.|[^\"])*\")##", boost::regex_constants::optimize), Common::PaletteIndex::String, false},
	{boost::regex(R"##(//.*)##", boost::regex_constants::optimize), Common::PaletteIndex::Comment, false},
};


static bool Matches(const Common::Glyph* g, const char* c)
{
	for (; *c != '\0' && (g->mChar == *c); c++, g++);
	return *c == '\0';
}


static void OneLineColorize(std::vector<Common::Line>& aEditorLines, int aLineStart, int aLineEnd, const std::vector<ColorizerRule>& aRuleList)
{
	boost::cmatch regexMatch;
	static std::string tempLineBuffer;
	static std::vector<std::pair<int, int>> tempTokenLocations;
	for (int currentLine = aLineStart; currentLine < aLineEnd; currentLine++)
	{
		// Set up temp buffer for regex matching
		tempLineBuffer.resize(aEditorLines[currentLine].size());
		for (int i = 0; i < aEditorLines[currentLine].size(); i++)
			tempLineBuffer[i] = aEditorLines[currentLine][i].mChar;

		// Tokenize
		tempTokenLocations.clear();
		static boost::regex tokenizePattern = boost::regex(R"##(\w+)##", boost::regex_constants::optimize);
		const char* currentStart = tempLineBuffer.c_str();
		while (true)
		{
			bool matched = false;
			try { matched = boost::regex_search(currentStart, tempLineBuffer.c_str() + tempLineBuffer.length(), regexMatch, tokenizePattern); }
			catch (...) {}
			if (!matched)
				break;
			auto& v = *regexMatch.begin();
			tempTokenLocations.push_back({ v.first - tempLineBuffer.c_str(), v.second - tempLineBuffer.c_str() });
			currentStart = v.second;
		}

		for (const ColorizerRule& rule : aRuleList)
		{
			if (rule.needsTokenization)
			{
				for (const std::pair<int, int>& p : tempTokenLocations)
				{
					bool matched = false;
					
					try { matched = boost::regex_match(tempLineBuffer.c_str() + p.first, tempLineBuffer.c_str() + p.second, regexMatch, rule.pattern); }
					catch (...) {}
					if (!matched)
						continue;
					for (int i = p.first; i < p.second; i++)
						aEditorLines[currentLine][i].mColorIndex = rule.colorIndex;
				}
			}
			else
			{
				const char* currentStart = tempLineBuffer.c_str();
				while (true)
				{
					bool matched = false;
					try { matched = boost::regex_search(currentStart, tempLineBuffer.c_str() + tempLineBuffer.length(), regexMatch, rule.pattern); }
					catch (...) {}
					if (!matched)
						break;
					auto& v = *regexMatch.begin();
					int begin = v.first - tempLineBuffer.c_str();
					int length = v.second - v.first;
					for (int i = 0; i < length; i++)
						aEditorLines[currentLine][begin + i].mColorIndex = rule.colorIndex;
					currentStart = v.second;
				}
			}
		}
	}
}



void ColorizerC::OnLoad(std::vector<Common::Line>& aEditorLines)
{
	OneLineColorize(aEditorLines, 0, aEditorLines.size(), C_PATTERN_PALETTE_INDEX_PAIRS);

	mMultilineCommentStart.clear();
	mMultilineCommentEnd.clear();
	bool inMultilineComment = false;
	for (int i = 0; i < aEditorLines.size(); i++)
	{
		Common::Line& l = aEditorLines[i];
		//bool inOnelineComment = false;
		for (int j = 0; j < l.size(); j++)
		{
			Common::Glyph& g = l[j];
			if (inMultilineComment)
			{
				g.mColorIndex = Common::PaletteIndex::Comment;
				if (j < l.size() - 1 && Matches(&g, C_MULTILINE_COMMENT_END))
				{
					mMultilineCommentEnd[i] = j;
					l[j + 1].mColorIndex = Common::PaletteIndex::Comment;
					j++;
					inMultilineComment = false;
				}
				continue;
			}
			else if (j < l.size() - 1 && Matches(&g, C_MULTILINE_COMMENT_START))
			{
				mMultilineCommentStart[i] = j;
				inMultilineComment = true;
				g.mColorIndex = Common::PaletteIndex::Comment;
				continue;
			}

			//if (inOnelineComment)
			//{
			//	g.mColorIndex = Common::PaletteIndex::Comment;
			//	continue;
			//}
			//else if (j < l.size() - 1 && Matches(&g, C_ONELINE_COMMENT))
			//{
			//	inOnelineComment = true;
			//	g.mColorIndex = Common::PaletteIndex::Comment;
			//	continue;
			//}
			//g.mColorIndex = Common::PaletteIndex::Default;
		}
	}
}

void ColorizerC::OnChange(const std::string& aText, int aStartLine, int aStartChar, int aEndLine, int aEndChar, bool aAddition, std::vector<Common::Line>& aEditorLines)
{
	if (aAddition)
	{
		OneLineColorize(aEditorLines, aStartLine, aEndLine + 1, C_PATTERN_PALETTE_INDEX_PAIRS);

		int currentLine = aStartLine;
		//int currentChar = aStartChar;
		int currentChar = 0;
		while (true)
		{
			if (currentLine < aEditorLines.size() && currentChar < aEditorLines[currentLine].size())
			{
				//aEditorLines[currentLine][currentChar].mColorIndex = Common::PaletteIndex::Keyword;
				if ((currentChar < aEditorLines[currentLine].size() - 1 && Matches(&(aEditorLines[currentLine][currentChar]), C_MULTILINE_COMMENT_START)) ||
					(currentChar < aEditorLines[currentLine].size() - 1 && Matches(&(aEditorLines[currentLine][currentChar]), C_MULTILINE_COMMENT_END)))
				{
					OnLoad(aEditorLines); // TODO: should only colorize down if starting multiline comment
					return;
				}
			}
	
			// Go to next glyph
			currentChar++;
			if (currentChar >= aEditorLines[currentLine].size())
				currentLine++, currentChar = 0;
			if (currentLine > aEndLine || currentLine == aEndLine && currentChar >= aEndChar)
				break;
		}
	}
}