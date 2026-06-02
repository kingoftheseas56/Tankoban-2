# RCO reader descramble spike (2026-06-03, Agent 1).
# Replicates gallery-dl's readcomiconline images()+baeu() on rcostation.xyz
# (rguard.min.js v1.5.8) to PROVE the obfuscated page-image URLs are recoverable
# without a headless browser. If this prints real blogspot URLs that fetch as
# JPEGs, RCO can be our full catalog+metadata+cbz source.
import re, sys, base64, urllib.request

PAGE = "https://rcostation.xyz/Comic/Invincible/TPB-1-Family-matters"
UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36"

def fetch(url, referer=None, binary=False):
    req = urllib.request.Request(url, headers={"User-Agent": UA, **({"Referer": referer} if referer else {})})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read() if binary else r.read().decode("utf-8", "replace")

def baeu(url, root, root_blogspot="https://2.bp.blogspot.com"):
    if not root:
        root = root_blogspot
    url = url.replace("pw_.g28x", "b")
    url = url.replace("d2pr.x_27", "h")
    if url.startswith("https"):
        return url.replace(root_blogspot, root, 1)
    path, sep, query = url.partition("?")
    contains_s0 = "=s0" in path
    path = path[:-3 if contains_s0 else -6]
    path = path[15:33] + path[50:]      # step1
    path = path[0:-11] + path[-2:]      # step2
    path = base64.b64decode(path).decode()  # atob
    path = path[0:13] + path[17:]
    path = path[0:-2] + ("=s0" if contains_s0 else "=s1600")
    return root + "/" + path + sep + query

def main():
    page = fetch(PAGE)
    print(f"page bytes: {len(page)}")
    # Extract root passed to baeu(l, '<root>')
    m_root = re.search(r"return baeu\(l, '([^']*)'", page)
    root = m_root.group(1) if m_root else ""
    print(f"root from baeu call: {root!r}")
    # Extract the array var name: ... var <NAME> = '
    m_var = re.search(r"var pth = '[^']*';\s*var (\w+)\s*=\s*'", page)
    if not m_var:
        m_var = re.search(r"var (\w+)\s*=\s*'[^']*';[^\n]*baeu", page)
    var = m_var.group(1) if m_var else None
    print(f"array var: {var!r}")
    # Extract l.replace(/X/g, 'Y') junk-token replacements
    repls = re.findall(r"l = l\.replace\(/([^/]+)/g, [\"']([^\"']*)[\"']\)", page)
    print(f"replacements: {repls}")
    if not var:
        print("FAILED: could not locate array var — dumping baeu-context for analysis")
        i = page.find("baeu(")
        print(page[max(0,i-400):i+400] if i>=0 else "no baeu( in page")
        return
    parts = page.split(var)[2:]
    print(f"path tokens: {len(parts)}")
    ok = 0
    for idx, part in enumerate(parts[:3]):
        m = re.search(r"= '([^']*)'", part)
        if not m:
            continue
        path = m.group(1)
        for needle, repl in repls:
            path = path.replace(needle, repl)
        try:
            url = baeu(path, root)
        except Exception as e:
            print(f"[{idx}] descramble error: {e}; raw={path[:60]}")
            continue
        print(f"[{idx}] -> {url}")
        try:
            data = fetch(url, referer=PAGE, binary=True)
            magic = data[:3]
            is_jpg = magic == b"\xff\xd8\xff"
            is_png = data[:8] == b"\x89PNG\r\n\x1a\n"
            print(f"    fetched {len(data)} bytes; JPEG={is_jpg} PNG={is_png} magic={magic.hex()}")
            if is_jpg or is_png:
                ok += 1
        except Exception as e:
            print(f"    fetch error: {e}")
    print(f"\nRESULT: {ok}/3 page images recovered as real images")

if __name__ == "__main__":
    main()
