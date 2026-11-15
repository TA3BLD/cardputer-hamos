#!/usr/bin/env python3
"""Checks HamOS's Meshtastic app interface with the official Meshtastic
Python client, against devsim (which offers the interface on a second
pseudo terminal, framed as Meshtastic's serial API).

  ./devsim.out            # prints the USB port and the Meshtastic port
  pip install meshtastic pyserial
  python3 mesh_app_check.py /dev/pts/1 /dev/pts/0
"""
import json
import sys
import time

import meshtastic.serial_interface
import serial
from pubsub import pub

acked = set()


def on_receive(packet, interface):
    d = packet.get("decoded", {})
    if d.get("portnum") == "ROUTING_APP" and d.get("routing", {}).get("errorReason", "NONE") == "NONE":
        acked.add(d.get("requestId"))


pub.subscribe(on_receive, "meshtastic.receive")
iface = meshtastic.serial_interface.SerialInterface(devPath=sys.argv[1])
nodes = iface.nodes or {}
print("nodes:", sorted(n["user"]["longName"] for n in nodes.values()))
assert len(nodes) >= 3, nodes
assert iface.localNode.channels and iface.localNode.channels[0].role == 1      # PRIMARY
sent = iface.sendText("Uygulamadan merhaba", wantAck=True)
dm = iface.sendText("Direkt mesaj", destinationId="!a1b2c3d4", wantAck=True)
time.sleep(1.5)
assert sent.id in acked and dm.id in acked, (acked, sent.id, dm.id)
print("both messages acknowledged by HamOS")
iface.close()

usb = serial.Serial(sys.argv[2], 115200, timeout=2)
usb.write(b'{"id":1,"cmd":"mesh.list"}\n')
deadline = time.time() + 5
texts = []
while time.time() < deadline:
    line = usb.readline().decode(errors="replace").strip()
    if line.startswith('{"id":1'):
        texts = [m["text"] for m in json.loads(line)["messages"]]
        break
assert "Uygulamadan merhaba" in texts and "Direkt mesaj" in texts, texts
print("HamOS queued them for LoRa:", texts[-2:])
