#!/usr/bin/env bash
set -euo pipefail

# Verifies the i18n catalogs stay in sync with the source code:
#   * every runtime tr()/tr_format() string literal in src/** is present in po/en.po
#   * every en.po msgid is mirrored in po/zh.po (no missing Chinese translations)
#   * neither catalog contains fuzzy entries
#
# This is a static check (no network, no yai binary), so it runs under the
# network guard in run_all.sh like any other smoke test.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
  echo "po_sync_smoke: python3 is required" >&2
  exit 1
fi

python3 - "$ROOT" <<'PY'
import re, glob, sys, os

root = sys.argv[1]
src_dir = os.path.join(root, "src")


def unescape_c(lit):
    out = []
    i, n = 0, len(lit)
    while i < n:
        c = lit[i]
        if c == '\\' and i + 1 < n:
            nxt = lit[i + 1]
            out.append({'n': '\n', 't': '\t', 'r': '\r', '"': '"',
                        '\\': '\\', '/': '/', "'": "'"}.get(nxt, nxt))
            i += 2
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def read_c_string(s, k):
    """Starting at s[k] == '"', consume a C string literal. Returns (text, new_k)."""
    k += 1
    buf = []
    n = len(s)
    while k < n:
        d = s[k]
        if d == '\\':
            buf.append(s[k]); buf.append(s[k + 1]); k += 2
        elif d == '"':
            k += 1
            return ''.join(buf), k
        else:
            buf.append(d); k += 1
    return ''.join(buf), k


def next_token(s, k):
    """Return (kind, value, new_k) where kind is 'ws','str','char','other', or 'end'."""
    n = len(s)
    while k < n and s[k] in ' \t\r\n':
        k += 1
    if k >= n:
        return 'end', '', k
    c = s[k]
    if c == '"':
        text, k = read_c_string(s, k)
        return 'str', text, k
    if c == "'":
        k += 1
        while k < n and s[k] != "'":
            k += 2 if s[k] == '\\' else 1
        k += 1
        return 'char', '', k
    if c == '(':
        return 'other', '(', k + 1
    if c == ')':
        return 'other', ')', k + 1
    # any other single char (comma, operator, identifier, number, etc.)
    return 'other', c, k + 1


def extract_tr_literals():
    files = (glob.glob(os.path.join(src_dir, '**', '*.cpp'), recursive=True) +
             glob.glob(os.path.join(src_dir, '**', '*.hpp'), recursive=True))
    literals = []
    pat = re.compile(r'(?<![A-Za-z0-9_])tr(?:_format)?\(')
    for f in files:
        s = open(f, encoding='utf-8').read()
        for m in pat.finditer(s):
            depth, k = 1, m.end()
            parts = []
            run_ended = False  # becomes True once a non-adjacent token ends the first literal run
            while k < len(s) and depth > 0:
                kind, val, k = next_token(s, k)
                if kind == 'end':
                    break
                if kind == 'other' and val == '(':
                    depth += 1
                elif kind == 'other' and val == ')':
                    depth -= 1
                elif kind == 'str':
                    # Only the leading run of adjacent (C++-concatenated) literals
                    # forms the msgid. Format-arg placeholder names like "{count}"
                    # appear after a comma and must be excluded.
                    if not run_ended:
                        parts.append(unescape_c(val))
                else:
                    # a comma / operator / identifier / char literal ends the run
                    run_ended = True
            full = ''.join(parts)
            if full:
                literals.append(full)
    return literals


def parse_po(path):
    txt = open(path, encoding='utf-8').read()
    ids = []
    for block in re.findall(r'msgid\s+((?:"(?:\\.|[^"\\])*"\s*)+)', txt):
        segs = re.findall(r'"((?:\\.|[^"\\])*)"', block)
        s = ''.join(segs)
        s = (s.replace('\\n', '\n').replace('\\t', '\t').replace('\\r', '\r')
              .replace('\\"', '"').replace('\\\\', '\\'))
        ids.append(s)
    return set(ids)


src_set = set(extract_tr_literals())
en = parse_po(os.path.join(root, 'po', 'en.po'))
zh = parse_po(os.path.join(root, 'po', 'zh.po'))

missing_en = sorted(x for x in src_set if x not in en)
missing_zh = sorted(x for x in en if x and x not in zh)

en_raw = open(os.path.join(root, 'po', 'en.po'), encoding='utf-8').read()
zh_raw = open(os.path.join(root, 'po', 'zh.po'), encoding='utf-8').read()
fuzzy = ('#, fuzzy' in en_raw) or ('#, fuzzy' in zh_raw)

ok = True
if missing_en:
    ok = False
    print("FAIL: %d tr() string(s) missing from en.po:" % len(missing_en))
    for x in missing_en:
        print("   ", repr(x))
if missing_zh:
    ok = False
    print("FAIL: %d en.po msgid(s) missing from zh.po:" % len(missing_zh))
    for x in missing_zh:
        print("   ", repr(x))
if fuzzy:
    ok = False
    print("FAIL: a .po catalog contains fuzzy entries")

if ok:
    print("PASS: %d distinct tr() strings present in en.po and mirrored in zh.po; no fuzzy entries"
          % len(src_set))
    sys.exit(0)
print("po_sync_smoke: catalog sync check failed")
sys.exit(1)
PY
