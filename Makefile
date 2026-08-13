CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lcjson
DEBUGFLAGS = -g
PREFIX = /usr/local
TARGET = npkg

# NOTE: these must match the compile-time path macros in config/config.h
# changing one without other will make the installed binaries and
# directories/files be at different places.

NPKG_PACKAGES  = /etc/npkg/packages
NPKG_INSTALLED = /var/lib/npkg/installed
NPKG_BUILD     = /var/cache/npkg/build
MAKE_CONF      = /etc/npkg/make.conf

SRCS = $(wildcard src/*.c src/common/*.c)

.PHONY: all debug clean install uninstall

all: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) $(SRCS) -Iinclude -Iconfig -o $(TARGET) $(LDFLAGS)

debug:
	$(CC) $(DEBUGFLAGS) $(CFLAGS) $(SRCS) -Iinclude -Iconfig -o $(TARGET) $(LDFLAGS)


clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	install -d $(PREFIX)/bin

	install -d -m 755 $(NPKG_PACKAGES)
	install -d -m 755 $(NPKG_INSTALLED)
	install -d -m 755 $(NPKG_BUILD)

	# root owned, world-readable/non-writable, KEY=VALUE
	# are runned as root, by npkg, so a writable-by-others
	# is a straight path to root privileges.

	install -o root -g root -m 644 config/make.conf $(MAKE_CONF)

	for f in packages/*.json; do \
		dest=$(NPKG_PACKAGES)/$$(basename $$f); \
		[ -f $$dest ] || install -m644 $$f $$dest; \
	done

uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	# Intentionally leaves $(NPKG_PACKAGES), $(NPKG_INSTALLED),
	# $(NPKG_BUILD) and $(MAKE_CONF) in place. Removing the npkg binary
	# shouldn't take installed packages or user configuration with it

