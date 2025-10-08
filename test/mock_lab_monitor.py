import time
import json
import requests
from datetime import datetime
from random import choice

# ---------- CONFIGURATION ----------
SLACK_WEBHOOK_URL = "https://hooks.slack.com/services/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"
ESP32_IDS = [1, 2, 3]      # pretend chillers
ERROR_DICT_FILE = "error_codes.json"
POLL_INTERVAL = 5          # shorter interval for testing
# -----------------------------------

# Load dictionary of chiller error codes
with open(ERROR_DICT_FILE, "r") as f:
    ERROR_CODES = json.load(f)

# Keep track of already-reported errors
active_errors = {}

# ---------- SLACK UTILITIES ----------
def send_slack_message(text, emoji=":rotating_light:", color="#FF0000"):
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

# ---------- MOCK POLL FUNCTION ----------
def poll_chillers():
    # Randomly simulate errors for testing
    for slave_id in ESP32_IDS:
        error_code = choice([0, 0, 101, 202])  # mostly OK, sometimes error
        prev_code = active_errors.get(slave_id, 0)

        # New error
        if error_code != 0 and error_code != prev_code:
            desc = ERROR_CODES.get(str(error_code), "Unknown error code")
            msg = f"*Chiller {slave_id}* reported error `{error_code}`: {desc}"
            send_slack_message(msg, emoji=":warning:", color="#FFA500")
            active_errors[slave_id] = error_code
            print(f"Alert sent for chiller {slave_id}: {error_code}")

        # Error cleared
        elif error_code == 0 and prev_code != 0:
            send_slack_message(
                f"*Chiller {slave_id}* recovered. Previous error `{prev_code}` cleared.",
                emoji=":white_check_mark:",
                color="#36A64F"
            )
            del active_errors[slave_id]
            print(f"Chiller {slave_id} recovered.")

# ---------- MAIN ----------
print("🔧 Lab Monitoring System (mock) started.")
try:
    while True:
        poll_chillers()
        time.sleep(POLL_INTERVAL)
except KeyboardInterrupt:
    print("\nStopping Lab Monitor...")
