# Makefile for Sizer v2.0.1 - Disk Space Analyzer
# Author: Jean-Francois Lachance-Caumartin

PROG       := sizer
VERSION    := 2.0.1

CC         ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS     ?= -O2 -Wall -Wextra
CFLAGS     += -DAPP_VERSION=\"$(VERSION)\"
GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk+-3.0)
GTK_LIBS   := $(shell $(PKG_CONFIG) --libs gtk+-3.0)

# Installation paths (override with: make install PREFIX=/usr/local)
PREFIX     ?= /usr
DESTDIR    ?=
BINDIR      = $(DESTDIR)$(PREFIX)/bin
DATADIR     = $(DESTDIR)$(PREFIX)/share
APPDIR      = $(DATADIR)/applications
ICONDIR     = $(DATADIR)/icons/hicolor
SVGICONDIR  = $(ICONDIR)/scalable/apps
PIXMAPDIR   = $(DATADIR)/pixmaps

ICON_SIZES := 16 32 48 64 128 256

.PHONY: all clean install uninstall icons run

all: $(PROG)

$(PROG): $(PROG).c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS)

# Regenerate PNG icons from the SVG source (requires rsvg-convert).
icons: $(PROG).svg
	@for s in $(ICON_SIZES); do \
		rsvg-convert -w $$s -h $$s $(PROG).svg -o $(PROG)-$$s.png; \
	done

run: $(PROG)
	./$(PROG)

install: $(PROG)
	# Binary
	install -d $(BINDIR)
	install -m 0755 $(PROG) $(BINDIR)/$(PROG)
	# Desktop entry
	install -d $(APPDIR)
	install -m 0644 $(PROG).desktop $(APPDIR)/$(PROG).desktop
	# Scalable SVG icon
	install -d $(SVGICONDIR)
	install -m 0644 $(PROG).svg $(SVGICONDIR)/$(PROG).svg
	# Rasterized icons at standard sizes
	@for s in $(ICON_SIZES); do \
		if [ -f $(PROG)-$$s.png ]; then \
			install -d $(ICONDIR)/$${s}x$${s}/apps; \
			install -m 0644 $(PROG)-$$s.png \
				$(ICONDIR)/$${s}x$${s}/apps/$(PROG).png; \
		fi; \
	done
	# Legacy pixmap fallback
	install -d $(PIXMAPDIR)
	install -m 0644 $(PROG).png $(PIXMAPDIR)/$(PROG).png
	# Refresh caches so the icon shows up immediately
	-gtk-update-icon-cache -f -t $(ICONDIR) 2>/dev/null || true
	-update-desktop-database $(APPDIR) 2>/dev/null || true
	@echo ""
	@echo "Sizer v$(VERSION) installed. Launch with 'sizer' or from your menu."

uninstall:
	rm -f $(BINDIR)/$(PROG)
	rm -f $(APPDIR)/$(PROG).desktop
	rm -f $(SVGICONDIR)/$(PROG).svg
	@for s in $(ICON_SIZES); do \
		rm -f $(ICONDIR)/$${s}x$${s}/apps/$(PROG).png; \
	done
	rm -f $(PIXMAPDIR)/$(PROG).png
	-gtk-update-icon-cache -f -t $(ICONDIR) 2>/dev/null || true
	-update-desktop-database $(APPDIR) 2>/dev/null || true
	@echo "Sizer uninstalled."

clean:
	rm -f $(PROG)
