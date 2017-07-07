CPP=g++
CFLAGS=-Wall -Werror -pedantic -Og -g

all: veneplU

veneplU: veneplU.cpp
	@echo -e '\e[33mCompiling veneplU...\e[0m'
	@$(CPP) --std=c++17 veneplU.cpp -o veneplU $(CFLAGS)
	@echo -e '\e[32mDone!\e[0m'
