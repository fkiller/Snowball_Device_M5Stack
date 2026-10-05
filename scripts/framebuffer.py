"""Decode actual M5Stack sprite rows, including its RAM-bounded RGB332 buffer."""
def decode_framebuffer(rows, pixel_format, width=320, height=240):
    from PIL import Image
    if set(rows)!=set(range(height)):
        raise ValueError('Incomplete native framebuffer')
    size=width if pixel_format=='rgb332' else width*3
    if pixel_format not in ('rgb332','rgb888') or any(len(row)!=size for row in rows.values()):
        raise ValueError('Invalid native framebuffer format/row')
    data=b''.join(rows[y] for y in range(height))
    if pixel_format=='rgb888':
        return Image.frombytes('RGB',(width,height),data)
    image=Image.frombytes('P',(width,height),data)
    # M5GFX rgb332_t's bit replication, rather than a fabricated UI image.
    palette=[]
    for value in range(256):
        r=value&0xe0;r|=r>>3;r|=r>>6
        g=(value&0x1c)<<3;g|=g>>3;g|=g>>6
        b=(value&3)*85
        palette.extend((r,g,b))
    image.putpalette(palette)
    return image.convert('RGB')
