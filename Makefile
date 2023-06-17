# Compiler
CC = g++
FLAGS = -std=c++14
# SDL Include
SDL_INCLUDE = -I include
SDL_LIB = -F/Library/Frameworks -framework SDL2

# The build target executable
TARGET = tetris

all: $(TARGET)

$(TARGET): main.cpp
	$(CC) $(FLAGS) $(SDL_INCLUDE) $(SDL_LIB) $^ -o $@

debug: FLAGS += -g
debug: $(TARGET)

clean:
	$(RM) $(TARGET)