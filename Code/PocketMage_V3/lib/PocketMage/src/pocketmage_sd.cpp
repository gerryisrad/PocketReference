//  .d88888b  888888ba   //
//  88.    "' 88    `8b  //
//  `Y88888b. 88     88  //
//        `8b 88     88  //
//  d8'   .8P 88    .8P  //
//   Y88888P  8888888P   //
#pragma region COMMON

#include <pocketmage.h>
#include <globals.h>
#include <config.h> // for FULL_REFRESH_AFTER
#include <SD_MMC.h>
#include <SD.h>
#include <SPI.h>

static constexpr const char* TAG = "SD";

extern bool SAVE_POWER;

// Initialization of sd classes
static PocketmageSDMMC pm_sdmmc;
static PocketmageSDSPI pm_sdspi;
static PocketmageSDAUTO pm_sdauto;

// Helpers
static int countVisibleChars(String input) {
  int count = 0;

  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    // Check if the character is a visible character or space
    if (c >= 32 && c <= 126) {  // ASCII range for printable characters and space
      count++;
    }
  }

  return count;
}

// Setup for SD Class
// @ dependencies:
//   - setupOled()
//   - setupBZ()
//   - setupEINK()
void setupSD() {
  // ---------- File templates ----------
  static const char* GUIDE_BACKGROUND =
    "How to add custom backgrounds:\n"
    "1. Make a background that is 1 bit (black OR white) and 320x240 pixels.\n"
    "2. Export your background as a .bmp file.\n"
    "3. Use image2cpp to convert your image to a .bin file.\n"
    "   Settings: Invert Image Colors = TRUE, Swap Bits in Byte = FALSE.\n"
    "4. Place the .bin file in this folder.\n"
    "5. Enjoy your new custom wallpapers!";

  static const char* GUIDE_COMMANDS =
    "# PocketMage Keystrokes Guide\n"
    "This is a guide on common key combinations and commands on the PocketMage PDA device. "
    "The guide is split up into sections based on application.\n"
    "\n"
    "---\n"
    "## General Keystrokes (work in almost any app)\n"
    "- (FN) + ( < ) | Exit or back button\n"
    "- (FN) + ( > ) | Save document\n"
    "- (FN) + ( o ) | Clear Line\n"
    "- (FN) + (Key) | FN layer keymapping (legends on the PCB)\n"
    "- (SHFT) + (key) | Capital letter\n"
    "- ( o ) OR (ENTER) | Select button\n"
    "\n"
    "---\n"
    "## While Sleeping\n"
    "... (shortened for brevity) ...\n"; // keep all content or shorten as needed

  // ---------- SDMMC mode ----------
  if (!SD_SPI_COMPATIBILITY) {
    pocketmage::setCpuSpeed(240);
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);

    bool sdOK = false;
    bool startedSD = false;
    sdcard_type_t cardType = CARD_NONE;
    for (int attempt = 1; attempt <= 25; attempt++) {
        if (SD_MMC.begin("/sdcard", true)) {
            startedSD = true;
            delay(120); 
            cardType = SD_MMC.cardType();
            if (cardType != CARD_NONE) {
                sdOK = true;
                break;
            }
        }
        SD_MMC.end();
        delay(200);
    }

    if (!sdOK) {
        ESP_LOGE(TAG, "MOUNT FAILED");
        if (startedSD) {
            OLED().oledWord(
                String("SD Not Detected! [") +
                (cardType == CARD_MMC  ? "MMC"  :
                  cardType == CARD_SD   ? "SD"   :
                  cardType == CARD_SDHC ? "SDHC" :
                                          "NONE") + "]",
                false, false
            );
        } else {
          OLED().oledWord("SD Not Detected! [START_FAIL]", false, false);
        }

        delay(5000);
        if (ALLOW_NO_MICROSD) {
          OLED().oledWord("All Work Will Be Lost!", false, false);
          delay(5000);
          PM_SDMMC().setNoSD(true);
          return;
        } else {
          OLED().oledWord("Insert SD Card and Reboot!", false, false);
          delay(5000);
          OLED().setPowerSave(1);
          BZ().playJingle(Jingles::Shutdown);
          esp_deep_sleep_start();
          return;
        }
    }

    // ---------- Filesystem setup ----------
    const char* dirs[] = {"/sys", "/notes", "/journal", "/dict", "/apps",
                          "/apps/temp", "/assets", "/assets/backgrounds"};
    for (auto dir : dirs) if (!SD_MMC.exists(dir)) SD_MMC.mkdir(dir);

    // Create system guides
    if (!SD_MMC.exists("/assets/backgrounds/HOWTOADDBACKGROUNDS.txt")) {
      File f = SD_MMC.open("/assets/backgrounds/HOWTOADDBACKGROUNDS.txt", FILE_WRITE);
      if (f) { f.print(GUIDE_BACKGROUND); f.close(); }
    }

    if (!SD_MMC.exists("/sys/COMMAND_MANUAL.txt")) {
      File f = SD_MMC.open("/sys/COMMAND_MANUAL.txt", FILE_WRITE);
      if (f) { f.print(GUIDE_COMMANDS); f.close(); }
    }

    // Ensure system files exist
    const char* sysFiles[] = {"/sys/events.txt", "/sys/tasks.txt", "/sys/SDMMC_META.txt"};
    for (auto file : sysFiles) {
      if (!SD_MMC.exists(file)) {
        File f = SD_MMC.open(file, FILE_WRITE);
        if (f) f.close();
      }
    }
  }

  // ---------- SDSPI mode ----------
  else {
      pocketmage::setCpuSpeed(240);

      hspi = new SPIClass(HSPI);
      hspi->begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
      pinMode(hspi->pinSS(), OUTPUT);  //HSPI SS
      if (!SD.begin(SD_CS, *hspi, 40000000)) { // adjust SPI frequency as needed
          ESP_LOGE(TAG, "SPI SD Mount Failed");
          OLED().oledWord("SPI SD Not Detected!", false, false);
          delay(5000);

          if (ALLOW_NO_MICROSD) {
              OLED().oledWord("All Work Will Be Lost!", false, false);
              delay(5000);
              PM_SDSPI().setNoSD(true);
              return;
          } else {
              OLED().oledWord("Insert SD Card and Reboot!", false, false);
              delay(5000);
              OLED().setPowerSave(1);
              BZ().playJingle(Jingles::Shutdown);
              esp_deep_sleep_start();
              return;
          }
      }
      OLED().oledWord("SD Started In Compatibility Mode", false, false);
      delay(2000);

      // ---------- Filesystem setup ----------
      const char* dirs[] = {"/sys", "/notes", "/journal", "/dict", "/apps",
                            "/apps/temp", "/assets", "/assets/backgrounds"};
      for (auto dir : dirs) if (!SD.exists(dir)) SD.mkdir(dir);

      // Create system guides
      if (!SD.exists("/assets/backgrounds/HOWTOADDBACKGROUNDS.txt")) {
          File f = SD.open("/assets/backgrounds/HOWTOADDBACKGROUNDS.txt", FILE_WRITE);
          if (f) { f.print(GUIDE_BACKGROUND); f.close(); }
      }

      if (!SD.exists("/sys/COMMAND_MANUAL.txt")) {
          File f = SD.open("/sys/COMMAND_MANUAL.txt", FILE_WRITE);
          if (f) { f.print(GUIDE_COMMANDS); f.close(); }
      }

      // Ensure system files exist
      const char* sysFiles[] = {"/sys/events.txt", "/sys/tasks.txt", "/sys/SDMMC_META.txt"};
      for (auto file : sysFiles) {
          if (!SD.exists(file)) {
              File f = SD.open(file, FILE_WRITE);
              if (f) f.close();
          }
      }
  }
}

// SDAUTO decides automatically between SPI and SDMMC based on which works for a specific card.
#pragma region SDAUTO
PocketmageSDAUTO& PM_SDAUTO() { return pm_sdauto; }

void PocketmageSDAUTO::saveFile() {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().saveFile();
  else PM_SDMMC().saveFile();
}
void PocketmageSDAUTO::writeMetadata(const String& path) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().writeMetadata(path);
  else PM_SDMMC().writeMetadata(path);
}
void PocketmageSDAUTO::loadFile(bool showOLED) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().loadFile(showOLED);
  else PM_SDMMC().loadFile(showOLED);
}
void PocketmageSDAUTO::delFile(String fileName) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().delFile(fileName);
  else PM_SDMMC().delFile(fileName);
}
void PocketmageSDAUTO::deleteMetadata(String path) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().deleteMetadata(path);
  else PM_SDMMC().deleteMetadata(path);
}
void PocketmageSDAUTO::renFile(String oldFile, String newFile) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().renFile(oldFile, newFile);
  else PM_SDMMC().renFile(oldFile, newFile);
}
void PocketmageSDAUTO::renMetadata(String oldPath, String newPath) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().renMetadata(oldPath, newPath);
  else PM_SDMMC().renMetadata(oldPath, newPath);
}
void PocketmageSDAUTO::copyFile(String oldFile, String newFile) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().copyFile(oldFile, newFile);
  else PM_SDMMC().copyFile(oldFile, newFile);
}
void PocketmageSDAUTO::appendToFile(String path, String inText) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().appendToFile(path, inText);
  else PM_SDMMC().appendToFile(path, inText);
}

// ===================== low level functions =====================
void PocketmageSDAUTO::listDir(fs::FS &fs, const char *dirname) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().listDir(fs, dirname);
  else PM_SDMMC().listDir(fs, dirname);
}
void PocketmageSDAUTO::readFile(fs::FS &fs, const char *path) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().readFile(fs, path);
  else PM_SDMMC().readFile(fs, path);
}
String PocketmageSDAUTO::readFileToString(fs::FS &fs, const char *path) {
  if (SD_SPI_COMPATIBILITY) return PM_SDSPI().readFileToString(fs, path);
  return PM_SDMMC().readFileToString(fs, path);
}
void PocketmageSDAUTO::writeFile(fs::FS &fs, const char *path, const char *message) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().writeFile(fs, path, message);
  else PM_SDMMC().writeFile(fs, path, message);
}
void PocketmageSDAUTO::appendFile(fs::FS &fs, const char *path, const char *message) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().appendFile(fs, path, message);
  else PM_SDMMC().appendFile(fs, path, message);
}
void PocketmageSDAUTO::renameFile(fs::FS &fs, const char *path1, const char *path2) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().renameFile(fs, path1, path2);
  else PM_SDMMC().renameFile(fs, path1, path2);
}
void PocketmageSDAUTO::deleteFile(fs::FS &fs, const char *path) {
  if (SD_SPI_COMPATIBILITY) PM_SDSPI().deleteFile(fs, path);
  else PM_SDMMC().deleteFile(fs, path);
}
bool PocketmageSDAUTO::readBinaryFile(const char* path, uint8_t* buf, size_t len) {
  if (SD_SPI_COMPATIBILITY) return PM_SDSPI().readBinaryFile(path, buf, len);
  return PM_SDMMC().readBinaryFile(path, buf, len);
}
size_t PocketmageSDAUTO::getFileSize(const char* path) {
  if (SD_SPI_COMPATIBILITY) return PM_SDSPI().getFileSize(path);
  return PM_SDMMC().getFileSize(path);
}
#pragma endregion


// SDMMC is Espressif's built-in hardware for SD cards on ESP32
#pragma region SDMMC
// Access for other apps
PocketmageSDMMC& PM_SDMMC() { return pm_sdmmc; }
    
void PocketmageSDMMC::saveFile() {
  if (PM_SDMMC().getNoSD()) {
      OLED().oledWord("SAVE FAILED - No SD!");
      delay(5000);
      return;
  } else {
      SDActive = true;
      pocketmage::setCpuSpeed(240);
      delay(50);

      String textToSave = vectorToString();
      ESP_LOGV(TAG, "Text to save: %s", textToSave.c_str());

      if (PM_SDMMC().getEditingFile() == "" || PM_SDMMC().getEditingFile() == "-")
      PM_SDMMC().setEditingFile("/temp.txt");
      keypad.disableInterrupts();
      if (!PM_SDMMC().getEditingFile().startsWith("/"))
      PM_SDMMC().setEditingFile("/" + PM_SDMMC().getEditingFile());
      //OLED().oledWord("Saving File: "+ editingFile);
      PM_SDMMC().writeFile(SD_MMC, (PM_SDMMC().getEditingFile()).c_str(), textToSave.c_str());
      //OLED().oledWord("Saved: "+ editingFile);

      // Write MetaData
      PM_SDMMC().writeMetadata(PM_SDMMC().getEditingFile());

      // delay(1000);
      keypad.enableInterrupts();
      if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
      SDActive = false;
  }
}  
void PocketmageSDMMC::writeMetadata(const String& path) {
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  File file = SD_MMC.open(path);
  if (!file || file.isDirectory()) {
      OLED().oledWord("META WRITE ERR");
      delay(1000);
      ESP_LOGE(TAG, "Invalid file for metadata: %s", path);
      return;
  }
  // Get file size
  size_t fileSizeBytes = file.size();
  file.close();

  // Format size string
  String fileSizeStr = String(fileSizeBytes) + " Bytes";

  // Get line and char counts
  int charCount = countVisibleChars(PM_SDMMC().readFileToString(SD_MMC, path.c_str()));

  String charStr = String(charCount) + " Char";
  // Get current time from RTC
  DateTime now = CLOCK().nowDT();
  char timestamp[20];
  sprintf(timestamp, "%04d%02d%02d-%02d%02d", now.year(), now.month(), now.day(), now.hour(),
          now.minute());

  // Compose new metadata line
  String newEntry = path + "|" + timestamp + "|" + fileSizeStr + "|" + charStr;

  const char* metaPath = SYS_METADATA_FILE;
  // Read existing entries and rebuild the file without duplicates
  File metaFile = SD_MMC.open(metaPath, FILE_READ);
  String updatedMeta = "";
  bool replaced = false;

  if (metaFile) {
      while (metaFile.available()) {
      String line = metaFile.readStringUntil('\n');
      if (line.startsWith(path + "|")) {
          updatedMeta += newEntry + "\n";
          replaced = true;
      } else if (line.length() > 1) {
          updatedMeta += line + "\n";
      }
      }
      metaFile.close();
  }

  if (!replaced) {
      updatedMeta += newEntry + "\n";
  }
  // Write back the updated metadata
  metaFile = SD_MMC.open(metaPath, FILE_WRITE);
  if (!metaFile) {
      ESP_LOGE(TAG, "Failed to open metadata file for writing: %s", metaPath);
      return;
  }
  metaFile.print(updatedMeta);
  metaFile.close();
  ESP_LOGI(TAG, "Metadata updated");

  if (SAVE_POWER)
  pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  SDActive = false;
}  
void PocketmageSDMMC::loadFile(bool showOLED) {
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  if (PM_SDMMC().getNoSD()) {
      OLED().oledWord("LOAD FAILED - No SD!");
      delay(5000);
      return;
  } else {
      SDActive = true;
      pocketmage::setCpuSpeed(240);
      delay(50);

      keypad.disableInterrupts();
      if (showOLED)
      OLED().oledWord("Loading File");
      if (!PM_SDMMC().getEditingFile().startsWith("/"))
      PM_SDMMC().setEditingFile("/" + PM_SDMMC().getEditingFile());
      String textToLoad = PM_SDMMC().readFileToString(SD_MMC, (PM_SDMMC().getEditingFile()).c_str());
      ESP_LOGV(TAG, "Text to load: %s", textToLoad.c_str());

      stringToVector(textToLoad);
      keypad.enableInterrupts();
      if (showOLED) {
      OLED().oledWord("File Loaded");
      delay(200);
      }
      if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
      SDActive = false;
  }
}  
void PocketmageSDMMC::delFile(String fileName) {
  if (PM_SDMMC().getNoSD()) {
      OLED().oledWord("DELETE FAILED - No SD!");
      delay(5000);
      return;
  } else {
      SDActive = true;
      pocketmage::setCpuSpeed(240);
      delay(50);

      keypad.disableInterrupts();
      // OLED().oledWord("Deleting File: "+ fileName);
      if (!fileName.startsWith("/"))
      fileName = "/" + fileName;
      PM_SDMMC().deleteFile(SD_MMC, fileName.c_str());
      // OLED().oledWord("Deleted: "+ fileName);

      // Delete MetaData
      PM_SDMMC().deleteMetadata(fileName);

      delay(1000);
      keypad.enableInterrupts();
      if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
      SDActive = false;
  }
}  
void PocketmageSDMMC::deleteMetadata(String path) {
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  const char* metaPath = SYS_METADATA_FILE;

  // Open metadata file for reading
  File metaFile = SD_MMC.open(metaPath, FILE_READ);
  if (!metaFile) {
      ESP_LOGE(TAG, "Metadata file not found: %s", metaPath);
      return;
  }

  // Store lines that don't match the given path
  std::vector<String> keptLines;
  while (metaFile.available()) {
      String line = metaFile.readStringUntil('\n');
      if (!line.startsWith(path + "|")) {
      keptLines.push_back(line);
      }
  }
  metaFile.close();

  // Delete the original metadata file
  SD_MMC.remove(metaPath);

  // Recreate the file and write back the kept lines
  File writeFile = SD_MMC.open(metaPath, FILE_WRITE);
  if (!writeFile) {
      ESP_LOGE(TAG, "Failed to recreate metadata file. %s", writeFile.path());
      return;
  }

  for (const String& line : keptLines) {
      writeFile.println(line);
  }

  writeFile.close();
  ESP_LOGI(TAG, "Metadata entry deleted (if it existed).");
}  
void PocketmageSDMMC::renFile(String oldFile, String newFile) {
  if (PM_SDMMC().getNoSD()) {
      OLED().oledWord("RENAME FAILED - No SD!");
      delay(5000);
      return;
  } else {
      SDActive = true;
      pocketmage::setCpuSpeed(240);
      delay(50);

      keypad.disableInterrupts();
      // OLED().oledWord("Renaming "+ oldFile + " to " + newFile);
      if (!oldFile.startsWith("/"))
      oldFile = "/" + oldFile;
      if (!newFile.startsWith("/"))
      newFile = "/" + newFile;
      PM_SDMMC().renameFile(SD_MMC, oldFile.c_str(), newFile.c_str());
      OLED().oledWord(oldFile + " -> " + newFile);
      delay(1000);

      // Update MetaData
      PM_SDMMC().renMetadata(oldFile, newFile);

      keypad.enableInterrupts();
      if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
      SDActive = false;
  }
}  
void PocketmageSDMMC::renMetadata(String oldPath, String newPath) {
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);
  const char* metaPath = SYS_METADATA_FILE;

  // Open metadata file for reading
  File metaFile = SD_MMC.open(metaPath, FILE_READ);
  if (!metaFile) {
      ESP_LOGE(TAG, "Metadata file not found: %s", metaPath);
      return;
  }

  std::vector<String> updatedLines;

  while (metaFile.available()) {
      String line = metaFile.readStringUntil('\n');
      if (line.startsWith(oldPath + "|")) {
      // Replace old path with new path at the start of the line
      int separatorIndex = line.indexOf('|');
      if (separatorIndex != -1) {
          // Keep rest of line after '|'
          String rest = line.substring(separatorIndex);
          line = newPath + rest;
      } else {
          // Just replace whole line with new path if malformed
          line = newPath;
      }
      }
      updatedLines.push_back(line);
  }

  metaFile.close();

  // Delete old metadata file
  SD_MMC.remove(metaPath);

  // Recreate file and write updated lines
  File writeFile = SD_MMC.open(metaPath, FILE_WRITE);
  if (!writeFile) {
      ESP_LOGE(TAG, "Failed to recreate metadata file. %s", writeFile.path());
      return;
  }

  for (const String& l : updatedLines) {
      writeFile.println(l);
  }

  writeFile.close();
  ESP_LOGI(TAG, "Metadata updated for renamed file.");

  if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
} 
void PocketmageSDMMC::copyFile(String oldFile, String newFile) {
  if (PM_SDMMC().getNoSD()) {
      OLED().oledWord("COPY FAILED - No SD!");
      delay(5000);
      return;
  } else {
      SDActive = true;
      pocketmage::setCpuSpeed(240);
      delay(50);

      keypad.disableInterrupts();
      OLED().oledWord("Loading File");
      if (!oldFile.startsWith("/"))
      oldFile = "/" + oldFile;
      if (!newFile.startsWith("/"))
      newFile = "/" + newFile;
      String textToLoad = PM_SDMMC().readFileToString(SD_MMC, (oldFile).c_str());
      PM_SDMMC().writeFile(SD_MMC, (newFile).c_str(), textToLoad.c_str());
      OLED().oledWord("Saved: " + newFile);

      // Write MetaData
      PM_SDMMC().writeMetadata(newFile);

      delay(1000);
      keypad.enableInterrupts();

      if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
      SDActive = false;
  }
} 
void PocketmageSDMMC::appendToFile(String path, String inText) {
  if (PM_SDMMC().getNoSD()) {
      OLED().oledWord("OP FAILED - No SD!");
      delay(5000);
      return;
  } else {
      SDActive = true;
      pocketmage::setCpuSpeed(240);
      delay(50);

      keypad.disableInterrupts();
      PM_SDMMC().appendFile(SD_MMC, path.c_str(), inText.c_str());

      // Write MetaData
      PM_SDMMC().writeMetadata(path);

      keypad.enableInterrupts();

      if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
      SDActive = false;
  }
}

// ===================== low level functions =====================
// Low-Level SDMMC Operations switch to using internal fs::FS*
void PocketmageSDMMC::listDir(fs::FS &fs, const char *dirname) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Listing directory %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open directory: %s", root.path());
      return;
    }
    if (!root.isDirectory()) {
      noTimeout = false;
      ESP_LOGE(tag, "Not a directory: %s", root.path());
      
      return;
    }

    // Reset fileIndex and initialize filesList with "-"
    fileIndex_ = 0; // Reset fileIndex
    for (int i = 0; i < MAX_FILES; i++) {
      filesList_[i] = "-";
    }

    File file = root.openNextFile();
    while (file && fileIndex_ < MAX_FILES) {
      if (!file.isDirectory()) {
        String fileName = String(file.name());
        
        // Check if file is in the exclusion list
        bool excluded = false;
        for (const String &excludedFile : excludedFiles_) {
          if (fileName.equals(excludedFile) || ("/"+fileName).equals(excludedFile)) {
            excluded = true;
            break;
          }
        }

        if (!excluded) {
          filesList_[fileIndex_++] = fileName; // Store file name if not excluded
        }
      }
      file = root.openNextFile();
    }

    // for (int i = 0; i < fileIndex_; i++) { // Only print valid entries
    //   Serial.println(filesList_[i]);       // NOTE: This prints out valid files
    // }

    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDMMC::readFile(fs::FS &fs, const char *path) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Reading file %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open file for reading: %s", file.path());
      return;
    }

    file.close();
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
String PocketmageSDMMC::readFileToString(fs::FS &fs, const char *path) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return "";
  }
  else { 
    pocketmage::setCpuSpeed(240);
    delay(50);

    noTimeout = true;
    ESP_LOGI(tag, "Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open file for reading: %s", path);
      OLED().oledWord("Load Failed");
      delay(500);
      return "";  // Return an empty string on failure
    }

    ESP_LOGI(tag, "Reading from file: %s", file.path());
    String content = file.readString();

    file.close();
    EINK().setFullRefreshAfter(FULL_REFRESH_AFTER); //Force a full refresh
    noTimeout = false;
    return content;  // Return the complete String
  }
}
void PocketmageSDMMC::writeFile(fs::FS &fs, const char *path, const char *message) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Writing file: %s\r\n", path);
    delay(200);

    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open %s for writing", path);
      return;
    }
    if (file.print(message)) {
      ESP_LOGV(tag, "File written %s", path);
    } 
    else {
      ESP_LOGE(tag, "Write failed for %s", path);
    }
    file.close();
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDMMC::appendFile(fs::FS &fs, const char *path, const char *message) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open for appending: %s", path);
      return;
    }
    if (file.println(message)) {
      ESP_LOGV(tag, "Message appended to %s", path);
    } 
    else {
      ESP_LOGE(tag, "Append failed: %s", path);
    }
    file.close();
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDMMC::renameFile(fs::FS &fs, const char *path1, const char *path2) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Renaming file %s to %s\r\n", path1, path2);

    if (fs.rename(path1, path2)) {
      ESP_LOGV(tag, "Renamed %s to %s\r\n", path1, path2);
    } 
    else {
      ESP_LOGE(tag, "Rename failed: %s to %s", path1, path2);
    }
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDMMC::deleteFile(fs::FS &fs, const char *path) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Deleting file: %s\r\n", path);
    if (fs.remove(path)) {
      ESP_LOGV(tag, "File deleted: %s", path);
    } 
    else {
      ESP_LOGE(tag, "Delete failed for %s", path);
    }
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
bool PocketmageSDMMC::readBinaryFile(const char* path, uint8_t* buf, size_t len) {
  if (noSD_) {
      OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return false;
  }

  setCpuFrequencyMhz(240  );
  if (noTimeout)
    noTimeout = true;
    
  File f = SD_MMC.open(path, "r");
  if (!f || f.isDirectory()) {
    if (noTimeout)
      noTimeout = false;
    ESP_LOGE(tag, "Failed to open file: %s", path);
    return false;
  }

  size_t n = f.read(buf, len);
  f.close();

  if (noTimeout)
    noTimeout = false;
  if (SAVE_POWER)
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

  return n == len;
}
size_t PocketmageSDMMC::getFileSize(const char* path) {
  if (noSD_)
    return 0;

  File f = SD_MMC.open(path, "r");
  if (!f)
    return 0;
  size_t size = f.size();
  f.close();
  return size;
}
#pragma endregion


// SDSPI uses standard SPI communication for compatibility
#pragma region SDSPI
// Access for other apps
PocketmageSDSPI& PM_SDSPI() { return pm_sdspi; }

void PocketmageSDSPI::saveFile() {
  if (getNoSD()) {
    OLED().oledWord("SAVE FAILED - No SD!");
    delay(5000);
    return;
  }

  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  String textToSave = vectorToString();

  if (getEditingFile().isEmpty() || getEditingFile() == "-")
    setEditingFile("/temp.txt");

  keypad.disableInterrupts();

  if (!getEditingFile().startsWith("/"))
    setEditingFile("/" + getEditingFile());

  writeFile(SD, getEditingFile().c_str(), textToSave.c_str());
  writeMetadata(getEditingFile());

  keypad.enableInterrupts();

  if (SAVE_POWER)
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

  SDActive = false;
}
void PocketmageSDSPI::writeMetadata(const String& path) {
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  File file = SD.open(path);
  if (!file || file.isDirectory()) {
    OLED().oledWord("META WRITE ERR");
    delay(1000);
    ESP_LOGE(TAG, "Invalid file for metadata: %s", path.c_str());
    return;
  }

  // Get file size
  size_t fileSizeBytes = file.size();
  file.close();

  // Format size string
  String fileSizeStr = String(fileSizeBytes) + " Bytes";

  // Get line and char counts
  int charCount =
      countVisibleChars(PM_SDSPI().readFileToString(SD, path.c_str()));
  String charStr = String(charCount) + " Char";

  // Get current time from RTC
  DateTime now = CLOCK().nowDT();
  char timestamp[20];
  sprintf(timestamp, "%04d%02d%02d-%02d%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute());

  // Compose new metadata line
  String newEntry = path + "|" + timestamp + "|" + fileSizeStr + "|" + charStr;

  const char* metaPath = SYS_METADATA_FILE;

  // Read existing metadata
  File metaFile = SD.open(metaPath, FILE_READ);
  String updatedMeta;
  bool replaced = false;

  if (metaFile) {
    while (metaFile.available()) {
      String line = metaFile.readStringUntil('\n');
      if (line.startsWith(path + "|")) {
        updatedMeta += newEntry + "\n";
        replaced = true;
      } else if (line.length() > 1) {
        updatedMeta += line + "\n";
      }
    }
    metaFile.close();
  }

  if (!replaced) {
    updatedMeta += newEntry + "\n";
  }

  // Write back metadata
  metaFile = SD.open(metaPath, FILE_WRITE);
  if (!metaFile) {
    ESP_LOGE(TAG, "Failed to open metadata file for writing: %s", metaPath);
    return;
  }

  metaFile.print(updatedMeta);
  metaFile.close();
  ESP_LOGI(TAG, "Metadata updated");

  if (SAVE_POWER)
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

  SDActive = false;
}
void PocketmageSDSPI::loadFile(bool showOLED) {
  if (getNoSD()) {
    OLED().oledWord("LOAD FAILED - No SD!");
    delay(5000);
    return;
  }

  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  keypad.disableInterrupts();

  if (showOLED)
    OLED().oledWord("Loading File");

  if (!getEditingFile().startsWith("/"))
    setEditingFile("/" + getEditingFile());

  String text = readFileToString(SD, getEditingFile().c_str());
  stringToVector(text);

  keypad.enableInterrupts();

  if (showOLED) {
    OLED().oledWord("File Loaded");
    delay(200);
  }

  if (SAVE_POWER)
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

  SDActive = false;
}
void PocketmageSDSPI::delFile(String fileName) {
  if (PM_SDSPI().getNoSD()) {
    OLED().oledWord("DELETE FAILED - No SD!");
    delay(5000);
    return;
  } else {
    SDActive = true;
    pocketmage::setCpuSpeed(240);
    delay(50);

    keypad.disableInterrupts();
    // OLED().oledWord("Deleting File: " + fileName);

    if (!fileName.startsWith("/"))
      fileName = "/" + fileName;

    PM_SDSPI().deleteFile(SD, fileName.c_str());

    // Delete metadata
    PM_SDSPI().deleteMetadata(fileName);

    delay(1000);
    keypad.enableInterrupts();

    if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

    SDActive = false;
  }
}
void PocketmageSDSPI::deleteMetadata(String path) {
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  const char* metaPath = SYS_METADATA_FILE;

  // Open metadata file for reading
  File metaFile = SD.open(metaPath, FILE_READ);
  if (!metaFile) {
    ESP_LOGE(TAG, "Metadata file not found: %s", metaPath);
    return;
  }

  // Store lines that don't match the given path
  std::vector<String> keptLines;
  while (metaFile.available()) {
    String line = metaFile.readStringUntil('\n');
    if (!line.startsWith(path + "|")) {
      keptLines.push_back(line);
    }
  }
  metaFile.close();

  // Delete original metadata file
  SD.remove(metaPath);

  // Recreate file and write kept lines
  File writeFile = SD.open(metaPath, FILE_WRITE);
  if (!writeFile) {
    ESP_LOGE(TAG, "Failed to recreate metadata file. %s", writeFile.path());
    return;
  }

  for (const String& line : keptLines) {
    writeFile.println(line);
  }

  writeFile.close();
  ESP_LOGI(TAG, "Metadata entry deleted (if it existed).");
}
void PocketmageSDSPI::renFile(String oldFile, String newFile) {
  if (PM_SDSPI().getNoSD()) {
    OLED().oledWord("RENAME FAILED - No SD!");
    delay(5000);
    return;
  } else {
    SDActive = true;
    pocketmage::setCpuSpeed(240);
    delay(50);

    keypad.disableInterrupts();

    if (!oldFile.startsWith("/"))
      oldFile = "/" + oldFile;
    if (!newFile.startsWith("/"))
      newFile = "/" + newFile;

    if (SD.rename(oldFile.c_str(), newFile.c_str())) {
      OLED().oledWord(oldFile + " -> " + newFile);
      delay(1000);

      // Update metadata
      PM_SDSPI().renMetadata(oldFile, newFile);
    } else {
      ESP_LOGE(TAG, "Rename failed: %s -> %s", oldFile.c_str(), newFile.c_str());
      OLED().oledWord("RENAME FAILED");
      delay(1000);
    }

    keypad.enableInterrupts();
    if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    SDActive = false;
  }
}
void PocketmageSDSPI::renMetadata(String oldPath, String newPath) {
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  const char* metaPath = SYS_METADATA_FILE;

  // Open metadata file for reading
  File metaFile = SD.open(metaPath, FILE_READ);
  if (!metaFile) {
    ESP_LOGE(TAG, "Metadata file not found: %s", metaPath);
    return;
  }

  std::vector<String> updatedLines;

  while (metaFile.available()) {
    String line = metaFile.readStringUntil('\n');

    if (line.startsWith(oldPath + "|")) {
      int separatorIndex = line.indexOf('|');
      if (separatorIndex != -1) {
        String rest = line.substring(separatorIndex);
        line = newPath + rest;
      } else {
        line = newPath;
      }
    }

    updatedLines.push_back(line);
  }

  metaFile.close();

  // Delete old metadata file
  SD.remove(metaPath);

  // Recreate file and write updated lines
  File writeFile = SD.open(metaPath, FILE_WRITE);
  if (!writeFile) {
    ESP_LOGE(TAG, "Failed to recreate metadata file: %s", metaPath);
    return;
  }

  for (const String& l : updatedLines) {
    writeFile.println(l);
  }

  writeFile.close();
  ESP_LOGI(TAG, "Metadata updated for renamed file.");

  if (SAVE_POWER)
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
}
void PocketmageSDSPI::copyFile(String oldFile, String newFile) {
  if (PM_SDSPI().getNoSD()) {
    OLED().oledWord("COPY FAILED - No SD!");
    delay(5000);
    return;
  } else {
    SDActive = true;
    pocketmage::setCpuSpeed(240);
    delay(50);

    keypad.disableInterrupts();
    OLED().oledWord("Loading File");

    if (!oldFile.startsWith("/"))
      oldFile = "/" + oldFile;
    if (!newFile.startsWith("/"))
      newFile = "/" + newFile;

    // Read source file
    File src = SD.open(oldFile.c_str(), FILE_READ);
    if (!src || src.isDirectory()) {
      ESP_LOGE(TAG, "Failed to open source file: %s", oldFile.c_str());
      OLED().oledWord("COPY FAILED");
      keypad.enableInterrupts();
      return;
    }

    // Write destination file
    File dst = SD.open(newFile.c_str(), FILE_WRITE);
    if (!dst) {
      ESP_LOGE(TAG, "Failed to open destination file: %s", newFile.c_str());
      src.close();
      OLED().oledWord("COPY FAILED");
      keypad.enableInterrupts();
      return;
    }

    while (src.available()) {
      dst.write(src.read());
    }

    src.close();
    dst.close();

    OLED().oledWord("Saved: " + newFile);

    // Write metadata
    PM_SDSPI().writeMetadata(newFile);

    delay(1000);
    keypad.enableInterrupts();

    if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    SDActive = false;
  }
}
void PocketmageSDSPI::appendToFile(String path, String inText) {
  if (getNoSD()) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }

  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  keypad.disableInterrupts();

  if (!path.startsWith("/"))
    path = "/" + path;

  File file = SD.open(path.c_str(), FILE_APPEND);
  if (!file) {
    OLED().oledWord("APPEND FAILED");
    keypad.enableInterrupts();
    SDActive = false;
    if (SAVE_POWER)
      pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    return;
  }

  file.print(inText);
  file.close();

  // Write MetaData
  writeMetadata(path);

  keypad.enableInterrupts();

  if (SAVE_POWER)
    pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

  SDActive = false;
}

// ===================== low level functions =====================
// Low-Level SDMMC Operations switch to using internal fs::FS*
void PocketmageSDSPI::listDir(fs::FS &fs, const char *dirname) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Listing directory %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open directory: %s", root.path());
      return;
    }
    if (!root.isDirectory()) {
      noTimeout = false;
      ESP_LOGE(tag, "Not a directory: %s", root.path());
      
      return;
    }

    // Reset fileIndex and initialize filesList with "-"
    fileIndex_ = 0; // Reset fileIndex
    for (int i = 0; i < MAX_FILES; i++) {
      filesList_[i] = "-";
    }

    File file = root.openNextFile();
    while (file && fileIndex_ < MAX_FILES) {
      if (!file.isDirectory()) {
        String fileName = String(file.name());
        
        // Check if file is in the exclusion list
        bool excluded = false;
        for (const String &excludedFile : excludedFiles_) {
          if (fileName.equals(excludedFile) || ("/"+fileName).equals(excludedFile)) {
            excluded = true;
            break;
          }
        }

        if (!excluded) {
          filesList_[fileIndex_++] = fileName; // Store file name if not excluded
        }
      }
      file = root.openNextFile();
    }

    // for (int i = 0; i < fileIndex_; i++) { // Only print valid entries
    //   Serial.println(filesList_[i]);       // NOTE: This prints out valid files
    // }

    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDSPI::readFile(fs::FS &fs, const char *path) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Reading file %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open file for reading: %s", file.path());
      return;
    }

    file.close();
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
String PocketmageSDSPI::readFileToString(fs::FS &fs, const char *path) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return "";
  }
  else { 
    pocketmage::setCpuSpeed(240);
    delay(50);

    noTimeout = true;
    ESP_LOGI(tag, "Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory()) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open file for reading: %s", path);
      OLED().oledWord("Load Failed");
      delay(500);
      return "";  // Return an empty string on failure
    }

    ESP_LOGI(tag, "Reading from file: %s", file.path());
    String content = file.readString();

    file.close();
    EINK().setFullRefreshAfter(FULL_REFRESH_AFTER); //Force a full refresh
    noTimeout = false;
    return content;  // Return the complete String
  }
}
void PocketmageSDSPI::writeFile(fs::FS &fs, const char *path, const char *message) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Writing file: %s\r\n", path);
    delay(200);

    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open %s for writing", path);
      return;
    }
    if (file.print(message)) {
      ESP_LOGV(tag, "File written %s", path);
    } 
    else {
      ESP_LOGE(tag, "Write failed for %s", path);
    }
    file.close();
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDSPI::appendFile(fs::FS &fs, const char *path, const char *message) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file) {
      noTimeout = false;
      ESP_LOGE(tag, "Failed to open for appending: %s", path);
      return;
    }
    if (file.println(message)) {
      ESP_LOGV(tag, "Message appended to %s", path);
    } 
    else {
      ESP_LOGE(tag, "Append failed: %s", path);
    }
    file.close();
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDSPI::renameFile(fs::FS &fs, const char *path1, const char *path2) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Renaming file %s to %s\r\n", path1, path2);

    if (fs.rename(path1, path2)) {
      ESP_LOGV(tag, "Renamed %s to %s\r\n", path1, path2);
    } 
    else {
      ESP_LOGE(tag, "Rename failed: %s to %s", path1, path2);
    }
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
void PocketmageSDSPI::deleteFile(fs::FS &fs, const char *path) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return;
  }
  else {
    pocketmage::setCpuSpeed(240);
    delay(50);
    noTimeout = true;
    ESP_LOGI(tag, "Deleting file: %s\r\n", path);
    if (fs.remove(path)) {
      ESP_LOGV(tag, "File deleted: %s", path);
    } 
    else {
      ESP_LOGE(tag, "Delete failed for %s", path);
    }
    noTimeout = false;
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  }
}
bool PocketmageSDSPI::readBinaryFile(const char* path, uint8_t* buf, size_t len) {
  if (noSD_) {
    OLED().oledWord("OP FAILED - No SD!");
    delay(5000);
    return false;
  }

  setCpuFrequencyMhz(240);
  if (noTimeout) noTimeout = true;

  File f = SD.open(path, FILE_READ);
  if (!f || f.isDirectory()) {
    noTimeout = false;
    ESP_LOGE(tag, "Failed to open file: %s", path);
    return false;
  }

  size_t n = f.read(buf, len);
  f.close();

  noTimeout = false;
  if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);

  return n == len;
}
size_t PocketmageSDSPI::getFileSize(const char* path) {
  if (noSD_)
    return 0;

  File f = SD_MMC.open(path, "r");
  if (!f)
    return 0;
  size_t size = f.size();
  f.close();
  return size;
}
#pragma endregion