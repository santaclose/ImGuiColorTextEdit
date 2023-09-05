#include <algorithm>
#include <chrono>
#include <string>
#include <cmath>
#include <set>

#include "TextEditor.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h" // for imGui::GetCurrentWindow()

// TODO
// - multiline comments vs single-line: latter is blocking start of a ML

template<class InputIt1, class InputIt2, class BinaryPredicate>
bool equals(InputIt1 first1, InputIt1 last1,
	InputIt2 first2, InputIt2 last2, BinaryPredicate p)
{
	for (; first1 != last1 && first2 != last2; ++first1, ++first2)
	{
		if (!p(*first1, *first2))
			return false;
	}
	return first1 == last1 && first2 == last2;
}

TextEditor::TextEditor()
	: mLineSpacing(1.0f)
	, mUndoIndex(0)
	, mTabSize(4)
	, mOverwrite(false)
	, mReadOnly(false)
	, mAutoIndent(true)
	, mWithinRender(false)
	, mScrollToCursor(false)
	, mScrollToTop(false)
	, mColorizerEnabled(true)
	, mTextStart(20.0f)
	, mLongestLineLength(20.0f)
	, mLeftMargin(10)
	, mColorRangeMin(0)
	, mColorRangeMax(0)
	, mCheckComments(true)
	, mShowWhitespaces(true)
	, mStartTime(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
	, mLastClick(-1.0f)
{
	SetPalette(GetDarkPalette());
	mLines.push_back(Line());
}

TextEditor::~TextEditor()
{
}

void TextEditor::SetLanguageDefinition(const LanguageDefinition& aLanguageDef)
{
	mLanguageDefinition = &aLanguageDef;
	mRegexList.clear();

	for (const auto& r : mLanguageDefinition->mTokenRegexStrings)
		mRegexList.push_back(std::make_pair(boost::regex(r.first, boost::regex_constants::optimize), r.second));

	Colorize();
}

const char* TextEditor::GetLanguageDefinitionName() const
{
	return mLanguageDefinition != nullptr ? mLanguageDefinition->mName.c_str() : "unknown";
}

void TextEditor::SetPalette(const Palette& aValue)
{
	mPaletteBase = aValue;
}

std::string TextEditor::GetText(const Coordinates& aStart, const Coordinates& aEnd) const
{
	assert(aEnd > aStart);
	std::string result;

	auto lstart = aStart.mLine;
	auto lend = aEnd.mLine;
	auto istart = GetCharacterIndexR(aStart);
	auto iend = GetCharacterIndexR(aEnd);
	size_t s = 0;

	for (size_t i = lstart; i < lend; i++)
		s += mLines[i].size();

	result.reserve(s + s / 8);

	while (istart < iend || lstart < lend)
	{
		if (lstart >= (int)mLines.size())
			break;

		auto& line = mLines[lstart];
		if (istart < (int)line.size())
		{
			result += line[istart].mChar;
			istart++;
		}
		else
		{
			istart = 0;
			++lstart;
			result += '\n';
		}
	}

	return result;
}

TextEditor::Coordinates TextEditor::GetActualCursorCoordinates(int aCursor) const
{
	if (aCursor == -1)
		return SanitizeCoordinates(mState.mCursors[mState.mCurrentCursor].mInteractiveEnd);
	else
		return SanitizeCoordinates(mState.mCursors[aCursor].mInteractiveEnd);
}

TextEditor::Coordinates TextEditor::SanitizeCoordinates(const Coordinates& aValue) const
{
	auto line = aValue.mLine;
	auto column = aValue.mColumn;
	if (line >= (int)mLines.size())
	{
		if (mLines.empty())
		{
			line = 0;
			column = 0;
		}
		else
		{
			line = (int)mLines.size() - 1;
			column = GetLineMaxColumn(line);
		}
		return Coordinates(line, column);
	}
	else
	{
		column = mLines.empty() ? 0 : std::min(column, GetLineMaxColumn(line));
		return Coordinates(line, column);
	}
}

// https://en.wikipedia.org/wiki/UTF-8
// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
static int UTF8CharLength(TextEditor::Char c)
{
	if ((c & 0xFE) == 0xFC)
		return 6;
	if ((c & 0xFC) == 0xF8)
		return 5;
	if ((c & 0xF8) == 0xF0)
		return 4;
	else if ((c & 0xF0) == 0xE0)
		return 3;
	else if ((c & 0xE0) == 0xC0)
		return 2;
	return 1;
}

// "Borrowed" from ImGui source
static inline int ImTextCharToUtf8(char* buf, int buf_size, unsigned int c)
{
	if (c < 0x80)
	{
		buf[0] = (char)c;
		return 1;
	}
	if (c < 0x800)
	{
		if (buf_size < 2) return 0;
		buf[0] = (char)(0xc0 + (c >> 6));
		buf[1] = (char)(0x80 + (c & 0x3f));
		return 2;
	}
	if (c >= 0xdc00 && c < 0xe000)
	{
		return 0;
	}
	if (c >= 0xd800 && c < 0xdc00)
	{
		if (buf_size < 4) return 0;
		buf[0] = (char)(0xf0 + (c >> 18));
		buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
		buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[3] = (char)(0x80 + ((c) & 0x3f));
		return 4;
	}
	//else if (c < 0x10000)
	{
		if (buf_size < 3) return 0;
		buf[0] = (char)(0xe0 + (c >> 12));
		buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
		buf[2] = (char)(0x80 + ((c) & 0x3f));
		return 3;
	}
}

void TextEditor::DeleteRange(const Coordinates& aStart, const Coordinates& aEnd)
{
	assert(aEnd >= aStart);
	assert(!mReadOnly);

	if (aEnd == aStart)
		return;

	auto start = GetCharacterIndexL(aStart);
	auto end = GetCharacterIndexR(aEnd);

	if (aStart.mLine == aEnd.mLine)
	{
		auto n = GetLineMaxColumn(aStart.mLine);
		if (aEnd.mColumn >= n)
			RemoveGlyphsFromLine(aStart.mLine, start); // from start to end of line
		else
			RemoveGlyphsFromLine(aStart.mLine, start, end);
	}
	else
	{
		RemoveGlyphsFromLine(aStart.mLine, start); // from start to end of line
		RemoveGlyphsFromLine(aEnd.mLine, 0, end);
		auto& firstLine = mLines[aStart.mLine];
		auto& lastLine = mLines[aEnd.mLine];

		if (aStart.mLine < aEnd.mLine)
		{
			AddGlyphsToLine(aStart.mLine, firstLine.size(), lastLine.begin(), lastLine.end());
			for (int c = 0; c <= mState.mCurrentCursor; c++) // move up cursors in line that is being moved up
			{
				if (mState.mCursors[c].mInteractiveEnd.mLine > aEnd.mLine)
					break;
				else if (mState.mCursors[c].mInteractiveEnd.mLine != aEnd.mLine)
					continue;
				int otherCursorEndCharIndex = GetCharacterIndexR(mState.mCursors[c].mInteractiveEnd);
				int otherCursorStartCharIndex = GetCharacterIndexR(mState.mCursors[c].mInteractiveStart);
				int otherCursorNewEndCharIndex = GetCharacterIndexR(aStart) + otherCursorEndCharIndex;
				int otherCursorNewStartCharIndex = GetCharacterIndexR(aStart) + otherCursorStartCharIndex;
				auto targetEndCoords = Coordinates(aStart.mLine, GetCharacterColumn(aStart.mLine, otherCursorNewEndCharIndex));
				auto targetStartCoords = Coordinates(aStart.mLine, GetCharacterColumn(aStart.mLine, otherCursorNewStartCharIndex));
				SetCursorPosition(targetStartCoords, c, true);
				SetCursorPosition(targetEndCoords, c, false);
			}
			RemoveLines(aStart.mLine + 1, aEnd.mLine + 1);
		}
	}
}

int TextEditor::InsertTextAt(Coordinates& /* inout */ aWhere, const char* aValue)
{
	assert(!mReadOnly);

	int cindex = GetCharacterIndexR(aWhere);
	int totalLines = 0;
	while (*aValue != '\0')
	{
		assert(!mLines.empty());

		if (*aValue == '\r')
		{
			// skip
			++aValue;
		}
		else if (*aValue == '\n')
		{
			if (cindex < (int)mLines[aWhere.mLine].size())
			{
				auto& newLine = InsertLine(aWhere.mLine + 1);
				auto& line = mLines[aWhere.mLine];
				AddGlyphsToLine(aWhere.mLine + 1, 0, line.begin() + cindex, line.end());
				RemoveGlyphsFromLine(aWhere.mLine, cindex);
			}
			else
			{
				InsertLine(aWhere.mLine + 1);
			}
			++aWhere.mLine;
			aWhere.mColumn = 0;
			cindex = 0;
			++totalLines;
			++aValue;
		}
		else
		{
			auto& line = mLines[aWhere.mLine];
			auto d = UTF8CharLength(*aValue);
			while (d-- > 0 && *aValue != '\0')
				AddGlyphToLine(aWhere.mLine, cindex++, Glyph(*aValue++, PaletteIndex::Default));
			aWhere.mColumn = GetCharacterColumn(aWhere.mLine, cindex);
		}
	}

	return totalLines;
}

void TextEditor::AddUndo(UndoRecord& aValue)
{
	assert(!mReadOnly);
	//printf("AddUndo: (@%d.%d) +\'%s' [%d.%d .. %d.%d], -\'%s', [%d.%d .. %d.%d] (@%d.%d)\n",
	//	aValue.mBefore.mCursorPosition.mLine, aValue.mBefore.mCursorPosition.mColumn,
	//	aValue.mAdded.c_str(), aValue.mAddedStart.mLine, aValue.mAddedStart.mColumn, aValue.mAddedEnd.mLine, aValue.mAddedEnd.mColumn,
	//	aValue.mRemoved.c_str(), aValue.mRemovedStart.mLine, aValue.mRemovedStart.mColumn, aValue.mRemovedEnd.mLine, aValue.mRemovedEnd.mColumn,
	//	aValue.mAfter.mCursorPosition.mLine, aValue.mAfter.mCursorPosition.mColumn
	//	);

	mUndoBuffer.resize((size_t)(mUndoIndex + 1));
	mUndoBuffer.back() = aValue;
	++mUndoIndex;
}

TextEditor::Coordinates TextEditor::ScreenPosToCoordinates(const ImVec2& aPosition, bool aInsertionMode, bool* isOverLineNumber) const
{
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 local(aPosition.x - origin.x + 3.0f, aPosition.y - origin.y);

	if (isOverLineNumber != nullptr)
		*isOverLineNumber = local.x < mTextStart;

	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ").x;

	int lineNo = std::max(0, (int)floor(local.y / mCharAdvance.y));

	int columnCoord = 0;

	if (lineNo >= 0 && lineNo < (int)mLines.size())
	{
		auto& line = mLines.at(lineNo);

		//int columnIndex = 0;
		std::string cumulatedString = "";
		//float columnWidth = 0.0f;
		float columnX = 0.0f;
		//int delta = 0;

		// First we find the hovered column coord.
		for (size_t columnIndex = 0; columnIndex < line.size(); ++columnIndex)
		{
			float columnWidth = 0.0f;
			int delta = 0;

			if (line[columnIndex].mChar == '\t')
			{
				float oldX = columnX;
				columnX = (1.0f + std::floor((1.0f + columnX) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
				columnWidth = columnX - oldX;
				delta = mTabSize - (columnCoord % mTabSize);
			}
			else
			{
				char buf[7];
				auto d = UTF8CharLength(line[columnIndex].mChar);
				int i = 0;
				while (i < 6 && d-- > 0)
					buf[i++] = line[columnIndex].mChar;
				buf[i] = '\0';
				columnWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf).x;
				columnX += columnWidth;
				delta = 1;
			}

			if (mTextStart + columnX - (aInsertionMode ? 0.5f : 0.0f) * columnWidth < local.x)
				columnCoord += delta;
			else
				break;
		}

		// Then we reduce by 1 column coord if cursor is on the left side of the hovered column.
		//if (aInsertionMode && mTextStart + columnX - columnWidth * 2.0f < local.x)
		//	columnIndex = std::min((int)line.size() - 1, columnIndex + 1);
	}

	return SanitizeCoordinates(Coordinates(lineNo, columnCoord));
}

TextEditor::Coordinates TextEditor::FindWordStart(const Coordinates& aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndexL(at);

	if (cindex >= (int)line.size())
		return at;

	bool initialIsWordChar = IsGlyphWordChar(line[cindex]);
	bool initialIsSpace = isspace(line[cindex].mChar);
	uint8_t initialChar = line[cindex].mChar;
	bool needToAdvance = false;
	while (true)
	{
		--cindex;
		if (cindex < 0)
		{
			cindex = 0;
			break;
		}

		auto c = line[cindex].mChar;
		if (!IsUTFSequence(c))
		{
			bool isWordChar = IsGlyphWordChar(line[cindex]);
			bool isSpace = isspace(line[cindex].mChar);
			if (initialIsSpace && !isSpace || initialIsWordChar && !isWordChar || !initialIsWordChar && !initialIsSpace && initialChar != line[cindex].mChar)
			{
				needToAdvance = true;
				break;
			}
		}
	}
	at.mColumn = GetCharacterColumn(at.mLine, cindex);
	if (needToAdvance)
		MoveCoords(at, MoveDirection::Right);
	return at;
}

TextEditor::Coordinates TextEditor::FindWordEnd(const Coordinates& aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];
	auto cindex = GetCharacterIndexL(at);

	if (cindex >= (int)line.size())
		return at;

	bool initialIsWordChar = IsGlyphWordChar(line[cindex]);
	bool initialIsSpace = isspace(line[cindex].mChar);
	uint8_t initialChar = line[cindex].mChar;
	while (true)
	{
		auto d = UTF8CharLength(line[cindex].mChar);
		cindex += d;
		if (cindex >= (int)line.size())
			break;

		bool isWordChar = IsGlyphWordChar(line[cindex]);
		bool isSpace = isspace(line[cindex].mChar);
		if (initialIsSpace && !isSpace || initialIsWordChar && !isWordChar || !initialIsWordChar && !initialIsSpace && initialChar != line[cindex].mChar)
			break;
	}
	at.mColumn = GetCharacterColumn(at.mLine, cindex);
	return at;
}

TextEditor::Coordinates TextEditor::FindNextWord(const Coordinates& aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	// skip to the next non-word character
	auto cindex = GetCharacterIndexR(aFrom);
	bool isword = false;
	bool skip = false;
	if (cindex < (int)mLines[at.mLine].size())
	{
		auto& line = mLines[at.mLine];
		isword = !!isalnum(line[cindex].mChar);
		skip = isword;
	}

	while (!isword || skip)
	{
		if (at.mLine >= mLines.size())
		{
			auto l = std::max(0, (int)mLines.size() - 1);
			return Coordinates(l, GetLineMaxColumn(l));
		}

		auto& line = mLines[at.mLine];
		if (cindex < (int)line.size())
		{
			isword = isalnum(line[cindex].mChar);

			if (isword && !skip)
				return Coordinates(at.mLine, GetCharacterColumn(at.mLine, cindex));

			if (!isword)
				skip = false;

			cindex++;
		}
		else
		{
			cindex = 0;
			++at.mLine;
			skip = false;
			isword = false;
		}
	}

	return at;
}

int TextEditor::GetCharacterIndexL(const Coordinates& aCoordinates) const
{
	if (aCoordinates.mLine >= mLines.size())
		return -1;

	auto& line = mLines[aCoordinates.mLine];
	int c = 0;
	int i = 0;
	int tabCoordsLeft = 0;

	for (; i < line.size() && c < aCoordinates.mColumn;)
	{
		if (line[i].mChar == '\t')
		{
			if (tabCoordsLeft == 0)
				tabCoordsLeft = mTabSize - (c % mTabSize);
			if (tabCoordsLeft > 0)
				tabCoordsLeft--;
			c++;
		}
		else
			++c;
		if (tabCoordsLeft == 0)
			i += UTF8CharLength(line[i].mChar);
	}
	return i;
}

int TextEditor::GetCharacterIndexR(const Coordinates& aCoordinates) const
{
	if (aCoordinates.mLine >= mLines.size())
		return -1;
	auto& line = mLines[aCoordinates.mLine];
	int c = 0;
	int i = 0;
	for (; i < line.size() && c < aCoordinates.mColumn;)
	{
		if (line[i].mChar == '\t')
			c = (c / mTabSize) * mTabSize + mTabSize;
		else
			++c;
		i += UTF8CharLength(line[i].mChar);
	}
	return i;
}

int TextEditor::GetCharacterColumn(int aLine, int aIndex) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int col = 0;
	int i = 0;
	while (i < aIndex && i < (int)line.size())
	{
		auto c = line[i].mChar;
		i += UTF8CharLength(c);
		if (c == '\t')
			col = (col / mTabSize) * mTabSize + mTabSize;
		else
			col++;
	}
	return col;
}

int TextEditor::GetLineCharacterCount(int aLine) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int c = 0;
	for (unsigned i = 0; i < line.size(); c++)
		i += UTF8CharLength(line[i].mChar);
	return c;
}

int TextEditor::GetLineMaxColumn(int aLine) const
{
	if (aLine >= mLines.size())
		return 0;
	auto& line = mLines[aLine];
	int col = 0;
	for (unsigned i = 0; i < line.size(); )
	{
		auto c = line[i].mChar;
		if (c == '\t')
			col = (col / mTabSize) * mTabSize + mTabSize;
		else
			col++;
		i += UTF8CharLength(c);
	}
	return col;
}

bool TextEditor::IsOnWordBoundary(const Coordinates& aAt) const
{
	if (aAt.mLine >= (int)mLines.size() || aAt.mColumn == 0)
		return true;

	auto& line = mLines[aAt.mLine];
	auto cindex = GetCharacterIndexR(aAt);
	if (cindex >= (int)line.size())
		return true;

	if (mColorizerEnabled)
		return line[cindex].mColorIndex != line[size_t(cindex - 1)].mColorIndex;

	return isspace(line[cindex].mChar) != isspace(line[cindex - 1].mChar);
}

void TextEditor::RemoveLines(int aStart, int aEnd)
{
	assert(!mReadOnly);
	assert(aEnd >= aStart);
	assert(mLines.size() > (size_t)(aEnd - aStart));

	mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
	assert(!mLines.empty());

	OnLinesDeleted(aStart, aEnd);
}

void TextEditor::RemoveLine(int aIndex, const std::unordered_set<int>* aHandledCursors)
{
	assert(!mReadOnly);
	assert(mLines.size() > 1);

	mLines.erase(mLines.begin() + aIndex);
	assert(!mLines.empty());

	OnLineDeleted(aIndex, aHandledCursors);
}

void TextEditor::RemoveCurrentLines()
{
	UndoRecord u;
	u.mBefore = mState;

	if (AnyCursorHasSelection())
	{
		for (int c = mState.mCurrentCursor; c > -1; c--)
		{
			if (!mState.mCursors[c].HasSelection())
				continue;
			u.mOperations.push_back({ GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete });
			DeleteSelection(c);
		}
	}
	MoveHome();
	OnCursorPositionChanged(); // might combine cursors

	for (int c = mState.mCurrentCursor; c > -1; c--)
	{
		int currentLine = mState.mCursors[c].mInteractiveEnd.mLine;
		int nextLine = currentLine + 1;
		int prevLine = currentLine - 1;

		Coordinates toDeleteStart, toDeleteEnd;
		if (mLines.size() > nextLine) // next line exists
		{
			toDeleteStart = Coordinates(currentLine, 0);
			toDeleteEnd = Coordinates(nextLine, 0);
			SetCursorPosition({ mState.mCursors[c].mInteractiveEnd.mLine, 0 }, c);
		}
		else if (prevLine > -1) // previous line exists
		{
			toDeleteStart = Coordinates(prevLine, GetLineMaxColumn(prevLine));
			toDeleteEnd = Coordinates(currentLine, GetLineMaxColumn(currentLine));
			SetCursorPosition({ prevLine, 0 }, c);
		}
		else
		{
			toDeleteStart = Coordinates(currentLine, 0);
			toDeleteEnd = Coordinates(currentLine, GetLineMaxColumn(currentLine));
			SetCursorPosition({ currentLine, 0 }, c);
		}

		u.mOperations.push_back({ GetText(toDeleteStart, toDeleteEnd), toDeleteStart, toDeleteEnd, UndoOperationType::Delete });

		std::unordered_set<int> handledCursors = { c };
		if (toDeleteStart.mLine != toDeleteEnd.mLine)
			RemoveLine(currentLine, &handledCursors);
		else
			DeleteRange(toDeleteStart, toDeleteEnd);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::OnLineChanged(bool aBeforeChange, int aLine, int aColumn, int aCharCount, bool aDeleted) // adjusts cursor position when other cursor writes/deletes in the same line
{
	static std::unordered_map<int, int> cursorCharIndices;
	if (aBeforeChange)
	{
		cursorCharIndices.clear();
		for (int c = 0; c <= mState.mCurrentCursor; c++)
		{
			if (mState.mCursors[c].mInteractiveEnd.mLine == aLine && // cursor is at the line
				mState.mCursors[c].mInteractiveEnd.mColumn > aColumn && // cursor is to the right of changing part
				mState.mCursors[c].GetSelectionEnd() == mState.mCursors[c].GetSelectionStart()) // cursor does not have a selection
			{
				cursorCharIndices[c] = GetCharacterIndexR({ aLine, mState.mCursors[c].mInteractiveEnd.mColumn });
				cursorCharIndices[c] += aDeleted ? -aCharCount : aCharCount;
			}
		}
	}
	else
	{
		for (auto& item : cursorCharIndices)
			SetCursorPosition({ aLine, GetCharacterColumn(aLine, item.second) }, item.first);
	}
}

void TextEditor::RemoveGlyphsFromLine(int aLine, int aStartChar, int aEndChar)
{
	int column = GetCharacterColumn(aLine, aStartChar);
	auto& line = mLines[aLine];
	OnLineChanged(true, aLine, column, aEndChar - aStartChar, true);
	line.erase(line.begin() + aStartChar, aEndChar == -1 ? line.end() : line.begin() + aEndChar);
	OnLineChanged(false, aLine, column, aEndChar - aStartChar, true);
}

void TextEditor::AddGlyphsToLine(int aLine, int aTargetIndex, Line::iterator aSourceStart, Line::iterator aSourceEnd)
{
	int targetColumn = GetCharacterColumn(aLine, aTargetIndex);
	int charsInserted = std::distance(aSourceStart, aSourceEnd);
	auto& line = mLines[aLine];
	OnLineChanged(true, aLine, targetColumn, charsInserted, false);
	line.insert(line.begin() + aTargetIndex, aSourceStart, aSourceEnd);
	OnLineChanged(false, aLine, targetColumn, charsInserted, false);
}

void TextEditor::AddGlyphToLine(int aLine, int aTargetIndex, Glyph aGlyph)
{
	int targetColumn = GetCharacterColumn(aLine, aTargetIndex);
	auto& line = mLines[aLine];
	OnLineChanged(true, aLine, targetColumn, 1, false);
	line.insert(line.begin() + aTargetIndex, aGlyph);
	OnLineChanged(false, aLine, targetColumn, 1, false);
}

TextEditor::Line& TextEditor::InsertLine(int aIndex)
{
	assert(!mReadOnly);

	auto& result = *mLines.insert(mLines.begin() + aIndex, Line());
	OnLineAdded(aIndex);

	return result;
}

std::string TextEditor::GetWordUnderCursor() const
{
	auto c = GetCursorPosition();
	return GetWordAt(c);
}

std::string TextEditor::GetWordAt(const Coordinates& aCoords) const
{
	auto start = FindWordStart(aCoords);
	auto end = FindWordEnd(aCoords);

	std::string r;

	auto istart = GetCharacterIndexR(start);
	auto iend = GetCharacterIndexR(end);

	for (auto it = istart; it < iend; ++it)
		r.push_back(mLines[aCoords.mLine][it].mChar);

	return r;
}

ImU32 TextEditor::GetGlyphColor(const Glyph& aGlyph) const
{
	if (!mColorizerEnabled)
		return mPalette[(int)PaletteIndex::Default];
	if (aGlyph.mComment)
		return mPalette[(int)PaletteIndex::Comment];
	if (aGlyph.mMultiLineComment)
		return mPalette[(int)PaletteIndex::MultiLineComment];
	auto const color = mPalette[(int)aGlyph.mColorIndex];
	if (aGlyph.mPreprocessor)
	{
		const auto ppcolor = mPalette[(int)PaletteIndex::Preprocessor];
		const int c0 = ((ppcolor & 0xff) + (color & 0xff)) / 2;
		const int c1 = (((ppcolor >> 8) & 0xff) + ((color >> 8) & 0xff)) / 2;
		const int c2 = (((ppcolor >> 16) & 0xff) + ((color >> 16) & 0xff)) / 2;
		const int c3 = (((ppcolor >> 24) & 0xff) + ((color >> 24) & 0xff)) / 2;
		return ImU32(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24));
	}
	return color;
}

bool TextEditor::IsGlyphWordChar(const Glyph& aGlyph)
{
	int sizeInBytes = UTF8CharLength(aGlyph.mChar);
	return sizeInBytes > 1 ||
		aGlyph.mChar >= 'a' && aGlyph.mChar <= 'z' ||
		aGlyph.mChar >= 'A' && aGlyph.mChar <= 'Z' ||
		aGlyph.mChar >= '0' && aGlyph.mChar <= '9' ||
		aGlyph.mChar == '_';
}

void TextEditor::HandleKeyboardInputs(bool aParentIsFocused)
{
	if (ImGui::IsWindowFocused() || aParentIsFocused)
	{
		if (ImGui::IsWindowHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		//ImGui::CaptureKeyboardFromApp(true);

		ImGuiIO& io = ImGui::GetIO();
		auto isOSX = io.ConfigMacOSXBehaviors;
		auto alt = io.KeyAlt;
		auto ctrl = io.KeyCtrl;
		auto shift = io.KeyShift;
		auto super = io.KeySuper;

		auto isShortcut = (isOSX ? (super && !ctrl) : (ctrl && !super)) && !alt && !shift;
		auto isShiftShortcut = (isOSX ? (super && !ctrl) : (ctrl && !super)) && shift && !alt;
		auto isWordmoveKey = isOSX ? alt : ctrl;
		auto isAltOnly = alt && !ctrl && !shift && !super;
		auto isCtrlOnly = ctrl && !alt && !shift && !super;
		auto isShiftOnly = shift && !alt && !ctrl && !super;

		io.WantCaptureKeyboard = true;
		io.WantTextInput = true;

		if (!mReadOnly && isShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)))
			Undo();
		else if (!mReadOnly && isAltOnly && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			Undo();
		else if (!mReadOnly && isShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Y)))
			Redo();
		else if (!mReadOnly && isShiftShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)))
			Redo();
		else if (!alt && !ctrl && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)))
			MoveUp(1, shift);
		else if (!alt && !ctrl && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)))
			MoveDown(1, shift);
		else if ((isOSX ? !ctrl : !alt) && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow)))
			MoveLeft(shift, isWordmoveKey);
		else if ((isOSX ? !ctrl : !alt) && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow)))
			MoveRight(shift, isWordmoveKey);
		else if (!alt && !ctrl && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
			MoveUp(GetPageSize() - 4, shift);
		else if (!alt && !ctrl && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
			MoveDown(GetPageSize() - 4, shift);
		else if (ctrl && !alt && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveTop(shift);
		else if (ctrl && !alt && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveBottom(shift);
		else if (!alt && !ctrl && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveHome(shift);
		else if (!alt && !ctrl && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveEnd(shift);
		else if (!mReadOnly && !alt && !shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			Delete(ctrl);
		else if (!mReadOnly && !alt && !shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			Backspace(ctrl);
		else if (!mReadOnly && !alt && ctrl && shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_K)))
			RemoveCurrentLines();
		else if (!mReadOnly && !alt && ctrl && !shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftBracket)))
			ChangeCurrentLinesIndentation(false);
		else if (!mReadOnly && !alt && ctrl && !shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightBracket)))
			ChangeCurrentLinesIndentation(true);
		else if (!alt && ctrl && shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)))
			MoveUpCurrentLines();
		else if (!alt && ctrl && shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)))
			MoveDownCurrentLines();
		else if (!mReadOnly && !alt && ctrl && !shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Slash)))
			ToggleLineComment();
		else if (!alt && !ctrl && !shift && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			mOverwrite ^= true;
		else if (isCtrlOnly && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			Copy();
		else if (isShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C)))
			Copy();
		else if (!mReadOnly && isShiftOnly && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			Paste();
		else if (!mReadOnly && isShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
			Paste();
		else if (isShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_X)))
			Cut();
		else if (isShiftOnly && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			Cut();
		else if (isShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_A)))
			SelectAll();
		else if (isShortcut && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_D)))
			AddCursorForNextOccurrence();
        else if (!mReadOnly && !alt && !ctrl && !shift && !super && (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_KeypadEnter))))
			EnterCharacter('\n', false);
		else if (!mReadOnly && !alt && !ctrl && !super && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)))
			EnterCharacter('\t', shift);
		if (!mReadOnly && !io.InputQueueCharacters.empty() && ctrl == alt && !super)
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; i++)
			{
				auto c = io.InputQueueCharacters[i];
				if (c != 0 && (c == '\n' || c >= 32))
					EnterCharacter(c, shift);
			}
			io.InputQueueCharacters.resize(0);
		}
	}
}

void TextEditor::HandleMouseInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	/*
	Pan with middle mouse button
	*/
	mState.mPanning &= ImGui::IsMouseDown(2);
	if (mState.mPanning && ImGui::IsMouseDragging(2))
	{
		ImVec2 scroll = { ImGui::GetScrollX(), ImGui::GetScrollY() };
		ImVec2 currentMousePos = ImGui::GetMouseDragDelta(2);
		ImVec2 mouseDelta = {
			currentMousePos.x - mState.mLastMousePos.x,
			currentMousePos.y - mState.mLastMousePos.y
		};
		ImGui::SetScrollY(scroll.y - mouseDelta.y);
		ImGui::SetScrollX(scroll.x - mouseDelta.x);
		mState.mLastMousePos = currentMousePos;
	}

	// Mouse left button dragging (=> update selection)
	mState.mDraggingSelection &= ImGui::IsMouseDown(0);
	if (mState.mDraggingSelection && ImGui::IsMouseDragging(0))
	{
		io.WantCaptureMouse = true;
		Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos(), !mOverwrite);
		SetCursorPosition(cursorCoords, mState.GetLastAddedCursorIndex(), false);
	}

	if (ImGui::IsWindowHovered())
	{
		auto click = ImGui::IsMouseClicked(0);
		if (!shift && !alt)
		{
			auto doubleClick = ImGui::IsMouseDoubleClicked(0);
			auto t = ImGui::GetTime();
			auto tripleClick = click && !doubleClick && (mLastClick != -1.0f && (t - mLastClick) < io.MouseDoubleClickTime);
			if (click)
				mState.mDraggingSelection = true;

			/*
			Pan with middle mouse button
			*/

			if (ImGui::IsMouseClicked(2))
			{
				mState.mPanning = true;
				mState.mLastMousePos = ImGui::GetMouseDragDelta(2);
			}

			/*
			Left mouse button triple click
			*/

			if (tripleClick)
			{
				if (ctrl)
					mState.AddCursor();
				else
					mState.mCurrentCursor = 0;

				Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos());
				Coordinates targetCursorPos = cursorCoords.mLine < mLines.size() - 1 ?
					Coordinates{ cursorCoords.mLine + 1, 0 } :
					Coordinates{ cursorCoords.mLine, GetLineMaxColumn(cursorCoords.mLine) };
				SetSelection({ cursorCoords.mLine, 0 }, targetCursorPos, mState.mCurrentCursor);

				mLastClick = -1.0f;
			}

			/*
			Left mouse button double click
			*/

			else if (doubleClick)
			{
				if (ctrl)
					mState.AddCursor();
				else
					mState.mCurrentCursor = 0;

				Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos());
				SetSelection(FindWordStart(cursorCoords), FindWordEnd(cursorCoords), mState.mCurrentCursor);

				mLastClick = (float)ImGui::GetTime();
			}

			/*
			Left mouse button click
			*/
			else if (click)
			{
				if (ctrl)
					mState.AddCursor();
				else
					mState.mCurrentCursor = 0;

				bool isOverLineNumber;
				Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos(), !mOverwrite, &isOverLineNumber);
				if (isOverLineNumber)
				{
					Coordinates targetCursorPos = cursorCoords.mLine < mLines.size() - 1 ?
						Coordinates{ cursorCoords.mLine + 1, 0 } :
						Coordinates{ cursorCoords.mLine, GetLineMaxColumn(cursorCoords.mLine) };
					SetSelection({ cursorCoords.mLine, 0 }, targetCursorPos, mState.mCurrentCursor);
				}
				else
					SetCursorPosition(cursorCoords, mState.GetLastAddedCursorIndex());

				mLastClick = (float)ImGui::GetTime();
			}
			else if (ImGui::IsMouseReleased(0))
			{
				mState.SortCursorsFromTopToBottom();
				MergeCursorsIfPossible();
			}
		}
		else if (shift)
		{
			if (click)
			{
				Coordinates newSelection = ScreenPosToCoordinates(ImGui::GetMousePos(), !mOverwrite);
				SetCursorPosition(SanitizeCoordinates(newSelection), mState.mCurrentCursor, false);
			}
		}
	}
}

void TextEditor::UpdatePalette()
{
	/* Update palette with the current alpha from style */
	for (int i = 0; i < (int)PaletteIndex::Max; ++i)
	{
		auto color = U32ColorToVec4(mPaletteBase[i]);
		color.w *= ImGui::GetStyle().Alpha;
		mPalette[i] = ImGui::ColorConvertFloat4ToU32(color);
	}
}

void TextEditor::Render(bool aParentIsFocused)
{
	/* Compute mCharAdvance regarding to scaled font size (Ctrl + mouse wheel)*/
	const float fontSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#", nullptr, nullptr).x;
	mCharAdvance = ImVec2(fontSize, ImGui::GetTextLineHeightWithSpacing() * mLineSpacing);

	assert(mLineBuffer.empty());

	auto contentSize = ImGui::GetWindowContentRegionMax();
	auto drawList = ImGui::GetWindowDrawList();

	if (mScrollToTop)
	{
		mScrollToTop = false;
		ImGui::SetScrollY(0.f);
	}

	ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
	auto scrollX = ImGui::GetScrollX();
	auto scrollY = ImGui::GetScrollY();

	auto lineNo = (int)floor(scrollY / mCharAdvance.y);
	auto globalLineMax = (int)mLines.size();
	auto lineMax = std::max(0, std::min((int)mLines.size() - 1, lineNo + (int)floor((scrollY + contentSize.y) / mCharAdvance.y)));

	// Deduce mTextStart by evaluating mLines size (global lineMax) plus two spaces as text width
	char buf[16];
	snprintf(buf, 16, " %d ", globalLineMax);
	mTextStart = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x + mLeftMargin;

	if (!mLines.empty())
	{
		float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;

		while (lineNo <= lineMax)
		{
			ImVec2 lineStartScreenPos = ImVec2(cursorScreenPos.x, cursorScreenPos.y + lineNo * mCharAdvance.y);
			ImVec2 textScreenPos = ImVec2(lineStartScreenPos.x + mTextStart, lineStartScreenPos.y);

			auto& line = mLines[lineNo];
			mLongestLineLength = std::max(mTextStart + TextDistanceToLineStart(Coordinates(lineNo, GetLineMaxColumn(lineNo))), mLongestLineLength);
			auto columnNo = 0;
			Coordinates lineStartCoord(lineNo, 0);
			Coordinates lineEndCoord(lineNo, GetLineMaxColumn(lineNo));

			// Draw selection for the current line
			for (int c = 0; c <= mState.mCurrentCursor; c++)
			{
				float sstart = -1.0f;
				float ssend = -1.0f;

				assert(mState.mCursors[c].GetSelectionStart() <= mState.mCursors[c].GetSelectionEnd());
				if (mState.mCursors[c].GetSelectionStart() <= lineEndCoord)
					sstart = mState.mCursors[c].GetSelectionStart() > lineStartCoord ? TextDistanceToLineStart(mState.mCursors[c].GetSelectionStart()) : 0.0f;
				if (mState.mCursors[c].GetSelectionEnd() > lineStartCoord)
					ssend = TextDistanceToLineStart(mState.mCursors[c].GetSelectionEnd() < lineEndCoord ? mState.mCursors[c].GetSelectionEnd() : lineEndCoord);

				if (mState.mCursors[c].GetSelectionEnd().mLine > lineNo)
					ssend += mCharAdvance.x;

				if (sstart != -1 && ssend != -1 && sstart < ssend)
				{
					ImVec2 vstart(lineStartScreenPos.x + mTextStart + sstart, lineStartScreenPos.y);
					ImVec2 vend(lineStartScreenPos.x + mTextStart + ssend, lineStartScreenPos.y + mCharAdvance.y);
					drawList->AddRectFilled(vstart, vend, mPalette[(int)PaletteIndex::Selection]);
				}
			}

			// Draw line number (right aligned)
            if (mDrawLineNumbers) {
                snprintf(buf, 16, "%d  ", lineNo + 1);

                auto lineNoWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf, nullptr, nullptr).x;
                drawList->AddText(ImVec2(lineStartScreenPos.x + mTextStart - lineNoWidth, lineStartScreenPos.y), mPalette[(int)PaletteIndex::LineNumber], buf);
            }

			std::vector<Coordinates> cursorCoordsInThisLine;
			for (int c = 0; c <= mState.mCurrentCursor; c++)
			{
				if (mState.mCursors[c].mInteractiveEnd.mLine == lineNo)
					cursorCoordsInThisLine.push_back(mState.mCursors[c].mInteractiveEnd);
			}
			if (cursorCoordsInThisLine.size() > 0)
			{
				auto focused = ImGui::IsWindowFocused() || aParentIsFocused;

				// Render the cursors
				if (focused)
				{
					for (const auto& cursorCoords : cursorCoordsInThisLine)
					{
						float width = 1.0f;
						auto cindex = GetCharacterIndexR(cursorCoords);
						float cx = TextDistanceToLineStart(cursorCoords);

						if (mOverwrite && cindex < (int)line.size())
						{
							auto c = line[cindex].mChar;
							if (c == '\t')
							{
								auto x = (1.0f + std::floor((1.0f + cx) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
								width = x - cx;
							}
							else
							{
								char buf2[2];
								buf2[0] = line[cindex].mChar;
								buf2[1] = '\0';
								width = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, buf2).x;
							}
						}
						ImVec2 cstart(textScreenPos.x + cx, lineStartScreenPos.y);
						ImVec2 cend(textScreenPos.x + cx + width, lineStartScreenPos.y + mCharAdvance.y);
						drawList->AddRectFilled(cstart, cend, mPalette[(int)PaletteIndex::Cursor]);
					}
				}
			}

			// Render colorized text
			auto prevColor = line.empty() ? mPalette[(int)PaletteIndex::Default] : GetGlyphColor(line[0]);
			ImVec2 bufferOffset;

			for (int i = 0; i < line.size();)
			{
				auto& glyph = line[i];
				auto color = GetGlyphColor(glyph);

				if ((color != prevColor || glyph.mChar == '\t' || glyph.mChar == ' ') && !mLineBuffer.empty())
				{
					const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
					drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
					auto textSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, mLineBuffer.c_str(), nullptr, nullptr);
					bufferOffset.x += textSize.x;
					mLineBuffer.clear();
				}
				prevColor = color;

				if (glyph.mChar == '\t')
				{
					auto oldX = bufferOffset.x;
					bufferOffset.x = (1.0f + std::floor((1.0f + bufferOffset.x) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
					++i;

					if (mShowWhitespaces)
					{
						ImVec2 p1, p2, p3, p4;

						const auto s = ImGui::GetFontSize();
						const auto x1 = textScreenPos.x + oldX + 1.0f;
						const auto x2 = textScreenPos.x + bufferOffset.x - 1.0f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;

						p1 = ImVec2(x1, y);
						p2 = ImVec2(x2, y);
						p3 = ImVec2(x2 - s * 0.2f, y - s * 0.2f);
						p4 = ImVec2(x2 - s * 0.2f, y + s * 0.2f);

						drawList->AddLine(p1, p2, mPalette[(int)PaletteIndex::ControlCharacter]);
						drawList->AddLine(p2, p3, mPalette[(int)PaletteIndex::ControlCharacter]);
						drawList->AddLine(p2, p4, mPalette[(int)PaletteIndex::ControlCharacter]);
					}
				}
				else if (glyph.mChar == ' ')
				{
					if (mShowWhitespaces)
					{
						const auto s = ImGui::GetFontSize();
						const auto x = textScreenPos.x + bufferOffset.x + spaceSize * 0.5f;
						const auto y = textScreenPos.y + bufferOffset.y + s * 0.5f;
						drawList->AddCircleFilled(ImVec2(x, y), 1.5f, mPalette[(int)PaletteIndex::ControlCharacter], 4);
					}
					bufferOffset.x += spaceSize;
					i++;
				}
				else
				{
					auto l = UTF8CharLength(glyph.mChar);
					while (l-- > 0)
						mLineBuffer.push_back(line[i++].mChar);
				}
				++columnNo;
			}

			if (!mLineBuffer.empty())
			{
				const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
				drawList->AddText(newOffset, prevColor, mLineBuffer.c_str());
				mLineBuffer.clear();
			}

			++lineNo;
		}
	}

	ImGui::SetCursorPos(ImVec2(0, 0));
	ImGui::Dummy(ImVec2((mLongestLineLength + 15), (mLines.size() + GetPageSize()) * mCharAdvance.y));

	if (mScrollToCursor)
	{
		EnsureCursorVisible();
		mScrollToCursor = false;
	}
}

bool TextEditor::FindNextOccurrence(const char* aText, int aTextSize, const Coordinates& aFrom, Coordinates& outStart, Coordinates& outEnd)
{
	assert(aTextSize > 0);
	bool fmatches = false;
	int fline, ifline;
	int findex, ifindex;

	ifline = fline = aFrom.mLine;
	ifindex = findex = GetCharacterIndexR(aFrom);

	while (true)
	{
		bool matches;
		{ // match function
			int lineOffset = 0;
			int currentCharIndex = findex;
			int i = 0;
			for (; i < aTextSize; i++)
			{
				if (currentCharIndex == mLines[fline + lineOffset].size())
				{
					if (aText[i] == '\n' && fline + lineOffset + 1 < mLines.size())
					{
						currentCharIndex = 0;
						lineOffset++;
					}
					else
						break;
				}
				else
				{
					if (mLines[fline + lineOffset][currentCharIndex].mChar != aText[i])
						break;
					else
						currentCharIndex++;
				}
			}
			matches = i == aTextSize;
			if (matches)
			{
				outStart = { fline, GetCharacterColumn(fline, findex) };
				outEnd = { fline + lineOffset, GetCharacterColumn(fline + lineOffset, currentCharIndex) };
				return true;
			}
		}

		// move forward
		if (findex == mLines[fline].size()) // need to consider line breaks
		{
			if (fline == mLines.size() - 1)
			{
				fline = 0;
				findex = 0;
			}
			else
			{
				fline++;
				findex = 0;
			}
		}
		else
			findex++;

		// detect complete scan
		if (findex == ifindex && fline == ifline)
			return false;
	}

	return false;
}

bool TextEditor::Render(const char* aTitle, bool aParentIsFocused, const ImVec2& aSize, bool aBorder)
{
	if (mState.mCursorPositionChanged)
		OnCursorPositionChanged();
	mState.mCursorPositionChanged = false;

	mWithinRender = true;

	UpdatePalette();

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Background]));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

	ImGui::BeginChild(aTitle, aSize, aBorder, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);

	bool isFocused = ImGui::IsWindowFocused();
	HandleKeyboardInputs(aParentIsFocused);
	HandleMouseInputs();
	ColorizeInternal();
	Render(aParentIsFocused);

	ImGui::EndChild();

	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	mWithinRender = false;
	return isFocused;
}

void TextEditor::SetText(const std::string& aText)
{
	mLines.clear();
	mLines.emplace_back(Line());
	for (auto chr : aText)
	{
		if (chr == '\r')
		{
			// ignore the carriage return character
		}
		else if (chr == '\n')
			mLines.emplace_back(Line());
		else
		{
			mLines.back().emplace_back(Glyph(chr, PaletteIndex::Default));
		}
	}

	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}

void TextEditor::SetTextLines(const std::vector<std::string>& aLines)
{
	mLines.clear();

	if (aLines.empty())
	{
		mLines.emplace_back(Line());
	}
	else
	{
		mLines.resize(aLines.size());

		for (size_t i = 0; i < aLines.size(); ++i)
		{
			const std::string& aLine = aLines[i];

			mLines[i].reserve(aLine.size());
			for (size_t j = 0; j < aLine.size(); ++j)
				mLines[i].emplace_back(Glyph(aLine[j], PaletteIndex::Default));
		}
	}

	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}


void TextEditor::ChangeCurrentLinesIndentation(bool aIncrease)
{
	assert(!mReadOnly);

	UndoRecord u;
	u.mBefore = mState;

	for (int c = mState.mCurrentCursor; c > -1; c--)
	{
		for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--)
		{
			if (Coordinates{ currentLine, 0 } == mState.mCursors[c].GetSelectionEnd() && mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
				continue;

			if (aIncrease)
			{
				if (mLines[currentLine].size() > 0)
				{
					Coordinates lineStart = { currentLine, 0 };
					Coordinates insertionEnd = lineStart;
					InsertTextAt(insertionEnd, "\t"); // sets insertion end
					u.mOperations.push_back({ "\t", lineStart, insertionEnd, UndoOperationType::Add });
					Colorize(lineStart.mLine, 1);
				}
			}
			else
			{
				Coordinates start = { currentLine, 0 };
				Coordinates end = { currentLine, mTabSize };
				int charIndex = GetCharacterIndexL(end) - 1;
				while (charIndex > -1 && (mLines[currentLine][charIndex].mChar == ' ' || mLines[currentLine][charIndex].mChar == '\t')) charIndex--;
				bool onlySpaceCharactersFound = charIndex == -1;
				if (onlySpaceCharactersFound)
				{
					u.mOperations.push_back({ GetText(start, end), start, end, UndoOperationType::Delete });
					DeleteRange(start, end);
					Colorize(currentLine, 1);
				}
			}
		}
	}

	EnsureCursorVisible();
	if (u.mOperations.size() > 0)
		AddUndo(u);
}

void TextEditor::MoveUpCurrentLines()
{
	assert(!mReadOnly);

	UndoRecord u;
	u.mBefore = mState;

	std::set<int> affectedLines;
	int minLine = -1;
	int maxLine = -1;
	for (int c = mState.mCurrentCursor; c > -1; c--) // cursors are expected to be sorted from top to bottom
	{
		for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--)
		{
			if (Coordinates{ currentLine, 0 } == mState.mCursors[c].GetSelectionEnd() && mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
				continue;
			affectedLines.insert(currentLine);
			minLine = minLine == -1 ? currentLine : (currentLine < minLine ? currentLine : minLine);
			maxLine = maxLine == -1 ? currentLine : (currentLine > maxLine ? currentLine : maxLine);
		}
	}
	if (minLine == 0) // can't move up anymore
		return;

	Coordinates start = { minLine - 1, 0 };
	Coordinates end = { maxLine, GetLineMaxColumn(maxLine) };
	u.mOperations.push_back({ GetText(start, end), start, end, UndoOperationType::Delete });

	for (int line : affectedLines) // lines should be sorted here
		std::swap(mLines[line - 1], mLines[line]);
	for (int c = mState.mCurrentCursor; c > -1; c--)
	{
		mState.mCursors[c].mInteractiveStart.mLine -= 1;
		mState.mCursors[c].mInteractiveEnd.mLine -= 1;
		// no need to set mCursorPositionChanged as cursors will remain sorted
	}

	end = { maxLine, GetLineMaxColumn(maxLine) }; // this line is swapped with line above, need to find new max column
	u.mOperations.push_back({ GetText(start, end), start, end, UndoOperationType::Add });
	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::MoveDownCurrentLines()
{
	assert(!mReadOnly);

	UndoRecord u;
	u.mBefore = mState;

	std::set<int> affectedLines;
	int minLine = -1;
	int maxLine = -1;
	for (int c = 0; c <= mState.mCurrentCursor; c++) // cursors are expected to be sorted from top to bottom
	{
		for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--)
		{
			if (Coordinates{ currentLine, 0 } == mState.mCursors[c].GetSelectionEnd() && mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
				continue;
			affectedLines.insert(currentLine);
			minLine = minLine == -1 ? currentLine : (currentLine < minLine ? currentLine : minLine);
			maxLine = maxLine == -1 ? currentLine : (currentLine > maxLine ? currentLine : maxLine);
		}
	}
	if (maxLine == mLines.size() - 1) // can't move down anymore
		return;

	Coordinates start = { minLine, 0 };
	Coordinates end = { maxLine + 1, GetLineMaxColumn(maxLine + 1)};
	u.mOperations.push_back({ GetText(start, end), start, end, UndoOperationType::Delete });

	std::set<int>::reverse_iterator rit;
	for (rit = affectedLines.rbegin(); rit != affectedLines.rend(); rit++) // lines should be sorted here
		std::swap(mLines[*rit + 1], mLines[*rit]);
	for (int c = mState.mCurrentCursor; c > -1; c--)
	{
		mState.mCursors[c].mInteractiveStart.mLine += 1;
		mState.mCursors[c].mInteractiveEnd.mLine += 1;
		// no need to set mCursorPositionChanged as cursors will remain sorted
	}

	end = { maxLine + 1, GetLineMaxColumn(maxLine + 1) }; // this line is swapped with line below, need to find new max column
	u.mOperations.push_back({ GetText(start, end), start, end, UndoOperationType::Add });
	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::ToggleLineComment()
{
	assert(!mReadOnly);
	if (mLanguageDefinition == nullptr)
		return;
	const std::string& commentString = mLanguageDefinition->mSingleLineComment;

	UndoRecord u;
	u.mBefore = mState;

	bool shouldAddComment = false;
	std::unordered_set<int> affectedLines;
	for (int c = mState.mCurrentCursor; c > -1; c--)
	{
		for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--)
		{
			if (Coordinates{ currentLine, 0 } == mState.mCursors[c].GetSelectionEnd() && mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
				continue;
			affectedLines.insert(currentLine);
			int currentIndex = 0;
			while (currentIndex < mLines[currentLine].size() && (mLines[currentLine][currentIndex].mChar == ' ' || mLines[currentLine][currentIndex].mChar == '\t')) currentIndex++;
			if (currentIndex == mLines[currentLine].size())
				continue;
			int i = 0;
			while (i < commentString.length() && currentIndex + i < mLines[currentLine].size() && mLines[currentLine][currentIndex + i].mChar == commentString[i]) i++;
			bool matched = i == commentString.length();
			shouldAddComment |= !matched;
		}
	}

	if (shouldAddComment)
	{
		for (int currentLine : affectedLines) // order doesn't matter as changes are not multiline
		{
			Coordinates lineStart = { currentLine, 0 };
			Coordinates insertionEnd = lineStart;
			InsertTextAt(insertionEnd, (commentString + ' ').c_str()); // sets insertion end
			u.mOperations.push_back({ (commentString + ' ') , lineStart, insertionEnd, UndoOperationType::Add });
			Colorize(lineStart.mLine, 1);
		}
	}
	else
	{
		for (int currentLine : affectedLines) // order doesn't matter as changes are not multiline
		{
			int currentIndex = 0;
			while (currentIndex < mLines[currentLine].size() && (mLines[currentLine][currentIndex].mChar == ' ' || mLines[currentLine][currentIndex].mChar == '\t')) currentIndex++;
			if (currentIndex == mLines[currentLine].size())
				continue;
			int i = 0;
			while (i < commentString.length() && currentIndex + i < mLines[currentLine].size() && mLines[currentLine][currentIndex + i].mChar == commentString[i]) i++;
			bool matched = i == commentString.length();
			assert(matched);
			if (currentIndex + i < mLines[currentLine].size() && mLines[currentLine][currentIndex + i].mChar == ' ')
				i++;

			Coordinates start = { currentLine, GetCharacterColumn(currentLine, currentIndex) };
			Coordinates end = { currentLine, GetCharacterColumn(currentLine, currentIndex + i) };
			u.mOperations.push_back({ GetText(start, end) , start, end, UndoOperationType::Delete});
			DeleteRange(start, end);
			Colorize(currentLine, 1);
		}
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::EnterCharacter(ImWchar aChar, bool aShift)
{
	assert(!mReadOnly);

	bool hasSelection = AnyCursorHasSelection();
	bool anyCursorHasMultilineSelection = false;
	for (int c = mState.mCurrentCursor; c > -1; c--)
		if (mState.mCursors[c].GetSelectionStart().mLine != mState.mCursors[c].GetSelectionEnd().mLine)
		{
			anyCursorHasMultilineSelection = true;
			break;
		}
	bool isIndentOperation = hasSelection && anyCursorHasMultilineSelection && aChar == '\t';
	if (isIndentOperation)
	{
		ChangeCurrentLinesIndentation(!aShift);
		return;
	}

	UndoRecord u;
	u.mBefore = mState;

	if (hasSelection)
	{
		for (int c = mState.mCurrentCursor; c > -1; c--)
		{
			u.mOperations.push_back({ GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete });
			DeleteSelection(c);
		}
	}

	std::vector<Coordinates> coords;
	for (int c = mState.mCurrentCursor; c > -1; c--) // order important here for typing \n in the same line at the same time
	{
		auto coord = GetActualCursorCoordinates(c);
		coords.push_back(coord);
		UndoOperation added;
		added.mType = UndoOperationType::Add;
		added.mStart = coord;

		assert(!mLines.empty());

		if (aChar == '\n')
		{
			InsertLine(coord.mLine + 1);
			auto& line = mLines[coord.mLine];
			auto& newLine = mLines[coord.mLine + 1];

			added.mText = "";
			added.mText += (char)aChar;
			if (mAutoIndent)
				for (int i = 0; i < line.size() && isascii(line[i].mChar) && isblank(line[i].mChar); ++i)
				{
					newLine.push_back(line[i]);
					added.mText += line[i].mChar;
				}

			const size_t whitespaceSize = newLine.size();
			auto cindex = GetCharacterIndexR(coord);
			AddGlyphsToLine(coord.mLine + 1, newLine.size(), line.begin() + cindex, line.end());
			RemoveGlyphsFromLine(coord.mLine, cindex);
			SetCursorPosition(Coordinates(coord.mLine + 1, GetCharacterColumn(coord.mLine + 1, (int)whitespaceSize)), c);
		}
		else
		{
			char buf[7];
			int e = ImTextCharToUtf8(buf, 7, aChar);
			if (e > 0)
			{
				buf[e] = '\0';
				auto& line = mLines[coord.mLine];
				auto cindex = GetCharacterIndexR(coord);

				if (mOverwrite && cindex < (int)line.size())
				{
					auto d = UTF8CharLength(line[cindex].mChar);

					UndoOperation removed;
					removed.mType = UndoOperationType::Delete;
					removed.mStart = mState.mCursors[c].mInteractiveEnd;
					removed.mEnd = Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex + d));

					while (d-- > 0 && cindex < (int)line.size())
					{
						removed.mText += line[cindex].mChar;
						RemoveGlyphsFromLine(coord.mLine, cindex, cindex + 1);
					}
					u.mOperations.push_back(removed);
				}

				for (auto p = buf; *p != '\0'; p++, ++cindex)
					AddGlyphToLine(coord.mLine, cindex, Glyph(*p, PaletteIndex::Default));
				added.mText = buf;

				SetCursorPosition(Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex)), c);
			}
			else
				continue;
		}

		added.mEnd = GetActualCursorCoordinates(c);
		u.mOperations.push_back(added);
	}

	u.mAfter = mState;
	AddUndo(u);

	for (const auto& coord : coords)
		Colorize(coord.mLine - 1, 3);
	EnsureCursorVisible();
}

void TextEditor::SetReadOnlyEnabled(bool aValue)
{
	mReadOnly = aValue;
}

void TextEditor::SetAutoIndentEnabled(bool aValue)
{
	mAutoIndent = aValue;
}

void TextEditor::OnCursorPositionChanged()
{
	if (mState.mDraggingSelection)
		return;

	//std::cout << "Cursor position changed\n";
	mState.SortCursorsFromTopToBottom();
	MergeCursorsIfPossible();
}

void TextEditor::SetColorizerEnable(bool aValue)
{
	mColorizerEnabled = aValue;
}

void TextEditor::SetCursorPosition(const Coordinates& aPosition, int aCursor, bool aClearSelection)
{
	if (aCursor == -1)
		aCursor = mState.mCurrentCursor;

	mState.mCursorPositionChanged = true;
	if (aClearSelection)
		mState.mCursors[aCursor].mInteractiveStart = aPosition;
	if (mState.mCursors[aCursor].mInteractiveEnd != aPosition)
	{
		mState.mCursors[aCursor].mInteractiveEnd = aPosition;
		EnsureCursorVisible();
	}
}

void TextEditor::SetCursorPosition(int aLine, int aCharIndex, int aCursor, bool aClearSelection)
{
	SetCursorPosition({ aLine, GetCharacterColumn(aLine, aCharIndex) }, aCursor);
}

void TextEditor::SetTabSize(int aValue)
{
	mTabSize = std::max(1, std::min(8, aValue));
}

void TextEditor::SetLineSpacing(float aValue)
{
	mLineSpacing = std::max(1.0f, std::min(2.0f, aValue));
}

void TextEditor::InsertText(const std::string& aValue, int aCursor)
{
	InsertText(aValue.c_str(), aCursor);
}

void TextEditor::InsertText(const char* aValue, int aCursor)
{
	if (aValue == nullptr)
		return;
	if (aCursor == -1)
		aCursor = mState.mCurrentCursor;

	auto pos = GetActualCursorCoordinates(aCursor);
	auto start = std::min(pos, mState.mCursors[aCursor].GetSelectionStart());
	int totalLines = pos.mLine - start.mLine;

	totalLines += InsertTextAt(pos, aValue);

	SetCursorPosition(pos, aCursor);
	Colorize(start.mLine - 1, totalLines + 2);
}

void TextEditor::DeleteSelection(int aCursor)
{
	if (aCursor == -1)
		aCursor = mState.mCurrentCursor;

	if (mState.mCursors[aCursor].GetSelectionEnd() == mState.mCursors[aCursor].GetSelectionStart())
		return;

	DeleteRange(mState.mCursors[aCursor].GetSelectionStart(), mState.mCursors[aCursor].GetSelectionEnd());
	SetCursorPosition(mState.mCursors[aCursor].GetSelectionStart(), aCursor);
	Colorize(mState.mCursors[aCursor].GetSelectionStart().mLine, 1);
}

void TextEditor::MoveCoords(Coordinates& aCoords, MoveDirection aDirection, bool aWordMode, int aLineCount) const
{
	int cindex = GetCharacterIndexR(aCoords);
	int lindex = aCoords.mLine;
	auto& line = mLines[lindex];
	switch (aDirection)
	{
	case MoveDirection::Right:
		if (cindex >= line.size())
		{
			if (lindex < mLines.size() - 1)
			{
				aCoords.mLine = std::max(0, std::min((int)mLines.size() - 1, lindex + 1));
				aCoords.mColumn = 0;
			}
		}
		else
		{
			int delta = UTF8CharLength(line[cindex].mChar);
			cindex = std::min(cindex + delta, (int)line.size());
			int oneStepRightColumn = GetCharacterColumn(lindex, cindex);
			if (aWordMode)
			{
				aCoords = FindWordEnd(aCoords);
				aCoords.mColumn = std::max(aCoords.mColumn, oneStepRightColumn);
			}
			else
				aCoords.mColumn = oneStepRightColumn;
		}
		break;
	case MoveDirection::Left:
		if (cindex == 0)
		{
			if (lindex > 0)
			{
				aCoords.mLine = lindex - 1;
				aCoords.mColumn = GetLineMaxColumn(aCoords.mLine);
			}
		}
		else
		{
			--cindex;
			if (cindex > 0)
			{
				if ((int)mLines.size() > lindex)
				{
					while (cindex > 0 && IsUTFSequence(mLines[lindex][cindex].mChar))
						--cindex;
				}
			}
			aCoords.mColumn = GetCharacterColumn(lindex, cindex);
			if (aWordMode)
				aCoords = FindWordStart(aCoords);
		}
		break;
	case MoveDirection::Up:
		aCoords.mLine = std::max(0, lindex - aLineCount);
		break;
	case MoveDirection::Down:
		aCoords.mLine = std::max(0, std::min((int)mLines.size() - 1, lindex + aLineCount));
		break;
	}
}

void TextEditor::MoveUp(int aAmount, bool aSelect)
{
	for (int c = 0; c <= mState.mCurrentCursor; c++)
	{
		Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
		MoveCoords(newCoords, MoveDirection::Up, false, aAmount);
		SetCursorPosition(newCoords, c, !aSelect);
	}
	EnsureCursorVisible();
}

void TextEditor::MoveDown(int aAmount, bool aSelect)
{
	for (int c = 0; c <= mState.mCurrentCursor; c++)
	{
		assert(mState.mCursors[c].mInteractiveEnd.mColumn >= 0);
		Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
		MoveCoords(newCoords, MoveDirection::Down, false, aAmount);
		SetCursorPosition(newCoords, c, !aSelect);
	}
	EnsureCursorVisible();
}

void TextEditor::MoveLeft(bool aSelect, bool aWordMode)
{
	if (mLines.empty())
		return;

	if (AnyCursorHasSelection() && !aSelect && !aWordMode)
	{
		for (int c = 0; c <= mState.mCurrentCursor; c++)
			SetCursorPosition(mState.mCursors[c].GetSelectionStart(), c);
	}
	else
	{
		for (int c = 0; c <= mState.mCurrentCursor; c++)
		{
			Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
			MoveCoords(newCoords, MoveDirection::Left, aWordMode);
			SetCursorPosition(newCoords, c, !aSelect);
		}
	}
	EnsureCursorVisible();
}

void TextEditor::MoveRight(bool aSelect, bool aWordMode)
{
	if (mLines.empty())
		return;

	if (AnyCursorHasSelection() && !aSelect && !aWordMode)
	{
		for (int c = 0; c <= mState.mCurrentCursor; c++)
			SetCursorPosition(mState.mCursors[c].GetSelectionEnd(), c);
	}
	else
	{
		for (int c = 0; c <= mState.mCurrentCursor; c++)
		{
			Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
			MoveCoords(newCoords, MoveDirection::Right, aWordMode);
			SetCursorPosition(newCoords, c, !aSelect);
		}
	}
	EnsureCursorVisible();
}

void TextEditor::MoveTop(bool aSelect)
{
	SetCursorPosition(Coordinates(0, 0), mState.mCurrentCursor, !aSelect);
}

void TextEditor::TextEditor::MoveBottom(bool aSelect)
{
	int maxLine = (int)mLines.size() - 1;
	Coordinates newPos = Coordinates(maxLine, GetLineMaxColumn(maxLine));
	SetCursorPosition(newPos, mState.mCurrentCursor, !aSelect);
}

void TextEditor::MoveHome(bool aSelect)
{
	for (int c = 0; c <= mState.mCurrentCursor; c++)
		SetCursorPosition(Coordinates(mState.mCursors[c].mInteractiveEnd.mLine, 0), c, !aSelect);
}

void TextEditor::MoveEnd(bool aSelect)
{
	for (int c = 0; c <= mState.mCurrentCursor; c++)
	{
		int lindex = mState.mCursors[c].mInteractiveEnd.mLine;
		SetCursorPosition(Coordinates(lindex, GetLineMaxColumn(lindex)), c, !aSelect);
	}
}

void TextEditor::Delete(bool aWordMode, const EditorState* aEditorState)
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	if (AnyCursorHasSelection())
	{
		UndoRecord u;
		u.mBefore = aEditorState == nullptr ? mState : *aEditorState;
		for (int c = mState.mCurrentCursor; c > -1; c--)
		{
			if (!mState.mCursors[c].HasSelection())
				continue;
			u.mOperations.push_back({ GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete });
			DeleteSelection(c);
		}
		u.mAfter = mState;
		AddUndo(u);
	}
	else
	{
		EditorState stateBeforeDeleting = mState;
		MoveRight(true, aWordMode);
		if (!AllCursorsHaveSelection()) // can't do delete if any cursor at end of last line
		{
			if (AnyCursorHasSelection())
				MoveLeft();
			return;
		}

		OnCursorPositionChanged(); // might combine cursors
		Delete(aWordMode, &stateBeforeDeleting);
	}
}

void TextEditor::Backspace(bool aWordMode)
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	if (AnyCursorHasSelection())
		Delete(aWordMode);
	else
	{
		EditorState stateBeforeDeleting = mState;
		MoveLeft(true, aWordMode);
		if (!AllCursorsHaveSelection()) // can't do backspace if any cursor at {0,0}
		{
			if (AnyCursorHasSelection())
				MoveRight();
			return;
		}
			
		OnCursorPositionChanged(); // might combine cursors
		Delete(aWordMode, &stateBeforeDeleting);
	}
}

void TextEditor::SetSelection(int aStartLine, int aStartChar, int aEndLine, int aEndChar, int aCursor)
{
	Coordinates startCoords = { aStartLine, GetCharacterColumn(aStartLine, aStartChar) };
	Coordinates endCoords = { aEndLine, GetCharacterColumn(aEndLine, aEndChar) };
	SetSelection(startCoords, endCoords, aCursor);
}

void TextEditor::SetSelection(Coordinates aStart, Coordinates aEnd, int aCursor)
{
	if (aCursor == -1)
		aCursor = mState.mCurrentCursor;

	Coordinates minCoords = Coordinates(0, 0);
	int maxLine = (int)mLines.size() - 1;
	Coordinates maxCoords = Coordinates(maxLine, GetLineMaxColumn(maxLine));
	if (aStart < minCoords)
		aStart = minCoords;
	else if (aStart > maxCoords)
		aStart = maxCoords;
	if (aEnd < minCoords)
		aEnd = minCoords;
	else if (aEnd > maxCoords)
		aEnd = maxCoords;

	mState.mCursors[aCursor].mInteractiveStart = aStart;
	SetCursorPosition(aEnd, aCursor, false);
}

void TextEditor::SelectAll()
{
	mState.mCurrentCursor = 0;
	MoveTop();
	MoveBottom(true);
}

bool TextEditor::AnyCursorHasSelection() const
{
	for (int c = 0; c <= mState.mCurrentCursor; c++)
		if (mState.mCursors[c].HasSelection())
			return true;
	return false;
}

bool TextEditor::AllCursorsHaveSelection() const
{
	for (int c = 0; c <= mState.mCurrentCursor; c++)
		if (!mState.mCursors[c].HasSelection())
			return false;
	return true;
}

void TextEditor::Copy()
{
	if (AnyCursorHasSelection())
	{
		std::string clipboardText = GetClipboardText();
		ImGui::SetClipboardText(clipboardText.c_str());
	}
	else
	{
		if (!mLines.empty())
		{
			std::string str;
			auto& line = mLines[GetActualCursorCoordinates().mLine];
			for (auto& g : line)
				str.push_back(g.mChar);
			ImGui::SetClipboardText(str.c_str());
		}
	}
}

void TextEditor::Cut()
{
	if (mReadOnly)
	{
		Copy();
	}
	else
	{
		if (AnyCursorHasSelection())
		{
			UndoRecord u;
			u.mBefore = mState;

			Copy();
			for (int c = mState.mCurrentCursor; c > -1; c--)
			{
				u.mOperations.push_back({ GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete });
				DeleteSelection(c);
			}

			u.mAfter = mState;
			AddUndo(u);
		}
	}
}

void TextEditor::Paste()
{
	if (mReadOnly)
		return;

	// check if we should do multicursor paste
	std::string clipText = ImGui::GetClipboardText();
	bool canPasteToMultipleCursors = false;
	std::vector<std::pair<int, int>> clipTextLines;
	if (mState.mCurrentCursor > 0)
	{
		clipTextLines.push_back({ 0,0 });
		for (int i = 0; i < clipText.length(); i++)
		{
			if (clipText[i] == '\n')
			{
				clipTextLines.back().second = i;
				clipTextLines.push_back({ i + 1, 0 });
			}
		}
		clipTextLines.back().second = clipText.length();
		canPasteToMultipleCursors = clipTextLines.size() == mState.mCurrentCursor + 1;
	}

	if (clipText.length() > 0)
	{
		UndoRecord u;
		u.mBefore = mState;

		if (AnyCursorHasSelection())
		{
			for (int c = mState.mCurrentCursor; c > -1; c--)
			{
				u.mOperations.push_back({ GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete });
				DeleteSelection(c);
			}
		}

		for (int c = mState.mCurrentCursor; c > -1; c--)
		{
			Coordinates start = GetActualCursorCoordinates(c);
			if (canPasteToMultipleCursors)
			{
				std::string clipSubText = clipText.substr(clipTextLines[c].first, clipTextLines[c].second - clipTextLines[c].first);
				InsertText(clipSubText, c);
				u.mOperations.push_back({ clipSubText, start, GetActualCursorCoordinates(c), UndoOperationType::Add });
			}
			else
			{
				InsertText(clipText, c);
				u.mOperations.push_back({ clipText, start, GetActualCursorCoordinates(c), UndoOperationType::Add });
			}
		}

		u.mAfter = mState;
		AddUndo(u);
	}
}

int TextEditor::GetUndoIndex() const
{
	return mUndoIndex;
}

bool TextEditor::CanUndo() const
{
	return !mReadOnly && mUndoIndex > 0;
}

bool TextEditor::CanRedo() const
{
	return !mReadOnly && mUndoIndex < (int)mUndoBuffer.size();
}

void TextEditor::Undo(int aSteps)
{
	while (CanUndo() && aSteps-- > 0)
		mUndoBuffer[--mUndoIndex].Undo(this);
}

void TextEditor::Redo(int aSteps)
{
	while (CanRedo() && aSteps-- > 0)
		mUndoBuffer[mUndoIndex++].Redo(this);
}

void TextEditor::ClearExtraCursors()
{
	mState.mCurrentCursor = 0;
}

void TextEditor::ClearSelections()
{
	for (int c = mState.mCurrentCursor; c > -1; c--)
		mState.mCursors[c].mInteractiveEnd =
		mState.mCursors[c].mInteractiveStart =
		mState.mCursors[c].GetSelectionEnd();
}

void TextEditor::SelectNextOccurrenceOf(const char* aText, int aTextSize, int aCursor)
{
	if (aCursor == -1)
		aCursor = mState.mCurrentCursor;
	Coordinates nextStart, nextEnd;
	FindNextOccurrence(aText, aTextSize, mState.mCursors[aCursor].mInteractiveEnd, nextStart, nextEnd);
	SetSelection(nextStart, nextEnd, aCursor);
	EnsureCursorVisible(aCursor);
}

void TextEditor::AddCursorForNextOccurrence()
{
	const Cursor& currentCursor = mState.mCursors[mState.GetLastAddedCursorIndex()];
	if (currentCursor.GetSelectionStart() == currentCursor.GetSelectionEnd())
		return;

	std::string selectionText = GetText(currentCursor.GetSelectionStart(), currentCursor.GetSelectionEnd());
	Coordinates nextStart, nextEnd;
	if (!FindNextOccurrence(selectionText.c_str(), selectionText.length(), currentCursor.GetSelectionEnd(), nextStart, nextEnd))
		return;

	mState.AddCursor();
	SetSelection(nextStart, nextEnd, mState.mCurrentCursor);
	mState.SortCursorsFromTopToBottom();
	MergeCursorsIfPossible();
	EnsureCursorVisible();
}

const TextEditor::Palette& TextEditor::GetDarkPalette()
{
	const static Palette p = { {
			0xdcdfe4ff,	// Default
			0xe06c75ff,	// Keyword
			0xe5c07bff,	// Number
			0x98c379ff,	// String
			0xe0a070ff, // Char literal
			0x6a7384ff, // Punctuation
			0x808040ff,	// Preprocessor
			0xdcdfe4ff, // Identifier
			0x61afefff, // Known identifier
			0xc678ddff, // Preproc identifier
			0x3696a2ff, // Comment (single line)
			0x3696a2ff, // Comment (multi line)
			0x282c34ff, // Background
			0xe0e0e0ff, // Cursor
			0x2060a080, // Selection
			0xff200080, // ErrorMarker
			0xffffff15, // ControlCharacter
			0x0080f040, // Breakpoint
			0x7a8394ff, // Line number
			0x00000040, // Current line fill
			0x80808040, // Current line fill (inactive)
			0xa0a0a040, // Current line edge
		} };
	return p;
}

const TextEditor::Palette& TextEditor::GetMarianaPalette()
{
	const static Palette p = { {
			0xffffffff,	// Default
			0xc695c6ff,	// Keyword
			0xf9ae58ff,	// Number
			0x99c794ff,	// String
			0xe0a070ff, // Char literal
			0x5fb4b4ff, // Punctuation
			0x808040ff,	// Preprocessor
			0xffffffff, // Identifier
			0x4dc69bff, // Known identifier
			0xe0a0ffff, // Preproc identifier
			0xa6acb9ff, // Comment (single line)
			0xa6acb9ff, // Comment (multi line)
			0x303841ff, // Background
			0xe0e0e0ff, // Cursor
			0x6e7a8580, // Selection
			0xec5f6680, // ErrorMarker
			0xffffff30, // ControlCharacter
			0x0080f040, // Breakpoint
			0xffffffb0, // Line number
			0x4e5a6580, // Current line fill
			0x4e5a6530, // Current line fill (inactive)
			0x4e5a65b0, // Current line edge
		} };
	return p;
}

const TextEditor::Palette& TextEditor::GetLightPalette()
{
	const static Palette p = { {
			0x404040ff,	// None
			0x060cffff,	// Keyword	
			0x008000ff,	// Number
			0xa02020ff,	// String
			0x704030ff, // Char literal
			0x000000ff, // Punctuation
			0x606040ff,	// Preprocessor
			0x404040ff, // Identifier
			0x106060ff, // Known identifier
			0xa040c0ff, // Preproc identifier
			0x205020ff, // Comment (single line)
			0x205040ff, // Comment (multi line)
			0xffffffff, // Background
			0x000000ff, // Cursor
			0x00006040, // Selection
			0xff1000a0, // ErrorMarker
			0x90909090, // ControlCharacter
			0x0080f080, // Breakpoint
			0x005050ff, // Line number
			0x00000040, // Current line fill
			0x80808040, // Current line fill (inactive)
			0x00000040, // Current line edge
		} };
	return p;
}

const TextEditor::Palette& TextEditor::GetRetroBluePalette()
{
	const static Palette p = { {
			0xffff00ff,	// None
			0x00ffffff,	// Keyword	
			0x00ff00ff,	// Number
			0x008080ff,	// String
			0x008080ff, // Char literal
			0xffffffff, // Punctuation
			0x008000ff,	// Preprocessor
			0xffff00ff, // Identifier
			0xffffffff, // Known identifier
			0xff00ffff, // Preproc identifier
			0x808080ff, // Comment (single line)
			0x404040ff, // Comment (multi line)
			0x000080ff, // Background
			0xff8000ff, // Cursor
			0x00ffff80, // Selection
			0xff0000a0, // ErrorMarker
			0x0080ff80, // Breakpoint
			0x008080ff, // Line number
			0x00000040, // Current line fill
			0x80808040, // Current line fill (inactive)
			0x00000040, // Current line edge
		} };
	return p;
}

void TextEditor::MergeCursorsIfPossible()
{
	// requires the cursors to be sorted from top to bottom
	std::unordered_set<int> cursorsToDelete;
	if (AnyCursorHasSelection())
	{
		// merge cursors if they overlap
		for (int c = mState.mCurrentCursor; c > 0; c--)// iterate backwards through pairs
		{
			int pc = c - 1; // pc for previous cursor

			bool pcContainsC = mState.mCursors[pc].GetSelectionEnd() >= mState.mCursors[c].GetSelectionEnd();
			bool pcContainsStartOfC = mState.mCursors[pc].GetSelectionEnd() > mState.mCursors[c].GetSelectionStart();

			if (pcContainsC)
			{
				cursorsToDelete.insert(c);
			}
			else if (pcContainsStartOfC)
			{
				Coordinates pcStart = mState.mCursors[pc].GetSelectionStart();
				Coordinates cEnd = mState.mCursors[c].GetSelectionEnd();
				mState.mCursors[pc].mInteractiveEnd = cEnd;
				mState.mCursors[pc].mInteractiveStart = pcStart;
				cursorsToDelete.insert(c);
			}
		}
	}
	else
	{
		// merge cursors if they are at the same position
		for (int c = mState.mCurrentCursor; c > 0; c--)// iterate backwards through pairs
		{
			int pc = c - 1;
			if (mState.mCursors[pc].mInteractiveEnd == mState.mCursors[c].mInteractiveEnd)
				cursorsToDelete.insert(c);
		}
	}
	for (int c = mState.mCurrentCursor; c > -1; c--)// iterate backwards through each of them
	{
		if (cursorsToDelete.find(c) != cursorsToDelete.end())
			mState.mCursors.erase(mState.mCursors.begin() + c);
	}
	mState.mCurrentCursor -= cursorsToDelete.size();
}


std::string TextEditor::GetText() const
{
	auto lastLine = (int)mLines.size() - 1;
	auto lastLineLength = GetLineMaxColumn(lastLine);
	return GetText(Coordinates(), Coordinates(lastLine, lastLineLength));
}

std::vector<std::string> TextEditor::GetTextLines() const
{
	std::vector<std::string> result;

	result.reserve(mLines.size());

	for (auto& line : mLines)
	{
		std::string text;

		text.resize(line.size());

		for (size_t i = 0; i < line.size(); ++i)
			text[i] = line[i].mChar;

		result.emplace_back(std::move(text));
	}

	return result;
}

std::string TextEditor::GetClipboardText() const
{
	std::string result;
	for (int c = 0; c <= mState.mCurrentCursor; c++)
	{
		if (mState.mCursors[c].GetSelectionStart() < mState.mCursors[c].GetSelectionEnd())
		{
			if (result.length() != 0)
				result += '\n';
			result += GetText(mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd());
		}
	}
	return result;
}

std::string TextEditor::GetSelectedText(int aCursor) const
{
	if (aCursor == -1)
		aCursor = mState.mCurrentCursor;

	return GetText(mState.mCursors[aCursor].GetSelectionStart(), mState.mCursors[aCursor].GetSelectionEnd());
}

std::string TextEditor::GetCurrentLineText()const
{
	auto lineLength = GetLineMaxColumn(mState.mCursors[mState.mCurrentCursor].mInteractiveEnd.mLine);
	return GetText(
		Coordinates(mState.mCursors[mState.mCurrentCursor].mInteractiveEnd.mLine, 0),
		Coordinates(mState.mCursors[mState.mCurrentCursor].mInteractiveEnd.mLine, lineLength));
}

void TextEditor::ProcessInputs()
{
}

void TextEditor::Colorize(int aFromLine, int aLines)
{
	int toLine = aLines == -1 ? (int)mLines.size() : std::min((int)mLines.size(), aFromLine + aLines);
	mColorRangeMin = std::min(mColorRangeMin, aFromLine);
	mColorRangeMax = std::max(mColorRangeMax, toLine);
	mColorRangeMin = std::max(0, mColorRangeMin);
	mColorRangeMax = std::max(mColorRangeMin, mColorRangeMax);
	mCheckComments = true;
}

void TextEditor::ColorizeRange(int aFromLine, int aToLine)
{
	if (mLines.empty() || aFromLine >= aToLine || mLanguageDefinition == nullptr)
		return;

	std::string buffer;
	boost::cmatch results;
	std::string id;

	int endLine = std::max(0, std::min((int)mLines.size(), aToLine));
	for (int i = aFromLine; i < endLine; ++i)
	{
		auto& line = mLines[i];

		if (line.empty())
			continue;

		buffer.resize(line.size());
		for (size_t j = 0; j < line.size(); ++j)
		{
			auto& col = line[j];
			buffer[j] = col.mChar;
			col.mColorIndex = PaletteIndex::Default;
		}

		const char* bufferBegin = &buffer.front();
		const char* bufferEnd = bufferBegin + buffer.size();

		auto last = bufferEnd;

		for (auto first = bufferBegin; first != last; )
		{
			const char* token_begin = nullptr;
			const char* token_end = nullptr;
			PaletteIndex token_color = PaletteIndex::Default;

			bool hasTokenizeResult = false;

			if (mLanguageDefinition->mTokenize != nullptr)
			{
				if (mLanguageDefinition->mTokenize(first, last, token_begin, token_end, token_color))
					hasTokenizeResult = true;
			}

			if (hasTokenizeResult == false)
			{
				// todo : remove
				//printf("using regex for %.*s\n", first + 10 < last ? 10 : int(last - first), first);

				for (const auto& p : mRegexList)
				{
					if (boost::regex_search(first, last, results, p.first, boost::regex_constants::match_continuous))
					{
						hasTokenizeResult = true;

						auto& v = *results.begin();
						token_begin = v.first;
						token_end = v.second;
						token_color = p.second;
						break;
					}
				}
			}

			if (hasTokenizeResult == false)
			{
				first++;
			}
			else
			{
				const size_t token_length = token_end - token_begin;

				if (token_color == PaletteIndex::Identifier)
				{
					id.assign(token_begin, token_end);

					// todo : allmost all language definitions use lower case to specify keywords, so shouldn't this use ::tolower ?
					if (!mLanguageDefinition->mCaseSensitive)
						std::transform(id.begin(), id.end(), id.begin(), ::toupper);

					if (!line[first - bufferBegin].mPreprocessor)
					{
						if (mLanguageDefinition->mKeywords.count(id) != 0)
							token_color = PaletteIndex::Keyword;
						else if (mLanguageDefinition->mIdentifiers.count(id) != 0)
							token_color = PaletteIndex::KnownIdentifier;
						else if (mLanguageDefinition->mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
					else
					{
						if (mLanguageDefinition->mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
				}

				for (size_t j = 0; j < token_length; ++j)
					line[(token_begin - bufferBegin) + j].mColorIndex = token_color;

				first = token_end;
			}
		}
	}
}

void TextEditor::ColorizeInternal()
{
	if (mLines.empty() || !mColorizerEnabled || mLanguageDefinition == nullptr)
		return;

	if (mCheckComments)
	{
		auto endLine = mLines.size();
		auto endIndex = 0;
		auto commentStartLine = endLine;
		auto commentStartIndex = endIndex;
		auto withinString = false;
		auto withinSingleLineComment = false;
		auto withinPreproc = false;
		auto firstChar = true;			// there is no other non-whitespace characters in the line before
		auto concatenate = false;		// '\' on the very end of the line
		auto currentLine = 0;
		auto currentIndex = 0;
		while (currentLine < endLine || currentIndex < endIndex)
		{
			auto& line = mLines[currentLine];

			if (currentIndex == 0 && !concatenate)
			{
				withinSingleLineComment = false;
				withinPreproc = false;
				firstChar = true;
			}

			concatenate = false;

			if (!line.empty())
			{
				auto& g = line[currentIndex];
				auto c = g.mChar;

				if (c != mLanguageDefinition->mPreprocChar && !isspace(c))
					firstChar = false;

				if (currentIndex == (int)line.size() - 1 && line[line.size() - 1].mChar == '\\')
					concatenate = true;

				bool inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

				if (withinString)
				{
					line[currentIndex].mMultiLineComment = inComment;

					if (c == '\"')
					{
						if (currentIndex + 1 < (int)line.size() && line[currentIndex + 1].mChar == '\"')
						{
							currentIndex += 1;
							if (currentIndex < (int)line.size())
								line[currentIndex].mMultiLineComment = inComment;
						}
						else
							withinString = false;
					}
					else if (c == '\\')
					{
						currentIndex += 1;
						if (currentIndex < (int)line.size())
							line[currentIndex].mMultiLineComment = inComment;
					}
				}
				else
				{
					if (firstChar && c == mLanguageDefinition->mPreprocChar)
						withinPreproc = true;

					if (c == '\"')
					{
						withinString = true;
						line[currentIndex].mMultiLineComment = inComment;
					}
					else
					{
						auto pred = [](const char& a, const Glyph& b) { return a == b.mChar; };
						auto from = line.begin() + currentIndex;
						auto& startStr = mLanguageDefinition->mCommentStart;
						auto& singleStartStr = mLanguageDefinition->mSingleLineComment;

						if (!withinSingleLineComment && currentIndex + startStr.size() <= line.size() &&
							equals(startStr.begin(), startStr.end(), from, from + startStr.size(), pred))
						{
							commentStartLine = currentLine;
							commentStartIndex = currentIndex;
						}
						else if (singleStartStr.size() > 0 &&
							currentIndex + singleStartStr.size() <= line.size() &&
							equals(singleStartStr.begin(), singleStartStr.end(), from, from + singleStartStr.size(), pred))
						{
							withinSingleLineComment = true;
						}

						inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

						line[currentIndex].mMultiLineComment = inComment;
						line[currentIndex].mComment = withinSingleLineComment;

						auto& endStr = mLanguageDefinition->mCommentEnd;
						if (currentIndex + 1 >= (int)endStr.size() &&
							equals(endStr.begin(), endStr.end(), from + 1 - endStr.size(), from + 1, pred))
						{
							commentStartIndex = endIndex;
							commentStartLine = endLine;
						}
					}
				}
				if (currentIndex < (int)line.size())
					line[currentIndex].mPreprocessor = withinPreproc;
				currentIndex += UTF8CharLength(c);
				if (currentIndex >= (int)line.size())
				{
					currentIndex = 0;
					++currentLine;
				}
			}
			else
			{
				currentIndex = 0;
				++currentLine;
			}
		}
		mCheckComments = false;
	}

	if (mColorRangeMin < mColorRangeMax)
	{
		const int increment = (mLanguageDefinition->mTokenize == nullptr) ? 10 : 10000;
		const int to = std::min(mColorRangeMin + increment, mColorRangeMax);
		ColorizeRange(mColorRangeMin, to);
		mColorRangeMin = to;

		if (mColorRangeMax == mColorRangeMin)
		{
			mColorRangeMin = std::numeric_limits<int>::max();
			mColorRangeMax = 0;
		}
		return;
	}
}

float TextEditor::TextDistanceToLineStart(const Coordinates& aFrom) const
{
	auto& line = mLines[aFrom.mLine];
	float distance = 0.0f;
	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;
	int colIndex = GetCharacterIndexR(aFrom);
	for (size_t it = 0u; it < line.size() && it < colIndex; )
	{
		if (line[it].mChar == '\t')
		{
			distance = (1.0f + std::floor((1.0f + distance) / (float(mTabSize) * spaceSize))) * (float(mTabSize) * spaceSize);
			++it;
		}
		else
		{
			auto d = UTF8CharLength(line[it].mChar);
			char tempCString[7];
			int i = 0;
			for (; i < 6 && d-- > 0 && it < (int)line.size(); i++, it++)
				tempCString[i] = line[it].mChar;

			tempCString[i] = '\0';
			distance += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, tempCString, nullptr, nullptr).x;
		}
	}

	return distance;
}

void TextEditor::EnsureCursorVisible(int aCursor)
{
	if (aCursor == -1)
		aCursor = mState.GetLastAddedCursorIndex();

	if (!mWithinRender)
	{
		mScrollToCursor = true;
		return;
	}

	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	auto height = ImGui::GetWindowHeight();
	auto width = ImGui::GetWindowWidth();

	auto top = 1 + (int)ceil(scrollY / mCharAdvance.y);
	auto bottom = (int)ceil((scrollY + height) / mCharAdvance.y);

	auto left = (int)ceil(scrollX / mCharAdvance.x);
	auto right = (int)ceil((scrollX + width) / mCharAdvance.x);

	auto pos = GetActualCursorCoordinates(aCursor);
	auto len = TextDistanceToLineStart(pos);

	if (pos.mLine < top)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine - 1) * mCharAdvance.y));
	if (pos.mLine > bottom - 4)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine + 4) * mCharAdvance.y - height));
	if (len + mTextStart < left + 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart - 4));
	if (len + mTextStart > right - 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart + 4 - width));
}

int TextEditor::GetPageSize() const
{
	auto height = ImGui::GetWindowHeight() - 20.0f;
	return (int)floor(height / mCharAdvance.y);
}

TextEditor::UndoRecord::UndoRecord(
	const std::vector<UndoOperation>& aOperations,
	TextEditor::EditorState& aBefore,
	TextEditor::EditorState& aAfter)
	: mOperations(aOperations)
	, mBefore(aBefore)
	, mAfter(aAfter)
{
	for (const UndoOperation& o : mOperations)
		assert(o.mStart <= o.mEnd);
}

void TextEditor::UndoRecord::Undo(TextEditor* aEditor)
{
	for (int i = mOperations.size() - 1; i > -1; i--)
	{
		const UndoOperation& operation = mOperations[i];
		if (!operation.mText.empty())
		{
			switch (operation.mType)
			{
			case UndoOperationType::Delete:
			{
				auto start = operation.mStart;
				aEditor->InsertTextAt(start, operation.mText.c_str());
				aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 2);
				break;
			}
			case UndoOperationType::Add:
			{
				aEditor->DeleteRange(operation.mStart, operation.mEnd);
				aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 2);
				break;
			}
			}
		}
	}

	aEditor->mState = mBefore;
	aEditor->EnsureCursorVisible();
}

void TextEditor::UndoRecord::Redo(TextEditor* aEditor)
{
	for (int i = 0; i < mOperations.size(); i++)
	{
		const UndoOperation& operation = mOperations[i];
		if (!operation.mText.empty())
		{
			switch (operation.mType)
			{
			case UndoOperationType::Delete:
			{
				aEditor->DeleteRange(operation.mStart, operation.mEnd);
				aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 1);
				break;
			}
			case UndoOperationType::Add:
			{
				auto start = operation.mStart;
				aEditor->InsertTextAt(start, operation.mText.c_str());
				aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 1);
				break;
			}
			}
		}
	}

	aEditor->mState = mAfter;
	aEditor->EnsureCursorVisible();
}