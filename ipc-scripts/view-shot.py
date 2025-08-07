#!/usr/bin/python3

import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <app_id>")
    exit(1)

socket = WayfireSocket()
wpe = WPE(socket)

for view in socket.list_views():
    if view["app-id"] == sys.argv[1]:
        wpe.capture_view_shot(view["id"], "/tmp/view-shot.png")
