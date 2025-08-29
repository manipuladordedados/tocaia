CFLAGS = -std=c89 -Wall -pedantic
CC ?= cc

OBJ  = tocaia.o
EXEC = tocaia

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
INSTALL ?= /usr/bin/install -c -m 755

all: $(EXEC)

$(EXEC): $(OBJ)
	$(CC) $(OBJ) -o $(EXEC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean install uninstall

clean:
	rm -f $(OBJ) $(EXEC)

install: $(EXEC)
	mkdir -p $(DESTDIR)$(BINDIR)
	$(INSTALL) $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)
