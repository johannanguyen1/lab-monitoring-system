import requests

url = "https://hooks.slack.com/services/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"
payload = {"text": "slack webhook test from johanna!"}

r = requests.post(url, json=payload)
print("Status:", r.status_code)
