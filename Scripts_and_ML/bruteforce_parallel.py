import time
import csv
import threading
import requests

ESP_IP = "Yor IP address"
URL = f"http://{ESP_IP}/login"
PARAMS = {"token": "wrong"}

TIMEOUT = 2.0
SLEEP = 0.005  


def worker(stop_time: float, results: list, wid: int):
    s = requests.Session()
    local_count = 0

    while time.time() < stop_time:
        t0 = time.time()
        try:
            r = s.get(URL, params=PARAMS, timeout=TIMEOUT)
            status = r.status_code
        except requests.RequestException:
            status = "NET_ERR"

        dt_ms = int((time.time() - t0) * 1000)
        local_count += 1
        results.append((time.time(), wid, local_count, status, dt_ms))

        if status == 403:
            break

        time.sleep(SLEEP)

    results.append((time.time(), wid, local_count, "DONE", 0))


def main():

    print("=== PARALLEL BRUTE FORCE START ===")
    workers = int(input("specify number workers (t.ex. 10, 20, 40): "))
    duration = int(input("specify duration in seconds (t.ex. 10, 15, 20): "))
    start_time = time.time()
    stop_time = time.time() + duration
    results = []

    threads = []
    for wid in range(workers):
        t = threading.Thread(target=worker, args=(stop_time, results, wid), daemon=True)
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    end_time = time.time()
    total_runtime = end_time - start_time

    total_requests = sum(1 for r in results if r[3] not in ("DONE",))
    count_401 = sum(1 for r in results if r[3] == 401)
    count_403 = sum(1 for r in results if r[3] == 403)
    net_errs = sum(1 for r in results if r[3] == "NET_ERR")


    print("=== RESULTAT ===")
    print(f"Totala requests: {total_requests}")
    print(f"401 (LOGIN FAIL): {count_401}")
    print(f"403 (BLOCK): {count_403}")
    print(f"NÃ¤tverksfel: {net_errs}")
    print(f"Total runtime: {total_runtime:.2f} seconds")
    

    ts = int(time.time())
    out = f"bruteforce_parallel_{ESP_IP}_{ts}.csv"
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["timestamp", "worker_id", "worker_req#", "status", "ms"])
        w.writerows(results)

    print(f"Log saved: {out}")


if __name__ == "__main__":
    main()