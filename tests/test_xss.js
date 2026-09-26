const assert = require('assert');
const appJs = require('../web/app.js');

// 1. Test escapeHtml directly
assert.strictEqual(appJs.escapeHtml('x" onmouseover="alert(1)'), 'x&quot; onmouseover=&quot;alert(1)');
assert.strictEqual(appJs.escapeHtml("x');alert(1);//"), 'x&#39;);alert(1);//');
assert.strictEqual(appJs.escapeHtml('<script>alert("XSS")</script>'), '&lt;script&gt;alert(&quot;XSS&quot;)&lt;/script&gt;');

// 2. Test DOM building logic using a minimal DOM mock
function createMockElement(tagName) {
    const children = [];
    const listeners = {};
    return {
        tagName,
        style: {},
        textContent: '',
        className: '',
        children,
        appendChild(child) { children.push(child); },
        addEventListener(event, fn) { listeners[event] = fn; },
        listeners
    };
}

const tbody = createMockElement('tbody');
tbody.innerHTML = '';

const countElem = createMockElement('span');

global.document = {
    getElementById(id) {
        if (id === 'proc-tbody') return tbody;
        if (id === 'proc-search') return { value: '' };
        if (id === 'sort-select') return { value: 'pid' };
        if (id === 'process-count') return countElem;
        if (id === 'modal-pid') return createMockElement('span');
        if (id === 'modal-proc-name') return createMockElement('span');
        if (id === 'signal-modal') return { classList: { remove() {}, add() {} } };
        return null;
    },
    createElement(tag) {
        return createMockElement(tag);
    }
};

appJs.currentProcesses = [
    { pid: 1001, comm: 'x" onmouseover="alert(1)', state: 'S', num_threads: 1, cpu_usage_pct: 5.0, vm_rss_kb: 1024 },
    { pid: 1002, comm: "x');alert(1);//", state: 'R', num_threads: 2, cpu_usage_pct: 12.0, vm_rss_kb: 2048 },
    { pid: 1003, comm: '<img src=x onerror=alert(1)>', state: 'S', num_threads: 1, cpu_usage_pct: 1.0, vm_rss_kb: 512 }
];

appJs.renderProcessTable();

assert.strictEqual(tbody.children.length, 3);
assert.strictEqual(tbody.children[0].children[1].textContent, 'x" onmouseover="alert(1)');
assert.strictEqual(tbody.children[1].children[1].textContent, "x');alert(1);//");
assert.strictEqual(tbody.children[2].children[1].textContent, '<img src=x onerror=alert(1)>');

// Verify buttons have event listeners attached safely, no inline onclick strings
assert.strictEqual(typeof tbody.children[0].children[6].children[0].listeners['click'], 'function');

console.log("XSS Test Passed: DOM rendering safely handles quotes, tags, and script injection payloads.");
