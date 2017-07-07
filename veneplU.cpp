#define _X_OPEN_SOURCE
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include <algorithm>
#include <iostream>
#include <string>
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
  static bool isASCII(unsigned char c) {
    return c < 128;
  }
  static bool isContinuation(unsigned char c) {
    return c >= 128 && c < 192;
  }
  static bool is2ByteStarter(unsigned char c) {
    return c >= 192 && c < 224;
  }
  static bool is3ByteStarter(unsigned char c) {
    return c >= 224 && c < 240;
  }
  static bool is4ByteStarter(unsigned char c) {
    return c >= 240 && c < 248;
  }
  static size_t expectedContinuationBytes(unsigned char c) {
    return
      is2ByteStarter(c) ? 1 :
      is3ByteStarter(c) ? 2 : 3;
  }
  static constexpr int starterOffsets[3] = {192, 224, 240};
  S& s;
  size_t i;
};

enum SpecialKeys {
  UNKNOWN = 9001,
  UP,
  DOWN,
  LEFT,
  RIGHT,
  QUIT,
};

int getKey() {
  using std::cin;
  char c1 = cin.get();
  if (c1 >= 32) return c1;
  if (c1 == 27) {
    char c2 = cin.get();
    if (c2 == 91) {
      char c3 = cin.get();
      switch (c3) {
        case 65: return SpecialKeys::UP;
        case 66: return SpecialKeys::DOWN;
        case 68: return SpecialKeys::LEFT;
        case 67: return SpecialKeys::RIGHT;
        default: return SpecialKeys::UNKNOWN;
      }
    }
    return SpecialKeys::UNKNOWN;
  }
  if (c1 == 17) return SpecialKeys::QUIT;
  return SpecialKeys::UNKNOWN;
}

template<typename N> std::string&& toString(N n) {
  if (n == 0) return std::move(std::string("0"));
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
  return std::move(s);
}

// My personal favourite
constexpr size_t TAB_WIDTH = 2;

size_t wcwidthp(int codepoint) {
  // Tab width is configurable
  if (codepoint == '\t') return TAB_WIDTH;
  // Invalid byte characters are drawn as their hex in reverse video
  // Control characters are drawn as ^ plus another character
  if (codepoint < 32) return 2;
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

class Buffer {
public:
  Buffer() {
    getTerminalDimensions(width, height);
    addLineAtBack("I will shank your fucking mom");
    addLineAtBack("E-I-E-I-O");
    addLineAtBack("\tこんにちは");
    addLineAtBack("This line has invalid UTF-8 bytes like \x89");
    addLineAtBack("This one has control characters like \3");
  }
  std::vector<std::string> lines;
  std::vector<size_t> vlengths;
  // cursorCol can extend beyond the line length, but that
  // is treated as the end of that line
  size_t cursorRow = 0, cursorCol = 0;
  size_t cursorVCol = 0;
  size_t scrollRow = 0;
  size_t width, height;
  void draw() {
    // Clear entire screen and the scrollback buffer,
    // and move the cursor to the top-left corner.
    std::string output = CLEAR_EVERYTHING;
    // Draw each line.
    for (size_t i = 0; i < height - 1; ++i) {
      size_t lineno = i + scrollRow;
      if (lineno >= lines.size()) {
        output += "\x1b[34m~\x1b[0m\r\n";
      } else {
        drawLine(lines[lineno], output);
        output += "\r\n";
      }
    }
    // Info about the buffer.
    output += "\x1b[32;1mveneplū\x1b[0m - \x1b[36;1m";
    output += toString(lines.size()) + " v";
    output +=
      (lines.size() == 1) ? 'a' : 'e';
    output += "tál ";
    output += toString(cursorRow + 1);
    output +=
      (cursorRow == 0) ? "ma" :
      (cursorRow == 1) ? "mu" : "ru";
    output += " ";
    output += toString(cursorCol);
    output += " ";
    output += toString(cursorVCol);
    // Move cursor to correct position.
    output += "\x1b[";
    output += std::to_string(cursorRow - scrollRow + 1); // row
    output += ";";
    output += std::to_string(std::min(cursorVCol, vlengths[cursorRow]) + 1); // column
    output += "H";

    // Finally, actually render the damn thing.
    std::cout << output;
  }
  void react(int keycode) {
    switch (keycode) {
      case SpecialKeys::LEFT: left(); break;
      case SpecialKeys::RIGHT: right(); break;
      case SpecialKeys::UP: up(); break;
      case SpecialKeys::DOWN: down(); break;
    }
  }
private:
  void left() {
    cursorCol = std::min(cursorCol, lines[cursorRow].length());
    cursorVCol = std::min(cursorVCol, vlengths[cursorRow]);
    if (cursorCol > 0) {
      UTF8Iterator it(lines[cursorRow], cursorCol);
      --it;
      int codepoint = it.get();
      cursorCol = it.position();
      cursorVCol -= wcwidthp(codepoint);
    }
    // TODO move back one line if already at very left
  }
  void right() {
    cursorCol = std::min(cursorCol, lines[cursorRow].length());
    cursorVCol = std::min(cursorVCol, vlengths[cursorRow]);
    if (cursorCol < lines[cursorRow].length()) {
      UTF8Iterator it(lines[cursorRow], cursorCol);
      int codepoint = it.getAndAdvance();
      cursorCol = it.position();
      cursorVCol += wcwidthp(codepoint);
    }
    // TODO advance one line if already at very right
  }
  void up() {
    if (cursorRow > 0) {
      --cursorRow;
      cursorCol = unwcswidthp(lines[cursorRow], cursorVCol);
      cursorVCol = wcswidthp(lines[cursorRow], cursorCol);
    }
  }
  void down() {
    if (cursorRow < lines.size()) {
      ++cursorRow;
      cursorCol = unwcswidthp(lines[cursorRow], cursorVCol);
      cursorVCol = wcswidthp(lines[cursorRow], cursorCol);
    }
  }
  void addLineAtBack(const std::string& s) {
    lines.push_back(s);
    vlengths.push_back(wcswidthp(s));
  }
  void drawLine(const std::string& s, std::string& output) {
    // Draws the current line
    size_t taken = 0;
    UTF8Iterator<const std::string> it(s), end(s, true);
    // - 1 to leave room for a $ in case we need more lines
    while (taken < width - 1 && it != end) {
      size_t oldPosition = it.position();
      int codepoint = it.getAndAdvance();
      size_t len = it.position() - oldPosition;
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
      taken += len;
    }
  }
};

int main() {
  setlocale(LC_ALL, "");
  saveCanonicalMode();
  setRawMode();
  atexit(restoreCanonicalMode);
  Buffer buffer;
  int keycode = 0;
  while (keycode != SpecialKeys::QUIT) {
    keycode = getKey();
    //std::cout << keycode << "\r\n";
    buffer.react(keycode);
    buffer.draw();
  }
  /*
  std::string s = "こんにちは";
  UTF8Iterator<const std::string> begin(s), end(s, true);
  while (begin != end) {
    int codepoint = begin.getAndAdvance();
    std::cout << codepoint << " [" << wcwidth(codepoint) << "] ";
  }
  std::cout << '\n';
  std::cin.get();
  */
}
