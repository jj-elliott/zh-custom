"""Print a compact tree of a .wnd file: depth, type, name, rect (800x600), status, text label. Usage: wndtree.py file.wnd"""
import sys, re
for path in sys.argv[1:]:
    txt = open(path, encoding='latin-1').read()
    print('=====', path.replace(chr(92), '/').split('/')[-1])
    depth = 0
    cur = None
    for line in txt.splitlines():
        s = line.strip()
        if s == 'WINDOW':
            cur = {'depth': depth, 'type': '', 'name': '', 'rect': '', 'status': '', 'text': '', 'style': ''}
        elif s == 'CHILD':
            depth += 1
        elif s == 'ENDALLCHILDREN':
            depth -= 1
        elif s.startswith('WINDOWTYPE') and cur is not None:
            cur['type'] = s.split('=')[1].strip(' ;')
        elif s.startswith('NAME =') and cur is not None and not cur['name']:
            cur['name'] = s.split('"')[1].split(':')[-1]
        elif s.startswith('SCREENRECT') and cur is not None:
            m = re.search(r'UPPERLEFT:\s*(\d+)\s+(\d+)', s)
            cur['rect'] = m.group(1) + ',' + m.group(2)
        elif s.startswith('BOTTOMRIGHT') and cur is not None and cur['rect'].count(',') == 1:
            m = re.search(r'BOTTOMRIGHT:\s*(\d+)\s+(\d+)', s)
            cur['rect'] += '-' + m.group(1) + ',' + m.group(2)
        elif s.startswith('STATUS') and cur is not None and not cur['status']:
            cur['status'] = s.split('=')[1].strip(' ;')
        elif s.startswith('STYLE') and cur is not None and not cur['style']:
            cur['style'] = s.split('=')[1].strip(' ;')
        elif s.startswith('TEXT =') and cur is not None:
            cur['text'] = s.split('"')[1] if '"' in s else ''
        elif s == 'END' and cur is not None:
            flags = []
            if 'HIDDEN' in cur['status']: flags.append('hidden')
            if 'SEE_THRU' in cur['status']: flags.append('seethru')
            if 'IMAGE' in cur['status']: flags.append('img')
            print('  ' * cur['depth'] + cur['type'].ljust(14), cur['name'].ljust(34), cur['rect'].ljust(16),
                  (cur['text'] or '').ljust(28), ' '.join(flags))
            cur = None
