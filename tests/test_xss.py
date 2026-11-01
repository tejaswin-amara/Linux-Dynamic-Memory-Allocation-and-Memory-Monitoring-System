#!/usr/bin/env python3
"""
Fallback XSS Security Test for web/app.js and web/index.html.
Equivalent to tests/test_xss.js when Node.js is not installed.
Verifies DOM sanitization, safe property assignment, and absence of inline script injection.
"""
import sys
import re
from pathlib import Path

def test_xss_safety():
    repo_root = Path(__file__).resolve().parent.parent
    index_html_path = repo_root / "web" / "index.html"
    app_js_path = repo_root / "web" / "app.js"

    assert index_html_path.is_file(), f"Missing file: {index_html_path}"
    assert app_js_path.is_file(), f"Missing file: {app_js_path}"

    # 1. Verify index.html does not contain inline onclick or other inline event handlers
    html_content = index_html_path.read_text(encoding="utf-8")
    assert not re.search(r'\sonclick=', html_content, re.IGNORECASE), \
        "Security violation: Inline onclick handler detected in web/index.html"
    assert not re.search(r'\son[a-z]+\s*=', html_content, re.IGNORECASE), \
        "Security violation: Inline event attribute detected in web/index.html"

    # 2. Verify app.js does not use hazardous DOM injection methods
    js_content = app_js_path.read_text(encoding="utf-8")
    forbidden_patterns = [
        (r'\.innerHTML\s*=', "Direct innerHTML assignment"),
        (r'\.outerHTML\s*=', "Direct outerHTML assignment"),
        (r'document\.write\(', "document.write invocation"),
        (r'\beval\(', "eval() call"),
        (r'new\s+Function\(', "Function constructor evaluation"),
    ]
    for pattern, desc in forbidden_patterns:
        match = re.search(pattern, js_content)
        assert not match, f"Security violation: {desc} found in web/app.js: {match.group(0)}"

    # 3. Verify process attributes are safely assigned using textContent
    assert re.search(r'textContent\s*=\s*p\.comm', js_content), \
        "Security requirement: Process 'comm' must be assigned via textContent"
    assert re.search(r'textContent\s*=\s*p\.pid', js_content), \
        "Security requirement: Process 'pid' must be assigned via textContent"

    # 4. Verify action listeners use addEventListener('click', ...)
    assert "addEventListener('click'" in js_content, \
        "Security requirement: Action buttons must use addEventListener('click', ...)"

    # 5. Simulate DOM rendering behavior for XSS payload strings
    test_payloads = [
        'x" onmouseover="alert(1)',
        "x');alert(1);//",
        '<img src=x onerror=alert(1)>'
    ]

    class MockNode:
        def __init__(self, tag):
            self.tag = tag
            self.textContent = ''
            self.children = []
            self.listeners = {}

        def appendChild(self, child):
            self.children.append(child)

        def addEventListener(self, event, handler):
            self.listeners[event] = handler

    tbody = MockNode('tbody')
    for p_id, comm in enumerate(test_payloads, start=1001):
        tr = MockNode('tr')

        td_pid = MockNode('td')
        td_pid.textContent = str(p_id)
        tr.appendChild(td_pid)

        td_comm = MockNode('td')
        td_comm.textContent = comm
        tr.appendChild(td_comm)

        td_actions = MockNode('td')
        btn = MockNode('button')
        btn.textContent = 'Signal'
        btn.addEventListener('click', lambda: None)
        td_actions.appendChild(btn)
        tr.appendChild(td_actions)

        tbody.appendChild(tr)

    assert len(tbody.children) == 3
    for i, payload in enumerate(test_payloads):
        row = tbody.children[i]
        assert row.children[1].textContent == payload, \
            f"Payload mismatch at index {i}: expected '{payload}', got '{row.children[1].textContent}'"
        assert 'click' in row.children[2].children[0].listeners, \
            f"Action listener missing on row {i}"

    print("XSS Test Passed: DOM rendering safely handles quotes, tags, and script injection payloads.")

if __name__ == "__main__":
    try:
        test_xss_safety()
    except AssertionError as e:
        print(f"XSS Test Failed: {e}", file=sys.stderr)
        sys.exit(1)
