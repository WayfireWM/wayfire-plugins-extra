#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <view_id>")
    exit(1)

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

for view in commands_sock.list_views():
    if view["id"] == int(sys.argv[1]):
        commands_sock.unpin_view(int(sys.argv[1]))
