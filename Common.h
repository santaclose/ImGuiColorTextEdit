#pragma once

#include <assert.h>
#include <vector>
#include <string>


namespace Common {

	enum class PaletteIndex
	{
		Default,
		Keyword,
		Number,
		String,
		CharLiteral,
		Punctuation,
		Preprocessor,
		Identifier,
		KnownIdentifier,
		PreprocIdentifier,
		Comment,
		MultiLineComment,
		Background,
		Cursor,
		Selection,
		ErrorMarker,
		ControlCharacter,
		Breakpoint,
		LineNumber,
		CurrentLineFill,
		CurrentLineFillInactive,
		CurrentLineEdge,
		Max
	};
	typedef std::array<unsigned int, (unsigned)PaletteIndex::Max> Palette;

	struct Glyph
	{
		char mChar;
		PaletteIndex mColorIndex = PaletteIndex::Default;

		Glyph(char aChar, PaletteIndex aColorIndex) : mChar(aChar), mColorIndex(aColorIndex) {}
	};

	typedef std::vector<Glyph> Line;

	struct LanguageDefinition
	{
		typedef void(*OnLoad)(std::vector<Line>& aTextEditorLines);
		typedef void(*OnTextChanged)(std::vector<Line>& aTextEditorLines, int aLineStart, int aLineEnd);
		typedef void(*OnTextAdded)(std::vector<Line>& aTextEditorLines, int aLineStart, int aLineEnd);
		typedef void(*OnTextDeleted)(std::vector<Line>& aTextEditorLines, int aLineStart, int aLineEnd);
		typedef char* (*GetLineCommentString)();

		OnLoad mOnLoad = nullptr;
		OnTextChanged mOnTextChanged = nullptr;
		OnTextAdded mOnTextAdded = nullptr;
		OnTextDeleted mOnTextDeleted = nullptr;
		GetLineCommentString mGetLineCommentString = nullptr;
	};

	// Represents a character coordinate from the user's point of view,
	// i. e. consider an uniform grid (assuming fixed-width font) on the
	// screen as it is rendered, and each cell has its own coordinate, starting from 0.
	// Tabs are counted as [1..mTabSize] count empty spaces, depending on
	// how many space is necessary to reach the next tab stop.
	// For example, coordinate (1, 5) represents the character 'B' in a line "\tABC", when mTabSize = 4,
	// because it is rendered as "    ABC" on the screen.
	struct Coordinates
	{
		int mLine, mColumn;
		Coordinates() : mLine(0), mColumn(0) {}
		Coordinates(int aLine, int aColumn) : mLine(aLine), mColumn(aColumn)
		{
			assert(aLine >= 0);
			assert(aColumn >= 0);
		}
		static Coordinates Invalid() { static Coordinates invalid(-1, -1); return invalid; }

		bool operator ==(const Coordinates& o) const
		{
			return
				mLine == o.mLine &&
				mColumn == o.mColumn;
		}

		bool operator !=(const Coordinates& o) const
		{
			return
				mLine != o.mLine ||
				mColumn != o.mColumn;
		}

		bool operator <(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn < o.mColumn;
		}

		bool operator >(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn > o.mColumn;
		}

		bool operator <=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine < o.mLine;
			return mColumn <= o.mColumn;
		}

		bool operator >=(const Coordinates& o) const
		{
			if (mLine != o.mLine)
				return mLine > o.mLine;
			return mColumn >= o.mColumn;
		}

		Coordinates operator -(const Coordinates& o)
		{
			return Coordinates(mLine - o.mLine, mColumn - o.mColumn);
		}

		Coordinates operator +(const Coordinates& o)
		{
			return Coordinates(mLine + o.mLine, mColumn + o.mColumn);
		}
	};

	enum class UndoOperationType { Add, Delete };
	struct UndoOperation
	{
		std::string mText;
		Coordinates mStart;
		Coordinates mEnd;
		UndoOperationType mType;
	};
}