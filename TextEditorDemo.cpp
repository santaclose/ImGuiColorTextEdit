

/*
To run the demo

//clone imgui

// copy the ImGuiColorTextEdit folder into your desired example_... folder
// add .\ImGuiColorTextEdit to c++ include directory of the appropriate example... project (remember both debug and release)

// include TextEditor.h and the following definitions into example...\main.cpp
#include "TextEditor.h"
int TextEditorDemo();


// add a call to the demo  into example...\main.cpp

// 4. Show a textEditor frame in a simple window.
TextEditorDemo();
*/







#include "TextEditor.h"


// required for load/save
#include <fstream>
#include <sstream>
#include <chrono>       // required for autosave




#define RGB2FLOAT(r,g,b) (float)r/255.0f, (float)g/255.0f, (float)b/255.0f
#define RGB2IM32(RGB,A) (ImU32)(RGB | A << 24)

// Use these when an ImU32 is expected: In this format: RGB2IM32(RGB_lavenderblush, 255)
#define RGB_ghostwhite    248  |  248 <<8  |  255 <<16

// Use these when an ImVec4 is expected: In this format: ImVec4(CF_firebrick4, 1.00f)
#define CF_red    RGB2FLOAT(255,   0,   0)


// helpers
int timeNow() {
    return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}
std::string lower(std::string s) {
    for (int i = 0; i < s.size(); i++)
        s[i] = (char)std::tolower(s[i]);
    return s;
}

void TextEditor::MoveTo(const Coordinates& aPosition)
{
    SetCursorPosition(aPosition);
    mState.mCursors[mState.mCurrentCursor].mCursorPosition = aPosition;
    EnsureCursorVisible();
}


void TextEditor::Find()
{
    std::string curText = lower(GetText());
    std::string findStr = lower(mFindStr);
    static std::string oldFindStr = "";

    // if findStr has changed then reinitialise
    static int startLoc = 0;
    if (oldFindStr != findStr)
        startLoc = 0;

    int foundLoc = curText.find(findStr, startLoc);

    //if it's not on the first round and we've got to the end then wrap around to the beginning again
    if (startLoc > 0 && foundLoc == std::string::npos)
        foundLoc = curText.find(findStr);

    // if nothing has been found then don't proceed
    if (foundLoc == std::string::npos)
        return;

    oldFindStr = findStr;

    // editor uses a line/column location system to position the cursor or selection, so 
    // count how many '\n' characters occurred before the point findStr has been found
    // can't use GetLineMaxColumn here because it miscounts tab ('\t') characters
    startLoc = foundLoc + 1;
    int retCnt = 0;
    int lineStart = 0;
    for (int i = 0; i < foundLoc; i++) {
        if (curText[i] == '\n') {
            retCnt++;
            lineStart = i;
        }
    }
    if (foundLoc > 0)
        lineStart++;		// the count starts just after the previous '\n' so add 1

    // move to the selection
    Coordinates tecStart = { retCnt, (int)(foundLoc - lineStart) };
    Coordinates tecEnd = { retCnt, (int)(foundLoc - lineStart + findStr.size()) };
    SetSelection(tecStart, tecEnd, SelectionMode::Normal);

    // make as much of the selection visible as possible
    MoveTo(tecEnd);
    MoveTo(tecStart);
}



int TextEditor::Initialize() {

    mNeedsInit = false;

    static LanguageDefinition lang;

    this->SetLanguageDefinition(lang);
    this->SetPalette(TextEditor::GetLightPalette());

    return 0;
}


std::string readFile(std::string fileToLoad) {

    std::ifstream inFile(fileToLoad, std::ios::in);
    std::string data;
    inFile >> data;
    return data;

}

int loadEditorFile(TextEditor& editor, std::string fileName) {

    editor.SetText(readFile(fileName));
    editor.SetFilename(fileName);
    return 0;
}


int saveEditorFile(TextEditor& editor, std::string saveAsFileName = "") {

    if (editor.GetFilename() == "")
        return 1;

    if (saveAsFileName == "")
        saveAsFileName = editor.GetFilename();

    std::stringstream oFile(editor.GetText());
    std::cout << "Saving " << saveAsFileName << std::endl;

    editor.setLastSaveTime(timeNow());

    return 0;
}


void textEditorMenuBar(TextEditor& editor, bool& show, textEditorFlags Flags) {
//    if (ImGui::BeginMenuBar())
//    {

    if ((Flags & (textEditorFlags_NoOpen | textEditorFlags_NoSave | textEditorFlags_NoQuit)) == 0) {
        if (ImGui::BeginMenu("File"))
        {
            if ((Flags & textEditorFlags_NoOpen) == 0) {
                if (ImGui::MenuItem("Open TextEditorDemo.cpp"))
                    loadEditorFile(editor, "TextEditorDemo.cpp");
            }

            if ((Flags & textEditorFlags_NoSave) == 0) {
                if (editor.IsReadOnly())
                    ImGui::BeginDisabled();
                if (ImGui::MenuItem("Save"))
                    saveEditorFile(editor);
                if (ImGui::MenuItem("SaveAs newname.tmp"))
                    saveEditorFile(editor, "newname.tmp");
                if (editor.IsReadOnly())
                    ImGui::EndDisabled();
            }

            if ((Flags & textEditorFlags_NoQuit) == 0) {
                if (ImGui::MenuItem("Quit", "Alt-F4"))
                    show = false;
            }
            ImGui::EndMenu();
        }
    }

    if (ImGui::BeginMenu("Edit"))
    {
        if ((Flags & textEditorFlags_ReadOnly) != 0)
            ImGui::BeginDisabled();
        bool ro = editor.IsReadOnly();
        if (ImGui::MenuItem("Read-only mode", nullptr, &ro))
            editor.SetReadOnly(ro);
        if ((Flags & textEditorFlags_ReadOnly) != 0)
            ImGui::EndDisabled();

        ImGui::Separator();

        if (ImGui::MenuItem("Undo", "ALT-Backspace", nullptr, !ro && editor.CanUndo()))
            editor.Undo();
        if (ImGui::MenuItem("Redo", "Ctrl-Y", nullptr, !ro && editor.CanRedo()))
            editor.Redo();

        ImGui::Separator();

        if (ImGui::MenuItem("Copy", "Ctrl-C", nullptr, editor.HasSelection()))
            editor.Copy();
        if (ImGui::MenuItem("Cut", "Ctrl-X", nullptr, !ro && editor.HasSelection()))
            editor.Cut();
        if (ImGui::MenuItem("Delete", "Del", nullptr, !ro && editor.HasSelection()))
            editor.Delete();
        if (ImGui::MenuItem("Paste", "Ctrl-V", nullptr, !ro))
            editor.Paste();

        ImGui::Separator();

        if (ImGui::MenuItem("Select all", nullptr, nullptr))
            editor.SetSelection(TextEditor::Coordinates(), TextEditor::Coordinates(editor.GetTotalLines(), 0));

        ImGui::Separator();

        static char findChr[64] = "";
        if (ImGui::InputText("Find (F3)", findChr, IM_ARRAYSIZE(findChr)))		//ImGuiInputTextFlags_EnterReturnsTrue
            editor.SetFind(findChr);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Enter the (case insensitive) string to search for\n then press F3 to repeatedly search for it");
            ImGui::EndTooltip();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Dark palette"))
            editor.SetPalette(TextEditor::GetDarkPalette());
        if (ImGui::MenuItem("Light palette"))
            editor.SetPalette(TextEditor::GetLightPalette());
        if (ImGui::MenuItem("Retro blue palette"))
            editor.SetPalette(TextEditor::GetRetroBluePalette());
        ImGui::EndMenu();
    }
//    }
}

void textEditorStatusBar(TextEditor& editor) {
    auto cpos = editor.GetCursorPosition();
    ImGui::Text("%6d/%-6d %6d lines  | %s | %s | %s | %s", cpos.mLine + 1, cpos.mColumn + 1, editor.GetTotalLines(),
        editor.IsOverwrite() ? "Ovr" : "Ins",
        editor.CanUndo() ? "*" : " ",
        editor.GetLanguageDefinitionName(),
        editor.GetFilename().c_str()
    );
}

void textEditorHeaderStuff(TextEditor& editor, textEditorFlags Flags, std::string fileToEdit = "") {

    if (editor.needsInit())
        editor.Initialize();		// sets up the highlighter definitions - you can also find more by searching  TextEditor::LanguageDefinition

    static std::string curLoadedFile;

    if (curLoadedFile != fileToEdit) {
        // If there's already a file loaded which has been changed, then save it before loading the next
        if (curLoadedFile != "" && !editor.IsReadOnly())		// this doesn't work:  and editor.IsTextChanged())
            saveEditorFile(editor);
        curLoadedFile = fileToEdit;
        loadEditorFile(editor, fileToEdit);
        if (((Flags & textEditorFlags_ReadOnly) != 0) || ((Flags & textEditorFlags_ReadOnly_OnceOnly) != 0))
            editor.SetReadOnly(true);
    }
    if ((Flags & textEditorFlags_ReadOnly) != 0)
        editor.SetReadOnly(true);

}

int drawTextEditorFrame(TextEditor& editor, bool& show, const char* frameName, ImVec2 frameSize, textEditorFlags Flags, std::string fileToEdit) {

    static int saved = 0;
    float renderSizeY = frameSize.y;

    textEditorHeaderStuff(editor, Flags, fileToEdit);
    
    ImGui::BeginChild(frameName, frameSize, false);
    {
        //https://github.com/ocornut/imgui/issues/3518
        // I'm not sure how to get a menu bar across the top of a frame
        if ((Flags & textEditorFlags_NoMenuBar) == 0) {
            textEditorMenuBar(editor, show, Flags);
            renderSizeY -= 3 * 20;
        }


        if ((Flags & textEditorFlags_AutoSave) != 0 && saved != 0 && editor.IsTextChanged())
            ImGui::TextColored(ImVec4(CF_red, 1.00f), "WARNING: AUTOSAVE FAILURE");


        if ((Flags & textEditorFlags_NoStatusBar) == 0) {
            renderSizeY -= 20;
            if ((Flags & textEditorFlags_StatusBarTop) != 0)
                textEditorStatusBar(editor);
        }

        editor.Render("TextEditor", false, { frameSize.x, renderSizeY }, false);

        if ((Flags & textEditorFlags_NoStatusBar) == 0 && (Flags & textEditorFlags_StatusBarTop) == 0)
            textEditorStatusBar(editor);

    }
    ImGui::EndChild();


    if ((Flags & textEditorFlags_AutoSave) != 0) {
        if (editor.IsTextChanged()) {
            if (editor.getLastSaveTime() < timeNow() - editor.getAutoSaveInterval()) {
                saved = saveEditorFile(editor);
            }
        }
    }


    return 0;
}





// if you want to create your own version then just declare the required function
//int drawTextEditorFrame(TextEditor& editor, bool& show, const char* frameName = "##Text Editor Frame", ImVec2 frameSize = { -1, -1 }, textEditorFlags Flags = textEditorFlags_None, std::string fileToEdit = "");
//void textEditorMenuBar(TextEditor& editor, bool& show, textEditorFlags Flags = textEditorFlags_None);
//void textEditorStatusBar(TextEditor& editor);

void TextEditorDemo() {

    static bool showTextEditorFrame = true;
    if (showTextEditorFrame)
    {

        static TextEditor editor;
        textEditorFlags teFlags = textEditorFlags_None;


        if (ImGui::Begin("Text Editor Frame", &showTextEditorFrame))
        {

            ImGui::PushItemWidth(ImGui::GetFontSize() * -12);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("Menu"))
                {
                    ImGui::MenuItem("Console");
                    ImGui::MenuItem("Log");

                    ImGui::EndMenu();
                }
                //textEditorMenuBar(editor, showTextEditorFrame, teFlags);
                ImGui::EndMenuBar();
            }



            static bool menuInFrame = true;
            ImGui::Checkbox("Menu in TextEditor Frame", &menuInFrame);
            if (!menuInFrame)
                teFlags = (textEditorFlags)(teFlags | textEditorFlags_NoMenuBar);


            static bool statusBarAtTop = true;
            ImGui::Checkbox("Status Bar at Top", &statusBarAtTop);
            if (statusBarAtTop)
                teFlags = (textEditorFlags)(teFlags | textEditorFlags_StatusBarTop);

            static bool statusBarInFrame = true;
            ImGui::Checkbox("Status Bar in Text Editor Frame", &statusBarInFrame);
            if (!statusBarInFrame)
                teFlags = (textEditorFlags)(teFlags | textEditorFlags_NoStatusBar);

            static bool drawLineNumbers = true;
            if (ImGui::Checkbox("Number Lines", &drawLineNumbers))
                editor.drawLineNumbers(drawLineNumbers);


            if (!statusBarInFrame && statusBarAtTop)
                textEditorStatusBar(editor);

            //                    static bool showDecorations = true;
            //                    ImGui::Checkbox("Show Decorations", &showDecorations);        // this will override the others
            //                    if (!showDecorations)
            //                        teFlags = textEditorFlags_NoDecoration;

            ImGui::Text("The TextEditor frame begins below here!");
            ImGui::Separator();
            drawTextEditorFrame(editor, showTextEditorFrame, "##Text Editor Frame", { -1, 500 }, teFlags, "");


            ImGui::Separator();
            ImGui::Text("The TextEditor frame ends above here!");

            if (!statusBarInFrame && !statusBarAtTop)
                textEditorStatusBar(editor);

        }
        ImGui::End();
    }

}
