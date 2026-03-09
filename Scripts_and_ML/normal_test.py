import time, csv, requests

ESP_IP = " your IP Address "  
URL = f"http://{ESP_IP}/"
DURATION = 120      # 2 min normal trafik
SLEEP = 0.7         # ~1.25 req/s (normal)
TIMEOUT = 2.0

ts = int(time.time())
out = f"normal_{ESP_IP}_{ts}.csv"
s = requests.Session()

end = time.time() + DURATION
rows = []
i = 0

while time.time() < end:
    i += 1
    t0 = time.time()
    try:
        r = s.get(URL, timeout=TIMEOUT)
        status = r.status_code
    except requests.RequestException:
        status = "NET_ERR"
    dt_ms = int((time.time() - t0) * 1000)

    rows.append((time.time(), i, status, dt_ms))
    time.sleep(SLEEP)

with open(out, "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["timestamp", "i", "status", "ms"])
    w.writerows(rows)

print("Saved:", out)