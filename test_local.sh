#!/bin/bash

# Local test script for the notepad system

echo "=== Building C++ components ==="

# Build receiver
echo "Building receiver..."
g++ -o receiver_headless receiver_headless.cpp -pthread
if [ $? -ne 0 ]; then
    echo "Failed to build receiver"
    exit 1
fi

# Build sender
echo "Building sender..."
g++ -o sender_web sender_web.cpp -pthread
if [ $? -ne 0 ]; then
    echo "Failed to build sender"
    exit 1
fi

echo "=== C++ components built successfully ==="

echo ""
echo "=== Testing the system ==="

# Start receiver in background
echo "Starting receiver on port 10000..."
./receiver_headless 10000 &
RECEIVER_PID=$!

# Wait a moment for receiver to start
sleep 2

# Test sender with sample text
echo "Testing sender with sample text..."
echo "Hello from local test!" | ./sender_web localhost 10000

# Wait for transmission
sleep 2

# Kill receiver
kill $RECEIVER_PID

echo ""
echo "=== Local test completed ==="
echo ""
echo "To run the web interface:"
echo "1. Install Node.js dependencies: npm install"
echo "2. Start the web server: npm start"
echo "3. Open http://localhost:3000 in your browser"
echo "4. Make sure receiver is running: ./receiver_headless 10000"
