CPP=g++
CFLAGS=-Wall -Werror -pedantic -Og -g

all: veneplU

veneplU: veneplU.cpp
	@$(CPP) --std=c++17 veneplU.cpp -o veneplU $(CFLAGS)
	@echo -e '\e[32mCompiling veneplU...\e[0m'
