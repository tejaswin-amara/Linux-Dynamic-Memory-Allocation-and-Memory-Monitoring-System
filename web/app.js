// Real-time Telemetry Client for Linux Task Manager

let selectedPid = null;
let currentProcesses = [];
let apiToken = '';

if (typeof sessionStorage !== 'undefined') {
    apiToken = sessionStorage.getItem('memMonitorApiToken') || '';
}

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
    const searchInput = document.getElementById('proc-search');
    const searchFilter = searchInput ? searchInput.value.toLowerCase() : '';
    const sortSelect = document.getElementById('sort-select');
    const sortMode = sortSelect ? sortSelect.value : 'cpu';

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

    const countElem = document.getElementById('process-count');
    if (countElem) {
        countElem.innerText = filtered.length;
    }

    tbody.innerHTML = '';
    filtered.slice(0, 100).forEach(p => {
        const tr = document.createElement('tr');

        const tdPid = document.createElement('td');
        tdPid.textContent = p.pid;
        tr.appendChild(tdPid);

        const tdComm = document.createElement('td');
        tdComm.style.color = '#58a6ff';
        tdComm.style.fontWeight = '600';
        tdComm.textContent = p.comm;
        tr.appendChild(tdComm);

        const tdState = document.createElement('td');
        const badge = document.createElement('span');
        badge.className = 'badge';
        badge.style.background = '#30363d';
        badge.textContent = p.state;
        tdState.appendChild(badge);
        tr.appendChild(tdState);

        const tdThreads = document.createElement('td');
        tdThreads.textContent = p.num_threads;
        tr.appendChild(tdThreads);

        const tdCpu = document.createElement('td');
        tdCpu.textContent = `${p.cpu_usage_pct.toFixed(1)}%`;
        tr.appendChild(tdCpu);

        const tdRss = document.createElement('td');
        tdRss.textContent = p.vm_rss_kb.toLocaleString();
        tr.appendChild(tdRss);

        const tdActions = document.createElement('td');
        const btn = document.createElement('button');
        btn.className = 'btn btn-sm btn-secondary';
        btn.textContent = 'Signal';
        btn.addEventListener('click', () => openSignalModal(p.pid, p.comm));
        tdActions.appendChild(btn);
        tr.appendChild(tdActions);

        tbody.appendChild(tr);
    });
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
    if (!apiToken) {
        alert('Enter the API token above before sending a process signal.');
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
        alert(`Signal ${sigName} to PID ${selectedPid}: ${result.status || 'OK'}`);
        closeModal();
        fetchMetrics();
    } catch (e) {
        alert("Failed to send signal: " + e.message);
    }
}

if (typeof document !== 'undefined') {
    const searchElem = document.getElementById('proc-search');
    if (searchElem) searchElem.addEventListener('input', renderProcessTable);

    const sortElem = document.getElementById('sort-select');
    if (sortElem) sortElem.addEventListener('change', renderProcessTable);

    const refreshElem = document.getElementById('refresh-btn');
    if (refreshElem) refreshElem.addEventListener('click', fetchMetrics);

    document.querySelectorAll('.signal-btn').forEach((button) => {
        button.addEventListener('click', () => sendSignal(button.dataset.signal));
    });

    const closeModalElem = document.getElementById('close-modal-btn');
    if (closeModalElem) closeModalElem.addEventListener('click', closeModal);

    const tokenElem = document.getElementById('api-token');
    if (tokenElem) {
        tokenElem.value = apiToken;
        tokenElem.addEventListener('change', () => {
            apiToken = tokenElem.value.trim();
            if (typeof sessionStorage !== 'undefined') {
                sessionStorage.setItem('memMonitorApiToken', apiToken);
            }
        });
    }

    // Initial start & polling loop
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
