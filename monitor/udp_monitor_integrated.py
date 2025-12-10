import socket
import requests
import json
from datetime import datetime

# ================= CONFIGURATION =================
# Port must match the 'udpPort' in your ESP32 code
LISTEN_PORT = 4210

# Your Slack Webhook URL
SLACK_WEBHOOK_URL = "https://hooks.slack.com/services/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" 

# ================= SETUP =================
# Track active alarms to prevent spamming Slack
# Format: { '192.168.1.105': '[AL01] Low Level in Tank' }
active_alarms = {}

# Create UDP Socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

try:
    # "0.0.0.0" listens on all network interfaces (WiFi, Ethernet)
    sock.bind(("0.0.0.0", LISTEN_PORT))
    print(f"Monitoring Server Started.")
    print(f"   Listening on UDP port {LISTEN_PORT}")
    print("   Waiting for Chillers to report status...")
except Exception as e:
    print(f"Error binding to port {LISTEN_PORT}: {e}")
    print("   Tip: Is another instance of this script already running?")
    exit()

# ================= HELPER FUNCTIONS =================

def send_slack_message(text, emoji=":rotating_light:", color="#FF0000"):
    """Send formatted Slack message using webhook."""
    print(f"   -> Sending Slack Alert: {text}")
    
    payload = {
        "attachments": [
            {
                "color": color,
                "blocks": [
                    {
                        "type": "section",
                        "text": {"type": "mrkdwn", "text": f"{emoji} {text}"}
                    },
                    {
                        "type": "context",
                        "elements": [
                            {
                                "type": "mrkdwn",
                                "text": f"Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
                            }
                        ]
                    }
                ]
            }
        ]
    }
    try:
        r = requests.post(SLACK_WEBHOOK_URL, json=payload, timeout=5)
        if r.status_code != 200:
            print(f"   Slack API Error: {r.status_code}, {r.text}")
    except Exception as e:
        print(f"   Failed to send Slack message: {e}")

# ================= MAIN LOOP =================

while True:
    try:
        # 1. Receive Data (Blocking wait)
        # Buffer size 1024 is plenty for text messages
        data, addr = sock.recvfrom(1024)
        
        # 2. Decode Message
        message = data.decode('utf-8').strip()
        sender_ip = addr[0]
        
        # Debug print to see heartbeat in terminal
        # print(f"[{sender_ip}] says: {message}")

        # 3. LOGIC: Handle Alarms
        if message.startswith("ALARM:"):
            # Clean up message (remove "ALARM:" prefix)
            error_msg = message.replace("ALARM:", "").strip()
            
            # Check if this is a NEW error for this specific IP
            current_status = active_alarms.get(sender_ip)
            
            if current_status != error_msg:
                # It's a new error! Alert Slack.
                slack_text = f"*Chiller ({sender_ip})* reported error:\n`{error_msg}`"
                send_slack_message(slack_text, emoji=":warning:", color="#FFA500")
                
                # Update state
                active_alarms[sender_ip] = error_msg
                print(f"New Alarm from {sender_ip}: {error_msg}")

        # 4. LOGIC: Handle Recovery
        # (Assuming your ESP32 sends "System Normal" when alarms clear)
        elif "System Normal" in message:
            # Check if this IP previously had an alarm
            if sender_ip in active_alarms:
                last_error = active_alarms[sender_ip]
                
                # Alert Slack that it is fixed
                slack_text = f"*Chiller ({sender_ip})* has recovered.\nPrevious error `{last_error}` cleared."
                send_slack_message(slack_text, emoji=":white_check_mark:", color="#36A64F")
                
                # Clear from tracker
                del active_alarms[sender_ip]
                print(f"Recovery: {sender_ip} is back to normal.")

    except KeyboardInterrupt:
        print("\nStopping Monitor...")
        break
    except Exception as e:
        print(f"Error in main loop: {e}")