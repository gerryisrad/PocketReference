#include <globals.h>

enum TERMINAL_functions { PROMPT, EXAMPLE };
TERMINAL_functions CurrentTERMfunc = PROMPT;

static std::vector<String> terminalOutputs;
static String currentLine = "";
static String currentDir = "/";

#pragma region TERMINAL
void funcSelect(String command) {
  String returnText = "";

  // Add inputted command to terminal outputs
  terminalOutputs.push_back(currentDir + ">" + command);

  // Check whether command is a home/settings command
  returnText = commandSelect(command);
  if (returnText != "") {
    terminalOutputs.push_back(returnText);
    newState = true;
    return;
  }

  // things, stuff, items



  return;
}

void TERMINAL_INIT() {
  CurrentAppState = TERMINAL;
  CurrentTERMfunc = PROMPT;
  KB().setKeyboardState(NORMAL);
  currentLine = "";
  newState = true;
}

void processKB_TERMINAL() {
  String outLine = "";
  String command = "";

  switch (CurrentTERMfunc) {
    case PROMPT:
      command = textPrompt("", currentDir + ">");
      if (command != "_EXIT_") {
        funcSelect(command);
      }
      else HOME_INIT();
      break;
  }

}

void einkHandler_TERMINAL() {
  if (newState) {
    newState = false;
    display.fillRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
    
    int y = 14;
    for (const String& s : terminalOutputs) {
      display.setTextColor(GxEPD_WHITE);
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(5, y);
      display.print(s);
      y+=16;
    }

    EINK().refresh();
  }
}