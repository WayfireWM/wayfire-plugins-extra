#!/usr/bin/python3
#
# This is a simple script which demonstrates how to use the Wayfire OBS plugin using the IPC socket with the wayfire_socket.py helper.
# To use this, make sure that the ipc plugin is first in the plugin list, so that the WAYFIRE_SOCKET environment
# variable propagates to all processes, including autostarted processes. Also make sure to enable stipc and obs plugins.
#
# This script can be run from a terminal to change the opacity, brightness and saturation of views.
# Usage: ./script.py <app-id> <effect> <value> <duration>
# where <effect> is one of opacity, brightness or saturation, <value> is in the range 0-1 and <duration> is the animation duration in milliseconds

import sys
from wayfire import WayfireSocket
from wayfire.extra.wpe import WPE

socket = WayfireSocket()
wpe = WPE(socket)

for v in socket.list_views():
    if v["app-id"] == sys.argv[1]:
        if sys.argv[2] == "opacity":
            wpe.set_view_opacity(v["id"], float(sys.argv[3]), int(sys.argv[4]))
        elif sys.argv[2] == "brightness":
            wpe.set_view_brightness(v["id"], float(sys.argv[3]), int(sys.argv[4]))
        elif sys.argv[2] == "saturation":
            wpe.set_view_saturation(v["id"], float(sys.argv[3]), int(sys.argv[4]))

