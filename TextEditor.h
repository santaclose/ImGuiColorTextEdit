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
#include "imgui.h"

#include "Common.h"
#include "Colorizer.h"

class IMGUI_API TextEditor
{
public:
	// ------------- Exposed API ------------- //

	TextEditor();
	~TextEditor();

	enum class PaletteId
	{
		Dark, Light, Mariana, RetroBlue
	};
	enum class LanguageDefinitionId
	{
		None, Cpp, C, Cs, Python, Lua, Json, Sql, AngelScript, Glsl, Hlsl
	};
	enum class SetViewAtLineMode
	{
		FirstVisibleLine, Centered, LastVisibleLine
	};

	inline void SetReadOnlyEnabled(bool aValue) { mReadOnly = aValue; }
	inline bool IsReadOnlyEnabled() const { return mReadOnly; }
	inline void SetAutoIndentEnabled(bool aValue) { mAutoIndent = aValue; }
	inline bool IsAutoIndentEnabled() const { return mAutoIndent; }
	inline void SetShowWhitespacesEnabled(bool aValue) { mShowWhitespaces = aValue; }
	inline bool IsShowWhitespacesEnabled() const { return mShowWhitespaces; }
	inline void SetShowLineNumbersEnabled(bool aValue) { mShowLineNumbers = aValue; }
	inline bool IsShowLineNumbersEnabled() const { return mShowLineNumbers; }
	inline void SetShortTabsEnabled(bool aValue) { mShortTabs = aValue; }
	inline bool IsShortTabsEnabled() const { return mShortTabs; }
	inline int GetLineCount() const { return mLines.size(); }
	inline bool IsOverwriteEnabled() const { return mOverwrite; }
	void SetPalette(PaletteId aValue);
	PaletteId GetPalette() const { return mPaletteId; }
	void SetLanguageDefinition(LanguageDefinitionId aValue);
	LanguageDefinitionId GetLanguageDefinition() const { return mLanguageDefinitionId; };
	const char* GetLanguageDefinitionName() const;
	void SetTabSize(int aValue);
	inline int GetTabSize() const { return mTabSize; }
	void SetLineSpacing(float aValue);
	inline float GetLineSpacing() const { return mLineSpacing;  }

	inline static void SetDefaultPalette(PaletteId aValue) { defaultPalette = aValue; }
	inline static PaletteId GetDefaultPalette() { return defaultPalette; }

	void SelectAll();
	void SelectLine(int aLine);
	void SelectRegion(int aStartLine, int aStartChar, int aEndLine, int aEndChar);
	void SelectNextOccurrenceOf(const char* aText, int aTextSize, bool aCaseSensitive = true);
	void SelectAllOccurrencesOf(const char* aText, int aTextSize, bool aCaseSensitive = true);
	bool AnyCursorHasSelection() const;
	bool AllCursorsHaveSelection() const;
	void ClearExtraCursors();
	void ClearSelections();
	void SetCursorPosition(int aLine, int aCharIndex);
	inline void GetCursorPosition(int& outLine, int& outColumn) const
	{
		auto coords = GetActualCursorCoordinates();
		outLine = coords.mLine;
		outColumn = coords.mColumn;
	}
	int GetFirstVisibleLine();
	int GetLastVisibleLine();
	void SetViewAtLine(int aLine, SetViewAtLineMode aMode);

	void Copy();
	void Cut();
	void Paste();
	void Undo(int aSteps = 1);
	void Redo(int aSteps = 1);
	inline bool CanUndo() const { return !mReadOnly && mUndoIndex > 0; };
	inline bool CanRedo() const { return !mReadOnly && mUndoIndex < (int)mUndoBuffer.size(); };
	inline int GetUndoIndex() const { return mUndoIndex; };

	void SetText(const std::string& aText);
	std::string GetText() const;

	void SetTextLines(const std::vector<std::string>& aLines);
	std::vector<std::string> GetTextLines() const;

	bool Render(const char* aTitle, bool aParentIsFocused = false, const ImVec2& aSize = ImVec2(), bool aBorder = false);

	void ImGuiDebugPanel(const std::string& panelName = "Debug");
	void UnitTests();

private:
	// ------------- Generic utils ------------- //

	static inline ImVec4 U32ColorToVec4(ImU32 in)
	{
		float s = 1.0f / 255.0f;
		return ImVec4(
			((in >> IM_COL32_A_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_B_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_G_SHIFT) & 0xFF) * s,
			((in >> IM_COL32_R_SHIFT) & 0xFF) * s);
	}
	static inline bool IsUTFSequence(char c)
	{
		return (c & 0xC0) == 0x80;
	}
	static inline float Distance(const ImVec2& a, const ImVec2& b)
	{
		float x = a.x - b.x;
		float y = a.y - b.y;
		return sqrt(x * x + y * y);
	}
	template<typename T>
	static inline T Max(T a, T b) { return a > b ? a : b; }
	template<typename T>
	static inline T Min(T a, T b) { return a < b ? a : b; }

	// ------------- Internal ------------- //

	struct Cursor
	{
		Common::Coordinates mInteractiveStart = { 0, 0 };
		Common::Coordinates mInteractiveEnd = { 0, 0 };
		inline Common::Coordinates GetSelectionStart() const { return mInteractiveStart < mInteractiveEnd ? mInteractiveStart : mInteractiveEnd; }
		inline Common::Coordinates GetSelectionEnd() const { return mInteractiveStart > mInteractiveEnd ? mInteractiveStart : mInteractiveEnd; }
		inline bool HasSelection() const { return mInteractiveStart != mInteractiveEnd; }
	};

	struct EditorState // state to be restored with undo/redo
	{
		int mCurrentCursor = 0;
		int mLastAddedCursor = 0;
		std::vector<Cursor> mCursors = { {{0,0}} };
		void AddCursor();
		int GetLastAddedCursorIndex();
		void SortCursorsFromTopToBottom();
	};

	struct Identifier
	{
		Common::Coordinates mLocation;
		std::string mDeclaration;
	};

	typedef std::unordered_map<std::string, Identifier> Identifiers;
	//typedef std::vector<std::pair<boost::regex, Common::PaletteIndex>> RegexList;

	class UndoRecord
	{
	public:
		UndoRecord() {}
		~UndoRecord() {}

		UndoRecord(
			const std::vector<Common::UndoOperation>& aOperations,
			TextEditor::EditorState& aBefore,
			TextEditor::EditorState& aAfter);

		void Undo(TextEditor* aEditor);
		void Redo(TextEditor* aEditor);

		std::vector<Common::UndoOperation> mOperations;

		EditorState mBefore;
		EditorState mAfter;
	};

	std::string GetText(const Common::Coordinates& aStart, const Common::Coordinates& aEnd) const;
	std::string GetClipboardText() const;
	std::string GetSelectedText(int aCursor = -1) const;

	void SetCursorPosition(const Common::Coordinates& aPosition, int aCursor = -1, bool aClearSelection = true);

	int InsertTextAt(Common::Coordinates& aWhere, const char* aValue);
	void InsertTextAtCursor(const char* aValue, int aCursor = -1);

	enum class MoveDirection { Right = 0, Left = 1, Up = 2, Down = 3 };
	bool Move(int& aLine, int& aCharIndex, bool aLeft = false, bool aLockLine = false) const;
	void MoveCharIndexAndColumn(int aLine, int& aCharIndex, int& aColumn) const;
	void MoveCoords(Common::Coordinates& aCoords, MoveDirection aDirection, bool aWordMode = false, int aLineCount = 1) const;

	void MoveUp(int aAmount = 1, bool aSelect = false);
	void MoveDown(int aAmount = 1, bool aSelect = false);
	void MoveLeft(bool aSelect = false, bool aWordMode = false);
	void MoveRight(bool aSelect = false, bool aWordMode = false);
	void MoveTop(bool aSelect = false);
	void MoveBottom(bool aSelect = false);
	void MoveHome(bool aSelect = false);
	void MoveEnd(bool aSelect = false);
	void EnterCharacter(ImWchar aChar, bool aShift);
	void Backspace(bool aWordMode = false);
	void Delete(bool aWordMode = false, const EditorState* aEditorState = nullptr);

	void SetSelection(Common::Coordinates aStart, Common::Coordinates aEnd, int aCursor = -1);
	void SetSelection(int aStartLine, int aStartChar, int aEndLine, int aEndChar, int aCursor = -1);

	void SelectNextOccurrenceOf(const char* aText, int aTextSize, int aCursor = -1, bool aCaseSensitive = true);
	void AddCursorForNextOccurrence(bool aCaseSensitive = true);
	bool FindNextOccurrence(const char* aText, int aTextSize, const Common::Coordinates& aFrom, Common::Coordinates& outStart, Common::Coordinates& outEnd, bool aCaseSensitive = true);
	bool FindMatchingBracket(int aLine, int aCharIndex, Common::Coordinates& out);
	void ChangeCurrentLinesIndentation(bool aIncrease);
	void MoveUpCurrentLines();
	void MoveDownCurrentLines();
	void ToggleLineComment();
	void RemoveCurrentLines();

	float TextDistanceToLineStart(const Common::Coordinates& aFrom, bool aSanitizeCoords = true) const;
	void EnsureCursorVisible(int aCursor = -1, bool aStartToo = false);

	Common::Coordinates SanitizeCoordinates(const Common::Coordinates& aValue) const;
	Common::Coordinates GetActualCursorCoordinates(int aCursor = -1, bool aStart = false) const;
	Common::Coordinates ScreenPosToCoordinates(const ImVec2& aPosition, bool aInsertionMode = false, bool* isOverLineNumber = nullptr) const;
	Common::Coordinates FindWordStart(const Common::Coordinates& aFrom) const;
	Common::Coordinates FindWordEnd(const Common::Coordinates& aFrom) const;
	int GetCharacterIndexL(const Common::Coordinates& aCoordinates) const;
	int GetCharacterIndexR(const Common::Coordinates& aCoordinates) const;
	int GetCharacterColumn(int aLine, int aIndex) const;
	int GetFirstVisibleCharacterIndex(int aLine) const;
	int GetLineMaxColumn(int aLine, int aLimit = -1) const;

	Common::Line& InsertLine(int aIndex);
	void RemoveLine(int aIndex, const std::unordered_set<int>* aHandledCursors = nullptr);
	void RemoveLines(int aStart, int aEnd);
	void DeleteRange(const Common::Coordinates& aStart, const Common::Coordinates& aEnd);
	void DeleteSelection(int aCursor = -1);

	void RemoveGlyphsFromLine(int aLine, int aStartChar, int aEndChar = -1);
	void AddGlyphsToLine(int aLine, int aTargetIndex, Common::Line::iterator aSourceStart, Common::Line::iterator aSourceEnd);
	void AddGlyphToLine(int aLine, int aTargetIndex, Common::Glyph aGlyph);
	ImU32 GetGlyphColor(const Common::Glyph& aGlyph) const;

	void HandleKeyboardInputs(bool aParentIsFocused = false);
	void HandleMouseInputs();
	void UpdateViewVariables(float aScrollX, float aScrollY);
	void Render(bool aParentIsFocused = false);

	void OnCursorPositionChanged();
	void OnLineChanged(bool aBeforeChange, int aLine, int aColumn, int aCharCount, bool aDeleted);
	void MergeCursorsIfPossible();

	void AddUndo(UndoRecord& aValue);

	std::vector<Common::Line> mLines;
	EditorState mState;
	std::vector<UndoRecord> mUndoBuffer;
	int mUndoIndex = 0;

	int mTabSize = 4;
	float mLineSpacing = 1.0f;
	bool mOverwrite = false;
	bool mReadOnly = false;
	bool mAutoIndent = true;
	bool mShowWhitespaces = true;
	bool mShowLineNumbers = true;
	bool mShortTabs = false;

	int mSetViewAtLine = -1;
	SetViewAtLineMode mSetViewAtLineMode;
	int mEnsureCursorVisible = -1;
	bool mEnsureCursorVisibleStartToo = false;
	bool mScrollToTop = false;

	float mTextStart = 20.0f; // position (in pixels) where a code line starts relative to the left of the TextEditor.
	int mLeftMargin = 10;
	ImVec2 mCharAdvance;
	float mCurrentSpaceHeight = 20.0f;
	float mCurrentSpaceWidth = 20.0f;
	float mLastClickTime = -1.0f;
	ImVec2 mLastClickPos;
	int mFirstVisibleLine = 0;
	int mLastVisibleLine = 0;
	int mVisibleLineCount = 0;
	int mFirstVisibleColumn = 0;
	int mLastVisibleColumn = 0;
	int mVisibleColumnCount = 0;
	float mContentWidth = 0.0f;
	float mContentHeight = 0.0f;
	float mScrollX = 0.0f;
	float mScrollY = 0.0f;
	bool mPanning = false;
	bool mDraggingSelection = false;
	ImVec2 mLastMousePos;
	bool mCursorPositionChanged = false;
	bool mCursorOnBracket = false;
	Common::Coordinates mMatchingBracketCoords;

	int mColorRangeMin = 0;
	int mColorRangeMax = 0;
	bool mCheckComments = true;
	PaletteId mPaletteId;
	Common::Palette mPalette;
	LanguageDefinitionId mLanguageDefinitionId;
	Common::LanguageDefinition* mLanguageDefinition = nullptr;
	//RegexList mRegexList;
	Colorizer* mColorizer = nullptr;

	inline bool IsHorizontalScrollbarVisible() const { return mCurrentSpaceWidth > mContentWidth; }
	inline bool IsVerticalScrollbarVisible() const { return mCurrentSpaceHeight > mContentHeight; }
	inline int TabSizeAtColumn(int aColumn) const { return mTabSize - (aColumn % mTabSize); }

	static const Common::Palette& GetDarkPalette();
	static const Common::Palette& GetMarianaPalette();
	static const Common::Palette& GetLightPalette();
	static const Common::Palette& GetRetroBluePalette();
	static const std::unordered_map<char, char> OPEN_TO_CLOSE_CHAR;
	static const std::unordered_map<char, char> CLOSE_TO_OPEN_CHAR;
	static PaletteId defaultPalette;
};
