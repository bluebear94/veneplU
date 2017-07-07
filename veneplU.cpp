#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

const char* CLEAR_EVERYTHING = "\x1b[2J\x1b[3J\x1b[H";

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
class UTF8Iterator {
public:
  UTF8Iterator(const UTF8Iterator& other)
    : s(other.s), i(other.i) {}
  UTF8Iterator(std::string& s, bool end = false)
    : s(s), i(end ? s.length() : 0) {}
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
      for (size_t k = 1; k <= expectedContinuations; ++k) {
        unsigned char cont = s[i + k];
        if (!isContinuation(cont)) {
          ok = false;
          break;
        }
        codepoint = (codepoint << 6) | (cont & 0x7f);
      }
      if (ok)
        j += expectedContinuations;
      else
        codepoint = -curr;
    }
    if constexpr (advance)
      i = j;
    if constexpr (lmode)
      return j - oldI;
    else
      return codepoint;
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
  std::string& s;
  size_t i;
};

class Buffer {
public:
  Buffer() {
    getTerminalDimensions(width, height);
    lines.push_back("I will shank your fucking mom");
  }
  std::vector<std::string> lines;
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
        output += lines[lineno];
        output += "\r\n";
      }
    }
    // Finally, actually render the damn thing.
    std::cout << output;
  }
};

int main() {
  saveCanonicalMode();
  setRawMode();
  atexit(restoreCanonicalMode);
  char c = 0;
  Buffer buffer;
  while (c != '\3') {
    read(0, &c, 1);
    //std::cout << ((int) c) << "\r\n";
    buffer.draw();
  }
}
