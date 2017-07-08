#define _X_OPEN_SOURCE
#include <locale.h>
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

#include <iostream>

struct termios oldSettings;

void saveCanonicalMode() {
  tcgetattr(0, &oldSettings);
}

void restoreCanonicalMode() {
  tcsetattr(0, 0, &oldSettings);
}

void setRawMode() {
  struct termios newSettings;
  memcpy(&newSettings, &oldSettings, sizeof(struct termios));
  newSettings.c_iflag &= ~(IXON | ICRNL);
  newSettings.c_cflag |= CS8;
  newSettings.c_lflag &= ~(ISIG | ICANON | ECHO);
  tcsetattr(0, 0, &newSettings);
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
  if (c1 == 17) exit(0);
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

void handler(int sig, siginfo_t* si, void* mydata) {
  std::cout << '\a';
  std::cout << "Resize\n";
}

int main() {
  saveCanonicalMode();
  setRawMode();
  atexit(restoreCanonicalMode);
  struct sigaction handlerW;
  handlerW.sa_flags = (SA_SIGINFO);
  sigemptyset(&handlerW.sa_mask);
  handlerW.sa_sigaction = handler;
  if (sigaction(SIGWINCH, &handlerW, nullptr) == -1)
    perror("sigaction");
  while (true) {
    int keycode = getKey();
    if (keycode == 'q') break;
    std::cout << keycode << '\n';
  }
}