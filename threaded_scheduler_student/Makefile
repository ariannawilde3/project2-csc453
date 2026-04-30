CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -pthread -g
INCLUDES=-Iinclude
SRC=src/main.c src/scheduler.c
OBJ=$(SRC:.c=.o)

.PHONY: all clean

all: clean scheduler_sim

scheduler_sim: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) -pthread

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f src/*.o scheduler_sim
