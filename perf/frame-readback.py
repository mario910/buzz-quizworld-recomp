"""Preserve the exact RGB bytes of a native Vulkan P6 readback as PNG."""
import argparse
from pathlib import Path
import struct
import zlib


def read_ppm(path):
    parts=path.read_bytes().split(b'\n',3)
    if len(parts)!=4 or parts[0]!=b'P6' or parts[2]!=b'255':raise ValueError('Unsupported native readback header')
    shape=parts[1].split()
    if len(shape)!=2:raise ValueError('Invalid readback dimensions')
    width,height=map(int,shape);rgb=parts[3]
    if not (0<width<=16384 and 0<height<=16384) or len(rgb)!=width*height*3:raise ValueError('Incomplete native readback')
    return width,height,rgb


def png_bytes(width,height,rgb):
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    stride=width*3
    scan=b''.join(b'\0'+rgb[i:i+stride] for i in range(0,len(rgb),stride))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('input',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();width,height,rgb=read_ppm(args.input)
    if not any(rgb):raise ValueError('Black readback does not prove game scene content')
    with args.output.open('xb') as stream:stream.write(png_bytes(width,height,rgb))
    print(f'Preserved exact {width}x{height} native RGB readback as PNG')


if __name__=='__main__':main()
