CC = gcc
CFLAGS = -Wall -Wextra -g
SRC = src/ls-v1.0.0.c
BIN = bin/ls

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

clean:
	rm -f $(BIN)
