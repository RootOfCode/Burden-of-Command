#!/bin/bash
# ================================================================
# build.sh — Burden of Command
# Produces a self-contained binary in the same directory as this script.
# Usage:  ./build.sh
# ================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT="$SCRIPT_DIR/boc"
LISP="$SCRIPT_DIR/boc.lisp"

echo "Building Burden of Command..."
echo "  Source : $LISP"
echo "  Output : $OUTPUT"
echo ""

sbcl --no-sysinit --no-userinit \
     --eval "(push :building *features*)" \
     --load "$LISP" \
     --eval "(sb-ext:save-lisp-and-die \"$OUTPUT\" \
               :toplevel #'cl-user::main \
               :executable t \
               :purify t)" \
     2>/dev/null

if [ -f "$OUTPUT" ]; then
    chmod +x "$OUTPUT"
    SIZE=$(du -sh "$OUTPUT" | cut -f1)
    echo "Done!  Binary: $OUTPUT  ($SIZE)"
    echo "Run with:  $OUTPUT"
else
    echo "Build failed. Make sure sbcl is installed and boc.lisp is present."
    exit 1
fi
