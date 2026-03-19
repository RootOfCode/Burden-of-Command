# =============================================================================
#  Burden of Command — Makefile
#  Targets: all  run  debug  clean  install  uninstall  dist  help
#
#  Works on Linux, macOS, and Windows (MinGW / MSYS2 / Git Bash).
#  On Windows with MSVC, use the Developer Command Prompt directly:
#    cl /O2 /Fe:boc.exe boc.c
# =============================================================================

# ── Source & binary name ──────────────────────────────────────────────────────

SRC     := boc.c
VERSION := 4

# ── OS / compiler detection ───────────────────────────────────────────────────

ifeq ($(OS),Windows_NT)
    # MinGW / MSYS2 / Git Bash on Windows
    TARGET  := boc.exe
    RM      := del /Q /F
    MKDIR   := mkdir
    SEP     := \\
    OPEN    := start
    EXE_EXT := .exe
    # Use gcc if available, otherwise fall back to cc
    CC      ?= gcc
else
    UNAME := $(shell uname -s)
    TARGET  := boc
    RM      := rm -f
    MKDIR   := mkdir -p
    SEP     := /
    EXE_EXT :=
    ifeq ($(UNAME),Darwin)
        OPEN  := open
    else
        OPEN  := xdg-open
    endif
    CC      ?= cc
endif

# ── Compiler flags ────────────────────────────────────────────────────────────

CFLAGS_COMMON := -std=c99 -Wall -Wextra -Wpedantic

# Release: optimised, no debug info
CFLAGS_REL    := $(CFLAGS_COMMON) -O2

# Debug: no optimisation, full debug symbols, AddressSanitizer (POSIX only)
ifeq ($(OS),Windows_NT)
    CFLAGS_DBG := $(CFLAGS_COMMON) -O0 -g
else
    CFLAGS_DBG := $(CFLAGS_COMMON) -O0 -g -fsanitize=address,undefined
    LDFLAGS_DBG := -fsanitize=address,undefined
endif

# ── Install paths (POSIX only) ────────────────────────────────────────────────

PREFIX      ?= /usr/local
BINDIR      := $(PREFIX)/bin
MANDIR      := $(PREFIX)/share/man/man6
DOCDIR      := $(PREFIX)/share/doc/burden-of-command

# ── Dist archive name ─────────────────────────────────────────────────────────

DIST_NAME   := burden-of-command-v$(VERSION)
DIST_FILES  := $(SRC) Makefile README.md description.md

# =============================================================================
#  Primary targets
# =============================================================================

.PHONY: all run debug clean install uninstall dist help

## all — Build the release binary (default)
all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS_REL) -o $@ $<
	@echo ""
	@echo "  Built: $@"
	@echo "  Run:   make run"
	@echo ""

## run — Build (if needed) then launch the game
run: $(TARGET)
	./$(TARGET)

## debug — Build with debug symbols and sanitisers, then run
debug: $(SRC)
	$(CC) $(CFLAGS_DBG) $(LDFLAGS_DBG) -o boc_debug$(EXE_EXT) $<
	@echo "  Debug build: boc_debug$(EXE_EXT)"
	./boc_debug$(EXE_EXT)

# =============================================================================
#  Maintenance targets
# =============================================================================

## clean — Remove compiled binaries and dist archives
clean:
ifeq ($(OS),Windows_NT)
	-$(RM) boc.exe boc_debug.exe $(DIST_NAME).zip 2>NUL
else
	$(RM) boc boc_debug $(DIST_NAME).tar.gz $(DIST_NAME).zip
endif
	@echo "  Cleaned."

# =============================================================================
#  Install / uninstall (POSIX only)
# =============================================================================

## install — Install binary to $(PREFIX)/bin  [POSIX only, default: /usr/local]
install: $(TARGET)
ifeq ($(OS),Windows_NT)
	@echo "  install target is not supported on Windows."
	@echo "  Copy boc.exe to a directory in your PATH manually."
else
	$(MKDIR) $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/boc
	$(MKDIR) $(DESTDIR)$(DOCDIR)
	install -m 644 README.md  $(DESTDIR)$(DOCDIR)/README.md
	@echo ""
	@echo "  Installed to $(DESTDIR)$(BINDIR)/boc"
	@echo "  Docs:        $(DESTDIR)$(DOCDIR)/"
	@echo ""
endif

## uninstall — Remove installed files  [POSIX only]
uninstall:
ifeq ($(OS),Windows_NT)
	@echo "  uninstall target is not supported on Windows."
else
	$(RM) $(DESTDIR)$(BINDIR)/boc
	$(RM) -r $(DESTDIR)$(DOCDIR)
	@echo "  Uninstalled."
endif

# =============================================================================
#  Distribution archive
# =============================================================================

## dist — Create a source distribution archive (.tar.gz on POSIX, .zip on Windows)
dist: $(DIST_FILES)
ifeq ($(OS),Windows_NT)
	@echo "  Creating $(DIST_NAME).zip ..."
	powershell -NoProfile -Command \
	  "Compress-Archive -Path $(subst $(eval) ,',$(DIST_FILES)) \
	   -DestinationPath $(DIST_NAME).zip -Force"
	@echo "  Done: $(DIST_NAME).zip"
else
	@echo "  Creating $(DIST_NAME).tar.gz ..."
	$(MKDIR) $(DIST_NAME)
	cp $(DIST_FILES) $(DIST_NAME)/
	tar -czf $(DIST_NAME).tar.gz $(DIST_NAME)
	$(RM) -r $(DIST_NAME)
	@echo "  Done: $(DIST_NAME).tar.gz"
endif

# =============================================================================
#  Help
# =============================================================================

## help — Print this message
help:
	@echo ""
	@echo "  Burden of Command — build targets"
	@echo "  ─────────────────────────────────────────────────────────────────"
	@echo "  make              Build release binary   →  $(TARGET)"
	@echo "  make run          Build and launch the game"
	@echo "  make debug        Build with sanitisers, then run"
	@echo "  make clean        Remove compiled files and archives"
	@echo "  make install      Install to PREFIX=$(PREFIX)  (POSIX only)"
	@echo "  make uninstall    Remove installed files         (POSIX only)"
	@echo "  make dist         Create source archive"
	@echo "  make help         Print this message"
	@echo ""
	@echo "  Variables:"
	@echo "  CC=$(CC)   (override with: make CC=clang)"
	@echo "  PREFIX=$(PREFIX)   (override with: make install PREFIX=~/.local)"
	@echo ""
	@echo "  Windows MSVC — use the Developer Command Prompt directly:"
	@echo "    cl /O2 /Fe:boc.exe boc.c"
	@echo ""
