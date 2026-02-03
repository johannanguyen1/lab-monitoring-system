import time
import json
import requests
from datetime import datetime
from pymodbus.client import ModbusSerialClient

# ---------- CONFIGURATION ----------
SLACK_WEBHOOK_URL = "https://hooks.slack.com/services/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" 
PORT = "/dev/ttyUSB0"      # RS-485 USB adapter port
BAUDRATE = 9600
ESP32_IDS = [1, 2, 3]      # each corresponds to a chiller station
ERROR_DICT_FILE = "error_codes.json"
POLL_INTERVAL = 10         # seconds between checks
# -----------------------------------

# Load dictionary of chiller error codes
with open(ERROR_DICT_FILE, "r") as f:
    ERROR_CODES = json.load(f)

# Set up Modbus client (Raspberry Pi as master)
client = ModbusSerialClient(
    port=PORT,
    baudrate=BAUDRATE,
    parity='N',
    stopbits=1,
    bytesize=8,
    timeout=2
)
client.connect()

# Keep track of already-reported errors
active_errors = {}

# ---------- SLACK UTILITIES ----------
def send_slack_message(text, emoji=":rotating_light:", color="#FF0000"):
    """Send formatted Slack message using webhook."""
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
            print(f"Slack error: {r.status_code}, {r.text}")
    except Exception as e:
        print("Failed to send Slack message:", e)

# ---------- CORE MONITOR LOOP ----------
def poll_chillers():
    for slave_id in ESP32_IDS:
        try:
            # Each ESP32 exposes error code at input register 0
            rr = client.read_input_registers(address=0, count=1, slave=slave_id)
            if rr.isError():
                print(f"Failed to read chiller {slave_id}")
                continue

            error_code = rr.registers[0]
            prev_code = active_errors.get(slave_id, 0)

            # If new error appears
            if error_code != 0 and error_code != prev_code:
                desc = ERROR_CODES.get(str(error_code), "Unknown error code")
                msg = f"*Chiller {slave_id}* reported error `{error_code}`: {desc}"
                send_slack_message(msg, emoji=":warning:", color="#FFA500")
                active_errors[slave_id] = error_code
                print(f"Alert sent for chiller {slave_id}: {error_code}")

            # If error cleared
            elif error_code == 0 and prev_code != 0:
                send_slack_message(
                    f"*Chiller {slave_id}* recovered. Previous error `{prev_code}` cleared.",
                    emoji=":white_check_mark:",
                    color="#36A64F"
                )
                del active_errors[slave_id]
                print(f"Chiller {slave_id} recovered.")

        except Exception as e:
            print(f"Error polling chiller {slave_id}: {e}")

# ---------- MAIN ----------
print("Lab Monitoring System started.")
try:
    while True:
        poll_chillers()
        time.sleep(POLL_INTERVAL)
except KeyboardInterrupt:
    print("\nStopping Lab Monitor...")
finally:
    client.close()
