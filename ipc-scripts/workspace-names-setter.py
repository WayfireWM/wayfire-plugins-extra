#!/usr/bin/python3

from wayfire import WayfireSocket
import json
import sys

# Simple script to set workspace names

if len(sys.argv) != 2:
    print("Invalid usage, exactly one argument required: a workspace name for the current output and workspace.")
    print(len(sys.argv))
    exit(-1)

sock = WayfireSocket()

output = sock.get_focused_output()
output_name = output["name"]
current_workspace = str(int(output["workspace"]["x"]) + int(output["workspace"]["y"]) * int(output["workspace"]["grid_width"]) + 1)
json_string = "{\"workspace-names/" + output_name + "_workspace_" + current_workspace + "\": \"" + sys.argv[1] + "\"}"
sock.set_option_values(json.loads(json_string))
