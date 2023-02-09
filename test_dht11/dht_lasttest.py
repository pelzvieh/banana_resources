#!/usr/bin/python3
import sys
import time
import datetime
import pathlib
import sys

platform_dir = pathlib.PosixPath('/sys/devices/platform/')
if not platform_dir.is_dir():
    print("/sys/devices/platform/ is not a directory", file=sys.stderr)
    sys.exit(1)
dht11_device_path = next(next(platform_dir.glob('dht11*')).glob('iio:device*'))
temperature_path = dht11_device_path / 'in_temp_input'
humidity_path = dht11_device_path / 'in_humidityrelative_input'
if not temperature_path.is_file() or not humidity_path.is_file():
    print(f"No char device files found at {temperature_path} or {humidity_path}", file=sys.stderr)
    sys.exit(2)

successes = 0
start_ts = datetime.datetime.now().timestamp()
now_ts = start_ts
inval = 0
timeout = 0
while successes < 10 and now_ts - start_ts < 220:
    now_ts = datetime.datetime.now().timestamp()
    try:
        with open(temperature_path, "r") as temperature_file:
            temperature_c = float(next(temperature_file)) / 1000.0
        with open(humidity_path, "r") as humidity_file:
            humidity = float(next(humidity_file)) / 1000.0
        successes += 1
        print(f"{{\n    \"temperature\": {temperature_c},\n    \"humidity\": {humidity},\n"
              f"    \"timestamp\": \"{now_ts}\"\n}}\n",
              file=sys.stderr
            )
        time.sleep(2)

    except TimeoutError as error:
        print(f"{now_ts} Timeout reading values (edges missing): {format(error)}", file=sys.stderr)
        timeout = timeout + 1

    except OSError as error:
        print(f"{now_ts} Error reading values: {format(error)}", file=sys.stderr)
        inval = inval + 1

print(f"suc/inval/timeout {successes} {inval} {timeout}")

