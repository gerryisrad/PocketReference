// MechE Bible — PocketMage OTA App
// A mechanical engineering dictionary/indexer for the PocketMage PDA.
// Store .md entries in /meche/entries/ on the SD card.
// Browse, search, read, and create entries on-device.
// Images: place 1-bit BMP files in /meche/images/, reference as ![filename.bmp]

#include <globals.h>
#if OTA_APP

// No STL containers — all static to avoid heap fragmentation on OTA

// ── Configuration ─────────────────────────────────────────────────────────────
#define MAX_ENTRIES         64
#define MAX_NAME_LEN        48
#define DISPLAY_LINE_CAP   256
#define LINES_PER_PAGE      12
#define LINES_PER_CHUNK    100
#define MAX_CHUNKS          32
#define TEXT_POOL_CAP     8192
#define WORD_REF_CAP       512
#define MAX_WORD_LEN        64
#define DISPLAY_WIDTH_BUFFER  8
#define SPECIAL_PADDING      20
#define WORDWIDTH_BUFFER      2
#define HEADING_LINE_PADDING  6
#define NORMAL_LINE_PADDING   3
#define CONTENT_START_Y      18
#define PICKER_VISIBLE       10
#define FILTER_MAX           16
#define EDITOR_MAX_LINES     64
#define EDITOR_LINE_LEN      80
#define SPACEWIDTH_SYMBOL   "M"

// ── App modes ─────────────────────────────────────────────────────────────────
enum AppMode { MODE_BROWSER, MODE_VIEWER, MODE_EDITOR, MODE_CONFIRM };
static AppMode appMode = MODE_BROWSER;

// ── Entry list ────────────────────────────────────────────────────────────────
static char s_entryNames[MAX_ENTRIES][MAX_NAME_LEN];
static int  s_entryCount     = 0;
static int  s_filteredIdx[MAX_ENTRIES];
static int  s_filteredCount  = 0;
static int  s_browserSel     = 0;
static int  s_browserScroll  = 0;
static char s_filter[FILTER_MAX + 1] = "";
static int  s_filterLen      = 0;

// ── Paths ─────────────────────────────────────────────────────────────────────
static const char* const ENTRIES_DIR = "/meche/entries";
static const char* const IMAGES_DIR  = "/meche/images";

static char s_entryPath[128];
static char s_entryDisplayName[MAX_NAME_LEN];

static void setEntryPath(const char* fname) {
  snprintf(s_entryPath, sizeof(s_entryPath), "/meche/entries/%s", fname);
  char base[MAX_NAME_LEN];
  strncpy(base, fname, sizeof(base) - 1);
  base[sizeof(base) - 1] = '\0';
  int len = (int)strlen(base);
  if (len > 3 && strcmp(base + len - 3, ".md") == 0)
    base[len - 3] = '\0';
  // Replace underscores with spaces for display
  for (int i = 0; base[i]; i++)
    if (base[i] == '_') base[i] = ' ';
  strncpy(s_entryDisplayName, base, sizeof(s_entryDisplayName) - 1);
  s_entryDisplayName[sizeof(s_entryDisplayName) - 1] = '\0';
}

// ── Font setup ────────────────────────────────────────────────────────────────
struct FontMap {
  const GFXfont* normal;
  const GFXfont* normal_B;
  const GFXfont* h1;
  const GFXfont* h2;
  const GFXfont* h3;
  const GFXfont* code;
  const GFXfont* list;
};

static FontMap s_fonts;

static void initFonts() {
  s_fonts.normal   = &FreeSerif9pt7b;
  s_fonts.normal_B = &FreeSerifBold9pt7b;
  s_fonts.h1       = &FreeSerif12pt7b;
  s_fonts.h2       = &FreeSerifBold9pt7b;
  s_fonts.h3       = &FreeSerif9pt7b;
  s_fonts.code     = &FreeMonoBold9pt7b;
  s_fonts.list     = &FreeSerif9pt7b;
}

static const GFXfont* pickFont(char style, bool bold) {
  switch (style) {
    case '1': return s_fonts.h1;
    case '2': return s_fonts.h2;
    case '3': return bold ? s_fonts.h2 : s_fonts.h3;
    case 'C': return s_fonts.code;
    case '>': return s_fonts.normal;
    case '-': return s_fonts.list;
    case 'L': return s_fonts.list;
    default:  return bold ? s_fonts.normal_B : s_fonts.normal;
  }
}

// ── Layout pools ──────────────────────────────────────────────────────────────
struct WordRef {
  const char* text;
  bool        bold;
};

struct DisplayLine {
  ulong    lineIdx;
  uint16_t wordStart;
  uint8_t  wordCount;
};

struct SourceLine {
  char     style;
  uint16_t lineStart;
  uint8_t  lineCount;
  ulong    orderedListNum;
};

static char        s_textPool[TEXT_POOL_CAP];
static int         s_textPoolUsed     = 0;
static WordRef     s_wordRefs[WORD_REF_CAP];
static int         s_wordRefsUsed     = 0;
static DisplayLine s_displayLines[DISPLAY_LINE_CAP];
static int         s_displayLinesUsed = 0;
static SourceLine  s_sourceLines[LINES_PER_CHUNK];
static int         s_sourceLinesUsed  = 0;
static ulong       s_lineIndex        = 0;
static ulong       s_pageStartLine    = 0;

// ── Chunk index ───────────────────────────────────────────────────────────────
struct ChunkInfo {
  size_t offset;
  char   heading[48];
};

static ChunkInfo chunks[MAX_CHUNKS];
static int   chunkCount   = 0;
static int   currentChunk = 0;
static ulong pageIndex    = 0;
static bool  needsRedraw  = false;
static bool  fileError    = false;

// ── Editor state ──────────────────────────────────────────────────────────────
static char   s_editorLines[EDITOR_MAX_LINES][EDITOR_LINE_LEN];
static int    s_editorLineCount = 0;
static int    s_editorCursorLine = 0;
static int    s_editorCursorCol  = 0;
static int    s_editorScroll     = 0;
static char   s_editorFilename[MAX_NAME_LEN] = "";
static bool   s_editorDirty = false;

// ── Confirm dialog ────────────────────────────────────────────────────────────
static char   s_confirmMsg[64] = "";
static AppMode s_confirmReturn = MODE_BROWSER;

// ── Helpers ───────────────────────────────────────────────────────────────────
static int getMaxPage() {
  return (s_displayLinesUsed <= 0) ? 0 : (s_displayLinesUsed - 1) / LINES_PER_PAGE;
}

static void seamlessRestart() {
  Preferences prefs;
  prefs.begin("PocketMage", false);
  prefs.putBool("Seamless_Reboot", true);
  prefs.end();
  ESP.restart();
}

// ── Text pool ─────────────────────────────────────────────────────────────────
static const char* internWord(const char* src, int len) {
  int copyLen = (len > MAX_WORD_LEN) ? MAX_WORD_LEN : len;
  if (s_textPoolUsed + copyLen + 1 > TEXT_POOL_CAP) return nullptr;
  char* dst = s_textPool + s_textPoolUsed;
  memcpy(dst, src, copyLen);
  dst[copyLen] = '\0';
  s_textPoolUsed += copyLen + 1;
  return dst;
}

static void commitDisplayLine(int wordStart, int wordCount, SourceLine& src) {
  if (s_displayLinesUsed >= DISPLAY_LINE_CAP) return;
  DisplayLine& dl = s_displayLines[s_displayLinesUsed++];
  dl.lineIdx   = s_lineIndex++;
  dl.wordStart = (uint16_t)wordStart;
  dl.wordCount = (uint8_t)(wordCount > 255 ? 255 : wordCount);
  src.lineCount++;
}

static void layoutSegment(const char* seg, int segLen, bool bold,
                          char style, uint16_t textWidth,
                          int& dlWordStart, int& dlWordCount, int& lineWidth,
                          SourceLine& src) {
  display.setFont(pickFont(style, bold));
  int16_t  x1, y1;
  uint16_t sw, sh;
  display.getTextBounds(SPACEWIDTH_SYMBOL, 0, 0, &x1, &y1, &sw, &sh);

  int wStart = 0;
  while (wStart < segLen) {
    int wEnd = wStart;
    while (wEnd < segLen && seg[wEnd] != ' ') wEnd++;
    int wLen = wEnd - wStart;
    if (wLen > 0) {
      const char* wordText = internWord(seg + wStart, wLen);
      if (!wordText || s_wordRefsUsed >= WORD_REF_CAP) return;

      uint16_t wpx, hpx;
      display.getTextBounds(wordText, 0, 0, &x1, &y1, &wpx, &hpx);
      int addWidth = (int)wpx + (int)sw + WORDWIDTH_BUFFER;

      if (lineWidth > 0 && lineWidth + addWidth > (int)textWidth) {
        commitDisplayLine(dlWordStart, dlWordCount, src);
        dlWordStart = s_wordRefsUsed;
        dlWordCount = 0;
        lineWidth   = 0;
      }
      s_wordRefs[s_wordRefsUsed].text = wordText;
      s_wordRefs[s_wordRefsUsed].bold = bold;
      s_wordRefsUsed++;
      dlWordCount++;
      lineWidth += addWidth;
    }
    wStart = wEnd + 1;
  }
}

static void layoutSourceLine(const String& text, char style, ulong orderedListNum) {
  if (s_sourceLinesUsed >= LINES_PER_CHUNK) return;

  SourceLine& src    = s_sourceLines[s_sourceLinesUsed++];
  src.style          = style;
  src.orderedListNum = orderedListNum;
  src.lineStart      = (uint16_t)s_displayLinesUsed;
  src.lineCount      = 0;

  if (style == 'B' || style == 'H') {
    commitDisplayLine(s_wordRefsUsed, 0, src);
    return;
  }

  uint16_t textWidth = (uint16_t)(display.width() - DISPLAY_WIDTH_BUFFER);
  if (style == '>' || style == 'C')
    textWidth -= SPECIAL_PADDING;
  else if (style == '-' || style == 'L')
    textWidth -= 2 * SPECIAL_PADDING;

  int dlWordStart = s_wordRefsUsed;
  int dlWordCount = 0;
  int lineWidth   = 0;

  const char* raw = text.c_str();
  int n = (int)text.length();
  int i = 0;
  while (i < n) {
    bool bold = false;
    int segStart, segEnd;

    if (raw[i] == '*' && i + 1 < n && raw[i + 1] == '*') {
      bold     = true;
      segStart = i + 2;
      const char* e = strstr(raw + segStart, "**");
      segEnd = e ? (int)(e - raw) : n;
      i      = segEnd + 2;
    } else if (raw[i] == '*') {
      // Treat single * as bold too (italic not distinct on 1-bit)
      bold     = true;
      segStart = i + 1;
      const char* e = strchr(raw + segStart, '*');
      segEnd = e ? (int)(e - raw) : n;
      i      = segEnd + 1;
    } else {
      const char* nb = strstr(raw + i, "**");
      const char* ni = strchr(raw + i, '*');
      segStart = i;
      segEnd   = n;
      if (nb) segEnd = min(segEnd, (int)(nb - raw));
      if (ni) segEnd = min(segEnd, (int)(ni - raw));
      i = segEnd;
    }

    if (segEnd > segStart)
      layoutSegment(raw + segStart, segEnd - segStart, bold,
                    style, textWidth, dlWordStart, dlWordCount, lineWidth, src);
  }

  if (dlWordCount > 0)
    commitDisplayLine(dlWordStart, dlWordCount, src);
}

// ── Chunk loading ─────────────────────────────────────────────────────────────
static void buildIndex() {
  chunkCount = 0;

  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  File f = SD_MMC.open(s_entryPath, FILE_READ);
  if (!f) { 
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    SDActive = false;
    fileError = true; 
    return; 
  }

  int lineCount = 0;
  chunks[0].offset = 0;
  chunks[0].heading[0] = '\0';
  chunkCount = 1;

  while (f.available()) {
    char buf[256];
    int len = 0;
    while (f.available() && len < 255) {
      char c = (char)f.read();
      if (c == '\n') break;
      buf[len++] = c;
    }
    buf[len] = '\0';
    if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';

    if (buf[0] == '#' && buf[1] == ' ') {
      if (chunks[chunkCount - 1].heading[0] == '\0') {
        strncpy(chunks[chunkCount - 1].heading, buf + 2, 47);
        chunks[chunkCount - 1].heading[47] = '\0';
      }
    }
    lineCount++;
    if (lineCount % LINES_PER_CHUNK == 0 && chunkCount < MAX_CHUNKS) {
      chunks[chunkCount].offset = (size_t)f.position();
      chunks[chunkCount].heading[0] = '\0';
      chunkCount++;
    }
  }
  
  f.close();
  
  if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  SDActive = false;
}

static void loadChunk(int idx) {
  if (idx < 0 || idx >= chunkCount) return;

  File f = SD_MMC.open(s_entryPath, FILE_READ);
  if (!f) { fileError = true; return; }

  f.seek(chunks[idx].offset);
  size_t endOffset = (idx + 1 < chunkCount) ? chunks[idx + 1].offset : 0;

  s_textPoolUsed     = 0;
  s_wordRefsUsed     = 0;
  s_displayLinesUsed = 0;
  s_sourceLinesUsed  = 0;
  s_lineIndex        = 0;

  ulong listCounter = 1;
  int   lineCount   = 0;

  while (f.available()) {
    if (endOffset != 0 && (size_t)f.position() >= endOffset) break;
    if (lineCount >= LINES_PER_CHUNK) break;

    String raw = f.readStringUntil('\n');
    raw.trim();

    char   st = 'T';
    String content;

    // Skip image tags for now (we render them separately)
    if (raw.startsWith("![") && raw.indexOf(".bmp]") > 0) {
      st = 'B'; // blank line placeholder for image
    } else if (raw.length() == 0) {
      st = 'B';
    } else if (raw == "---") {
      st = 'H';
    } else if (raw.startsWith("# ")) {
      st = '1'; content = raw.substring(2);
    } else if (raw.startsWith("## ")) {
      st = '2'; content = raw.substring(3);
    } else if (raw.startsWith("### ")) {
      st = '3'; content = raw.substring(4);
    } else if (raw.startsWith("> ")) {
      st = '>'; content = raw.substring(2);
    } else if (raw.startsWith("- ")) {
      st = '-'; content = raw.substring(2); listCounter = 1;
    } else if (raw.startsWith("```")) {
      st = 'C';
    } else if (raw.length() >= 3 && isDigit(raw[0]) && raw[1] == '.' && raw[2] == ' ') {
      st = 'L'; content = raw.substring(3);
    } else {
      content = std::move(raw);
    }

    ulong listNum = (st == 'L') ? listCounter++ : 0;
    if (st != 'L') listCounter = 1;

    layoutSourceLine(content, st, listNum);
    lineCount++;
  }
  f.close();

  if (s_sourceLinesUsed == 0)
    layoutSourceLine(String("(empty entry)"), 'T', 0);

  needsRedraw = true;
}

// ── Entry scanning ────────────────────────────────────────────────────────────
static void scanEntries() {
  s_entryCount = 0;

  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  // Create directories if they don't exist
  if (!SD_MMC.exists("/meche")) SD_MMC.mkdir("/meche");
  if (!SD_MMC.exists(ENTRIES_DIR)) SD_MMC.mkdir(ENTRIES_DIR);
  if (!SD_MMC.exists(IMAGES_DIR)) SD_MMC.mkdir(IMAGES_DIR);

  File dir = SD_MMC.open(ENTRIES_DIR);
  if (!dir || !dir.isDirectory()) {
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    SDActive = false;
    return;
  }

  File entry = dir.openNextFile();
  while (entry && s_entryCount < MAX_ENTRIES) {
    if (!entry.isDirectory()) {
      const char* full  = entry.name();
      const char* slash = strrchr(full, '/');
      const char* fname = slash ? slash + 1 : full;
      int flen = (int)strlen(fname);
      if (flen > 3 && strcmp(fname + flen - 3, ".md") == 0) {
        strncpy(s_entryNames[s_entryCount], fname, MAX_NAME_LEN - 1);
        s_entryNames[s_entryCount][MAX_NAME_LEN - 1] = '\0';
        s_entryCount++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  SDActive = false;

  // Sort entries alphabetically
  for (int i = 0; i < s_entryCount - 1; i++) {
    for (int j = i + 1; j < s_entryCount; j++) {
      if (strcasecmp(s_entryNames[i], s_entryNames[j]) > 0) {
        char tmp[MAX_NAME_LEN];
        memcpy(tmp, s_entryNames[i], MAX_NAME_LEN);
        memcpy(s_entryNames[i], s_entryNames[j], MAX_NAME_LEN);
        memcpy(s_entryNames[j], tmp, MAX_NAME_LEN);
      }
    }
  }
}

// ── Filter logic ──────────────────────────────────────────────────────────────
static void applyFilter() {
  s_filteredCount = 0;
  for (int i = 0; i < s_entryCount && s_filteredCount < MAX_ENTRIES; i++) {
    if (s_filterLen == 0) {
      s_filteredIdx[s_filteredCount++] = i;
    } else {
      // Case-insensitive substring match
      char lower_name[MAX_NAME_LEN];
      strncpy(lower_name, s_entryNames[i], MAX_NAME_LEN - 1);
      lower_name[MAX_NAME_LEN - 1] = '\0';
      for (int c = 0; lower_name[c]; c++)
        lower_name[c] = tolower(lower_name[c]);

      char lower_filter[FILTER_MAX + 1];
      strncpy(lower_filter, s_filter, FILTER_MAX);
      lower_filter[FILTER_MAX] = '\0';
      for (int c = 0; lower_filter[c]; c++)
        lower_filter[c] = tolower(lower_filter[c]);

      if (strstr(lower_name, lower_filter))
        s_filteredIdx[s_filteredCount++] = i;
    }
  }
  // Clamp selection
  if (s_browserSel >= s_filteredCount) s_browserSel = max(0, s_filteredCount - 1);
  if (s_browserScroll > s_browserSel) s_browserScroll = s_browserSel;
}

// ── Editor helpers ────────────────────────────────────────────────────────────
static void editorInit(const char* fname) {
  memset(s_editorLines, 0, sizeof(s_editorLines));
  s_editorLineCount  = 0;
  s_editorCursorLine = 0;
  s_editorCursorCol  = 0;
  s_editorScroll     = 0;
  s_editorDirty      = false;

  if (fname && fname[0]) {
    strncpy(s_editorFilename, fname, MAX_NAME_LEN - 1);
    s_editorFilename[MAX_NAME_LEN - 1] = '\0';

    // Load existing file
    char path[128];
    snprintf(path, sizeof(path), "/meche/entries/%s", fname);

    SDActive = true;
    pocketmage::setCpuSpeed(240);
    delay(50);

    File f = SD_MMC.open(path, FILE_READ);
    if (f) {
      while (f.available() && s_editorLineCount < EDITOR_MAX_LINES) {
        String line = f.readStringUntil('\n');
        line.trim();
        strncpy(s_editorLines[s_editorLineCount], line.c_str(), EDITOR_LINE_LEN - 1);
        s_editorLineCount++;
      }
      f.close();
    }
    
    if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
    SDActive = false;
    
  } else {
    s_editorFilename[0] = '\0';
    // Start with a template
    strcpy(s_editorLines[0], "# New Entry");
    strcpy(s_editorLines[1], "");
    strcpy(s_editorLines[2], "**Category:** ");
    strcpy(s_editorLines[3], "**Tags:** ");
    strcpy(s_editorLines[4], "");
    strcpy(s_editorLines[5], "---");
    strcpy(s_editorLines[6], "");
    strcpy(s_editorLines[7], "Write your content here.");
    s_editorLineCount = 8;
  }
}

static void editorSave() {
  // If no filename, derive from first heading
  if (s_editorFilename[0] == '\0') {
    // Find first line starting with "# "
    for (int i = 0; i < s_editorLineCount; i++) {
      if (strncmp(s_editorLines[i], "# ", 2) == 0) {
        char name[MAX_NAME_LEN];
        strncpy(name, s_editorLines[i] + 2, MAX_NAME_LEN - 5);
        name[MAX_NAME_LEN - 5] = '\0';
        // Replace spaces with underscores
        for (int c = 0; name[c]; c++) {
          if (name[c] == ' ') name[c] = '_';
          else if (name[c] == '/' || name[c] == '\\') name[c] = '-';
        }
        snprintf(s_editorFilename, MAX_NAME_LEN, "%s.md", name);
        break;
      }
    }
    if (s_editorFilename[0] == '\0')
      strcpy(s_editorFilename, "untitled.md");
  }

  char path[128];
  snprintf(path, sizeof(path), "/meche/entries/%s", s_editorFilename);
  
  SDActive = true;
  pocketmage::setCpuSpeed(240);
  delay(50);

  File f = SD_MMC.open(path, FILE_WRITE);
  if (f) {
    for (int i = 0; i < s_editorLineCount; i++) {
      f.print(s_editorLines[i]);
      f.print('\n');
    }
    f.close();
  }
  
  if (SAVE_POWER) pocketmage::setCpuSpeed(POWER_SAVE_FREQ);
  SDActive = false;
  
  s_editorDirty = false;
}

// ── OLED update ───────────────────────────────────────────────────────────────
static void updateOLED() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);

  if (appMode == MODE_BROWSER) {
    u8g2.drawStr(1, 9, "MechE Bible");
    char hint[48];
    if (s_filterLen > 0)
      snprintf(hint, sizeof(hint), "Filter: %s (%d)", s_filter, s_filteredCount);
    else
      snprintf(hint, sizeof(hint), "< > select  SPC open  (%d entries)", s_filteredCount);
    u8g2.drawStr(1, 20, hint);
  } else if (appMode == MODE_VIEWER) {
    String title = s_entryDisplayName;
    if ((int)title.length() > 36) title = title.substring(0, 35) + "~";
    u8g2.drawStr(1, 9, title.c_str());
    char info[32];
    snprintf(info, sizeof(info), "Pg %lu/%d  Ch %d/%d",
             (unsigned long)(pageIndex + 1), getMaxPage() + 1,
             currentChunk + 1, chunkCount);
    u8g2.drawStr(1, 20, info);
  } else if (appMode == MODE_EDITOR) {
    u8g2.drawStr(1, 9, "Editor");
    char info[48];
    snprintf(info, sizeof(info), "Ln %d Col %d  %s",
             s_editorCursorLine + 1, s_editorCursorCol + 1,
             s_editorDirty ? "[modified]" : "");
    u8g2.drawStr(1, 20, info);
  } else if (appMode == MODE_CONFIRM) {
    u8g2.drawStr(1, 9, s_confirmMsg);
    u8g2.drawStr(1, 20, "SPC=Yes  ESC=No");
  }

  u8g2.sendBuffer();
}

// ── Document rendering ────────────────────────────────────────────────────────
static int renderSourceLine(int si, int startX, int startY) {
  const SourceLine& src   = s_sourceLines[si];
  char              style = src.style;

  if (src.lineCount > 0 &&
      s_displayLines[src.lineStart + src.lineCount - 1].lineIdx < s_pageStartLine)
    return 0;

  if (style == 'H') {
    display.drawFastHLine(0, startY + 3, display.width(), GxEPD_BLACK);
    display.drawFastHLine(0, startY + 4, display.width(), GxEPD_BLACK);
    return 8;
  }
  if (style == 'B') return 12;

  int drawX = startX;
  if (style == '>')
    drawX += SPECIAL_PADDING;
  else if (style == '-' || style == 'L')
    drawX += 2 * SPECIAL_PADDING;
  else if (style == 'C')
    drawX += SPECIAL_PADDING / 2;

  int cursorY = startY;

  for (int li = src.lineStart; li < src.lineStart + src.lineCount; li++) {
    const DisplayLine& dl = s_displayLines[li];
    if (dl.lineIdx < s_pageStartLine) continue;

    int      cx      = drawX;
    uint16_t max_hpx = 0;

    for (int wi = dl.wordStart; wi < dl.wordStart + dl.wordCount; wi++) {
      const WordRef& w = s_wordRefs[wi];
      display.setFont(pickFont(style, w.bold));
      int16_t  x1, y1;
      uint16_t wpx, hpx;
      display.getTextBounds(w.text, cx, cursorY, &x1, &y1, &wpx, &hpx);
      if (hpx > max_hpx) max_hpx = hpx;
    }
    if (style == '1' || style == '2' || style == '3') max_hpx += 4;

    for (int wi = dl.wordStart; wi < dl.wordStart + dl.wordCount; wi++) {
      const WordRef& w = s_wordRefs[wi];
      display.setFont(pickFont(style, w.bold));
      int16_t  x1, y1;
      uint16_t wpx, hpx, sw, sh;
      display.getTextBounds(w.text, cx, cursorY, &x1, &y1, &wpx, &hpx);
      display.getTextBounds(SPACEWIDTH_SYMBOL, cx, cursorY, &x1, &y1, &sw, &sh);
      display.setCursor(cx, cursorY + max_hpx);
      display.print(w.text);
      cx += (int)wpx + (int)sw;
    }

    uint8_t pad = (style == '1' || style == '2' || style == '3') ? HEADING_LINE_PADDING
                                                                  : NORMAL_LINE_PADDING;
    cursorY += (int)max_hpx + (int)pad;
  }

  // Decorations
  if (style == '>') {
    display.drawFastVLine(SPECIAL_PADDING / 2, startY, cursorY - startY, GxEPD_BLACK);
    display.drawFastVLine(SPECIAL_PADDING / 2 + 1, startY, cursorY - startY, GxEPD_BLACK);
  } else if (style == 'C') {
    display.drawFastVLine(SPECIAL_PADDING / 4, startY, cursorY - startY, GxEPD_BLACK);
    display.drawFastVLine(SPECIAL_PADDING / 4 + 1, startY, cursorY - startY, GxEPD_BLACK);
  } else if (style == '1' || style == '2' || style == '3') {
    display.drawFastHLine(0, cursorY - 2, display.width(), GxEPD_BLACK);
  } else if (style == '-') {
    display.fillCircle(drawX - 8, startY + 8, 3, GxEPD_BLACK);
  } else if (style == 'L') {
    char num[16];
    snprintf(num, sizeof(num), "%lu. ", src.orderedListNum);
    display.setFont(pickFont('T', false));
    int16_t  x1, y1;
    uint16_t wpx, hpx;
    display.getTextBounds(num, 0, 0, &x1, &y1, &wpx, &hpx);
    display.setCursor(drawX - (int)wpx - 5, startY + (int)hpx);
    display.print(num);
  }

  return cursorY - startY;
}

static void renderDocument(int startX, int startY) {
  s_pageStartLine = pageIndex * LINES_PER_PAGE;
  int cursorY = startY;
  for (int si = 0; si < s_sourceLinesUsed; si++) {
    if (cursorY >= display.height() - 6) break;
    cursorY += renderSourceLine(si, startX, cursorY);
  }
}

// ── OTA App Entry Points ──────────────────────────────────────────────────────
void APP_INIT() {
  initFonts();
  fileError    = false;
  currentChunk = 0;
  pageIndex    = 0;
  needsRedraw  = true;

  appMode = MODE_BROWSER;
  scanEntries();
  s_filterLen = 0;
  s_filter[0] = '\0';
  applyFilter();
  s_browserSel    = 0;
  s_browserScroll = 0;

  // Set default OLED text parameters
  display.setTextColor(GxEPD_BLACK);
  display.setTextWrap(false);
  
  updateOLED();
  EINK().forceSlowFullUpdate(true);
}

void processKB_APP() {
  static uint32_t lastKbTime = 0;
  if (millis() - lastKbTime < 150) return;

  char ch = KB().updateKeypress();
  if (!ch) return;
  lastKbTime = millis();

  // ╔══════════════════════════════════════════════════════════════════════════╗
  // ║  UNIVERSAL EXIT — FN+CENTER (code 7) — works from ANY mode             ║
  // ║  This can NEVER be soft-locked. Always returns to PocketMage OS.       ║
  // ╚══════════════════════════════════════════════════════════════════════════╝
  if (ch == 7) {
    OLED().oledWord("Exiting to PM OS");
    delay(500);
    rebootToPocketMage();
    return;
  }

  // ── Modifier keys (SHIFT / FN toggle) — handled globally ──────────────────
  if (ch == 17) {  // SHIFT toggle
    auto st = KB().getKeyboardState();
    if (st == SHIFT || st == FN_SHIFT)
      KB().setKeyboardState(NORMAL);
    else if (st == FUNC)
      KB().setKeyboardState(FN_SHIFT);
    else
      KB().setKeyboardState(SHIFT);
    return;
  }
  if (ch == 18) {  // FN toggle
    auto st = KB().getKeyboardState();
    if (st == FUNC || st == FN_SHIFT)
      KB().setKeyboardState(NORMAL);
    else if (st == SHIFT)
      KB().setKeyboardState(FN_SHIFT);
    else
      KB().setKeyboardState(FUNC);
    return;
  }

  // ── Browser mode ────────────────────────────────────────────────────────────
  if (appMode == MODE_BROWSER) {
    if (ch == 27) {  // ESC — exit to PocketMage OS
      OLED().oledWord("Exiting to PM OS");
      delay(500);
      rebootToPocketMage();
      return;
    }
    if (ch == 21) {  // RIGHT (>) — next entry
      if (s_browserSel < s_filteredCount - 1) {
        s_browserSel++;
        if (s_browserSel >= s_browserScroll + PICKER_VISIBLE)
          s_browserScroll = s_browserSel - PICKER_VISIBLE + 1;
        needsRedraw = true;
      }
    } else if (ch == 19) {  // LEFT (<) — prev entry
      if (s_browserSel > 0) {
        s_browserSel--;
        if (s_browserSel < s_browserScroll)
          s_browserScroll = s_browserSel;
        needsRedraw = true;
      }
    } else if (ch == 6) {  // FN+RIGHT — jump 10 entries down
      s_browserSel = min(s_browserSel + 10, max(0, s_filteredCount - 1));
      if (s_browserSel >= s_browserScroll + PICKER_VISIBLE)
        s_browserScroll = s_browserSel - PICKER_VISIBLE + 1;
      needsRedraw = true;
    } else if (ch == 12) {  // FN+LEFT — jump 10 entries up
      s_browserSel = max(s_browserSel - 10, 0);
      if (s_browserSel < s_browserScroll)
        s_browserScroll = s_browserSel;
      needsRedraw = true;
    } else if (ch == 32 || ch == 13 || ch == 20) {  // SPACE / ENTER / CENTER — open entry
      if (s_filteredCount > 0) {
        int realIdx = s_filteredIdx[s_browserSel];
        setEntryPath(s_entryNames[realIdx]);
        appMode = MODE_VIEWER;
        currentChunk = 0;
        pageIndex    = 0;
        fileError    = false;
        buildIndex();
        loadChunk(0);
        updateOLED();
      }
    } else if (ch == 'n' || ch == 'N') {  // N — new entry
      appMode = MODE_EDITOR;
      editorInit(nullptr);
      needsRedraw = true;
      updateOLED();
    } else if (ch == 8) {  // BACKSPACE — remove filter char
      if (s_filterLen > 0) {
        s_filter[--s_filterLen] = '\0';
        applyFilter();
        s_browserSel = 0;
        s_browserScroll = 0;
        needsRedraw = true;
      }
    } else if (ch >= 32 && ch < 127 && ch != 'n' && ch != 'N') {
      // Printable — add to search filter
      if (s_filterLen < FILTER_MAX) {
        s_filter[s_filterLen++] = ch;
        s_filter[s_filterLen]   = '\0';
        applyFilter();
        s_browserSel = 0;
        s_browserScroll = 0;
        needsRedraw = true;
      }
    }
    updateOLED();
    return;
  }

  // ── Viewer mode ─────────────────────────────────────────────────────────────
  if (appMode == MODE_VIEWER) {
    if (ch == 27) {  // ESC — back to browser
      appMode = MODE_BROWSER;
      needsRedraw = true;
      updateOLED();
      return;
    }
    if (ch == 'e' || ch == 'E') {  // E — edit this entry
      const char* slash = strrchr(s_entryPath, '/');
      const char* fname = slash ? slash + 1 : s_entryPath;
      appMode = MODE_EDITOR;
      editorInit(fname);
      needsRedraw = true;
      updateOLED();
      return;
    }
    if (ch == 21 || ch == 32) {  // RIGHT or SPACE — next page
      if ((int)pageIndex < getMaxPage()) {
        pageIndex++;
        needsRedraw = true;
      } else if (currentChunk + 1 < chunkCount) {
        currentChunk++;
        pageIndex = 0;
        loadChunk(currentChunk);
      }
    } else if (ch == 19) {  // LEFT — prev page
      if (pageIndex > 0) {
        pageIndex--;
        needsRedraw = true;
      } else if (currentChunk > 0) {
        currentChunk--;
        pageIndex = 0;
        loadChunk(currentChunk);
      }
    } else if (ch == 6) {  // FN+RIGHT — next chunk
      if (currentChunk + 1 < chunkCount) {
        currentChunk++;
        pageIndex = 0;
        loadChunk(currentChunk);
      }
      KB().setKeyboardState(NORMAL);
    } else if (ch == 12) {  // FN+LEFT — prev chunk
      if (currentChunk > 0) {
        currentChunk--;
        pageIndex = 0;
        loadChunk(currentChunk);
      }
      KB().setKeyboardState(NORMAL);
    } else if (ch == 'b' || ch == 'B') {  // B — back to browser
      appMode = MODE_BROWSER;
      needsRedraw = true;
      updateOLED();
      return;
    }
    updateOLED();
    return;
  }

  // ── Editor mode ─────────────────────────────────────────────────────────────
  if (appMode == MODE_EDITOR) {
    if (ch == 27) {  // ESC — confirm discard or exit
      if (s_editorDirty) {
        strcpy(s_confirmMsg, "Discard changes?");
        s_confirmReturn = MODE_BROWSER;
        appMode = MODE_CONFIRM;
        updateOLED();
        needsRedraw = true;
      } else {
        appMode = MODE_BROWSER;
        scanEntries();
        applyFilter();
        needsRedraw = true;
        updateOLED();
      }
      return;
    }

    // FN+ENTER — save and exit
    if (ch == 13 && (KB().getKeyboardState() == FUNC || KB().getKeyboardState() == FN_SHIFT)) {
      editorSave();
      OLED().oledWord("Saved!");
      delay(500);
      appMode = MODE_BROWSER;
      scanEntries();
      applyFilter();
      needsRedraw = true;
      updateOLED();
      KB().setKeyboardState(NORMAL);
      return;
    }

    // Navigation within editor
    if (ch == 21) {  // RIGHT (>) — move cursor down one line
      if (s_editorCursorLine < s_editorLineCount - 1) {
        s_editorCursorLine++;
        int len = (int)strlen(s_editorLines[s_editorCursorLine]);
        if (s_editorCursorCol > len) s_editorCursorCol = len;
        needsRedraw = true;
      }
    } else if (ch == 19) {  // LEFT (<) — move cursor up one line
      if (s_editorCursorLine > 0) {
        s_editorCursorLine--;
        int len = (int)strlen(s_editorLines[s_editorCursorLine]);
        if (s_editorCursorCol > len) s_editorCursorCol = len;
        needsRedraw = true;
      }
    } else if (ch == 6) {  // FN+RIGHT — move cursor right within line
      int len = (int)strlen(s_editorLines[s_editorCursorLine]);
      if (s_editorCursorCol < len) {
        s_editorCursorCol++;
        needsRedraw = true;
      }
      KB().setKeyboardState(NORMAL);
    } else if (ch == 12) {  // FN+LEFT — move cursor left within line
      if (s_editorCursorCol > 0) {
        s_editorCursorCol--;
        needsRedraw = true;
      }
      KB().setKeyboardState(NORMAL);
    } else if (ch == 26) {  // FN+SHIFT+RIGHT — jump to end of line
      s_editorCursorCol = (int)strlen(s_editorLines[s_editorCursorLine]);
      needsRedraw = true;
      KB().setKeyboardState(NORMAL);
    } else if (ch == 24) {  // FN+SHIFT+LEFT — jump to start of line
      s_editorCursorCol = 0;
      needsRedraw = true;
      KB().setKeyboardState(NORMAL);
    } else if (ch == 13) {  // ENTER — new line
      if (s_editorLineCount < EDITOR_MAX_LINES) {
        for (int i = s_editorLineCount; i > s_editorCursorLine + 1; i--)
          memcpy(s_editorLines[i], s_editorLines[i - 1], EDITOR_LINE_LEN);
        s_editorLineCount++;
        char* cur = s_editorLines[s_editorCursorLine];
        int curLen = (int)strlen(cur);
        if (s_editorCursorCol < curLen) {
          strncpy(s_editorLines[s_editorCursorLine + 1],
                  cur + s_editorCursorCol, EDITOR_LINE_LEN - 1);
          cur[s_editorCursorCol] = '\0';
        } else {
          s_editorLines[s_editorCursorLine + 1][0] = '\0';
        }
        s_editorCursorLine++;
        s_editorCursorCol = 0;
        s_editorDirty = true;
        needsRedraw = true;
      }
    } else if (ch == 8) {  // BACKSPACE — delete char before cursor
      if (s_editorCursorCol > 0) {
        char* line = s_editorLines[s_editorCursorLine];
        int len = (int)strlen(line);
        memmove(line + s_editorCursorCol - 1, line + s_editorCursorCol, len - s_editorCursorCol + 1);
        s_editorCursorCol--;
        s_editorDirty = true;
        needsRedraw = true;
      } else if (s_editorCursorLine > 0) {
        // Merge with previous line
        char* prev = s_editorLines[s_editorCursorLine - 1];
        int prevLen = (int)strlen(prev);
        char* cur = s_editorLines[s_editorCursorLine];
        strncat(prev, cur, EDITOR_LINE_LEN - 1 - prevLen);
        for (int i = s_editorCursorLine; i < s_editorLineCount - 1; i++)
          memcpy(s_editorLines[i], s_editorLines[i + 1], EDITOR_LINE_LEN);
        s_editorLineCount--;
        s_editorCursorLine--;
        s_editorCursorCol = prevLen;
        s_editorDirty = true;
        needsRedraw = true;
      }
    } else if (ch >= 32 && ch < 127) {  // Printable character
      char* line = s_editorLines[s_editorCursorLine];
      int len = (int)strlen(line);
      if (len < EDITOR_LINE_LEN - 2) {
        memmove(line + s_editorCursorCol + 1, line + s_editorCursorCol, len - s_editorCursorCol + 1);
        line[s_editorCursorCol] = ch;
        s_editorCursorCol++;
        s_editorDirty = true;
        needsRedraw = true;
      }
      // Reset modifier state after typing (except for numbers in FN mode)
      if (!(ch >= '0' && ch <= '9') && KB().getKeyboardState() != NORMAL)
        KB().setKeyboardState(NORMAL);
    }

    // Keep scroll in view
    int editorVisibleLines = 11;
    if (s_editorCursorLine < s_editorScroll) s_editorScroll = s_editorCursorLine;
    if (s_editorCursorLine >= s_editorScroll + editorVisibleLines)
      s_editorScroll = s_editorCursorLine - editorVisibleLines + 1;

    updateOLED();
    return;
  }

  // ── Confirm dialog ──────────────────────────────────────────────────────────
  if (appMode == MODE_CONFIRM) {
    if (ch == 32 || ch == 13 || ch == 20) {  // SPACE / ENTER / CENTER — Yes
      appMode = s_confirmReturn;
      if (s_confirmReturn == MODE_BROWSER) {
        scanEntries();
        applyFilter();
      }
      needsRedraw = true;
      updateOLED();
    } else if (ch == 27) {  // ESC — No, go back to editor
      appMode = MODE_EDITOR;
      updateOLED();
      needsRedraw = true;
    }
    return;
  }
}

void einkHandler_APP() {
  if (!needsRedraw) return;
  needsRedraw = false;

  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  // ── Browser mode ────────────────────────────────────────────────────────────
  if (appMode == MODE_BROWSER) {
    // Header
    display.setFont(&Font5x7Fixed);
    display.setCursor(4, 11);
    if (s_filterLen > 0) {
      char headerTxt[64];
      snprintf(headerTxt, sizeof(headerTxt), "MechE Bible  [%s]", s_filter);
      display.print(headerTxt);
    } else {
      display.print("MechE Bible");
    }
    display.drawFastHLine(0, 14, display.width(), GxEPD_BLACK);

    if (s_filteredCount == 0) {
      display.setFont(&FreeSerif9pt7b);
      display.setCursor(10, 50);
      if (s_entryCount == 0)
        display.print("No entries. Press N to create.");
      else
        display.print("No matching entries.");
    } else {
      display.setFont(&FreeSerif9pt7b);
      int lineH = 20;
      int y     = 14 + lineH;
      for (int i = s_browserScroll;
           i < s_filteredCount && i < s_browserScroll + PICKER_VISIBLE;
           i++) {
        if (i == s_browserSel) {
          display.fillRect(0, y - lineH + 2, display.width(), lineH, GxEPD_BLACK);
          display.setTextColor(GxEPD_WHITE);
        } else {
          display.setTextColor(GxEPD_BLACK);
        }
        // Show name without .md, underscores as spaces
        int realIdx = s_filteredIdx[i];
        char displayName[MAX_NAME_LEN];
        strncpy(displayName, s_entryNames[realIdx], sizeof(displayName) - 1);
        displayName[sizeof(displayName) - 1] = '\0';
        int dlen = (int)strlen(displayName);
        if (dlen > 3 && strcmp(displayName + dlen - 3, ".md") == 0)
          displayName[dlen - 3] = '\0';
        for (int c = 0; displayName[c]; c++)
          if (displayName[c] == '_') displayName[c] = ' ';
        display.setCursor(6, y);
        display.print(displayName);
        y += lineH;
      }
      display.setTextColor(GxEPD_BLACK);
    }

    // Footer
    display.setFont(&Font5x7Fixed);
    display.setCursor(2, display.height() - 2);
    display.print("< > nav  SPC open  N new  ESC exit");

    EINK().refresh();
    updateOLED();
    return;
  }

  // ── Viewer mode ─────────────────────────────────────────────────────────────
  if (appMode == MODE_VIEWER) {
    if (fileError || chunkCount == 0) {
      display.setFont(&FreeSerif9pt7b);
      display.setCursor(10, 30);
      display.print("Cannot open entry file");
      EINK().refresh();
      return;
    }

    display.setFont(&Font5x7Fixed);
    String header = s_entryDisplayName;
    if ((int)header.length() > 44) header = header.substring(0, 43) + "~";
    display.setCursor(4, 11);
    display.print(header);
    display.drawFastHLine(0, 14, display.width(), GxEPD_BLACK);

    renderDocument(4, CONTENT_START_Y);

    // Footer
    display.setFont(&Font5x7Fixed);
    display.setCursor(2, display.height() - 2);
    display.print("< > page  E edit  ESC back");

    EINK().refresh();
    updateOLED();
    return;
  }

  // ── Editor mode ─────────────────────────────────────────────────────────────
  if (appMode == MODE_EDITOR) {
    display.setFont(&Font5x7Fixed);
    display.setCursor(4, 11);
    display.print("Editor");
    if (s_editorDirty) display.print(" *");
    display.drawFastHLine(0, 14, display.width(), GxEPD_BLACK);

    display.setFont(&Font5x7Fixed);
    int lineH = 18;
    int y = 18;
    int visibleLines = (display.height() - 30) / lineH;

    for (int i = s_editorScroll; i < s_editorLineCount && i < s_editorScroll + visibleLines; i++) {
      if (i == s_editorCursorLine) {
        // Highlight current line
        display.fillRect(0, y - 2, display.width(), lineH, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
      } else {
        display.setTextColor(GxEPD_BLACK);
      }

      // Line number
      char lnum[5];
      snprintf(lnum, sizeof(lnum), "%3d", i + 1);
      display.setCursor(2, y + 10);
      display.print(lnum);

      // Content (truncate to fit)
      char truncLine[48];
      strncpy(truncLine, s_editorLines[i], 46);
      truncLine[46] = '\0';
      display.setCursor(24, y + 10);
      display.print(truncLine);

      y += lineH;
    }
    display.setTextColor(GxEPD_BLACK);

    // Footer
    display.setFont(&Font5x7Fixed);
    display.setCursor(2, display.height() - 2);
    display.print("FN+Enter save  ESC discard  < > nav");

    EINK().refresh();
    updateOLED();
    return;
  }

  // ── Confirm dialog ──────────────────────────────────────────────────────────
  if (appMode == MODE_CONFIRM) {
    display.setFont(&FreeSerif12pt7b);
    display.setCursor(20, 80);
    display.print(s_confirmMsg);

    display.setFont(&FreeSerif9pt7b);
    display.setCursor(40, 140);
    display.print("Space = Yes    ESC = No");

    EINK().refresh();
    updateOLED();
    return;
  }
}

#endif