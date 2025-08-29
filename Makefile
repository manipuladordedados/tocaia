CC ?= cc
CFLAGS = -std=c89 -Wall -pedantic

OBJ  = tocaia.o
EXEC = tocaia

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
INSTALL ?= install

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
	$(INSTALL) -m 755 $(EXEC) $(DESTDIR)$(BINDIR)/$(EXEC)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(EXEC)
