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
  
  #pragma region Basic Commands
  
  // Clear command window
  if (command == "clear") {
    terminalOutputs.clear();
    newState = true;
    return;
  }

  // Help
  else if (command == "help") {
    terminalOutputs.push_back("Available commands:");
    terminalOutputs.push_back("ls                  List dir");
    terminalOutputs.push_back("cd <dir>          Change dir");
    terminalOutputs.push_back("rm <file>        Remove file");
    terminalOutputs.push_back("rm -r <dir>       Remove dir");
    terminalOutputs.push_back("cp <src> <dest>    Copy file");
    terminalOutputs.push_back("mv <src> <dest>  Move/rename");
    terminalOutputs.push_back("touch <file>     Create file");
    terminalOutputs.push_back("clear         Clear terminal");
    terminalOutputs.push_back("help          Show this help");

    newState = true;
    return;
  }

  #pragma region File Operations
  // Enter directory
  else if (command.startsWith("cd")) {
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

  // List directory
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
          if (file.isDirectory()) lineOutput += "[DIR]";
          else lineOutput += "     ";
          lineOutput += file.name();
          if (!file.isDirectory()) {
            lineOutput += " * ";
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

  // Make directory
  else if (command.startsWith("mkdir")) {
    pocketmage::setCpuSpeed(240);

    // Format the path
    String arg = command.substring(5);
    arg.trim();
    String newDirPath = currentDir;
    if (arg.length() > 0) {
      if (arg.startsWith("/"))
        newDirPath = arg;
      else {
        if (!currentDir.endsWith("/"))
          newDirPath = currentDir + "/";
        newDirPath += arg;
      }
    }
    else {
      returnText = "Path not defined";
    }

    // Create the directory
    if (!global_fs->exists(newDirPath)) global_fs->mkdir(newDirPath);
    currentDir = newDirPath;

    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    if (returnText != "") {
      terminalOutputs.push_back(returnText);
      OLED().oledWord(returnText);
      delay(1000);
    }
    newState = true;
    return;
  }

  // Remove directory
  else if (command.startsWith("rm -r")) {
    pocketmage::setCpuSpeed(240);
    String arg = command.substring(5);
    arg.trim();

    String dirPath = currentDir;
    if (arg.length() > 0) {
      if (arg.startsWith("/"))
        dirPath = arg;
      else {
        if (!currentDir.endsWith("/"))
          dirPath += "/";
        dirPath += arg;
      }
    } else {
      returnText = "Path not defined";
    }

    if (returnText == "" && global_fs->exists(dirPath)) {
      File root = global_fs->open(dirPath);
      if (!root) {
        returnText = "Failed to open path";
      }
      else {
        if (!root.isDirectory()) {
          // Simple file delete
          if (!global_fs->remove(dirPath))
            returnText = "Failed to remove file";
        }
        else {
          // Recursive directory delete
          File entry;
          while (true) {
            entry = root.openNextFile();
            if (!entry)
              break;

            String entryPath = dirPath;
            if (!entryPath.endsWith("/"))
              entryPath += "/";
            entryPath += entry.name();

            if (entry.isDirectory()) {
              // recurse inline by reusing rm -r logic
              String subCmd = "rm -r " + entryPath;
              command = subCmd;
              root.close();
              pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
              newState = true;
              return;
            } else {
              global_fs->remove(entryPath);
            }
            entry.close();
          }
          root.close();

          // directory now empty
          if (!global_fs->rmdir(dirPath))
            returnText = "Failed to remove directory";
        }
      }
    }
    else if (returnText == "") {
      returnText = "Path not found";
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

  // Remove file
  else if (command.startsWith("rm ") && !command.startsWith("rm -r")) {
    pocketmage::setCpuSpeed(240);
    String arg = command.substring(2);
    arg.trim();

    String dirPath = currentDir;
    if (arg.length() > 0) {
      if (arg.startsWith("/"))
        dirPath = arg;
      else {
        if (!currentDir.endsWith("/"))
          dirPath += "/";
        dirPath += arg;
      }
    } else {
      returnText = "Path not defined";
    }

    if (returnText == "" && global_fs->exists(dirPath)) {
      File f = global_fs->open(dirPath);
      if (!f) {
        returnText = "Failed to open file";
      }
      else if (f.isDirectory()) {
        returnText = "Not a file - use <rm -r>";
      }
      else {
        f.close();  // REQUIRED
        if (!global_fs->remove(dirPath))
          returnText = "Delete failed";
      }
      if (f) f.close();
    }
    else if (returnText == "") {
      returnText = "Path not found";
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

  // Copy file
  else if (command.startsWith("cp ")) {
    pocketmage::setCpuSpeed(240);

    String args = command.substring(3);
    args.trim();

    int spaceIdx = args.indexOf(' ');
    if (spaceIdx == -1) {
      returnText = "Usage: cp <src> <dest>";
    } else {
      String src = args.substring(0, spaceIdx);
      String dest = args.substring(spaceIdx + 1);
      src.trim();
      dest.trim();

      String srcPath = src.startsWith("/") ? src : (currentDir + (currentDir.endsWith("/") ? "" : "/") + src);
      String destPath = dest.startsWith("/") ? dest : (currentDir + (currentDir.endsWith("/") ? "" : "/") + dest);

      if (!global_fs->exists(srcPath)) {
        returnText = "Source not found";
      } else {
        File srcFile = global_fs->open(srcPath, FILE_READ);
        if (!srcFile || srcFile.isDirectory()) {
          returnText = "Source is not a file";
        } else {
          File destFile = global_fs->open(destPath, FILE_WRITE);
          if (!destFile) {
            returnText = "Failed to create destination";
          } else {
            uint8_t buf[512];
            size_t n;
            while ((n = srcFile.read(buf, sizeof(buf))) > 0) {
              destFile.write(buf, n);
            }
            destFile.close();
          }
          srcFile.close();
        }
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

  // Move file
  else if (command.startsWith("mv ")) {
    pocketmage::setCpuSpeed(240);

    String args = command.substring(3);
    args.trim();

    int spaceIdx = args.indexOf(' ');
    if (spaceIdx == -1) {
      returnText = "Usage: mv <src> <dest>";
    } else {
      String src = args.substring(0, spaceIdx);
      String dest = args.substring(spaceIdx + 1);
      src.trim();
      dest.trim();

      String srcPath = src.startsWith("/") ? src : (currentDir + (currentDir.endsWith("/") ? "" : "/") + src);
      String destPath = dest.startsWith("/") ? dest : (currentDir + (currentDir.endsWith("/") ? "" : "/") + dest);

      if (!global_fs->exists(srcPath)) {
        returnText = "Source not found";
      } else {
        // Try fast rename first
        if (!global_fs->rename(srcPath, destPath)) {
          // Fallback: copy + delete
          File srcFile = global_fs->open(srcPath, FILE_READ);
          if (!srcFile || srcFile.isDirectory()) {
            returnText = "Source is not a file";
          } else {
            File destFile = global_fs->open(destPath, FILE_WRITE);
            if (!destFile) {
              returnText = "Failed to create destination";
            } else {
              uint8_t buf[512];
              size_t n;
              while ((n = srcFile.read(buf, sizeof(buf))) > 0) {
                destFile.write(buf, n);
              }
              destFile.close();
              global_fs->remove(srcPath);
            }
            srcFile.close();
          }
        }
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

  // Create empty file (touch)
  else if (command.startsWith("touch ")) {
    pocketmage::setCpuSpeed(240);

    String arg = command.substring(6);
    arg.trim();

    if (arg.length() == 0) {
      returnText = "Usage: touch <file>";
    } else {
      String filePath = arg.startsWith("/")
        ? arg
        : (currentDir + (currentDir.endsWith("/") ? "" : "/") + arg);

      if (global_fs->exists(filePath)) {
        File f = global_fs->open(filePath);
        if (f && f.isDirectory()) {
          returnText = "Is a directory";
        }
        if (f) f.close();
      } else {
        File f = global_fs->open(filePath, FILE_WRITE);
        if (!f) {
          returnText = "Failed to create file";
        } else {
          f.close();
        }
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

  #pragma region Fallback
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