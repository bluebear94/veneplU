CPP=g++
CFLAGS=-Wall -Werror -pedantic -Og -g
CFLAGS_RELEASE=-Wall -Werror -pedantic -O3

all: veneplU

veneplU: veneplU.cpp
	@echo -e '\e[33mCompiling veneplU...\e[0m'
	@$(CPP) --std=c++17 veneplU.cpp -o veneplU $(CFLAGS_RELEASE)
	@echo -e '\e[32mDone!\e[0m'
