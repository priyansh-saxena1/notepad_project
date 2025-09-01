#!/usr/bin/env python3
import socket
import os
from datetime import datetime

def main():
    # Get port from environment variable (Render provides PORT)
    port = int(os.environ.get('PORT', 5000))
    host = '0.0.0.0'  # Listen on all interfaces for Render
    
    print(f"Starting receiver server on {host}:{port}")
    print(f"Time: {datetime.now()}")
    
    # Create socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        sock.bind((host, port))
        sock.listen(5)
        print(f"✅ Server listening on port {port}")
        print("Waiting for connections...")
        
        while True:
            client_sock, client_addr = sock.accept()
            print(f"\n🔗 Connection from {client_addr}")
            
            try:
                # Receive data
                data = client_sock.recv(1024).decode('utf-8')
                if data:
                    print(f"📨 Received: {data}")
                    print(f"⏰ Time: {datetime.now()}")
                    
                    # Send acknowledgment back
                    response = f"Message received: {data}"
                    client_sock.send(response.encode('utf-8'))
                    print(f"📤 Sent acknowledgment back")
                
            except Exception as e:
                print(f"❌ Error handling client: {e}")
            finally:
                client_sock.close()
                print(f"🔌 Connection closed with {client_addr}")
                
    except Exception as e:
        print(f"❌ Server error: {e}")
    finally:
        sock.close()
        print("🛑 Server stopped")

if __name__ == "__main__":
    main()