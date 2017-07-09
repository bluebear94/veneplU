#define _X_OPEN_SOURCE
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

const char* CLEAR_EVERYTHING = "\x1b[2J\x1b[3J\x1b[H\x1b[0m";
const char* HEX_DIGITS = "0123456789ABCDEF";
const char* DOZ_DIGITS = "0123456789XE";

struct termios oldSettings;

void saveCanonicalMode() {
  tcgetattr(0, &oldSettings);
}

void restoreCanonicalMode() {
  tcsetattr(0, 0, &oldSettings);
  std::cout << CLEAR_EVERYTHING;
}

void setRawMode() {
  struct termios newSettings;
  memcpy(&newSettings, &oldSettings, sizeof(struct termios));
  newSettings.c_iflag &= ~(IXON | ICRNL);
  newSettings.c_cflag |= CS8;
  newSettings.c_lflag &= ~(ISIG | ICANON | ECHO);
  tcsetattr(0, 0, &newSettings);
}

void getTerminalDimensions(size_t& width, size_t& height) {
  // Issue an ioctl call
  struct winsize w;
  ioctl(0, TIOCGWINSZ, &w);
  width = w.ws_col;
  height = w.ws_row;
}

int mkdirRecursive(std::string dir) {
  size_t lastSlash = dir.rfind('/');
  if (lastSlash != std::string::npos) {
    int stat = mkdirRecursive(dir.substr(0, lastSlash));
    if (stat != 0) return stat;
  }
  if (dir.empty()) return 0; // Surely you have a / dir?
  int stat = mkdir(dir.c_str(),
      S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  if (stat != 0 && errno != EEXIST) return errno;
  return 0;
}

const char* WHITESPACE = " \t\n\r";
std::string trimWhitespace(const std::string& s) {
  size_t begin = s.find_first_not_of(WHITESPACE);
  size_t end = s.find_last_not_of(WHITESPACE);
  if (begin == std::string::npos || end == std::string::npos)
    return std::move(std::string(""));
  return std::move(s.substr(begin, end + 1));
}

std::string toLower(const std::string& s) {
  std::string t = s;
  for (char& c : t) c = tolower(c);
  return std::move(t);
}

bool isTruthy(const std::string& s) {
  std::string t = toLower(s);
  return t == "1" || t == "true" ||
    t == "yes" || t == "y" ||
    t == "sel" || t == "one";
}

std::string getHome() {
  struct passwd* pw = getpwuid(getuid());
  return std::string(pw->pw_dir);
}

bool isASCII(unsigned char c) {
  return c < 128;
}
bool isContinuation(unsigned char c) {
  return c >= 128 && c < 192;
}
bool is2ByteStarter(unsigned char c) {
  return c >= 192 && c < 224;
}
bool is3ByteStarter(unsigned char c) {
  return c >= 224 && c < 240;
}
bool is4ByteStarter(unsigned char c) {
  return c >= 240 && c < 248;
}
size_t expectedContinuationBytes(unsigned char c) {
  return
    is2ByteStarter(c) ? 1 :
    is3ByteStarter(c) ? 2 : 3;
}
constexpr int starterOffsets[3] = {192, 224, 240};

// Technically not an iterator in the traditional sense,
// since returning a reference upon dereferencing
// is not feasible. To emphasise this, we use different
// names for the dereferencing methods.
template<typename S = std::string>
class UTF8Iterator {
public:
  UTF8Iterator(const UTF8Iterator& other)
    : s(other.s), i(other.i) {}
  UTF8Iterator(S& s, bool end = false)
    : s(s), i(end ? s.length() : 0) {}
  UTF8Iterator(S& s, size_t i)
    : s(s), i(i) {}
  int get() const {
    return const_cast<UTF8Iterator*>(this)->get2<false>();
  }
  int getLength() const {
    return const_cast<UTF8Iterator*>(this)->get2<false, true>();
  }
  int getAndAdvance() {
    return get2<true>();
  }
  UTF8Iterator& operator++() {
    (void) get2<true>();
    return *this;
  }
  UTF8Iterator operator++(int dummy) {
    (void) dummy;
    UTF8Iterator old(*this);
    (void) get2<true>();
    return old;
  }
  UTF8Iterator& operator--() {
    recede();
    return *this;
  }
  UTF8Iterator operator--(int dummy) {
    (void) dummy;
    UTF8Iterator old(*this);
    recede();
    return old;
  }
  size_t position() const {
    return i;
  }
  bool operator==(const UTF8Iterator& other) {
    return &(s) == &(other.s) && i == other.i;
  }
  bool operator!=(const UTF8Iterator& other) {
    return !(*this == other);
  }
private:
  template<bool advance = false, bool lmode = false>
  int get2() {
    size_t oldI = i;
    size_t j = i + 1;
    int codepoint;
    // We do our operations with unsigned char's to simplify things.
    unsigned char curr = (unsigned char) s[i];
    if (isASCII(curr)) {
      codepoint = curr;
    } else if (isContinuation(curr) || curr >= 248) {
      // Invalid values are mapped to negative integers.
      // This is so that files that somehow have malformed text
      // can still be edited.
      codepoint = -curr;
    } else  {
      size_t expectedContinuations = expectedContinuationBytes(curr);
      codepoint = curr - starterOffsets[expectedContinuations - 1];
      bool ok = true;
      if (j + expectedContinuations <= s.length()) {
        for (size_t k = 1; k <= expectedContinuations; ++k) {
          unsigned char cont = s[i + k];
          if (!isContinuation(cont)) {
            ok = false;
            break;
          }
          codepoint = (codepoint << 6) | (cont & 0x7f);
        }
      } else ok = false;
      if (ok)
        j += expectedContinuations;
      else
        codepoint = -curr;
    }
    if constexpr (advance) i = j;
    if constexpr (lmode)
      return j - oldI;
    else
      return codepoint;
  }
  void recede() {
    // Go back a byte until we encounter an ASCII character or a starter
    size_t oldI = i;
    size_t j = i;
    do {
      --j;
    } while (j > 0 && (isContinuation(s[j]) || ((unsigned char) s[j] >= 248)));
    // Now verify that this is a valid UTF-8 sequence
    // and spans all the way to the old position
    i = j;
    int codepoint = get2<true>();
    // Not a valid UTF-8 sequence or it doesn't span all the way
    // Recede only one byte.
    if (codepoint < 0 || position() != oldI) {
      i = oldI - 1;
    } else {
      i = j;
    }
  }
  S& s;
  size_t i;
};

std::string utf8CodepointToChar(int code) {
  if (code < 0) return std::string(1, (char) -code);
  if (code < 128) return std::string(1, (char) code);
  std::string s;
  if (code < 0x800) { // 2 bytes
    s += (char) (0xC0 | (code >> 6));
    s += (char) (0x80 | (code & 63));
  } else if (code < 0x10000) { // 3 bytes
    s += (char) (0xE0 | (code >> 12));
    s += (char) (0x80 | ((code >> 6) & 63));
    s += (char) (0x80 | (code & 63));
  } else if (code < 0x101000) { // 4 bytes
    s += (char) (0xF0 | (code >> 18));
    s += (char) (0x80 | ((code >> 12) & 63));
    s += (char) (0x80 | ((code >> 6) & 63));
    s += (char) (0x80 | (code & 63));
  }
  return s;
}

enum SpecialKeys {
  UNKNOWN = -9001,
  UP,
  DOWN,
  LEFT,
  RIGHT,
  QUIT,
  BACKSPACE,
  DELETE,
  ENTER,
  SAVE,
  COPY,
  SAVE_AS,
  DHR_MODE,
  RESET,
};

int get1c() {
  errno = 0;
  unsigned char c;
  read(0, &c, 1);
  if (errno == EINTR) return -1;
  return c;
}

int getKey() {
  using std::cin;
  int c = get1c();
  if (c == -1) return SpecialKeys::RESET;
  unsigned char c1 = c;
  if (c1 == 127) return SpecialKeys::BACKSPACE;
  if (c1 >= 32) {
    if (isASCII(c1)) return c1;
    else if (isContinuation(c1) || c1 >= 248) {
      return -c1;
    } else  {
      size_t expectedContinuations = expectedContinuationBytes(c1);
      int codepoint = c1 - starterOffsets[expectedContinuations - 1];
      bool ok = true;
      size_t k;
      for (k = 1; k <= expectedContinuations; ++k) {
        unsigned char cont = cin.get();
        if (!isContinuation(cont)) {
          ok = false;
          break;
        }
        codepoint = (codepoint << 6) | (cont & 0x7f);
      }
      if (!ok) {
        codepoint = -c1;
        for (size_t j = 1; j <= k; ++j) {
          cin.unget();
        }
      }
      return codepoint;
    }
  }
  if (c1 == 13) return SpecialKeys::ENTER;
  if (c1 == 27) {
    char c2 = cin.get();
    if (c2 == 91) {
      char c3 = cin.get();
      switch (c3) {
        case 65: return SpecialKeys::UP;
        case 66: return SpecialKeys::DOWN;
        case 68: return SpecialKeys::LEFT;
        case 67: return SpecialKeys::RIGHT;
        case 51: {
          char c4 = cin.get();
          if (c4 == 126) return SpecialKeys::DELETE;
          return SpecialKeys::UNKNOWN;
        }
        default: return SpecialKeys::UNKNOWN;
      }
    }
    return SpecialKeys::UNKNOWN;
  }
  if (c1 == 17) return SpecialKeys::QUIT;
  if (c1 == 19) return SpecialKeys::SAVE;
  if (c1 == 3) return SpecialKeys::COPY;
  if (c1 == 28) {
    int codepoint = getKey();
    switch (codepoint) {
    case SpecialKeys::SAVE:
      return SpecialKeys::SAVE_AS;
    default:
      return codepoint;
    }
  }
  if (c1 == 4) return SpecialKeys::DHR_MODE;
  return SpecialKeys::UNKNOWN;
}

template<typename N> std::string toString(N n) {
  if (n == 0) return std::string("0");
  std::string s;
  bool negative = false;
  if (n < 0) {
    negative = true;
    n = -n;
  }
  while (n != 0) {
    int d = n % 12;
    s += DOZ_DIGITS[d];
    n /= 12;
  }
  if (negative) s += '-';
  std::reverse(s.begin(), s.end());
  return s;
}

const char* VOWELS = "aeiouy";
class DHRBox {
public:
  int feed(int key) {
    int res = feed2(key);
    if (res >= 0) reset();
    return res;
  }
  bool upper = false;
  bool forceStress = false;
  bool forceUnstress = false;
  void reset() {
    upper = false;
    forceStress = false;
    forceUnstress = false;
  }
private:
  int feed2(int key) {
    if (key == 'q') upper = !upper;
    else if (key == '\'') {
      forceStress = !forceStress;
      if (forceStress) forceUnstress = false;
    } else if (key == '`') {
      forceUnstress = !forceUnstress;
      if (forceUnstress) forceStress = false;
    } else if (key == 'x') {
      return upper ? L'Ḣ' : L'ḣ';
    } else if (islower(key)) {
      if (forceStress) {
        const char* where = strchr(VOWELS, key);
        if (where != nullptr)
          return (upper ? L"ÁÉÍÓÚÝ" : L"áéíóúý")[where - VOWELS];
      }
      return upper ? toupper(key) : key;
    } else if (isupper(key)) {
      if (forceUnstress) {
        const char* where = strchr(VOWELS, tolower(key));
        if (where != nullptr)
          return (upper ? L"ĀĒĪŌŪȲ" : L"āēīōūȳ")[where - VOWELS];
      }
      return (upper ? uumap : ulmap)[key - 'A'];
    } else return key;
    return -1;
  }
  static constexpr wchar_t ulmap[26] = {
    L'â', 0, 0, L'ḋ', L'ê',
    0, L'ġ', L'ħ', L'î', 0,
    0, 0, 0, L'ṅ', L'ô',
    0, 0, 0, L'ṡ', L'ṫ',
    L'û', 0, L'ẏ', 0, L'ŷ',
    L'ż'
  };
  static constexpr wchar_t uumap[26] = {
    L'Â', 0, 0, L'Ḋ', L'Ê',
    0, L'Ġ', L'Ħ', L'Î', 0,
    0, 0, 0, L'Ṅ', L'Ô',
    0, 0, 0, L'Ṡ', L'Ṫ',
    L'Û', 0, L'Ẏ', 0, L'Ŷ',
    L'Ż'
  };
}; // 'twas done

// My personal favourite
constexpr size_t TAB_WIDTH = 2;

size_t wcwidthp(int codepoint) {
  // Tab width is configurable
  if (codepoint == '\t') return TAB_WIDTH;
  // Invalid byte characters are drawn as their hex in reverse video
  // Control characters are drawn as ^ plus another character
  if (codepoint < 32 || codepoint == 127) return 2;
  return wcwidth(codepoint);
}

size_t wcswidthp(const std::string& s) {
  size_t sum = 0;
  UTF8Iterator<const std::string> begin(s), end(s, true);
  while (begin != end) {
    int codepoint = begin.getAndAdvance();
    sum += wcwidthp(codepoint);
  }
  return sum;
}

size_t wcswidthp(const std::string& s, size_t len) {
  size_t sum = 0;
  UTF8Iterator<const std::string> begin(s), end(s, len);
  while (begin != end) {
    int codepoint = begin.getAndAdvance();
    sum += wcwidthp(codepoint);
  }
  return sum;
}

size_t unwcswidthp(const std::string& s, size_t vlen) {
  size_t sum = 0;
  UTF8Iterator<const std::string> begin(s), end(s, true);
  while (sum < vlen && begin != end) {
    int codepoint = begin.getAndAdvance();
    sum += wcwidthp(codepoint);
  }
  return begin.position();
}

// Define a global variable so the signal handler can use it
class Buffer;
Buffer* globalBuffer = nullptr;

// Options

enum BoolOptions {
  B_LINE_NUMBERS = 0,
  // add new ones before this line
  B_COUNT
};
const std::unordered_map<std::string, size_t> boolOptionsByName = {
  {"vatarika", 0},
};

class Buffer {
public:
  std::vector<std::string> lines;
  std::vector<size_t> vlengths;
  // cursorCol can extend beyond the line length, but that
  // is treated as the end of that line
  size_t cursorRow = 0, cursorCol = 0;
  size_t cursorVCol = 0;
  size_t scrollRow = 0;
  size_t scrollCol = 0;
  size_t scrollVCol = 0;
  size_t width, height;
  bool shouldResize = false;
  bool dirty = false;
  bool prompting = false;
  bool first = true;
  std::string message;
  std::string promptInput;
  size_t promptVLength;
  int messageColour;
  std::string filename;
  DHRBox box;
  bool isDHR = false;
  class Options {
  public:
    Options() :
      boolOptions(BoolOptions::B_COUNT) {}
    bool lineno() const { return boolOptions[BoolOptions::B_LINE_NUMBERS]; }
    std::vector<bool> boolOptions;
  };
  Options options;
  Buffer() {
    getTerminalDimensions(width, height);
    registerHandler();
    addLineAtBack("");
    readOptions();
  }
  void read(const char* fname) {
    lines.clear();
    vlengths.clear();
    filename = fname;
    std::ifstream fh(fname, std::ios::binary);
    if (fh.fail()) {
      dirty = true;
      return;
    }
    std::string curLine;
    while (!fh.eof()) {
      char c = fh.get();
      if (c == '\n') {
        addLineAtBack(curLine);
        curLine = "";
      }
      else curLine += c;
    }
  }
  void readOptions() {
    std::ifstream fh(getHome() + "/.veneplU_dat/options");
    if (fh.fail()) return;
    std::string s;
    std::vector<std::string> invalidOptions;
    while (getline(fh, s)) {
      size_t firstNonspace = s.find_first_not_of(WHITESPACE);
      if (firstNonspace == std::string::npos || s[firstNonspace] == '#')
        continue;
      // Get the parts
      size_t split = s.find('=');
      std::string key = trimWhitespace(s.substr(0, split));
      std::string value = trimWhitespace(s.substr(split + 1));
      auto it1 = boolOptionsByName.find(key);
      if (it1 != boolOptionsByName.end()) {
        size_t index = it1->second;
        options.boolOptions[index] = isTruthy(value);
      } else {
        // invalid option
        invalidOptions.push_back(key);
        std::cout << key << '\n';
        std::cin.get();
      }
    }
    if (!invalidOptions.empty()) {
      message = "";
      for (const std::string opt : invalidOptions) {
        message += '"';
        message += opt;
        message += "\" ";
      }
      message += "turotenus kêl ";
      message +=
        (invalidOptions.size() == 1) ? "ase" :
        (invalidOptions.size() == 2) ? "ases" : "ese";
      message += '!';
      messageColour = 9;
    }
  }
  void draw() {
    resizeIfNecessary();
    // Clear entire screen and the scrollback buffer,
    // and move the cursor to the top-left corner.
    std::string output = CLEAR_EVERYTHING;
    size_t rows = 0;
    size_t lineno = scrollRow;
    // Draw each line.
    while (rows < height - 1) {
      if (lineno >= lines.size()) {
        drawBlank(output, lineno);
        ++rows;
      } else {
        rows += drawLine(lines[lineno], output, lineno);
      }
      ++lineno;
    }
    if (message.empty()) {
      // Info about the buffer.
      output += "\x1b[32;1mveneplū\x1b[0m -";
      if (filename != "") {
        output += " \x1b[35;1m";
        output += filename;
        if (dirty)
          output += "\x1b[31;1m*";
      } else {
        output += " \x1b[31;1m*";
      }
      output += " \x1b[36;1m";
      output += toString(lines.size()) + " v";
      output +=
        (lines.size() == 1) ? 'a' : 'e';
      output += "tál ";
      output += toString(cursorRow + 1);
      output +=
        (cursorRow == 0) ? "ma" :
        (cursorRow == 1) ? "mu" : "ru";
      output += " | ";
      output += toString(cursorVCol + 1);
      output +=
        (cursorVCol == 0) ? "ma" :
        (cursorVCol == 1) ? "mu" : "ru";
      output += " vżama";
      if (isDHR) {
        output += " \x1b[33;1mḊ[";
        output += box.upper ? 'K' : 'k';
        output +=
          box.forceStress ? "ûú" :
          box.forceUnstress ? "ūu" : "ûu";
        output += "]";
      }
    } else {
      drawMessage(output);
    }
    // Move cursor to correct position.
    output += "\x1b[";
    output += std::to_string(cursorRow - scrollRow + 1); // row
    output += ";";
    size_t horizontalOffset = options.lineno() ? 6 : 0;
    output += std::to_string(std::min(cursorVCol, vlengths[cursorRow]) + 1 + horizontalOffset); // column
    output += "H";
    // Finally, actually render the damn thing.
    write(0, output.c_str(), output.length());
  }
  void react(int keycode) {
    if (!first) message = "";
    else first = false;
    if (isDHR && keycode >= 0) {
      keycode = box.feed(keycode);
      if (keycode <= 0) {
        if (keycode == 0) std::cout << '\a';
        keycode = SpecialKeys::UNKNOWN;
      }
    }
    switch (keycode) {
      case SpecialKeys::LEFT: left(); break;
      case SpecialKeys::RIGHT: right(); break;
      case SpecialKeys::UP: up(); break;
      case SpecialKeys::DOWN: down(); break;
      case SpecialKeys::BACKSPACE: backspace(); break;
      case SpecialKeys::DELETE: del(); break;
      case SpecialKeys::ENTER: insertNewLine(); break;
      case SpecialKeys::SAVE: saveIntractive(); break;
      case SpecialKeys::SAVE_AS: saveIntractive(true); break;
      case SpecialKeys::DHR_MODE: isDHR = !isDHR; box.reset(); break;
      case SpecialKeys::RESET: std::cout << '\a'; break;
      case SpecialKeys::UNKNOWN: break;
      default: insert(keycode);
    }
  }
private:
  size_t actualWidth() const {
    size_t xoff = 0;
    if (prompting) xoff = message.length() + 2;
    else if (options.lineno()) xoff = 6;
    return width - xoff;
  }
  void left() {
    auto& line = prompting ? promptInput : lines[cursorRow];
    auto& vlength = prompting ? promptVLength : vlengths[cursorRow];
    cursorCol = std::min(cursorCol, line.length());
    cursorVCol = std::min(cursorVCol, vlength);
    if (cursorCol > 0) {
      UTF8Iterator it(line, cursorCol);
      --it;
      int codepoint = it.get();
      cursorCol = it.position();
      cursorVCol -= wcwidthp(codepoint);
      if (cursorCol < scrollCol) {
        scrollCol = cursorCol;
        scrollVCol = cursorVCol;
      }
    } else if (cursorRow > 0 && !prompting) {
      --cursorRow;
      cursorCol = lines[cursorRow].length();
      cursorVCol = vlengths[cursorRow];
      scrollCol = cursorCol;
      scrollVCol = cursorVCol;
      // Get the earliest character that we can anchor to
      UTF8Iterator it(lines[cursorRow], cursorCol);
      size_t nReceded = 0;
      while (nReceded < actualWidth()) {
        --it;
        int codepoint = it.get();
        size_t gw = wcwidthp(codepoint);
        nReceded += gw;
        if (it.position() == 0) break;
      }
      scrollCol = it.position();
      scrollVCol -= nReceded;
    }
    // Out of bounds?
    if (cursorRow < scrollRow) {
      --scrollRow;
    }
  }
  void right() {
    if (cursorRow == lines.size()) return;
    auto& line = prompting ? promptInput : lines[cursorRow];
    auto& vlength = prompting ? promptVLength : vlengths[cursorRow];
    cursorCol = std::min(cursorCol, line.length());
    cursorVCol = std::min(cursorVCol, vlength);
    if (cursorCol < line.length()) {
      size_t old = cursorCol;
      UTF8Iterator it(line, cursorCol);
      int codepoint = it.getAndAdvance();
      cursorCol = it.position();
      size_t gw = wcwidthp(codepoint);
      cursorVCol += gw;
      if (cursorVCol >= scrollVCol + actualWidth()) {
        scrollVCol += gw;
        scrollCol += cursorCol - old;
      }
    } else if (cursorRow < lines.size() && !prompting) {
      ++cursorRow;
      cursorCol = 0;
      cursorVCol = 0;
      // no possibility of horizontal OOB here
      scrollCol = 0;
      scrollVCol = 0;
    }
    // Out of bounds?
    if (cursorRow >= scrollRow + height - 1) {
      ++scrollRow;
    }
  }
  void horizontalScrollAdjust() {
    if (cursorCol < scrollCol) {
      scrollCol = cursorCol;
      scrollVCol = cursorVCol;
    }
    if (cursorVCol >= scrollVCol + actualWidth()) {
      // Get the earliest character that we can anchor to
      UTF8Iterator it(lines[cursorRow], cursorCol);
      size_t nReceded = 0;
      while (nReceded < actualWidth()) {
        --it;
        int codepoint = it.get();
        size_t gw = wcwidthp(codepoint);
        nReceded += gw;
        if (it.position() == 0) break;
      }
      scrollCol = it.position();
      scrollVCol -= nReceded;
    }
  }
  // The following two methods are not used in prompts.
  void up() {
    if (cursorRow > 0) {
      --cursorRow;
      cursorCol = unwcswidthp(lines[cursorRow], cursorVCol);
      cursorVCol = wcswidthp(lines[cursorRow], cursorCol);
    }
    // Out of bounds?
    if (cursorRow < scrollRow) {
      --scrollRow;
    }
    horizontalScrollAdjust();
  }
  void down() {
    if (cursorRow < lines.size()) {
      ++cursorRow;
      if (cursorRow < lines.size()) {
        cursorCol = unwcswidthp(lines[cursorRow], cursorVCol);
        cursorVCol = wcswidthp(lines[cursorRow], cursorCol);
      } else {
        cursorCol = 0;
        cursorVCol = 0;
      }
    }
    // Out of bounds?
    if (cursorRow >= scrollRow + height - 1) {
      ++scrollRow;
    }
    horizontalScrollAdjust();
  }
  void del() {
    auto& line = prompting ? promptInput : lines[cursorRow];
    auto& vlength = prompting ? promptVLength : vlengths[cursorRow];
    cursorCol = std::min(cursorCol, line.length());
    cursorVCol = std::min(cursorVCol, vlength);
    if (cursorCol < line.length()) {
      UTF8Iterator it(line, cursorCol);
      int codepoint = it.getAndAdvance();
      int length = it.position() - cursorCol;
      line.erase(cursorCol, length);
      vlength -= wcwidthp(codepoint);
      // Possibility of non-UTF-8 bytes merging into UTF-8 codepoints
      if (codepoint < 0)
        cursorVCol = wcswidthp(line, cursorCol);
      if (!prompting) dirty = true;
    } else if (cursorRow < lines.size() - 1 && !prompting) {
      // Merge the two lines
      lines[cursorRow] += lines[cursorRow + 1];
      vlengths[cursorRow] += vlengths[cursorRow + 1];
      lines.erase(lines.begin() + cursorRow + 1);
    }
  }
  void backspace() {
    auto& line = prompting ? promptInput : lines[cursorRow];
    auto& vlength = prompting ? promptVLength : vlengths[cursorRow];
    cursorCol = std::min(cursorCol, line.length());
    cursorVCol = std::min(cursorVCol, vlength);
    if (cursorCol > 0) {
      UTF8Iterator it(line, cursorCol);
      --it;
      cursorCol = it.position();
      int codepoint = it.getAndAdvance();
      int length = it.position() - cursorCol;
      cursorVCol -= wcwidthp(codepoint);
      line.erase(cursorCol, length);
      vlength -= wcwidthp(codepoint);
      // Possibility of non-UTF-8 bytes merging into UTF-8 codepoints
      if (codepoint < 0)
        cursorVCol = wcswidthp(line, cursorCol);
      if (!prompting) dirty = true;
    } else if (cursorRow > 0 && !prompting) {
      // Merge the two lines
      --cursorRow;
      cursorCol = lines[cursorRow].length();
      cursorVCol = vlengths[cursorRow];
      lines[cursorRow] += lines[cursorRow + 1];
      vlengths[cursorRow] += vlengths[cursorRow + 1];
      lines.erase(lines.begin() + cursorRow + 1);
      dirty = true;
    }
  }
  void insert(int codepoint) {
    // non-newline case
    if (!prompting && cursorRow == lines.size()) {
      addLineAtBack("");
    }
    auto& line = prompting ? promptInput : lines[cursorRow];
    auto& vlength = prompting ? promptVLength : vlengths[cursorRow];
    cursorCol = std::min(cursorCol, line.length());
    cursorVCol = std::min(cursorVCol, vlength);
    std::string insertion = utf8CodepointToChar(codepoint);
    line.insert(cursorCol, insertion);
    cursorCol += insertion.length();
    cursorVCol += wcwidthp(codepoint);
    vlength += wcwidthp(codepoint);
    // Possibility of non-UTF-8 bytes merging into UTF-8 codepoints
    if (codepoint < 0)
      cursorVCol = wcswidthp(line, cursorCol);
    if (!prompting) dirty = true;
  }
  // Not used in prompts.
  void insertNewLine() {
    if (cursorRow == lines.size()) {
      addLineAtBack("");
    } else {
      // Split the line in two. Anything after the cursor gets moved
      // to another line.
      addLineAt(lines[cursorRow].substr(cursorCol), cursorRow + 1);
      lines[cursorRow].erase(cursorCol);
      vlengths[cursorRow] = cursorVCol;
      ++cursorRow;
      cursorCol = 0;
      cursorVCol = 0;
    }
    dirty = true;
  }
  void addLineAtBack(const std::string& s) {
    lines.push_back(s);
    vlengths.push_back(wcswidthp(s));
  }
  void addLineAt(const std::string& s, size_t i) {
    lines.insert(lines.begin() + i, s);
    vlengths.insert(vlengths.begin() + i, wcswidthp(s));
  }
  void drawLineNo(std::string& output, size_t lineno) {
    if (options.lineno()) {
      std::string lstr = toString(lineno + 1);
      output += "\x1b[38;5;208m";
      output += std::string(5 - lstr.length(), ' ');
      output += lstr;
      output += ' ';
      output += "\x1b[0m";
    }
  }
  size_t drawLine(const std::string& s, std::string& output, size_t lineno,
      size_t start = 0, bool newline = true) {
    // Draws the current line
    size_t taken = 0;
    if (newline) drawLineNo(output, lineno);
    if (options.lineno() && newline) taken += 5;
    UTF8Iterator<const std::string> it(s), end(s, true);
    bool broken = false;
    // - 1 to leave room for a $ in case we need more lines
    while (it != end) {
      size_t oldPosition = it.position();
      int codepoint = it.getAndAdvance();
      size_t len = it.position() - oldPosition;
      size_t w = wcwidthp(codepoint);
      if (taken + w >= width - 1 - start) {
        broken = true;
        break;
      }
      // Append as-is
      if (codepoint >= ' ')
        output += s.substr(oldPosition, len);
      // Is it an invalid byte?
      else if (codepoint < 0) {
        int byte = -codepoint;
        int high = (byte >> 4) & 15; // cut to 0 - 15 range for good measure
        int low = byte & 15;
        output += "\x1b[7m"; // reverse video
        output += HEX_DIGITS[high];
        output += HEX_DIGITS[low];
        output += "\x1b[0m"; // reset
      }
      // Is it tab?
      else if (codepoint == '\t') {
        for (size_t i = 0; i < TAB_WIDTH; ++i)
          output += " ";
      }
      // Is it a control character?
      else if (codepoint < ' ') {
        output += "\x1b[7m"; // reverse video
        output += '^';
        output += ('@' + codepoint);
        output += "\x1b[0m"; // reset
      }
      // Is it backspace?
      else if (codepoint == 127) {
        output += "\x1b[7m^?\x1b[0m"; // reset
      }
      taken += len;
    }
    if (broken) {
      output += "\x1b[9999C\x1b[34;1m$\x1b[0m";
      if (newline) output += "\x1b[E";
    }
    else if (newline) output += "\r\n";
    return 1;
  }
  void drawBlank(std::string& output, size_t lineno) {
    drawLineNo(output, lineno);
    output += "\x1b[34m~\x1b[0m\r\n";
  }
  void drawMessage(std::string& output) {
    output += "\x1b[3";
    output += (char) ('0' + (messageColour & 7));
    if ((messageColour & 8) != 0)
      output += ";1";
    output += "m";
    output += message;
  }
  void saveIntractive(bool forcePrompt = false) {
    std::string fname;
    if (filename == "" || forcePrompt) {
      message = "Sydál kentos mej kemeṫys?";
      messageColour = 14;
      bool stat1 = prompt();
      if (!stat1 || promptInput.empty()) {
        message = "Syda kêl nelterus.";
        messageColour = 1;
        return;
      }
      fname = promptInput;
    } else fname = filename;
    std::error_code stat = save(fname);
    if (stat) {
      message = "Syda kêl nelteġerus: ";
      message += stat.message();
      messageColour = 9;
    } else {
      message = "Syda nelterus.";
      messageColour = 10;
    }
  }
  std::error_code save(const std::string& fname) {
    // Create the parent directory (if needed)
    size_t lastSlash = fname.rfind('/');
    if (lastSlash != std::string::npos) {
      int stat = mkdirRecursive(fname.substr(0, lastSlash));
      if (stat != 0) return std::error_code(stat, std::system_category());
    }
    // Create the actual file
    std::ofstream out;
    out.open(fname, std::ios::binary | std::ios::out);
    // Output each line
    for (const std::string& line : lines) {
      out << line << '\n';
    }
    if (!out.good()) return std::error_code(errno, std::system_category());
    dirty = false;
    filename = fname;
    return std::error_code();
  }
  void promptMessage() {
    std::string output;
    output += "\x1b[";
    output += std::to_string(height);
    output += ";1H\x1b[0m";
    for (size_t i = 0; i < width; ++i) output +=  ' ';
    output += "\x1b[";
    output += std::to_string(height);
    output += ";1H";
    drawMessage(output);
    output += "  \x1b[0m";
    write(0, output.c_str(), output.length());
  }
  bool prompt() {
    // Save cursor position
    size_t oldCol = cursorCol;
    size_t oldVCol = cursorVCol;
    prompting = true;
    cursorCol = 0;
    cursorVCol = 0;
    promptInput = "";
    promptMessage();
    size_t offset = wcswidthp(message);
    int keycode = 0;
    bool done = false;
    while (true) {
      if (resizeIfNecessary(true)) {
        promptMessage();
      } else {
        keycode = getKey();
        if (keycode == SpecialKeys::QUIT) break;
        else if (keycode == SpecialKeys::ENTER) {
          done = true;
          break;
        }
        switch (keycode) {
          case SpecialKeys::LEFT: left(); break;
          case SpecialKeys::RIGHT: right(); break;
          case SpecialKeys::BACKSPACE: backspace(); break;
          case SpecialKeys::DELETE: del(); break;
          case SpecialKeys::UNKNOWN: break;
          default: if (keycode >= 0) insert(keycode);
        }
      }
      std::string output;
      // Move cursor 2 spaces after message
      output += "\x1b[";
      output += std::to_string(height);
      output += ';';
      output += std::to_string(offset + 3);
      output += 'H';
      for (size_t i = offset + 3; i < width; ++i) output += ' ';
      output += "\x1b[";
      output += std::to_string(height);
      output += ';';
      output += std::to_string(offset + 3);
      output += 'H';
      // Print message
      drawLine(promptInput, output, 0, offset + 2, false);
      write(0, output.c_str(), output.length());
    }
    prompting = false;
    // Restore cursor position
    cursorCol = oldCol;
    cursorVCol = oldVCol;
    return done;
  }
  static void handler(int sig, siginfo_t* si, void* mydata) {
    std::cout << '\a';
    globalBuffer->shouldResize = true;
  }
  void registerHandler() {
    globalBuffer = this;
    struct sigaction handlerW;
    handlerW.sa_flags = (SA_SIGINFO);
    sigemptyset(&handlerW.sa_mask);
    handlerW.sa_sigaction = handler;
    if (sigaction(SIGWINCH, &handlerW, nullptr) == -1)
      perror("sigaction");
  }
  bool resizeIfNecessary(bool autodraw = false) {
    if (shouldResize) {
      getTerminalDimensions(width, height);
      shouldResize = false;
      if (autodraw) draw();
      return true;
    }
    return false;
  }
};

int main(int argc, char** argv) {
  setlocale(LC_ALL, "");
  saveCanonicalMode();
  setRawMode();
  atexit(restoreCanonicalMode);
  /*
  char c = 0;
  while (c != '\3') {
    c = std::cin.get();
    std::cout << (int) c << '\n';
  }
  */
  Buffer buffer;
  if (argc > 1) buffer.read(argv[1]);
  int keycode = 0;
  buffer.draw();
  while (keycode != SpecialKeys::QUIT) {
    keycode = buffer.shouldResize ? SpecialKeys::UNKNOWN : getKey();
    //std::cout << keycode << "\r\n";
    buffer.react(keycode);
    buffer.draw();
  }
}
