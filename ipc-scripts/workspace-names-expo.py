#!/usr/bin/python3

#
# The MIT License (MIT)
#
# Copyright (c) 2025 Scott Moreau <oreaus@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

from wayfire import WayfireSocket
import json

# Simple script to show workspace names when expo is active

sock = WayfireSocket()

sock.watch(["plugin-activation-state-changed"])

def show_option_values(enabled):
    json_string = "{\"workspace-names/show_option_values\": \"true\"}"
    sock.set_option_values(json.loads(json_string))
    if enabled:
        json_string = "{\"workspace-names/show_option_names\": \"true\"}"
    else:
        json_string = "{\"workspace-names/show_option_names\": \"false\"}"
    sock.set_option_values(json.loads(json_string))

while True:
    try:
        msg = sock.read_next_event()
        if "event" in msg:
            plugin_changed_info = msg
            if plugin_changed_info["plugin"] == "expo":
                if plugin_changed_info["state"] == True:
                    show_option_values(True)
                elif plugin_changed_info["state"] == False:
                    show_option_values(False)
    except KeyboardInterrupt:
        exit(0)
