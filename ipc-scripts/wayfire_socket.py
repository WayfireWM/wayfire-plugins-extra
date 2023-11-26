import socket
import json as js

def get_msg_template(method: str):
    # Create generic message template
    message = {}
    message["method"] = method
    message["data"] = {}
    return message

class WayfireSocket:
    def __init__(self, socket_name):
        self.client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.client.connect(socket_name)

    def read_exact(self, n):
        response = bytes()
        while n > 0:
            read_this_time = self.client.recv(n)
            if not read_this_time:
                raise Exception("Failed to read anything from the socket!")
            n -= len(read_this_time)
            response += read_this_time

        return response

    def read_message(self):
        rlen = int.from_bytes(self.read_exact(4), byteorder="little")
        response_message = self.read_exact(rlen)
        return js.loads(response_message)

    def send_json(self, msg):
        data = js.dumps(msg).encode('utf8')
        header = len(data).to_bytes(4, byteorder="little")
        self.client.send(header)
        self.client.send(data)
        return self.read_message()

    def watch(self):
        message = get_msg_template("window-rules/events/watch")
        return self.send_json(message)

    def list_views(self):
        message = get_msg_template("window-rules/list-views")
        return self.send_json(message)

    def list_outputs(self):
        message = get_msg_template("window-rules/list-outputs")
        return self.send_json(message)

    def set_view_opacity(self, view_id: int, opacity: float, duration: int):
        message = get_msg_template("wf/obs/set-view-opacity")
        message["data"] = {}
        message["data"]["view-id"] = view_id
        message["data"]["opacity"] = opacity
        message["data"]["duration"] = duration
        return self.send_json(message)

    def set_view_brightness(self, view_id: int, brightness: float, duration: int):
        message = get_msg_template("wf/obs/set-view-brightness")
        message["data"] = {}
        message["data"]["view-id"] = view_id
        message["data"]["brightness"] = brightness
        message["data"]["duration"] = duration
        return self.send_json(message)

    def set_view_saturation(self, view_id: int, saturation: float, duration: int):
        message = get_msg_template("wf/obs/set-view-saturation")
        message["data"] = {}
        message["data"]["view-id"] = view_id
        message["data"]["saturation"] = saturation
        message["data"]["duration"] = duration
        return self.send_json(message)
