#!/usr/bin/env python3
"""Project core UI entries into Qt catalogs; leave platform-only text intact."""
import argparse
import importlib.util
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('deskport_ui_catalog', ROOT/'shared/deskport-core/i18n/catalog.py')
core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(core)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
import copy

records = core.records()
metadata = core.qt_catalogs()
# New locales reuse context/source metadata, never another language's text.
for language in core.languages():
    qt = language['qt']
    if qt != 'en' and qt not in metadata:
        metadata[qt] = {**metadata['fr'], 'attributes': {'version':'2.1', 'language':qt}}
# All primary locales use the current UI context inventory. Retain historical
# platform contexts too, so existing optional screens keep their translations.
for language in core.languages():
    qt = language['qt']
    if qt == 'en':
        continue
    current = metadata[qt]
    contexts = {context['name']: context for context in current['contexts']}
    merged = copy.deepcopy(metadata['fr']['contexts'])
    for context in merged:
        previous = contexts.pop(context['name'], None)
        if previous:
            sources = {message['source'] for message in context['messages']}
            context['messages'].extend(message for message in previous['messages'] if message['source'] not in sources)
    current['contexts'] = merged + list(contexts.values())
for qt, description in metadata.items():
    code = {'zh_CN':'zh-Hans', 'zh_TW':'zh-Hant'}.get(qt, qt.replace('_','-'))
    path = ROOT/'app/languages'/f'qml_{qt}.ts'
    tree = ET.ElementTree(ET.Element('TS', description['attributes']))
    for description_context in description['contexts']:
        context = ET.SubElement(tree.getroot(), 'context')
        ET.SubElement(context, 'name').text = description_context['name']
        for description_message in description_context['messages']:
            source = description_message['source']
            row = records[source]
            message = ET.SubElement(context, 'message', description_message['attributes'])
            for element in description_message['elements']:
                message.append(ET.fromstring(element))
            ET.SubElement(message, 'source').text = source
            translation = ET.SubElement(message, 'translation')
            value = row.get('_qt', {}).get(description_context['name'], {}).get(code, row.get(code, ''))
            translation.text = value
            status = description_message['translationType']
            if status in ('obsolete','vanished'):
                translation.set('type', status)
            elif not value:
                translation.set('type', 'unfinished')
    ET.indent(tree, space='    ')
    expected = '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n'+ET.tostring(tree.getroot(), encoding='unicode')+'\n'
    if args.check:
        if not path.exists() or path.read_text() != expected:
            raise SystemExit(f'{path.name}: regenerate with scripts/sync-translations.py')
    else:
        path.write_text(expected)
print('PASS: all Qt UI dictionaries are generated from the pinned core table')
