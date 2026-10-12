"""Small valid image, PDF and video inputs for native presentation tests."""
from pathlib import Path
import struct
import subprocess
import sys
import zlib


def image(file):
    def chunk(kind, data):
        payload = kind + data
        return struct.pack('>I', len(data)) + payload + struct.pack('>I', zlib.crc32(payload))
    width, height = 240, 120
    pixels = (b'\0' + bytes((42, 103, 175)) * width) * height
    file.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(pixels)) + chunk(b'IEND', b''))


def pdf(file):
    stream = b'0.16 0.4 0.69 rg 0 0 240 120 re f\n'
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>',
               b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 240 120] /Contents 4 0 R >>',
               b'<< /Length ' + str(len(stream)).encode() + b' >>\nstream\n' + stream + b'endstream']
    data, offsets = b'%PDF-1.4\n', [0]
    for number, value in enumerate(objects, 1):
        offsets.append(len(data))
        data += f'{number} 0 obj\n'.encode() + value + b'\nendobj\n'
    start = len(data)
    data += f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode()
    data += b''.join(f'{offset:010d} 00000 n \n'.encode() for offset in offsets[1:])
    data += f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{start}\n%%EOF\n'.encode()
    file.write_bytes(data)


def video(file):
    codec = 'libx264' if file.suffix == '.mp4' else 'libvpx'
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                    'color=c=0x2a67af:s=240x120:r=10', '-t', '1', '-an', '-c:v', codec,
                    '-threads', '1', '-y', str(file)], check=True, timeout=30)


if __name__ == '__main__':
    directory = Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    image(directory / 'frame.png')
    pdf(directory / 'brief.pdf')
    video(directory / 'demo.webm')
    video(directory / 'demo.mp4')
