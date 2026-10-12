#!/usr/bin/env python3
"""Stage DeskPort icons from app/res/deskport.svg outside the source checkout.

Requires ImageMagick and Pillow. The SVG is the only geometry source, including
Apple's layered artwork. Copy reviewed ICO, ICNS, PNG and .icon files into app/.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import xml.etree.ElementTree as ET

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parent.parent
SVG = "{http://www.w3.org/2000/svg}"
SIZES = (16, 24, 32, 48, 64, 128, 256, 512, 1024)


def apple_color(color):
    channels = [int(color[index:index + 2], 16) / 255 for index in (1, 3, 5)]
    return 'extended-srgb:' + ','.join(f'{channel:.5f}' for channel in channels) + ',1.00000'


def apple_layers(source, output):
    tree = ET.parse(source).getroot()
    ET.register_namespace("", SVG[1:-1])
    document = output / "AppIcon.icon"
    assets = document / "Assets"
    assets.mkdir(parents=True, exist_ok=True)
    layers = []
    groups = [node for node in tree if node.tag == SVG + "g"]
    tile = next(node for node in groups if node.attrib['id'] == 'tile')
    stops = tree.find(SVG + 'defs').find(SVG + "linearGradient[@id='tile-fill']").findall(SVG + 'stop')
    colors = [apple_color(stop.attrib['stop-color']) for stop in (stops[0], stops[-1])]
    for node in groups:
        # The system draws Apple's tile and mask. Adding the portable tile as
        # a layer would produce a second, inset rounded frame.
        if node.attrib['id'] == 'tile':
            continue
        name = node.attrib["id"] + ".svg"
        layer = ET.Element(SVG + "svg", {key: tree.attrib[key]
                                        for key in ("width", "height", "viewBox")})
        layer.append(copy.deepcopy(tree.find(SVG + "defs")))
        artwork = copy.deepcopy(node)
        artwork.attrib.pop("data-icon-name", None)
        # Apple supplies the shadow; a baked copy would tint into a second glyph.
        for parent in artwork.iter():
            for child in list(parent):
                if child.attrib.pop('data-icon-shadow', None):
                    parent.remove(child)
        layer.append(artwork)
        ET.indent(layer, space='  ')
        (assets / name).write_bytes(ET.tostring(layer, encoding='utf-8') + b'\n')
        material = {
            "name": node.attrib["data-icon-name"], "image-name": name,
            "glass-specializations": [{"value": False},
                                      {"appearance": "tinted", "value": True}],
            "fill-specializations": [{"appearance": "tinted", "value": {
                "solid": "extended-gray:1.00000,1.00000"}}],
        }
        if node.attrib['id'] == 'screen':
            material['fill-specializations'][0]['value']['solid'] = 'extended-gray:0.15000,1.00000'
        layers.append(material)
    # Icon Composer lists the frontmost layer first; SVG paints it last.
    manifest = {
        "fill-specializations": [
            {"value": {"linear-gradient": colors}},
            {"appearance": "dark", "value": {"solid": apple_color(tile.attrib['data-icon-dark'])}},
            {"appearance": "tinted", "value": {"solid": "extended-gray:0.60000,0.00000"}},
        ],
        "groups": [
            {"name": "Lifted workspace", "layers": list(reversed(layers)),
             "lighting": "combined", "shadow": {"kind": "neutral", "opacity": 0.16},
             "specular": False, "translucency": {"enabled": False, "value": 0.08}},
        ],
        "supported-platforms": {"squares": "shared"},
    }
    (document / "icon.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (assets / 'tile.svg').unlink(missing_ok=True)


def render_svg(tree, svg, png):
    ET.ElementTree(tree).write(svg, encoding='utf-8')
    subprocess.run(['magick', '-background', 'none', str(svg), '-depth', '8', str(png)], check=True)


def mobile_assets(source, output):
    """Stage opaque Apple fallback pixels and masked Android launcher layers."""
    mobile = output / 'mobile'
    mobile.mkdir(exist_ok=True)
    shutil.copy2(source, mobile / 'deskport.svg')
    shutil.copy2(output / 'deskport-1024.png', mobile / 'deskport-mark.png')
    shutil.copytree(output / 'AppIcon.icon', mobile / 'AppIcon.icon', dirs_exist_ok=True)
    tree = ET.parse(source).getroot()
    tile = next(node for node in tree if node.attrib.get('id') == 'tile')
    rectangle = list(tile)[0]
    rectangle.attrib.update(x='0', y='0', width='1024', height='1024', rx='0')
    for edge in list(tile)[1:]:
        tile.remove(edge)
    render_svg(tree, output / 'mobile-square.svg', mobile / 'deskport.png')
    with Image.open(mobile / 'deskport.png') as pixels:
        pixels.convert('RGB').save(mobile / 'deskport.png')

    foreground = ET.parse(source).getroot()
    foreground.remove(next(node for node in foreground if node.attrib.get('id') == 'tile'))
    # Keep all artwork inside Android's 66/108 dp adaptive-mask safe region.
    scaled = ET.Element(SVG + 'g', {'transform': 'translate(153.6 153.6) scale(.70)'})
    for node in list(foreground):
        if node.tag == SVG + 'g':
            foreground.remove(node)
            for parent in node.iter():
                for child in list(parent):
                    if child.attrib.pop('data-icon-shadow', None):
                        parent.remove(child)
            scaled.append(node)
    foreground.append(scaled)
    resources = mobile / 'Android'
    bitmap = resources / 'drawable-nodpi'
    bitmap.mkdir(parents=True, exist_ok=True)
    render_svg(foreground, output / 'android-foreground.svg', output / 'android-foreground.png')
    icon = Image.open(output / 'android-foreground.png').convert('RGBA')
    icon.resize((432, 432), Image.Resampling.LANCZOS).save(bitmap / 'deskport_launcher_foreground.png')

    def layer_alpha(name):
        isolated = copy.deepcopy(foreground)
        group = list(isolated)[-1]
        for node in list(group):
            if node.attrib.get('id') != name:
                group.remove(node)
        render_svg(isolated, output / f'android-{name}.svg', output / f'android-{name}.png')
        return Image.open(output / f'android-{name}.png').convert('RGBA').getchannel('A')

    mask = ImageChops.lighter(ImageChops.subtract(icon.getchannel('A'), layer_alpha('screen')),
                             layer_alpha('prompt'))
    mono = Image.new('RGBA', icon.size, 'white')
    mono.putalpha(mask)
    mono.resize((432, 432), Image.Resampling.LANCZOS).save(bitmap / 'deskport_launcher_monochrome.png')
    drawable = resources / 'drawable'; drawable.mkdir(exist_ok=True)
    stops = tree.find(SVG + 'defs').find(SVG + "linearGradient[@id='tile-fill']").findall(SVG + 'stop')
    start, end = (stops[index].attrib['stop-color'] for index in (0, -1))
    (drawable / 'deskport_launcher_background.xml').write_text(
        '<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">\n'
        f'  <gradient android:angle="135" android:startColor="{start}" android:endColor="{end}"/>\n</shape>\n')
    for version in ('', '-v26', '-v33'):
        folder = resources / ('mipmap-anydpi' + version); folder.mkdir(exist_ok=True)
        if not version:
            xml = '<bitmap xmlns:android="http://schemas.android.com/apk/res/android" android:src="@drawable/deskport_icon"/>\n'
        else:
            xml = '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            xml += '  <background android:drawable="@drawable/deskport_launcher_background"/>\n'
            xml += '  <foreground android:drawable="@drawable/deskport_launcher_foreground"/>\n'
            if version == '-v33':
                xml += '  <monochrome android:drawable="@drawable/deskport_launcher_monochrome"/>\n'
            xml += '</adaptive-icon>\n'
        (folder / 'deskport_launcher.xml').write_text(xml)
    manifest = {
        'schema': 1,
        'source': {'repository': 'https://github.com/keithxc/deskport',
                   'path': 'app/res/deskport.svg', 'sha256': hashlib.sha256(source.read_bytes()).hexdigest()},
        'assets': {str(path.relative_to(mobile)): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in sorted(mobile.rglob('*')) if path.is_file() and path.name != 'brand-manifest.json'},
    }
    (mobile / 'brand-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--mobile", action='store_true', help='also stage Apple and Android client assets')
    args = parser.parse_args()
    output = args.output.resolve()
    if output == ROOT or ROOT in output.parents:
        parser.error("stage generated assets outside the source checkout")
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / "app/res/deskport.svg"
    master = output / "deskport-1024.png"
    subprocess.run(["magick", "-background", "none", str(source),
                    "-depth", "8", str(master)], check=True)
    with Image.open(master) as original:
        icon = original.convert("RGBA")
        for size in SIZES:
            icon.resize((size, size), Image.Resampling.LANCZOS).save(
                output / f"deskport-{size}.png")
        icon.save(output / "deskport.ico", sizes=[(s, s) for s in SIZES if s <= 256])
        iconset = output / "deskport.iconset"
        iconset.mkdir(exist_ok=True)
        for points in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                pixels = points * scale
                name = f"icon_{points}x{points}{'@2x' if scale == 2 else ''}.png"
                icon.resize((pixels, pixels), Image.Resampling.LANCZOS).save(iconset / name)
        if shutil.which("iconutil"):
            subprocess.run(["iconutil", "-c", "icns", "-o",
                            str(output / "deskport.icns"), str(iconset)], check=True)
        else:
            icon.save(output / "deskport.icns")
    apple_layers(source, output)
    if args.mobile:
        mobile_assets(source, output)
    print(f"Staged portable and Apple layered icons in {output}")


if __name__ == "__main__":
    main()
