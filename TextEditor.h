#pragma once

#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <boost/regex.hpp>
#include "imgui.h"

class IMGUI_API TextEditor
{
public:
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

	struct Cursor
	{
		Coordinates mInteractiveStart = { 0, 0 };
		Coordinates mInteractiveEnd = { 0, 0 };
		inline Coordinates GetSelectionStart() const { return mInteractiveStart < mInteractiveEnd ? mInteractiveStart : mInteractiveEnd; }
		inline Coordinates GetSelectionEnd() const { return mInteractiveStart > mInteractiveEnd ? mInteractiveStart : mInteractiveEnd; }
		inline bool HasSelection() const { return mInteractiveStart != mInteractiveEnd; }
	};

	struct EditorState
	{
		bool mPanning = false;
		bool mDraggingSelection = false;
		ImVec2 mLastMousePos;
		int mCurrentCursor = 0;
		int mLastAddedCursor = 0;
		bool mCursorPositionChanged = false;
		std::vector<Cursor> mCursors = { {{0,0}} };
		void AddCursor()
		{
			// vector is never resized to smaller size, mCurrentCursor points to last available cursor in vector
			mCurrentCursor++;
			mCursors.resize(mCurrentCursor + 1);
			mLastAddedCursor = mCurrentCursor;
		}
		int GetLastAddedCursorIndex()
		{
			return mLastAddedCursor > mCurrentCursor ? 0 : mLastAddedCursor;
		}
		void SortCursorsFromTopToBottom()
		{
			Coordinates lastAddedCursorPos = mCursors[GetLastAddedCursorIndex()].mInteractiveEnd;
			std::sort(mCursors.begin(), mCursors.begin() + (mCurrentCursor + 1), [](const Cursor& a, const Cursor& b) -> bool
				{
					return a.GetSelectionStart() < b.GetSelectionStart();
				});
			// update last added cursor index to be valid after sort
			for (int c = mCurrentCursor; c > -1; c--)
				if (mCursors[c].mInteractiveEnd == lastAddedCursorPos)
					mLastAddedCursor = c;
		}
	};

	struct Identifier
	{
		Coordinates mLocation;
		std::string mDeclaration;
	};

	typedef std::string String;
	typedef std::unordered_map<std::string, Identifier> Identifiers;
	typedef std::unordered_set<std::string> Keywords;
	typedef std::array<ImU32, (unsigned)PaletteIndex::Max> Palette;
	typedef uint8_t Char;

	struct Glyph
	{
		Char mChar;
		PaletteIndex mColorIndex = PaletteIndex::Default;
		bool mComment : 1;
		bool mMultiLineComment : 1;
		bool mPreprocessor : 1;

		Glyph(Char aChar, PaletteIndex aColorIndex) : mChar(aChar), mColorIndex(aColorIndex),
			mComment(false), mMultiLineComment(false), mPreprocessor(false) {}
	};

	typedef std::vector<Glyph> Line;
	typedef std::vector<Line> Lines;

	struct LanguageDefinition
	{
		typedef std::pair<std::string, PaletteIndex> TokenRegexString;
		typedef std::vector<TokenRegexString> TokenRegexStrings;
		typedef bool(*TokenizeCallback)(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, PaletteIndex& paletteIndex);

		std::string mName;
		Keywords mKeywords;
		Identifiers mIdentifiers;
		Identifiers mPreprocIdentifiers;
		std::string mCommentStart, mCommentEnd, mSingleLineComment;
		char mPreprocChar;

		TokenizeCallback mTokenize;

		TokenRegexStrings mTokenRegexStrings;

		bool mCaseSensitive;

		LanguageDefinition()
			: mPreprocChar('#'), mTokenize(nullptr), mCaseSensitive(true)
		{
		}

		static const LanguageDefinition& CPlusPlus();
		static const LanguageDefinition& HLSL();
		static const LanguageDefinition& GLSL();
		static const LanguageDefinition& Python();
		static const LanguageDefinition& C();
		static const LanguageDefinition& SQL();
		static const LanguageDefinition& AngelScript();
		static const LanguageDefinition& Lua();
		static const LanguageDefinition& CSharp();
		static const LanguageDefinition& Json();
	};

	enum class UndoOperationType { Add, Delete };
	struct UndoOperation
	{
		std::string mText;
		TextEditor::Coordinates mStart;
		TextEditor::Coordinates mEnd;
		UndoOperationType mType;
	};

	TextEditor();
	~TextEditor();

	void SetLanguageDefinition(const LanguageDefinition& aLanguageDef);
	const char* GetLanguageDefinitionName() const;

	const Palette& GetPalette() const { return mPaletteBase; }
	void SetPalette(const Palette& aValue);

	bool Render(const char* aTitle, bool aParentIsFocused = false, const ImVec2& aSize = ImVec2(), bool aBorder = false);
	void SetText(const std::string& aText);
	std::string GetText() const;

	void SetTextLines(const std::vector<std::string>& aLines);
	std::vector<std::string> GetTextLines() const;

	std::string GetClipboardText() const;
	std::string GetSelectedText(int aCursor = -1) const;
	std::string GetCurrentLineText()const;

	int GetTotalLines() const { return (int)mLines.size(); }
	bool IsOverwrite() const { return mOverwrite; }

	// Interface functions
	void SetReadOnlyEnabled(bool aValue);
	bool IsReadOnlyEnabled() const { return mReadOnly; }
	void SetAutoIndentEnabled(bool aValue);
	bool IsAutoIndentEnabled() const { return mAutoIndent; }

	void OnCursorPositionChanged();

	bool IsColorizerEnabled() const { return mColorizerEnabled; }
	void SetColorizerEnable(bool aValue);

	Coordinates GetCursorPosition() const { return GetActualCursorCoordinates(); }
	void SetCursorPosition(const Coordinates& aPosition, int aCursor = -1, bool aClearSelection = true);
	void SetCursorPosition(int aLine, int aCharIndex, int aCursor = -1, bool aClearSelection = true);

	inline void OnLineDeleted(int aLineIndex, const std::unordered_set<int>* aHandledCursors = nullptr)
	{
		for (int c = 0; c <= mState.mCurrentCursor; c++)
		{
			if (mState.mCursors[c].mInteractiveEnd.mLine >= aLineIndex)
			{
				if (aHandledCursors == nullptr || aHandledCursors->find(c) == aHandledCursors->end()) // move up if has not been handled already
					SetCursorPosition({ mState.mCursors[c].mInteractiveEnd.mLine - 1, mState.mCursors[c].mInteractiveEnd.mColumn }, c);
			}
		}
	}
	inline void OnLinesDeleted(int aFirstLineIndex, int aLastLineIndex)
	{
		for (int c = 0; c <= mState.mCurrentCursor; c++)
		{
			if (mState.mCursors[c].mInteractiveEnd.mLine >= aFirstLineIndex)
			{
				int targetLine = mState.mCursors[c].mInteractiveEnd.mLine - (aLastLineIndex - aFirstLineIndex);
				targetLine = targetLine < 0 ? 0 : targetLine;
				SetCursorPosition({ targetLine , mState.mCursors[c].mInteractiveEnd.mColumn }, c);
			}
		}
	}
	inline void OnLineAdded(int aLineIndex)
	{
		for (int c = 0; c <= mState.mCurrentCursor; c++)
		{
			if (mState.mCursors[c].mInteractiveEnd.mLine >= aLineIndex)
				SetCursorPosition({ mState.mCursors[c].mInteractiveEnd.mLine + 1, mState.mCursors[c].mInteractiveEnd.mColumn }, c);
		}
	}

	inline void SetShowWhitespaces(bool aValue) { mShowWhitespaces = aValue; }
	inline bool IsShowingWhitespaces() const { return mShowWhitespaces; }

	inline ImVec4 U32ColorToVec4(ImU32 in) {
		float s = 1.0f / 255.0f;
		return ImVec4(
			((in >> IM_COL32_A_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_B_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_G_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_R_SHIFT) & 0xFF) * s);
	}

	void SetTabSize(int aValue);
	inline int GetTabSize() const { return mTabSize; }
	void SetLineSpacing(float aValue);
	float GetLineSpacing() const { return mLineSpacing;  }

	void InsertText(const std::string& aValue, int aCursor = -1);
	void InsertText(const char* aValue, int aCursor = -1);

	enum class MoveDirection { Right = 0, Left = 1, Up = 2, Down = 3 };
	void MoveCoords(Coordinates& aCoords, MoveDirection aDirection, bool aWordMode = false, int aLineCount = 1) const;
	void MoveUp(int aAmount = 1, bool aSelect = false);
	void MoveDown(int aAmount = 1, bool aSelect = false);
	void MoveLeft(bool aSelect = false, bool aWordMode = false);
	void MoveRight(bool aSelect = false, bool aWordMode = false);
	void MoveTop(bool aSelect = false);
	void MoveBottom(bool aSelect = false);
	void MoveHome(bool aSelect = false);
	void MoveEnd(bool aSelect = false);

	void SetSelection(int aStartLine, int aStartChar, int aEndLine, int aEndChar, int aCursor = -1);
	void SetSelection(Coordinates aStart, Coordinates aEnd, int aCursor = -1);
	void SelectAll();
	bool AnyCursorHasSelection() const;
	bool AllCursorsHaveSelection() const;

	void Copy();
	void Cut();
	void Paste();
	void Delete(bool aWordMode = false, const EditorState* aEditorState = nullptr);

	int GetUndoIndex() const;
	bool CanUndo() const;
	bool CanRedo() const;
	void Undo(int aSteps = 1);
	void Redo(int aSteps = 1);

	void ClearExtraCursors();
	void ClearSelections();
	void SelectNextOccurrenceOf(const char* aText, int aTextSize, int aCursor = -1);
	void AddCursorForNextOccurrence();

	static const Palette& GetMarianaPalette();
	static const Palette& GetDarkPalette();
	static const Palette& GetLightPalette();
	static const Palette& GetRetroBluePalette();

	static bool IsGlyphWordChar(const Glyph& aGlyph);

	void ImGuiDebugPanel(const std::string& panelName = "Debug");
	void UnitTests();

	inline bool needsInit() { return mNeedsInit; }
	int Initialize();

	inline void setLastSaveTime(uint64_t lastSaveTime = 0) { mLastSaveTime = lastSaveTime; }
    inline uint64_t getLastSaveTime() { return mLastSaveTime; }
    inline void setAutoSaveInterval(uint64_t autoSaveInterval = 0) { mAutoSaveInterval = autoSaveInterval; }
    inline uint64_t getAutoSaveInterval() { return mAutoSaveInterval; }

	inline void SetFilename(const std::string& aFilename) { mFilename = aFilename; }
	inline std::string GetFilename() const { return mFilename; }

	void MoveTo(const Coordinates& aPosition);
	void SetFind(const std::string& aFindStr) { mFindStr = aFindStr; }
	void Find();

    inline void drawLineNumbers(bool dln = true) { mDrawLineNumbers = dln; }


private:
	inline bool IsUTFSequence(char c) const
	{
		return (c & 0xC0) == 0x80;
	}

	typedef std::vector<std::pair<boost::regex, PaletteIndex>> RegexList;

	void MergeCursorsIfPossible();

	class UndoRecord
	{
	public:
		UndoRecord() {}
		~UndoRecord() {}

		UndoRecord(
			const std::vector<UndoOperation>& aOperations,
			TextEditor::EditorState& aBefore,
			TextEditor::EditorState& aAfter);

		void Undo(TextEditor* aEditor);
		void Redo(TextEditor* aEditor);

		std::vector<UndoOperation> mOperations;

		EditorState mBefore;
		EditorState mAfter;
	};

	typedef std::vector<UndoRecord> UndoBuffer;

	void ProcessInputs();
	void Colorize(int aFromLine = 0, int aCount = -1);
	void ColorizeRange(int aFromLine = 0, int aToLine = 0);
	void ColorizeInternal();
	float TextDistanceToLineStart(const Coordinates& aFrom) const;
	void EnsureCursorVisible(int aCursor = -1);
	int GetPageSize() const;
	std::string GetText(const Coordinates& aStart, const Coordinates& aEnd) const;
	Coordinates GetActualCursorCoordinates(int aCursor = -1) const;
	Coordinates SanitizeCoordinates(const Coordinates& aValue) const;
	void DeleteRange(const Coordinates& aStart, const Coordinates& aEnd);
	int InsertTextAt(Coordinates& aWhere, const char* aValue);
	void AddUndo(UndoRecord& aValue);
	Coordinates ScreenPosToCoordinates(const ImVec2& aPosition, bool aInsertionMode = false, bool* isOverLineNumber = nullptr) const;
	Coordinates FindWordStart(const Coordinates& aFrom) const;
	Coordinates FindWordEnd(const Coordinates& aFrom) const;
	Coordinates FindNextWord(const Coordinates& aFrom) const;
	int GetCharacterIndexL(const Coordinates& aCoordinates) const;
	int GetCharacterIndexR(const Coordinates& aCoordinates) const;
	int GetCharacterColumn(int aLine, int aIndex) const;
	int GetLineCharacterCount(int aLine) const;
	int GetLineMaxColumn(int aLine) const;
	bool IsOnWordBoundary(const Coordinates& aAt) const;
	void RemoveLines(int aStart, int aEnd);
	void RemoveLine(int aIndex, const std::unordered_set<int>* aHandledCursors = nullptr);
	void RemoveCurrentLines();
	void OnLineChanged(bool aBeforeChange, int aLine, int aColumn, int aCharCount, bool aDeleted);
	void RemoveGlyphsFromLine(int aLine, int aStartChar, int aEndChar = -1);
	void AddGlyphsToLine(int aLine, int aTargetIndex, Line::iterator aSourceStart, Line::iterator aSourceEnd);
	void AddGlyphToLine(int aLine, int aTargetIndex, Glyph aGlyph);
	Line& InsertLine(int aIndex);

	void ChangeCurrentLinesIndentation(bool aIncrease);
	void MoveUpCurrentLines();
	void MoveDownCurrentLines();
	void ToggleLineComment();

	void EnterCharacter(ImWchar aChar, bool aShift);
	void Backspace(bool aWordMode = false);
	void DeleteSelection(int aCursor = -1);
	std::string GetWordUnderCursor() const;
	std::string GetWordAt(const Coordinates& aCoords) const;
	ImU32 GetGlyphColor(const Glyph& aGlyph) const;

	void HandleKeyboardInputs(bool aParentIsFocused = false);
	void HandleMouseInputs();
	void UpdatePalette();
	void Render(bool aParentIsFocused = false);

	bool FindNextOccurrence(const char* aText, int aTextSize, const Coordinates& aFrom, Coordinates& outStart, Coordinates& outEnd);

	float mLineSpacing;
	Lines mLines;
	EditorState mState;
	UndoBuffer mUndoBuffer;
	int mUndoIndex;

	int mTabSize;
	bool mOverwrite;
	bool mReadOnly;
	bool mAutoIndent;
	bool mWithinRender;
	bool mScrollToCursor;
	bool mScrollToTop;
	bool mColorizerEnabled;
	float mTextStart;                   // position (in pixels) where a code line starts relative to the left of the TextEditor.
	int  mLeftMargin;
	int mColorRangeMin, mColorRangeMax;
	bool mShowWhitespaces;
	float mLongestLineLength;

	Palette mPaletteBase;
	Palette mPalette;
	const LanguageDefinition* mLanguageDefinition = nullptr;
	RegexList mRegexList;

	bool mCheckComments;
	ImVec2 mCharAdvance;
	std::string mLineBuffer;
	uint64_t mStartTime;

	float mLastClick;



	bool mDrawLineNumbers = true;

	uint64_t mLastSaveTime = 0;
	uint64_t mAutoSaveInterval = 5;
	std::string mFilename;
	std::string mFindStr;

	bool mNeedsInit = true;

};




enum textEditorFlags
{
	textEditorFlags_None = 0,
	textEditorFlags_NoStatusBar = 1 << 1,  // Disable the status bar
	textEditorFlags_StatusBarTop = 1 << 2,  // Status bar on Top (else at bottom)
	textEditorFlags_NoMenuBar = 1 << 3,  // disable the menu-bar
	textEditorFlags_MenuBarOutsideFrame = 1 << 4,  // disable the menu-bar
	textEditorFlags_ReadOnly = 1 << 5,
	textEditorFlags_ReadOnly_OnceOnly = 1 << 6,
	textEditorFlags_NoOpen = 1 << 7,  // disable open
	textEditorFlags_NoSave = 1 << 8,  // disable save
	textEditorFlags_AutoSave = 1 << 9,
	textEditorFlags_NoQuit = 1 << 10,  // disable quit
	textEditorFlags_NoLineNumbers = 1 << 11,  // don't display line numbers

	textEditorFlags_NoDecoration = textEditorFlags_NoStatusBar | textEditorFlags_NoMenuBar | textEditorFlags_NoLineNumbers,
};