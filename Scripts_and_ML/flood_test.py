import time
import csv
import threading
import requests

ESP_IP = " your IP Address "  
BASE_URL = f"http://{ESP_IP}/"

TIMEOUT = 4.0


def flood_worker(stop_time: float, results: list, worker_id: int):
    s = requests.Session()
    local_count = 0

    while time.time() < stop_time:
        t0 = time.time()
        try:
            r = s.get(BASE_URL, timeout=TIMEOUT)
            status = r.status_code
        except requests.RequestException:
            status = "NET_ERR"

        dt_ms = int((time.time() - t0) * 1000)

        local_count += 1
        results.append((time.time(), worker_id, local_count, status, dt_ms))

        time.sleep(0.012)  

        if status == 403:
            break

    results.append((time.time(), worker_id, local_count, "DONE", 0))


def main():

    print("=== FLOOD TEST START ===")
    
    # 🔹 Interaktiv input
    workers = int(input("specify number workers (t.ex. 20, 50, 80): "))
    duration = int(input("specify duration in seconds (t.ex. 15, 20): "))

    print(f"\nStartar flood mot {BASE_URL}")
    print(f"Workers: {workers}")
    print(f"Duration: {duration} seconds\n")

    stop_time = time.time() + duration
    results = []

    threads = []

    for wid in range(workers):
        t = threading.Thread(
            target=flood_worker,
            args=(stop_time, results, wid),
            daemon=True
        )
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    # ===== Summering =====
    total_requests = sum(1 for r in results if r[3] not in ("DONE",))
    count_403 = sum(1 for r in results if r[3] == 403)
    net_errs = sum(1 for r in results if r[3] == "NET_ERR")

    print("=== RESULTAT ===")
    print(f"Totala requests: {total_requests}")
    print(f"403 (BLOCK): {count_403}")
    print(f"Nätverksfel: {net_errs}")

    # ===== save logg =====
    ts = int(time.time())
    filename = f"flood_{ESP_IP}_{ts}.csv"

    with open(filename, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["timestamp", "worker_id", "worker_req#", "status", "ms"])
        w.writerows(results)

    print(f"Logg sparad som: {filename}")


if __name__ == "__main__":
    main()