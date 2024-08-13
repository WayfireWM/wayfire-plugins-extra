#!/usr/bin/python3

import os
import sys
from wayfire_socket import *

if len(sys.argv) < 3:
    print(f"Usage: {sys.argv[0]} <view_id> <layer> [workspace_x], [workspace_y]")
    exit(1)

addr = os.getenv('WAYFIRE_SOCKET')

commands_sock = WayfireSocket(addr)

for view in commands_sock.list_views():
    if view["id"] == int(sys.argv[1]):
        if len(sys.argv) == 3:
            commands_sock.pin_view(int(sys.argv[1]), sys.argv[2], None, None)
        if len(sys.argv) == 4:
            commands_sock.pin_view(int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), None)
        elif len(sys.argv) == 5:
            commands_sock.pin_view(int(sys.argv[1]), sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
