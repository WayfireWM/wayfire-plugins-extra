#!/usr/bin/python3

import sys

from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <view_id>")
    exit(1)

socket = WayfireSocket()
wpe = WPE(socket)

for view in socket.list_views():
    if view["id"] == int(sys.argv[1]):
        wpe.unpin_view(int(sys.argv[1]))
