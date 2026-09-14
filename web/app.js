// Real-time Telemetry Client for Linux Task Manager

let selectedPid = null;
let currentProcesses = [];

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

function updateDashboard(data) {
    if (!data) return;

    // 1. CPU
    if (data.cpu) {
        const cpuPct = data.cpu.total_usage_pct.toFixed(1);
        document.getElementById('cpu-pct').innerText = `${cpuPct}%`;
        document.getElementById('cpu-bar').style.width = `${Math.min(100, Math.max(0, cpuPct))}%`;
        document.getElementById('cpu-sub').innerText = `Cores: ${data.cpu.core_count} | User: ${data.cpu.user_pct.toFixed(1)}% | Sys: ${data.cpu.system_pct.toFixed(1)}%`;
    }

    // 2. Memory
    if (data.mem) {
        const memPct = data.mem.mem_usage_pct.toFixed(1);
        document.getElementById('mem-pct').innerText = `${memPct}%`;
        document.getElementById('mem-bar').style.width = `${Math.min(100, Math.max(0, memPct))}%`;
        const usedMb = ((data.mem.mem_total_kb - data.mem.mem_available_kb) / 1024).toFixed(0);
        const totalMb = (data.mem.mem_total_kb / 1024).toFixed(0);
        document.getElementById('mem-sub').innerText = `Used: ${usedMb} MB / ${totalMb} MB`;

        const swapPct = data.mem.swap_usage_pct.toFixed(1);
        document.getElementById('swap-pct').innerText = `${swapPct}%`;
        document.getElementById('swap-bar').style.width = `${Math.min(100, Math.max(0, swapPct))}%`;
        const swapUsedMb = ((data.mem.swap_total_kb - data.mem.swap_free_kb) / 1024).toFixed(0);
        const swapTotalMb = (data.mem.swap_total_kb / 1024).toFixed(0);
        document.getElementById('swap-sub').innerText = `Used: ${swapUsedMb} MB / ${swapTotalMb} MB`;
    }

    // 3. Processes
    if (data.processes) {
        currentProcesses = data.processes;
        renderProcessTable();
    }
}

function renderProcessTable() {
    const tbody = document.getElementById('proc-tbody');
    const searchFilter = document.getElementById('proc-search').value.toLowerCase();
    const sortMode = document.getElementById('sort-select').value;

    let filtered = currentProcesses.filter(p => 
        p.comm.toLowerCase().includes(searchFilter) || p.pid.toString().includes(searchFilter)
    );

    if (sortMode === 'cpu') {
        filtered.sort((a, b) => b.cpu_usage_pct - a.cpu_usage_pct);
    } else if (sortMode === 'mem') {
        filtered.sort((a, b) => b.vm_rss_kb - a.vm_rss_kb);
    } else if (sortMode === 'pid') {
        filtered.sort((a, b) => a.pid - b.pid);
    }

    document.getElementById('process-count').innerText = filtered.length;

    tbody.innerHTML = filtered.slice(0, 100).map(p => `
        <tr>
            <td>${p.pid}</td>
            <td style="color: #58a6ff; font-weight: 600;">${escapeHtml(p.comm)}</td>
            <td><span class="badge" style="background:#30363d;">${p.state}</span></td>
            <td>${p.num_threads}</td>
            <td>${p.cpu_usage_pct.toFixed(1)}%</td>
            <td>${p.vm_rss_kb.toLocaleString()}</td>
            <td>
                <button class="btn btn-sm btn-secondary" onclick="openSignalModal(${p.pid}, '${escapeHtml(p.comm)}')">Signal</button>
            </td>
        </tr>
    `).join('');
}

function openSignalModal(pid, comm) {
    selectedPid = pid;
    document.getElementById('modal-pid').innerText = pid;
    document.getElementById('modal-proc-name').innerText = comm;
    document.getElementById('signal-modal').classList.remove('hidden');
}

function closeModal() {
    selectedPid = null;
    document.getElementById('signal-modal').classList.add('hidden');
}

async function sendSignal(sigName) {
    if (!selectedPid) return;
    try {
        const res = await fetch('/api/process/signal', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ pid: selectedPid, signal: sigName })
        });
        const result = await res.json();
        alert(`Signal ${sigName} to PID ${selectedPid}: ${result.status || 'OK'}`);
        closeModal();
        fetchMetrics();
    } catch (e) {
        alert("Failed to send signal: " + e.message);
    }
}

function escapeHtml(str) {
    return str.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

document.getElementById('proc-search').addEventListener('input', renderProcessTable);
document.getElementById('sort-select').addEventListener('change', renderProcessTable);
document.getElementById('refresh-btn').addEventListener('click', fetchMetrics);

// Initial start & polling loop
fetchMetrics();
setInterval(fetchMetrics, 1000);
