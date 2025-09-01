#!/bin/bash
#
# Build script for P2P Collaborative Notepad
#

set -e  # Exit on any error

echo "=== P2P Collaborative Notepad Build Script ==="

# Check for required tools
echo "Checking build dependencies..."

if ! command -v g++ &> /dev/null; then
    echo "ERROR: g++ compiler not found. Please install build tools:"
    echo "  Ubuntu/Debian: sudo apt-get install build-essential"
    exit 1
fi

if ! ldconfig -p | grep libfltk &> /dev/null; then
    echo "ERROR: FLTK library not found. Please install it with:"
    echo "  sudo apt-get install libfltk1.3-dev"
    exit 1
fi

echo "✓ All dependencies found"

# Check C++17 support
echo "Checking C++17 support..."
GCC_VERSION=$(g++ -dumpversion | cut -d. -f1)
if [ "$GCC_VERSION" -lt 7 ]; then
    echo "WARNING: GCC version $GCC_VERSION may not fully support C++17"
    echo "         Minimum recommended version is GCC 7"
fi

# Common FLTK libs
FLTK_LIBS="-lfltk -lX11 -lXext -lXft -lXinerama -lpthread"

# Build sender
echo "Building sender..."
g++ -std=c++17 -Wall -Wextra -O2 sender.cpp $FLTK_LIBS -o sender
echo "✓ Sender built successfully"

# Build receiver
echo "Building receiver..."
g++ -std=c++17 -Wall -Wextra -O2 receiver.cpp $FLTK_LIBS -o receiver
echo "✓ Receiver built successfully"

echo ""
echo "=== Build Complete ==="
echo "Executables created:"
echo "  ./sender   - Collaborative text editor (sender)"
echo "  ./receiver - Collaborative text editor (receiver)"
echo ""
echo "Quick start:"
echo "  1. Run './receiver' in one terminal"
echo "  2. Run './sender' in another terminal"
echo "  3. Type in the sender window and see it appear in receiver"
echo ""
echo "For network setup, see README.md"

