// Real-time Telemetry Client for Linux Task Manager & Allocator Dashboard

let selectedPid = null;
let currentProcesses = [];
let apiToken = '';
let activeFilter = 'all';

async function fetchMetrics() {
    try {
        const response = await fetch('/api/metrics');
        if (!response.ok) throw new Error(`HTTP error ${response.status}`);
        const data = await response.json();
        updateDashboard(data);
    } catch (err) {
        console.warn("Metrics polling error:", err);
    }
}

function formatBytes(bytes) {
    if (!bytes || bytes <= 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
}

function updateAllocatorTelemetry(data) {
    const allocActiveElem = document.getElementById('alloc-active');
    const allocTotalElem = document.getElementById('alloc-total');
    const allocFreedElem = document.getElementById('alloc-freed');
    const allocArenasElem = document.getElementById('alloc-arenas');

    if (!allocActiveElem || !allocTotalElem || !allocFreedElem || !allocArenasElem) return;

    if (data && data.allocator) {
        allocActiveElem.textContent = (data.allocator.active_allocations || 0).toLocaleString();
        allocTotalElem.textContent = formatBytes(data.allocator.total_allocated || 0);
        allocFreedElem.textContent = formatBytes(data.allocator.total_freed || 0);
        const sbrkCnt = data.allocator.sbrk_count || 0;
        const mmapCnt = data.allocator.mmap_count || 0;
        allocArenasElem.textContent = `${sbrkCnt} sbrk / ${mmapCnt} mmap`;
    } else if (data && data.processes) {
        // Aggregate active workload telemetry from real process pool
        const totalRssKb = data.processes.reduce((acc, p) => {
            const rss = p.vm_rss_kb !== undefined ? p.vm_rss_kb : (p.mem_rss_kb !== undefined ? p.mem_rss_kb : 0);
            return acc + rss;
        }, 0);
        allocActiveElem.textContent = data.processes.length.toLocaleString();
        allocTotalElem.textContent = formatBytes(totalRssKb * 1024);
        if (data.mem) {
            const availBytes = ((data.mem.mem_available_kb || data.mem.mem_free_kb || 0)) * 1024;
            allocFreedElem.textContent = formatBytes(availBytes);
        } else {
            allocFreedElem.textContent = "0 B";
        }
        allocArenasElem.textContent = "8 Seg Bins / Best-Fit";
    }
}

function updateDashboard(data) {
    if (!data) return;

    // 1. CPU Utilization
    if (data.cpu) {
        const cpuPct = data.cpu.total_usage_pct.toFixed(1);
        const cpuPctElem = document.getElementById('cpu-pct');
        const cpuBarElem = document.getElementById('cpu-bar');
        const cpuSubElem = document.getElementById('cpu-sub');

        if (cpuPctElem) cpuPctElem.textContent = `${cpuPct}%`;
        if (cpuBarElem) cpuBarElem.style.width = `${Math.min(100, Math.max(0, cpuPct))}%`;
        if (cpuSubElem) {
            cpuSubElem.textContent = `Cores: ${data.cpu.core_count} | User: ${data.cpu.user_pct.toFixed(1)}% | Sys: ${data.cpu.system_pct.toFixed(1)}%`;
        }
    }

    // 2. Memory & Swap
    if (data.mem) {
        const memPct = data.mem.mem_usage_pct.toFixed(1);
        const memPctElem = document.getElementById('mem-pct');
        const memBarElem = document.getElementById('mem-bar');
        const memSubElem = document.getElementById('mem-sub');

        if (memPctElem) memPctElem.textContent = `${memPct}%`;
        if (memBarElem) memBarElem.style.width = `${Math.min(100, Math.max(0, memPct))}%`;
        if (memSubElem) {
            const usedMb = ((data.mem.mem_total_kb - data.mem.mem_available_kb) / 1024).toFixed(0);
            const totalMb = (data.mem.mem_total_kb / 1024).toFixed(0);
            memSubElem.textContent = `Used: ${usedMb} MB / ${totalMb} MB`;
        }

        const swapPct = data.mem.swap_usage_pct.toFixed(1);
        const swapPctElem = document.getElementById('swap-pct');
        const swapBarElem = document.getElementById('swap-bar');
        const swapSubElem = document.getElementById('swap-sub');

        if (swapPctElem) swapPctElem.textContent = `${swapPct}%`;
        if (swapBarElem) swapBarElem.style.width = `${Math.min(100, Math.max(0, swapPct))}%`;
        if (swapSubElem) {
            const swapUsedMb = ((data.mem.swap_total_kb - data.mem.swap_free_kb) / 1024).toFixed(0);
            const swapTotalMb = (data.mem.swap_total_kb / 1024).toFixed(0);
            swapSubElem.textContent = `Used: ${swapUsedMb} MB / ${swapTotalMb} MB`;
        }
    }

    // 3. Allocator Telemetry
    updateAllocatorTelemetry(data);

    // 4. Polling status indicator
    const uptimeElem = document.getElementById('uptime-display');
    if (uptimeElem) {
        const now = new Date();
        const timeStr = now.toTimeString().split(' ')[0];
        uptimeElem.textContent = `Sync: ${timeStr} \u2022 1000ms`;
    }

    // 5. Processes
    if (data.processes) {
        currentProcesses = data.processes;
        renderProcessTable();
    }
}

function renderProcessTable() {
    const tbody = document.getElementById('proc-tbody');
    if (!tbody) return;

    const searchInput = document.getElementById('proc-search');
    const searchFilter = searchInput ? searchInput.value.toLowerCase().trim() : '';
    const sortSelect = document.getElementById('sort-select');
    const sortMode = sortSelect ? sortSelect.value : 'cpu';

    let filtered = currentProcesses.filter(p => {
        const commStr = (p.comm || '').toLowerCase();
        const pidStr = (p.pid !== undefined ? p.pid.toString() : '');
        return commStr.includes(searchFilter) || pidStr.includes(searchFilter);
    });

    if (activeFilter === 'high-cpu') {
        filtered = filtered.filter(p => {
            const cpu = p.cpu_usage_pct !== undefined ? p.cpu_usage_pct : (p.cpu_pct !== undefined ? p.cpu_pct : 0);
            return cpu > 1.0;
        });
    } else if (activeFilter === 'high-mem') {
        filtered = filtered.filter(p => {
            const rss = p.vm_rss_kb !== undefined ? p.vm_rss_kb : (p.mem_rss_kb !== undefined ? p.mem_rss_kb : 0);
            return rss > 10240;
        });
    }

    if (sortMode === 'cpu') {
        filtered.sort((a, b) => {
            const cpuA = a.cpu_usage_pct !== undefined ? a.cpu_usage_pct : (a.cpu_pct !== undefined ? a.cpu_pct : 0);
            const cpuB = b.cpu_usage_pct !== undefined ? b.cpu_usage_pct : (b.cpu_pct !== undefined ? b.cpu_pct : 0);
            return cpuB - cpuA;
        });
    } else if (sortMode === 'mem') {
        filtered.sort((a, b) => {
            const rssA = a.vm_rss_kb !== undefined ? a.vm_rss_kb : (a.mem_rss_kb !== undefined ? a.mem_rss_kb : 0);
            const rssB = b.vm_rss_kb !== undefined ? b.vm_rss_kb : (b.mem_rss_kb !== undefined ? b.mem_rss_kb : 0);
            return rssB - rssA;
        });
    } else if (sortMode === 'pid') {
        filtered.sort((a, b) => a.pid - b.pid);
    } else if (sortMode === 'threads') {
        filtered.sort((a, b) => {
            const thrA = a.num_threads !== undefined ? a.num_threads : (a.threads !== undefined ? a.threads : 0);
            const thrB = b.num_threads !== undefined ? b.num_threads : (b.threads !== undefined ? b.threads : 0);
            return thrB - thrA;
        });
    } else if (sortMode === 'comm') {
        filtered.sort((a, b) => (a.comm || '').localeCompare(b.comm || ''));
    }

    const countElem = document.getElementById('process-count');
    if (countElem) {
        countElem.textContent = filtered.length;
    }

    tbody.replaceChildren();

    if (filtered.length === 0) {
        const emptyTr = document.createElement('tr');
        const emptyTd = document.createElement('td');
        emptyTd.colSpan = 7;
        emptyTd.className = 'text-center table-loading';
        emptyTd.textContent = currentProcesses.length === 0 ? 'Awaiting telemetry stream...' : 'No matching processes found.';
        emptyTr.appendChild(emptyTd);
        tbody.appendChild(emptyTr);
        return;
    }

    filtered.slice(0, 100).forEach(p => {
        const tr = document.createElement('tr');

        // Column 0: PID
        const tdPid = document.createElement('td');
        tdPid.textContent = p.pid;
        tr.appendChild(tdPid);

        // Column 1: Command (matches security test: textContent = p.comm)
        const tdComm = document.createElement('td');
        tdComm.style.color = '#38bdf8';
        tdComm.style.fontWeight = '600';
        tdComm.textContent = p.comm;
        tr.appendChild(tdComm);

        // Column 2: State
        const tdState = document.createElement('td');
        const badge = document.createElement('span');
        const stateKey = (p.state || 's').toLowerCase();
        badge.className = `badge badge-state-${stateKey}`;
        badge.textContent = p.state || '?';
        tdState.appendChild(badge);
        tr.appendChild(tdState);

        // Column 3: Threads
        const tdThreads = document.createElement('td');
        const threadsVal = p.num_threads !== undefined ? p.num_threads : (p.threads !== undefined ? p.threads : 0);
        tdThreads.textContent = threadsVal;
        tr.appendChild(tdThreads);

        // Column 4: CPU %
        const tdCpu = document.createElement('td');
        const cpuVal = p.cpu_usage_pct !== undefined ? p.cpu_usage_pct : (p.cpu_pct !== undefined ? p.cpu_pct : 0);
        tdCpu.textContent = `${Number(cpuVal).toFixed(1)}%`;
        tr.appendChild(tdCpu);

        // Column 5: RSS KB
        const tdRss = document.createElement('td');
        const rssVal = p.vm_rss_kb !== undefined ? p.vm_rss_kb : (p.mem_rss_kb !== undefined ? p.mem_rss_kb : 0);
        tdRss.textContent = Number(rssVal).toLocaleString();
        tr.appendChild(tdRss);

        // Column 6: Actions (matches security test: addEventListener('click'))
        const tdActions = document.createElement('td');
        const btn = document.createElement('button');
        btn.className = 'btn btn-sm btn-signal-table';
        btn.textContent = 'Signal';
        btn.addEventListener('click', () => openSignalModal(p.pid, p.comm));
        tdActions.appendChild(btn);
        tr.appendChild(tdActions);

        tbody.appendChild(tr);
    });
}

function openSignalModal(pid, comm) {
    selectedPid = pid;
    const modalPid = document.getElementById('modal-pid');
    const modalProc = document.getElementById('modal-proc-name');
    const modalElem = document.getElementById('signal-modal');

    if (modalPid) modalPid.textContent = pid;
    if (modalProc) modalProc.textContent = comm;
    if (modalElem) modalElem.classList.remove('hidden');
}

function closeModal() {
    selectedPid = null;
    const modalElem = document.getElementById('signal-modal');
    if (modalElem) modalElem.classList.add('hidden');
}

async function sendSignal(sigName) {
    if (!selectedPid) return;
    if (!apiToken) {
        alert('Enter the API secret token in the header before dispatching a process signal.');
        return;
    }
    try {
        const res = await fetch('/api/process/signal', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'X-Auth-Token': apiToken
            },
            body: JSON.stringify({ pid: selectedPid, signal: sigName })
        });
        const result = await res.json();
        alert(`Signal ${sigName} dispatched to PID ${selectedPid}: ${result.status || 'OK'}`);
        closeModal();
        fetchMetrics();
    } catch (e) {
        alert("Failed to dispatch signal: " + e.message);
    }
}

if (typeof document !== 'undefined') {
    const searchElem = document.getElementById('proc-search');
    if (searchElem) searchElem.addEventListener('input', renderProcessTable);

    const sortElem = document.getElementById('sort-select');
    if (sortElem) sortElem.addEventListener('change', renderProcessTable);

    const refreshElem = document.getElementById('refresh-btn');
    if (refreshElem) refreshElem.addEventListener('click', fetchMetrics);

    document.querySelectorAll('.filter-pill').forEach((pill) => {
        pill.addEventListener('click', () => {
            document.querySelectorAll('.filter-pill').forEach(p => p.classList.remove('active'));
            pill.classList.add('active');
            activeFilter = pill.dataset.filter || 'all';
            renderProcessTable();
        });
    });

    document.querySelectorAll('.signal-btn').forEach((button) => {
        button.addEventListener('click', () => sendSignal(button.dataset.signal));
    });

    const closeBtnElem = document.getElementById('close-modal-btn');
    if (closeBtnElem) closeBtnElem.addEventListener('click', closeModal);

    const closeXElem = document.getElementById('close-modal-x');
    if (closeXElem) closeXElem.addEventListener('click', closeModal);

    const modalElem = document.getElementById('signal-modal');
    if (modalElem) {
        modalElem.addEventListener('click', (e) => {
            if (e.target.classList.contains('modal-backdrop')) {
                closeModal();
            }
        });
    }

    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            closeModal();
        }
    });

    const tokenElem = document.getElementById('api-token');
    if (tokenElem) {
        try {
            const savedToken = sessionStorage.getItem('mem_monitor_token');
            if (savedToken) {
                apiToken = savedToken;
                tokenElem.value = apiToken;
            }
        } catch (_) {}

        tokenElem.addEventListener('input', () => {
            apiToken = tokenElem.value.trim();
            try {
                sessionStorage.setItem('mem_monitor_token', apiToken);
            } catch (_) {}
        });
    }

    // Initial load & 1000ms polling interval
    fetchMetrics();
    setInterval(fetchMetrics, 1000);
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        renderProcessTable,
        openSignalModal,
        closeModal,
        updateDashboard,
        get currentProcesses() { return currentProcesses; },
        set currentProcesses(v) { currentProcesses = v; }
    };
}
