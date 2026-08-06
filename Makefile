CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lcjson
DEBUGFLAGS = -g
PREFIX = /usr/local
TARGET = npkg

NPKG_PACKAGES  = /etc/npkg/packages
NPKG_INSTALLED = /var/lib/npkg/installed
NPKG_BUILD     = /var/cache/npkg/build
MAKE_CONF      = /etc/npkg/make.conf

all:
	$(CC) $(CFLAGS) src/*.c -Iinclude -Iconfig -o $(TARGET) $(LDFLAGS)

.PHONY: clean install uninstall debug

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -d $(PREFIX)/bin

	install -d -m 755 $(NPKG_PACKAGES)
	install -d -m 755 $(NPKG_INSTALLED)
	install -d -m 755 $(NPKG_BUILD)
	install -m 644 config/make.conf $(MAKE_CONF)

	for f in packages/*.json; do \
		dest=$(NPKG_PACKAGES)/$$(basename $$f); \
		[ -f $$dest ] || install -m644 $$f $$dest; \
	done

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)

debug:
	$(CC) $(DEBUGFLAGS) $(CFLAGS) src/*.c -Iinclude -Iconfig -o $(TARGET) $(LDFLAGS)

