import sys, time
chunks = []
total = 0
try:
    for i in range(60):
        chunks.append(bytearray(10*1024*1024))  # 10 MiB anon
        for j in range(0, len(chunks[-1]), 4096):
            chunks[-1][j] = 1
        total += 10
except MemoryError:
    pass
print(f"allocated {total} MiB", flush=True)
time.sleep(3)
del chunks
print("released", flush=True)
