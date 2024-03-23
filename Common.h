#pragma once

#include <assert.h>

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
	typedef std::array<ImU32, (unsigned)PaletteIndex::Max> Palette;

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
}