import time
import csv
import threading
import random
import string
import requests

ESP_IP = " your IP Address " 
BASE_URL = f"http://{ESP_IP}"

DURATION_S = 20             
WORKERS = 8                
TIMEOUT = 1.5

def random_path():
    
    suffix = "".join(random.choices(string.ascii_lowercase + string.digits, k=10))
    return f"/no_such_{suffix}_{int(time.time()*1000)}"

def scan_worker(stop_time: float, results: list, worker_id: int):
    s = requests.Session()
    local_count = 0

    while time.time() < stop_time:
        path = random_path()
        url = BASE_URL + path

        t0 = time.time()
        try:
            r = s.get(url, timeout=TIMEOUT)
            status = r.status_code
        except requests.RequestException:
            status = "NET_ERR"
        dt_ms = int((time.time() - t0) * 1000)

        local_count += 1
        results.append((time.time(), worker_id, local_count, status, dt_ms, path))

        if status == 403:
            
            break

    results.append((time.time(), worker_id, local_count, "DONE", 0, ""))

def main():
    stop_time = time.time() + DURATION_S
    results = []

    print(f"Scan starting: {WORKERS} workers for {DURATION_S}s -> {BASE_URL}/no_such_*")

    threads = []
    for wid in range(WORKERS):
        t = threading.Thread(target=scan_worker, args=(stop_time, results, wid), daemon=True)
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

 
    total_requests = sum(1 for r in results if r[3] not in ("DONE",))
    count_404 = sum(1 for r in results if r[3] == 404)
    count_403 = sum(1 for r in results if r[3] == 403)
    net_errs = sum(1 for r in results if r[3] == "NET_ERR")

    print(f"Total logged requests: {total_requests}")
    print(f"404 responses (scan): {count_404}")
    print(f"403 responses (blocked): {count_403}")
    print(f"Network errors: {net_errs}")

    ts = int(time.time())
    out = f"scan_{ESP_IP}_{ts}.csv"
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["timestamp", "worker_id", "worker_req#", "status", "ms", "path"])
        w.writerows(results)

    print(f"Log saved: {out}")

if __name__ == "__main__":
    main()