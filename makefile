.PHONY: all move

flags = -Wall -Wextra -pedantic
version = -std=c++20
optimise = -O3 -s
exe = hexdump.exe

all: $(exe)

$(exe): main.cpp
	g++ -o $@ $^ $(flags) $(optimise) $(version)

move: $(exe)
	copy /Y /B $^ C:\projects\utility