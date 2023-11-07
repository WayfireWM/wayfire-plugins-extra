#!/usr/bin/python3
#
# This is a simple script which demonstrates how to use the Wayfire OBS plugin using the IPC socket with the wayfire_socket.py helper.
# To use this, make sure that the ipc plugin is first in the plugin list, so that the WAYFIRE_SOCKET environment
# variable propagates to all processes, including autostarted processes. Also make sure to enable stipc and obs plugins.
#
# This script can be run from a terminal to change the opacity, brightness and saturation of views.
# Usage: ./script.py <app-id> <effect> <value> <duration>
# where <effect> is one of opacity, brightness or saturation, <value> is in the range 0-1 and <duration> is the animation duration in milliseconds

import os
import sys
from wayfire_socket import *

addr = os.getenv('WAYFIRE_SOCKET')

# Important: we connect to Wayfire's IPC two times. The one socket is used for reading events (view-mapped, view-focused, etc).
# The other is used for sending commands and querying Wayfire.
# We could use the same socket, but this would complicate reading responses, as events and query responses would be mixed with just one socket.
commands_sock = WayfireSocket(addr)

for v in commands_sock.list_views():
    if v["app-id"] == sys.argv[1]:
        if sys.argv[2] == "opacity":
            commands_sock.set_view_opacity(v["id"], float(sys.argv[3]), int(sys.argv[4]))
        elif sys.argv[2] == "brightness":
            commands_sock.set_view_brightness(v["id"], float(sys.argv[3]), int(sys.argv[4]))
        elif sys.argv[2] == "saturation":
            commands_sock.set_view_saturation(v["id"], float(sys.argv[3]), int(sys.argv[4]))

