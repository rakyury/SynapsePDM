"""Strictly parse the complete shared DBC and verify every HAL wire layout."""
from pathlib import Path
import struct
import cantools

root = Path(__file__).resolve().parents[2]
database = cantools.database.load_file(root / "SynapsePDM/CAN DB/SynapsePDM.dbc", strict=True)
tid = 0x01020304
vectors = (
    (0x580, dict(Version=1, Flags=5, Sequence=65535, Uptime=0x12345678),
     struct.pack("<BBHI", 1, 5, 65535, 0x12345678)),
    (0x581, dict(Channel=300, Quality=0, Sequence=255, Value=-2147483648),
     struct.pack("<HBBi", 300, 0, 255, -2147483648)),
    (0x582, dict(Transaction=tid, Operation=2, Status=0, NextOffset=128),
     struct.pack("<IBBH", tid, 2, 0, 128)),
    (0x583, dict(Transaction=tid, ActiveCRC32=0xffffffff), struct.pack("<II", tid, 0xffffffff)),
    (0x584, dict(Channel=102, Quality=0, Sequence=255, Value=-40000),
     struct.pack("<HBBi", 102, 0, 255, -40000)),
    (0x584, dict(Channel=110, Quality=2, Sequence=0, Value=36816),
     struct.pack("<HBBi", 110, 2, 0, 36816)),
    (0x584, dict(Channel=100, Quality=4, Sequence=1, Value=0),
     struct.pack("<HBBi", 100, 4, 1, 0)),
    (0x590, dict(Transaction=tid, Length=128, Version=1, Reserved=0),
     struct.pack("<IHBB", tid, 128, 1, 0)),
    (0x591, dict(Transaction=tid, Offset=126, Byte0=0x42, Byte1=0xff),
     struct.pack("<IHBB", tid, 126, 0x42, 0xff)),
    (0x592, dict(Transaction=tid, CRC32=0xcbf43926), struct.pack("<II", tid, 0xcbf43926)),
    (0x593, dict(Transaction=tid, Reserved=0), struct.pack("<II", tid, 0)),
    (0x594, dict(Transaction=tid, Reserved=0), struct.pack("<II", tid, 0)),
)
assert len({m.frame_id for m in database.messages}) == len(database.messages)
for frame_id, values, expected in vectors:
    message = database.get_message_by_frame_id(frame_id)
    assert message.length == 8 and not message.is_extended_frame
    assert message.encode(values, strict=True) == expected, hex(frame_id)
    assert message.decode(expected, decode_choices=False) == values, hex(frame_id)
print(f"Strict DBC: {len(database.messages)} messages; all ten HAL wire layouts match")
