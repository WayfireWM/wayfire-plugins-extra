#!/usr/bin/python3

import os
import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

if len(sys.argv) < 4:
    print(f"Usage: {sys.argv[0]} <view_id: int> <layer: str> <resize: bool> [workspace_x: int], [workspace_y: int]")
    exit(1)

addr = os.getenv("WAYFIRE_SOCKET")

sock = WayfireSocket(addr)
wpe = WPE(sock)

for view in sock.list_views():
    if view["id"] == int(sys.argv[1]):
        if len(sys.argv) == 4:
            wpe.pin_view(int(sys.argv[1]), sys.argv[2], sys.argv[3].lower() == "true", None, None)
        if len(sys.argv) == 5:
            wpe.pin_view(int(sys.argv[1]), sys.argv[2], sys.argv[3].lower() == "true", int(sys.argv[4]), None)
        elif len(sys.argv) == 6:
            wpe.pin_view(int(sys.argv[1]), sys.argv[2], sys.argv[3].lower() == "true", int(sys.argv[4]), int(sys.argv[5]))
