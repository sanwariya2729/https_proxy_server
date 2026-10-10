#!/usr/bin/env python3
"""
Load testing tool for the Secure HTTP Forward Proxy

Sends concurrent requests through the proxy and measures:
- Throughput (requests/second)
- Latency percentiles (p50, p95, p99)
- Error rate
- Success rate

Usage:
    python3 load_test.py --proxy http://127.0.0.1:8080 --target http://localhost:9000 --requests 1000 --concurrency 10
"""

import argparse
import time
import requests
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from typing import List

@dataclass
class RequestResult:
    success: bool
    duration_ms: float
    status_code: int = 0
    error: str = ""

def send_request(proxy_url: str, target_url: str, timeout: int) -> RequestResult:
    """Send a single request through the proxy"""
    start = time.time()

    try:
        proxies = {"http": proxy_url}
        response = requests.get(target_url, proxies=proxies, timeout=timeout)
        duration_ms = (time.time() - start) * 1000

        return RequestResult(
            success=(200 <= response.status_code < 300),
            duration_ms=duration_ms,
            status_code=response.status_code
        )
    except Exception as e:
        duration_ms = (time.time() - start) * 1000
        return RequestResult(
            success=False,
            duration_ms=duration_ms,
            error=str(e)
        )

def run_load_test(proxy_url: str, target_url: str, num_requests: int,
                  concurrency: int, timeout: int) -> List[RequestResult]:
    """Run load test with specified parameters"""
    print(f"Starting load test:")
    print(f"  Proxy: {proxy_url}")
    print(f"  Target: {target_url}")
    print(f"  Total requests: {num_requests}")
    print(f"  Concurrency: {concurrency}")
    print(f"  Timeout: {timeout}s")
    print()

    results = []
    start_time = time.time()

    with ThreadPoolExecutor(max_workers=concurrency) as executor:
        futures = [
            executor.submit(send_request, proxy_url, target_url, timeout)
            for _ in range(num_requests)
        ]

        completed = 0
        for future in as_completed(futures):
            result = future.result()
            results.append(result)
            completed += 1

            if completed % 100 == 0 or completed == num_requests:
                print(f"Progress: {completed}/{num_requests} requests completed", end='\r')

    total_time = time.time() - start_time
    print(f"\nTotal time: {total_time:.2f}s")

    return results

def print_statistics(results: List[RequestResult], total_time: float):
    """Print test statistics"""
    successful = [r for r in results if r.success]
    failed = [r for r in results if not r.success]

    print("\n=== Results ===")
    print(f"Total requests: {len(results)}")
    print(f"Successful: {len(successful)} ({len(successful)/len(results)*100:.1f}%)")
    print(f"Failed: {len(failed)} ({len(failed)/len(results)*100:.1f}%)")
    print(f"Throughput: {len(results)/total_time:.2f} req/s")

    if successful:
        latencies = [r.duration_ms for r in successful]
        latencies.sort()

        print("\n=== Latency (ms) ===")
        print(f"Min: {min(latencies):.2f}")
        print(f"Mean: {statistics.mean(latencies):.2f}")
        print(f"Median (p50): {statistics.median(latencies):.2f}")
        print(f"p95: {latencies[int(len(latencies) * 0.95)]:.2f}")
        print(f"p99: {latencies[int(len(latencies) * 0.99)]:.2f}")
        print(f"Max: {max(latencies):.2f}")

    if failed:
        print("\n=== Failure Breakdown ===")
        error_counts = {}
        for r in failed:
            error_type = r.error.split(':')[0] if r.error else f"HTTP {r.status_code}"
            error_counts[error_type] = error_counts.get(error_type, 0) + 1

        for error_type, count in sorted(error_counts.items(), key=lambda x: -x[1]):
            print(f"  {error_type}: {count}")

def main():
    parser = argparse.ArgumentParser(description='Load test the HTTP proxy')
    parser.add_argument('--proxy', required=True, help='Proxy URL (e.g., http://127.0.0.1:8080)')
    parser.add_argument('--target', required=True, help='Target URL (e.g., http://localhost:9000)')
    parser.add_argument('--requests', type=int, default=1000, help='Total number of requests')
    parser.add_argument('--concurrency', type=int, default=10, help='Concurrent workers')
    parser.add_argument('--timeout', type=int, default=30, help='Request timeout in seconds')

    args = parser.parse_args()

    start_time = time.time()
    results = run_load_test(
        args.proxy,
        args.target,
        args.requests,
        args.concurrency,
        args.timeout
    )
    total_time = time.time() - start_time

    print_statistics(results, total_time)

if __name__ == '__main__':
    main()
