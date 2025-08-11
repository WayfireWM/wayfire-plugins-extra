#!/usr/bin/python3

from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

socket = WayfireSocket()
wpe = WPE(socket)
socket.watch(['view-focused'])

def sort_views():
    try:
        views = socket.list_views()
        outputs = socket.list_outputs()
        for o in outputs:
            i = 0
            timestamps = []
            for v in views:
                if v["output-name"] != o["name"] or v["role"] == "desktop-environment":
                    continue
                if not v["mapped"]:
                    continue
                if "parent" in v and v["minimized"]:
                    continue
                if v["last-focus-timestamp"] <= 0:
                    continue
                timestamps.append(v["last-focus-timestamp"])
                i += 1
            if i == 0:
                continue
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
                    if v["output-name"] != o["name"] or v["role"] == "desktop-environment":
                        break
                    if not v["mapped"]:
                        break
                    if "parent" in v and v["minimized"]:
                        break
                    o_value += o_step
                    b_value += b_step
                    s_value += s_step
                    wpe.set_view_opacity(v["id"], o_value, 1000)
                    wpe.set_view_brightness(v["id"], b_value, 1000)
                    wpe.set_view_saturation(v["id"], s_value, 1000)
                    break
    except Exception as error:
        print("An exception occurred:", error)
        pass

sort_views()

while True:
    try:
        msg = socket.read_next_event()
    except KeyboardInterrupt:
        for v in socket.list_views():
            if v["role"] == "desktop-environment":
                continue
            wpe.set_view_opacity(v["id"], 1.0, 500)
            wpe.set_view_brightness(v["id"], 1.0, 500)
            wpe.set_view_saturation(v["id"], 1.0, 500)
        exit(0)
    sort_views()
