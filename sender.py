#!/usr/bin/env python3
import socket
import sys
import os
from datetime import datetime

def main():
    # Get receiver details from environment variables or command line
    receiver_host = os.environ.get('RECEIVER_HOST', 'localhost')
    receiver_port = int(os.environ.get('RECEIVER_PORT', '5000'))
    
    # Override with command line arguments if provided
    if len(sys.argv) >= 2:
        receiver_host = sys.argv[1]
    if len(sys.argv) >= 3:
        receiver_port = int(sys.argv[2])
    
    message = os.environ.get('MESSAGE', 'Hello from sender!')
    
    print(f"🚀 Sender starting...")
    print(f"📡 Target: {receiver_host}:{receiver_port}")
    print(f"💬 Message: {message}")
    print(f"⏰ Time: {datetime.now()}")
    
    try:
        # Create socket and connect
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10)  # 10 second timeout
        
        print(f"🔗 Connecting to {receiver_host}:{receiver_port}...")
        sock.connect((receiver_host, receiver_port))
        print("✅ Connected successfully!")
        
        # Send message
        sock.send(message.encode('utf-8'))
        print(f"📤 Message sent: {message}")
        
        # Wait for response
        response = sock.recv(1024).decode('utf-8')
        print(f"📨 Response from server: {response}")
        print("🎉 Test completed successfully!")
        
    except socket.timeout:
        print("❌ Connection timed out!")
        sys.exit(1)
    except ConnectionRefusedError:
        print("❌ Connection refused! Is the server running?")
        sys.exit(1)
    except Exception as e:
        print(f"❌ Error: {e}")
        sys.exit(1)
    finally:
        try:
            sock.close()
        except:
            pass

if __name__ == "__main__":
    main()