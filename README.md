# Notepad Client-Server System

A simple notepad-like client-server system built with C++ for the backend and a web interface for user interaction, designed for deployment on Render.

## Overview

This system consists of:
- **Receiver Service**: C++ application that listens for incoming text messages and displays them
- **Sender Service**: Web application with Node.js frontend and C++ backend for sending messages
- **Web GUI**: Simple HTML/CSS/JavaScript interface for typing and sending text

## Quick Start

### Local Development

1. **Build and test the C++ components**:
   ```bash
   # On Linux/Mac
   chmod +x test_local.sh
   ./test_local.sh
   
   # On Windows (PowerShell)
   .\test_local.ps1
   ```

2. **Run the web interface**:
   ```bash
   npm install
   npm start
   ```

3. **Access the web GUI**: Open http://localhost:3000

### Render Deployment

1. **Deploy both services** following the guide in `RENDER_DEPLOYMENT.md`
2. **Configure the sender** to connect to your receiver service URL
3. **Access your deployed web interface** and start sending messages!

## File Structure

### Core C++ Files
- `receiver_headless.cpp` - Headless receiver service
- `sender_web.cpp` - C++ sender that accepts input from stdin

### Web Interface
- `web_server.js` - Node.js web server
- `web_gui.html` - Web-based notepad interface
- `package.json` - Node.js dependencies

### Deployment
- `Dockerfile.receiver` - Docker configuration for receiver service
- `Dockerfile.sender` - Docker configuration for sender web service
- `render.yaml` - Render deployment configuration
- `RENDER_DEPLOYMENT.md` - Detailed deployment guide

### Testing
- `test_local.sh` - Local testing script (Linux/Mac)
- `test_local.ps1` - Local testing script (Windows)

## How It Works

1. **Receiver Service** listens on a configurable port (default: 10000)
2. **Web Interface** allows users to type text and specify receiver connection details
3. **Sender Service** receives text via web API, spawns C++ sender process
4. **C++ Sender** connects to receiver and transmits the text using the original protocol
5. **Receiver** displays received messages in its logs

## Original C++ Logic

The system preserves the original socket-based communication protocol from `sender.cpp` and `receiver.cpp`:
- Custom packet format with operations (Insert, Delete, Replace)
- Sequence numbering and packet ordering
- Heartbeat mechanism for connection management
- Document model with line-based text operations

## Web Interface Features

- Simple, clean interface for typing text
- Configurable receiver host/port settings
- Real-time status updates and logging
- Auto-save text to browser localStorage
- Responsive design for mobile and desktop

## Deployment Options

### Render (Recommended)
- Free tier available
- Automatic builds from Git
- Managed SSL certificates
- See `RENDER_DEPLOYMENT.md` for details

### Docker
- Pre-built Dockerfiles for both services
- Can be deployed on any Docker-compatible platform
- Suitable for local development and testing

## Environment Variables

### Receiver Service
- `PORT` - Port to listen on (default: 10000)

### Sender Service
- `PORT` - Web server port (default: 3000)
- `RECEIVER_HOST` - Default receiver hostname
- `RECEIVER_PORT` - Default receiver port

## Security Considerations

- Services are publicly accessible when deployed
- No authentication implemented
- Consider adding basic auth for production use
- All communication is unencrypted (suitable for demos/testing)

## License

This project is provided as-is for educational purposes.
