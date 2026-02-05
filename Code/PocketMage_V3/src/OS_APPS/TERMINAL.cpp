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
  String totalMsg = currentDir + ">" + command;
  if (totalMsg.length() > 28) totalMsg = totalMsg.substring(0,28);
  terminalOutputs.push_back(totalMsg);

  command.toLowerCase();
  
  // enter directory
  if (command.startsWith("cd")) {
    pocketmage::setCpuSpeed(240);
    // Remove "cd " prefix and trim whitespace
    String arg = command.substring(2);
    arg.trim();
    if (arg.length() == 0) {
      currentDir = "/";  // 'cd' alone returns to root
    } else {
      String newPath = arg;
      // Handle relative paths
      if (!newPath.startsWith("/")) {
        if (!currentDir.endsWith("/"))
          currentDir += "/";
        newPath = currentDir + newPath;
      }
      // Remove trailing '/' unless root
      if (newPath.length() > 1 && newPath.endsWith("/")) {
        newPath.remove(newPath.length() - 1);
      }

      // Check if directory exists
      if (global_fs->exists(newPath)) {
        File f = global_fs->open(newPath);
        if (f && f.isDirectory()) {
          currentDir = newPath;
        } else {
          returnText = "cd: Not a directory";
        }
        f.close();
      } else {
        returnText = "cd: No such directory";
      }
    }

    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    if (returnText != "") {
      terminalOutputs.push_back(returnText);
      OLED().oledWord(returnText);
      delay(1000);
    }
    newState = true;
    return;
  }

  // list directory
  else if (command.startsWith("ls")) {
    pocketmage::setCpuSpeed(240);
    String arg = command.substring(2);
    arg.trim();
    String listPath = currentDir;
    if (arg.length() > 0) {
      if (arg.startsWith("/"))
        listPath = arg;
      else {
        if (!currentDir.endsWith("/"))
          listPath = currentDir + "/";
        listPath += arg;
      }
    }

    if (global_fs->exists(listPath)) {
      File dir = global_fs->open(listPath);
      if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
          String lineOutput = "";
          if (file.isDirectory()) lineOutput += "[DIR] ";
          else lineOutput += "      ";
          lineOutput += file.name();
          if (!file.isDirectory()) {
            lineOutput += " | ";
            lineOutput += String(file.size()) + "b";
          }
          if (lineOutput.length() > 28) lineOutput = lineOutput.substring(0,28);
          terminalOutputs.push_back(lineOutput);

          lineOutput = "";
          file = dir.openNextFile();
        }
        dir.close();
      } else {
        returnText = "ls: Not a directory";
      }
    } else {
      returnText = "ls: No such directory";
    }
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    if (returnText != "") {
      terminalOutputs.push_back(returnText);
      OLED().oledWord(returnText);
      delay(1000);
    }
    newState = true;
    return;
  }

  // Check whether command is a home/settings command
  returnText = commandSelect(command);
  if (returnText != "") {
    terminalOutputs.push_back(returnText);
    newState = true;
    return;
  }
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
      } else
        HOME_INIT();
      break;
  }
}

void einkHandler_TERMINAL() {
  if (newState) {
    newState = false;
    display.fillRect(0, 0, display.width(), display.height(), GxEPD_BLACK);

    if (terminalOutputs.size() < 14) {
      int y = 14;
      for (const String& s : terminalOutputs) {
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(5, y);
        display.print(s);
        y += 16;
      }
    } else {
      int y = display.height() - 5;
      for (int i = terminalOutputs.size() - 1; i >= 0; i--) {
        if (y < 0)
          break;
        const String& s = terminalOutputs[i];
        display.setTextColor(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setCursor(5, y);
        display.print(s);
        y -= 16;
      }
    }

    EINK().refresh();
  }
}