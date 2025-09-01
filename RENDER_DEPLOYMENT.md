# Render Deployment Guide for Notepad System

## Overview
This guide explains how to deploy the notepad client-server system on Render with:
- A receiver service (C++ backend that accepts and displays messages)
- A sender service (Node.js web app with C++ backend for sending messages)

## Files Structure
```
notepad_project/
├── receiver_headless.cpp    # Headless C++ receiver
├── sender_web.cpp           # C++ sender for web interface
├── web_server.js            # Node.js web server
├── web_gui.html            # Web-based GUI
├── package.json            # Node.js dependencies
├── Dockerfile.receiver     # Docker config for receiver
├── Dockerfile.sender       # Docker config for sender
└── render.yaml             # Render configuration
```

## Step 1: Deploy the Receiver Service

1. **Create a new Web Service** on Render:
   - Connect your GitHub repository
   - Select "Docker" as the environment
   - Set the Dockerfile path to `Dockerfile.receiver`

2. **Configure the receiver service**:
   - Service Name: `notepad-receiver`
   - Region: Choose your preferred region
   - Branch: `main`
   - Dockerfile Path: `Dockerfile.receiver`
   - Port: `10000`

3. **Environment Variables**:
   ```
   PORT=10000
   ```

4. **Deploy**: Click "Create Web Service"

5. **Note the URL**: After deployment, note your receiver URL (e.g., `https://notepad-receiver-xxx.onrender.com`)

## Step 2: Deploy the Sender Web Service

1. **Create another Web Service** on Render:
   - Connect the same GitHub repository
   - Select "Docker" as the environment
   - Set the Dockerfile path to `Dockerfile.sender`

2. **Configure the sender service**:
   - Service Name: `notepad-sender`
   - Region: Same as receiver for better latency
   - Branch: `main`
   - Dockerfile Path: `Dockerfile.sender`
   - Port: `3000`

3. **Environment Variables**:
   ```
   PORT=3000
   RECEIVER_HOST=notepad-receiver-xxx.onrender.com
   RECEIVER_PORT=10000
   ```
   
   Replace `notepad-receiver-xxx.onrender.com` with your actual receiver URL (without https://)

4. **Deploy**: Click "Create Web Service"

## Step 3: Access Your Application

1. **Get your sender service URL** (e.g., `https://notepad-sender-xyz.onrender.com`)
2. **Open in browser**: Navigate to your sender URL
3. **Use the interface**:
   - The receiver host should be pre-filled with your receiver service URL
   - Type text in the textarea
   - Click "Send to Receiver" to transmit the text
   - Check the receiver logs to see the received messages

## Step 4: Monitor Your Services

1. **Receiver Logs**: 
   - Go to Render Dashboard → notepad-receiver → Logs
   - You'll see connection logs and received messages

2. **Sender Logs**:
   - Go to Render Dashboard → notepad-sender → Logs
   - You'll see web requests and C++ sender output

## Example Usage

1. Open `https://your-sender-service.onrender.com`
2. In the web interface:
   - Receiver Host: `your-receiver-service.onrender.com`
   - Receiver Port: `10000`
   - Type your message in the text area
   - Click "Send to Receiver"
3. Check receiver logs to see the message

## Troubleshooting

### Common Issues:

1. **Connection Failed**:
   - Ensure receiver service is running
   - Check receiver service URL is correct (without https://)
   - Verify port 10000 is accessible

2. **Services Not Starting**:
   - Check build logs for compilation errors
   - Ensure all required files are in the repository

3. **Messages Not Appearing**:
   - Check receiver logs for incoming connections
   - Verify sender is connecting to correct host/port

### Service URLs:
- Receiver service typically gets URL like: `notepad-receiver-abc123.onrender.com`
- Sender service typically gets URL like: `notepad-sender-def456.onrender.com`
- Use the receiver's hostname (without https://) in the sender configuration

## Cost Considerations

- Both services use Render's free tier
- Services may sleep after 15 minutes of inactivity
- First request after sleep may take 30+ seconds to wake up
- For production use, consider upgrading to paid plans for always-on services

## Security Notes

- Services are publicly accessible
- No authentication implemented
- Consider adding basic auth for production use
- All traffic between services uses Render's internal network
