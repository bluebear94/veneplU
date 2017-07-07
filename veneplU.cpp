#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

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

int main() {
  saveCanonicalMode();
  setRawMode();
  char c = 0;
  while (c != '\3') {
    read(0, &c, 1);
    std::cout << ((int) c) << "\r\n";
  }
  atexit(restoreCanonicalMode);
}
