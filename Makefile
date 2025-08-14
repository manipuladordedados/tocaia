CFLAGS = -std=c89 -Wall -pedantic
UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
    CC = gcc
endif
ifeq ($(UNAME), OpenBSD)
    CC = clang
endif
ifeq ($(UNAME), FreeBSD)
    CC = clang
endif
ifeq ($(UNAME), NetBSD)
    CC = clang
endif

OBJ  = tocaia.o
EXEC = tocaia

$(EXEC): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(OBJ) $(EXEC)