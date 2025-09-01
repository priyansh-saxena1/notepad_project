# Simple Client-Server Test: Render + Local Docker

## Setup Instructions

### Part 1: Deploy Receiver on Render

1. **Create a new Render Web Service:**
   - Go to https://render.com and create an account
   - Click "New +" → "Web Service"
   - Connect your GitHub repo or upload these files

2. **Configure the service:**
   - **Build Command:** (leave empty)
   - **Start Command:** `python receiver.py`
   - **Environment:** Python 3
   - **Instance Type:** Free tier is fine
   
3. **Your service will get a URL like:** `https://your-service-name.onrender.com`

### Part 2: Run Sender Locally in Docker

1. **Build the Docker image:**
   ```powershell
   docker build -f Dockerfile.sender.simple -t sender .
   ```

2. **Run the sender container:**
   ```powershell
   # Replace YOUR_RENDER_URL with your actual Render service URL (without https://)
   docker run --rm -e RECEIVER_HOST=your-service-name.onrender.com -e RECEIVER_PORT=443 -e MESSAGE="Hello from Docker!" sender
   ```

   **Alternative with command line arguments:**
   ```powershell
   docker run --rm sender python sender.py your-service-name.onrender.com 443
   ```

### Part 3: Testing and Verification

1. **Check Render logs:**
   - Go to your Render dashboard
   - Click on your service
   - Check the "Logs" tab to see incoming messages

2. **Test with curl first:**
   ```powershell
   # Test if your Render service is accessible
   curl https://your-service-name.onrender.com
   ```

3. **Monitor connection:**
   - Watch Render logs while running the Docker container
   - You should see connection logs and message reception

### Example Commands

**For a service deployed as `myclientserver.onrender.com`:**

```powershell
# Build sender image
docker build -f Dockerfile.sender.simple -t sender .

# Run sender (using HTTPS port 443 for Render)
docker run --rm -e RECEIVER_HOST=myclientserver.onrender.com -e RECEIVER_PORT=443 -e MESSAGE="Test from local Docker" sender

# Or with custom message
docker run --rm -e RECEIVER_HOST=myclientserver.onrender.com -e RECEIVER_PORT=443 -e MESSAGE="Connection test $(Get-Date)" sender
```

### Troubleshooting

- **Connection refused:** Check if your Render service is running
- **Timeout:** Verify the Render URL and that it's publicly accessible
- **Port issues:** Render services use HTTPS (port 443) for external access
- **Logs:** Always check Render logs to see if messages are being received

### Notes

- Render services use HTTPS/SSL, so the connection will be on port 443
- The receiver script automatically uses the PORT environment variable provided by Render
- Free tier Render services may take 30-60 seconds to "wake up" if idle
