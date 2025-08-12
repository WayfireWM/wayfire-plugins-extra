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
import signal
import json
import sys
import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gtk, GLib

# Simple script to set workspace names with a gui

sock = WayfireSocket()

class MyWindow(Gtk.ApplicationWindow):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.set_default_size(400, 300)
        self.set_title("Workspace Names Setter")
        self.box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.entry = Gtk.Entry()
        self.entry.set_placeholder_text("Workspace " + self.get_workspace_number())
        self.entry.connect("activate", self.on_entry_activate)
        self.button = Gtk.Button()
        self.button.set_label("Set Workspace Name")
        self.button.connect("clicked", self.on_button_clicked)
        self.box.append(self.entry)
        self.box.append(self.button)
        self.set_child(self.box)

        self.connect("close-request", self.on_window_close)
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGINT, self.on_sigint)

    def on_window_close(self, window):
        self.get_application().quit()

    def on_sigint(self):
        self.get_application().quit()

    def get_workspace_number(self):
        output = sock.get_focused_output()
        return str(int(output["workspace"]["x"]) + int(output["workspace"]["y"]) * int(output["workspace"]["grid_width"]) + 1)

    def set_workspace_name(self, name):
        output = sock.get_focused_output()
        output_name = output["name"]
        current_workspace = self.get_workspace_number()
        json_string = "{\"workspace-names/" + output_name + "_workspace_" + current_workspace + "\": \"" + name + "\"}"
        try:
            sock.set_option_values(json.loads(json_string))
            exit(0)
        except Exception as e:
            print(e)
            print("The option \"" + output_name + "_workspace_" + current_workspace + " = Name\" must exist in the [workspace-names] section of the wayfire config file before using this application.")
            dialog = Gtk.MessageDialog(
                transient_for=self,
                modal=True,
                message_type=Gtk.MessageType.ERROR,
                buttons=Gtk.ButtonsType.OK,
                text=f"{e}\n\nThe option \"" + output_name + "_workspace_" + current_workspace + " = Name\" must exist in the [workspace-names] section of the wayfire config file before using this application.",
            )
            dialog.connect("response", self.on_dialog_response)
            dialog.present()

    def on_dialog_response(self, dialog, response_id):
        dialog.destroy()
        exit(0)

    def on_entry_activate(self, entry):
        name = self.entry.get_text()
        if name:
            self.set_workspace_name(name)

    def on_button_clicked(self, button):
        name = self.entry.get_text()
        if name:
            self.set_workspace_name(name)

def on_activate(app):
    win = MyWindow(application=app)
    win.present()

if __name__ == "__main__":
    app = Gtk.Application(application_id="wayfire.workspace-names.setter")
    app.connect("activate", on_activate)
    app.run(None)
