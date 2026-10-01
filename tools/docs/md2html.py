#!/usr/bin/env python3
"""Builds docs/AcquaThermoNet.html from docs/DOCUMENTATION.md, no dependencies.

Handles the Markdown subset used by the documentation: headings, paragraphs,
lists, tables, fenced code, ```mermaid diagrams, images, links, **bold**,
*italic* and `code`. Images are embedded (SVG inline as data URI, PNG as
base64), so the HTML is a single self-contained file; the Mermaid diagrams
are rendered by mermaid.js from a CDN (without network their source is shown).

Usage:
  tools/docs/md2html.py [SOURCE.md] [OUTPUT.html]
  (default: docs/DOCUMENTATION.md -> docs/AcquaThermoNet.html)

Run it after every change to DOCUMENTATION.md.
"""
import base64, html, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'docs', 'DOCUMENTATION.md')
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'docs', 'AcquaThermoNet.html')
BASE = os.path.dirname(os.path.abspath(SRC))


def slug(text):
    """GitHub heading anchor"""
    s = re.sub(r'<[^>]+>', '', text).strip().lower()
    s = re.sub(r'[^\w\- ]', '', s)
    return s.replace(' ', '-')


def embed(src):
    path = os.path.join(BASE, src)
    if re.match(r'^[a-z]+:', src) or not os.path.exists(path):
        return src
    data = open(path, 'rb').read()
    mime = 'image/svg+xml' if src.endswith('.svg') else 'image/png'
    return 'data:%s;base64,%s' % (mime, base64.b64encode(data).decode())


def inline(text):
    """Inline markup on text that may contain HTML entities already"""
    codes = []

    def keep(m):
        codes.append('<code>%s</code>' % html.escape(m.group(1), quote=False))
        return '\x00%d\x00' % (len(codes) - 1)

    text = re.sub(r'`([^`]+)`', keep, text)
    text = html.escape(text, quote=False)
    # entities written in the source (&lt; &gt;) were escaped twice: restore
    text = re.sub(r'&amp;(lt|gt|amp|nbsp);', r'&\1;', text)
    text = re.sub(r'!\[([^\]]*)\]\(([^)]+)\)',
                  lambda m: '<img alt="%s" src="%s">' % (m.group(1), embed(m.group(2))), text)

    def link(m):
        href = m.group(2)
        if href.endswith('.md') or '.md#' in href:
            href = href  # kept relative: readable in the repository
        return '<a href="%s">%s</a>' % (href, m.group(1))

    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', link, text)
    text = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', text)
    text = re.sub(r'(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])', r'<em>\1</em>', text)
    text = re.sub(r'<br/>', '<br>', text)
    return re.sub(r'\x00(\d+)\x00', lambda m: codes[int(m.group(1))], text)


def cells(row):
    row = row.strip()
    if row.startswith('|'):
        row = row[1:]
    if row.endswith('|'):
        row = row[:-1]
    return [c.strip() for c in row.split('|')]


def convert(lines):
    out, toc = [], []
    i, n = 0, len(lines)
    title = 'AcquaThermoNet'
    while i < n:
        line = lines[i]
        s = line.strip()

        if s.startswith('```'):
            lang = s[3:].strip()
            body = []
            i += 1
            while i < n and not lines[i].strip().startswith('```'):
                body.append(lines[i])
                i += 1
            i += 1
            code = html.escape('\n'.join(body), quote=False)
            if lang == 'mermaid':
                out.append('<figure class="diagram"><pre class="mermaid">%s</pre></figure>' % code)
            else:
                out.append('<pre class="code"><code class="lang-%s">%s</code></pre>' % (lang or 'text', code))
            continue

        m = re.match(r'^(#{1,4})\s+(.*)$', s)
        if m:
            level, text = len(m.group(1)), m.group(2)
            if level == 1:
                title = re.sub(r'[*`]', '', text)
                out.append('<h1>%s</h1>' % inline(text))
            else:
                ident = slug(text)
                if level == 2:
                    toc.append((ident, re.sub(r'[*`]', '', text)))
                    out.append('</section><section id="%s">' % ident)
                out.append('<h%d id="%s">%s</h%d>' % (level, ident, inline(text), level))
            i += 1
            continue

        if s == '---':
            i += 1
            continue

        if s.startswith('|') and i + 1 < n and re.match(r'^\|[\s:|-]+\|$', lines[i + 1].strip()):
            head = cells(s)
            i += 2
            rows = []
            while i < n and lines[i].strip().startswith('|'):
                rows.append(cells(lines[i]))
                i += 1
            t = ['<div class="table"><table>']
            if any(head):
                t.append('<thead><tr>%s</tr></thead>' % ''.join('<th>%s</th>' % inline(c) for c in head))
            t.append('<tbody>')
            for r in rows:
                t.append('<tr>%s</tr>' % ''.join('<td>%s</td>' % inline(c) for c in r))
            t.append('</tbody></table></div>')
            out.append(''.join(t))
            continue

        m = re.match(r'^(\s*)([-*]|\d+\.)\s+(.*)$', line)
        if m:
            ordered = m.group(2)[0].isdigit()
            tag = 'ol' if ordered else 'ul'
            items = []
            while i < n:
                m = re.match(r'^(\s*)([-*]|\d+\.)\s+(.*)$', lines[i])
                if m:
                    items.append(m.group(3))
                elif lines[i].startswith('  ') and lines[i].strip() and items:
                    items[-1] += ' ' + lines[i].strip()
                else:
                    break
                i += 1
            # task list items: "[ ] text"
            items = [re.sub(r'^\[ \]\s+', '\u2610 ', x) for x in items]
            out.append('<%s>%s</%s>' % (tag, ''.join('<li>%s</li>' % inline(x) for x in items), tag))
            continue

        if not s:
            i += 1
            continue

        para = [s]
        i += 1
        while i < n and lines[i].strip() and not re.match(r'^(#{1,4}\s|```|\||\s*([-*]|\d+\.)\s|---$)', lines[i].strip()):
            para.append(lines[i].strip())
            i += 1
        text = ' '.join(para)
        if re.fullmatch(r'!\[[^\]]*\]\([^)]+\)', text):
            out.append('<figure class="image">%s</figure>' % inline(text))
        elif re.fullmatch(r'\*[^*].*\*', text):
            out.append('<p class="caption">%s</p>' % inline(text))
        else:
            out.append('<p>%s</p>' % inline(text))
    return title, toc, out


CSS = r"""
:root {
  --bg: #F6F7F9; --panel: #FFFFFF; --text: #1F2937; --muted: #6B7280; --border: #E5E7EB;
  --accent: #AD1625; --accent-soft: #FBEAEC; --code-bg: #F3F4F6; --code-text: #111827;
  --side: #1F1F1F; --side-text: #D1D5DB; --side-active: #FFFFFF;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #17181A; --panel: #202226; --text: #E5E7EB; --muted: #9CA3AF; --border: #33363C;
    --accent: #E0485A; --accent-soft: #3A1E22; --code-bg: #2A2D33; --code-text: #E5E7EB;
    --side: #111214; --side-text: #A1A1AA; --side-active: #FFFFFF;
  }
}
:root[data-theme="dark"] {
  --bg: #17181A; --panel: #202226; --text: #E5E7EB; --muted: #9CA3AF; --border: #33363C;
  --accent: #E0485A; --accent-soft: #3A1E22; --code-bg: #2A2D33; --code-text: #E5E7EB;
  --side: #111214; --side-text: #A1A1AA; --side-active: #FFFFFF;
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; }
body { margin: 0; background: var(--bg); color: var(--text);
  font: 15px/1.6 -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
nav { position: fixed; top: 0; left: 0; bottom: 0; width: 270px; overflow-y: auto;
  background: var(--side); padding: 22px 0 30px; }
nav .brand { color: #fff; font-weight: 700; font-size: 17px; padding: 0 22px 4px; }
nav .brand span { color: #E0485A; }
nav .ver { color: var(--side-text); font-size: 12px; padding: 0 22px 16px; opacity: .8; }
nav a { display: block; color: var(--side-text); text-decoration: none; font-size: 13.5px;
  padding: 5px 22px; border-left: 3px solid transparent; }
nav a:hover { color: var(--side-active); background: rgba(255,255,255,.04); }
nav a.active { color: var(--side-active); border-left-color: #E0485A; background: rgba(255,255,255,.06); }
main { margin-left: 270px; padding: 32px 48px 80px; max-width: 1180px; }
header.hero { background: linear-gradient(135deg, #2A2A2A, #3B1117); color: #fff; border-radius: 12px;
  padding: 28px 32px; margin-bottom: 28px; }
header.hero h1 { margin: 0 0 6px; font-size: 28px; }
header.hero p { margin: 0; color: #E5E7EB; max-width: 820px; }
header.hero .chips { margin-top: 16px; display: flex; flex-wrap: wrap; gap: 8px; }
header.hero .chip { background: rgba(255,255,255,.1); border: 1px solid rgba(255,255,255,.18);
  border-radius: 999px; padding: 3px 12px; font-size: 12.5px; }
section { background: var(--panel); border: 1px solid var(--border); border-radius: 12px;
  padding: 6px 32px 22px; margin-bottom: 22px; }
section:empty { display: none; }
h1 { display: none; }
h2 { font-size: 22px; margin: 22px 0 12px; padding-bottom: 8px; border-bottom: 2px solid var(--accent-soft); }
h3 { font-size: 17px; margin: 24px 0 8px; color: var(--accent); }
h4 { font-size: 15px; margin: 18px 0 6px; }
a { color: var(--accent); }
p.caption { color: var(--muted); font-size: 13px; text-align: center; margin-top: -6px; }
code { background: var(--code-bg); color: var(--code-text); border-radius: 4px; padding: 1px 5px;
  font: 13px/1.5 "JetBrains Mono", "Fira Code", Menlo, Consolas, monospace; }
pre.code { background: var(--code-bg); border: 1px solid var(--border); border-radius: 8px;
  padding: 14px 16px; overflow-x: auto; }
pre.code code { background: none; padding: 0; font-size: 12.8px; }
.table { overflow-x: auto; margin: 12px 0 16px; }
table { border-collapse: collapse; width: 100%; font-size: 14px; }
th { text-align: left; background: var(--accent-soft); color: var(--text); font-weight: 600; }
th, td { border: 1px solid var(--border); padding: 7px 10px; vertical-align: top; }
tbody tr:nth-child(even) td { background: color-mix(in srgb, var(--code-bg) 45%, transparent); }
figure { margin: 18px 0; text-align: center; }
figure.image img { max-width: 100%; border-radius: 8px; border: 1px solid var(--border); background: #fff; }
figure.diagram { background: #FFFFFF; border: 1px solid var(--border); border-radius: 10px;
  padding: 16px; overflow-x: auto; }
pre.mermaid { margin: 0; font: 12px/1.4 Menlo, Consolas, monospace; color: #111827; text-align: left; }
pre.mermaid[data-processed] { text-align: center; }
.theme { position: fixed; top: 14px; right: 18px; z-index: 5; background: var(--panel); color: var(--text);
  border: 1px solid var(--border); border-radius: 999px; padding: 5px 12px; font-size: 12.5px; cursor: pointer; }
.menu { display: none; }
@media (max-width: 900px) {
  nav { transform: translateX(-100%); transition: transform .2s; z-index: 10; }
  nav.open { transform: none; }
  main { margin-left: 0; padding: 60px 16px 60px; }
  section { padding: 4px 16px 16px; }
  .menu { display: block; position: fixed; top: 14px; left: 16px; z-index: 11; background: var(--panel);
    color: var(--text); border: 1px solid var(--border); border-radius: 8px; padding: 5px 12px; }
}
@media print {
  nav, .theme, .menu { display: none; }
  main { margin: 0; padding: 0; max-width: none; }
  section { break-inside: auto; border: none; }
}
"""

JS = r"""
(function () {
  var root = document.documentElement;
  var btn = document.querySelector('.theme');
  function dark() {
    var t = root.getAttribute('data-theme');
    return t ? t === 'dark' : window.matchMedia('(prefers-color-scheme: dark)').matches;
  }
  try { var saved = localStorage.getItem('atn-doc-theme'); if (saved) root.setAttribute('data-theme', saved); } catch (e) {}
  function label() { btn.textContent = dark() ? 'Light' : 'Dark'; }
  btn.addEventListener('click', function () {
    var t = dark() ? 'light' : 'dark';
    root.setAttribute('data-theme', t);
    try { localStorage.setItem('atn-doc-theme', t); } catch (e) {}
    label();
  });
  label();

  var nav = document.querySelector('nav');
  document.querySelector('.menu').addEventListener('click', function () { nav.classList.toggle('open'); });
  nav.addEventListener('click', function (e) { if (e.target.tagName === 'A') nav.classList.remove('open'); });

  var links = Array.prototype.slice.call(nav.querySelectorAll('a[href^="#"]'));
  var sections = links.map(function (a) { return document.getElementById(a.getAttribute('href').slice(1)); });
  function spy() {
    var y = window.scrollY + 120, cur = 0;
    sections.forEach(function (s, i) { if (s && s.offsetTop <= y) cur = i; });
    links.forEach(function (a, i) { a.classList.toggle('active', i === cur); });
  }
  window.addEventListener('scroll', spy, { passive: true });
  spy();

  if (window.mermaid) {
    mermaid.initialize({ startOnLoad: false, theme: 'default', securityLevel: 'loose',
      flowchart: { htmlLabels: true, curve: 'basis' }, sequence: { mirrorActors: false } });
    mermaid.run({ querySelector: 'pre.mermaid' });
  }
})();
"""


def main():
    lines = open(SRC, encoding='utf-8').read().split('\n')
    title, toc, body = convert(lines)

    # intro: everything before the first h2, shown in the header
    first = next(k for k, b in enumerate(body) if b.startswith('</section>'))
    intro = [b for b in body[:first] if not b.startswith('<h1')]
    rest = body[first:]
    rest[0] = rest[0].replace('</section>', '', 1)
    ver = re.search(r'version \*\*([\d.]+)\*\*', open(SRC, encoding='utf-8').read())
    version = ver.group(1) if ver else ''

    nav = ''.join('<a href="#%s">%s</a>' % (i, html.escape(t)) for i, t in toc if not t.lower().startswith('contents'))
    rest_html = '\n'.join(rest) + '</section>'
    # the Markdown table of contents is replaced by the side navigation
    rest_html = re.sub(r'<section id="contents">.*?</section>', '', rest_html, flags=re.S)

    # project chips only on the main documentation
    chips = [] if os.path.basename(SRC) != 'DOCUMENTATION.md' else ['Qt 5 / C++17', 'ARM32 HMI 800x480', 'MQTT + Home Assistant', 'Modbus RTU RS485',
             'met.no weather', 'Telegram bot', 'Watchdog']
    page = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%s</title>
<meta name="description" content="AcquaThermoNet documentation, generated from %s">
<style>%s</style>
</head>
<body>
<button class="menu" type="button">Contents</button>
<button class="theme" type="button">Dark</button>
<nav>
  <div class="brand">Acqua<span>Thermo</span>Net</div>
  <div class="ver">%s</div>
  %s
</nav>
<main>
<header class="hero">
  <h1 style="display:block">%s</h1>
  %s
  <div class="chips">%s</div>
</header>
%s
</main>
<script src="https://cdn.jsdelivr.net/npm/mermaid@10.9.1/dist/mermaid.min.js"></script>
<script>%s</script>
</body>
</html>
""" % (html.escape(title), os.path.basename(SRC), CSS, ('Technical documentation v' + version) if version else 'Interface specification', nav, html.escape(title), '\n'.join(intro),
       ''.join('<span class="chip">%s</span>' % c for c in chips), rest_html, JS)
    # generated file: note the source
    page = page.replace('<head>', '<head>\n<!-- Generated by tools/docs/md2html.py from docs/%s: do not edit -->' % os.path.basename(SRC), 1)
    open(OUT, 'w', encoding='utf-8').write(page)
    print('written %s (%d KB)' % (OUT, len(page) // 1024))


if __name__ == '__main__':
    main()
