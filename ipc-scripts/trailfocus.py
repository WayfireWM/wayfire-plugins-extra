#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)
commands_sock.watch()

outputs = []

def output_realized(output):
    for o in outputs:
        if output == o:
            return True
    return False

def sort_views():
    try:
        views = commands_sock.list_views()
        outputs.clear()
        for v in views:
            if v["app-id"] == "$unfocus panel" or v["layer"] == "background":
                continue
            if "parent" in v and v["state"]["minimized"]:
                continue
            if not output_realized(v["output"]):
                outputs.append(v["output"])
        for o in outputs:
            i = 0
            timestamps = []
            for v in views:
                if v["output"] != o or v["app-id"] == "$unfocus panel" or v["layer"] == "background":
                    continue
                if "parent" in v and v["state"]["minimized"]:
                    continue
                timestamps.append(v["last-focus-timestamp"])
                i += 1
            timestamps.sort()
            o_step = 0.2 / i
            b_step = 0.5 / i
            s_step = 1.0 / i
            o_value = 0.8
            b_value = 0.5
            s_value = 0.0
            for t in timestamps:
                for v in views:
                    if t != v["last-focus-timestamp"]:
                        continue
                    if v["output"] != o or v["app-id"] == "$unfocus panel" or v["layer"] == "background":
                        break
                    if "parent" in v and v["state"]["minimized"]:
                        break
                    o_value += o_step
                    b_value += b_step
                    s_value += s_step
                    commands_sock.set_view_opacity(v["id"], o_value, 1000)
                    commands_sock.set_view_brightness(v["id"], b_value, 1000)
                    commands_sock.set_view_saturation(v["id"], s_value, 1000)
                    break
    except Exception as error:
        print("An exception occurred:", error)
        pass

sort_views()

while True:
    try:
        msg = commands_sock.read_message()
    except KeyboardInterrupt:
        for v in commands_sock.list_views():
            commands_sock.set_view_opacity(v["id"], 1.0, 500)
            commands_sock.set_view_brightness(v["id"], 1.0, 500)
            commands_sock.set_view_saturation(v["id"], 1.0, 500)
        exit(0)

    if "event" in msg and msg["event"] == "view-focused":
        sort_views()
