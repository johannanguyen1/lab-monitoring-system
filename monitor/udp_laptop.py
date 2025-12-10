import socket

# --- CONFIGURATION ---
LISTEN_PORT = 4210 # Must match the port in ESP32 code
# ---------------------

# Create a UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bind the socket to the port. 
# "0.0.0.0" means "listen on all available network adapters" (WiFi, Ethernet, etc.)
try:
    sock.bind(("0.0.0.0", LISTEN_PORT))
    print(f"Laptop is listening for UDP packets on port {LISTEN_PORT}...")
    print("waiting for ESP32...")
except Exception as e:
    print(f"Error binding to port {LISTEN_PORT}: {e}")
    exit()

while True:
    try:
        # Receive data (buffer size 1024 bytes)
        data, addr = sock.recvfrom(1024)
        message = data.decode('utf-8')
        
        # Print the received message
        print(f"📩 Received from {addr[0]}: {message}")
        
    except KeyboardInterrupt:
        print("\nStopping test...")
        break
    except Exception as e:
        print(f"Error: {e}")