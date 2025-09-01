# Local test script for the notepad system (PowerShell)

Write-Host "=== Building C++ components ===" -ForegroundColor Green

# Build receiver
Write-Host "Building receiver..." -ForegroundColor Yellow
g++ -o receiver_headless.exe receiver_headless.cpp -pthread
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to build receiver" -ForegroundColor Red
    exit 1
}

# Build sender
Write-Host "Building sender..." -ForegroundColor Yellow
g++ -o sender_web.exe sender_web.cpp -pthread
if ($LASTEXITCODE -ne 0) {
    Write-Host "Failed to build sender" -ForegroundColor Red
    exit 1
}

Write-Host "=== C++ components built successfully ===" -ForegroundColor Green

Write-Host ""
Write-Host "=== Testing the system ===" -ForegroundColor Green

# Start receiver in background
Write-Host "Starting receiver on port 10000..." -ForegroundColor Yellow
$receiverProcess = Start-Process -FilePath ".\receiver_headless.exe" -ArgumentList "10000" -PassThru

# Wait a moment for receiver to start
Start-Sleep -Seconds 2

# Test sender with sample text
Write-Host "Testing sender with sample text..." -ForegroundColor Yellow
"Hello from local test!" | .\sender_web.exe localhost 10000

# Wait for transmission
Start-Sleep -Seconds 2

# Kill receiver
if ($receiverProcess -and !$receiverProcess.HasExited) {
    $receiverProcess.Kill()
    Write-Host "Stopped receiver process" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Local test completed ===" -ForegroundColor Green
Write-Host ""
Write-Host "To run the web interface:" -ForegroundColor Cyan
Write-Host "1. Install Node.js dependencies: npm install" -ForegroundColor White
Write-Host "2. Start the web server: npm start" -ForegroundColor White
Write-Host "3. Open http://localhost:3000 in your browser" -ForegroundColor White
Write-Host "4. Make sure receiver is running: .\receiver_headless.exe 10000" -ForegroundColor White
