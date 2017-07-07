all: veneplU

veneplU: veneplU.cpp
	@g++ --std=c++17 veneplU.cpp -o veneplU
	@echo -e '\e[32mCompiling veneplU...\e[0m'
