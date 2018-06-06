CFLAGS += -ansi -O2 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
MAN1DIR ?= $(MANDIR)/man1

all: dweb

install:
	install -s dweb $(BINDIR)/
	install dweb.1 $(MAN1DIR)/

uninstall:
	rm -f $(BINDIR)/dweb
	rm -f $(MAN1DIR)/dweb.1

clean:
	rm -f dweb
