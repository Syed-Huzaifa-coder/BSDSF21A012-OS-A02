# Makefile - builds src/ls-v1.0.0.c -> bin/ls using obj/
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
SRC = src/ls-v1.0.0.c
OBJ = obj/ls-v1.0.0.o
BIN = bin/ls

.PHONY: all clean dirs

all: dirs $(BIN)

# ensure directories exist
dirs:
	mkdir -p bin obj man

# compile object
$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC) -o $(OBJ)

# link to executable
$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(BIN)

clean:
	rm -rf bin obj
