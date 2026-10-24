"""Print the window tree of a .wnd file (extracted with bigtool.py): indent = nesting, then type, name, the 800x600
rect (l,t-r,b), the text label and flags. Usage: python -I scripts/wndtree.py <file.wnd> [...]"""
import sys, re

def dump(path):
    txt = open(path, encoding='latin-1').read()
    print('=====', path.replace(chr(92), '/').split('/')[-1])
    stack = []      # open WINDOW records
    for line in txt.splitlines():
        s = line.strip()
        if s == 'WINDOW':
            stack.append({'type': '', 'name': '', 'rect': '', 'status': '', 'text': '', 'printed': False})
            continue
        if not stack:
            continue
        cur = stack[-1]
        if s == 'CHILD' or s == 'END' or s == 'ENDALLCHILDREN':
            if not cur['printed']:
                flags = [f for f, k in (('hidden', 'HIDDEN'), ('seethru', 'SEE_THRU'), ('img', 'IMAGE')) if k in cur['status']]
                print('  ' * (len(stack) - 1) + cur['type'].ljust(14), cur['name'].ljust(34), cur['rect'].ljust(16),
                      cur['text'].ljust(28), ' '.join(flags))
                cur['printed'] = True
            if s == 'END':
                stack.pop()
        elif s.startswith('WINDOWTYPE'):
            cur['type'] = s.split('=')[1].strip(' ;')
        elif s.startswith('NAME =') and not cur['name']:
            cur['name'] = s.split('"')[1].split(':')[-1] or '(unnamed)'
        elif s.startswith('SCREENRECT'):
            m = re.search(r'UPPERLEFT:\s*(\d+)\s+(\d+)', s)
            cur['rect'] = m.group(1) + ',' + m.group(2)
        elif s.startswith('BOTTOMRIGHT') and cur['rect'].count(',') == 1:
            m = re.search(r'BOTTOMRIGHT:\s*(\d+)\s+(\d+)', s)
            cur['rect'] += '-' + m.group(1) + ',' + m.group(2)
        elif s.startswith('STATUS') and not cur['status']:
            cur['status'] = s.split('=')[1].strip(' ;')
        elif s.startswith('TEXT =') and '"' in s:
            cur['text'] = s.split('"')[1]

for p in sys.argv[1:]:
    dump(p)
