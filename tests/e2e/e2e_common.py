"""
Common utilities, fixtures, and constants for E2E tests.
Provides clean wrappers for allocator dynamic library, LD_PRELOAD execution,
mem_monitor daemon lifecycle management, and HTTP client operations.
"""

import ctypes
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

PROJECT_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..")
)
LIBMYALLOC_PATH = os.path.join(PROJECT_ROOT, "libmyalloc.so")
MEM_MONITOR_PATH = os.path.join(PROJECT_ROOT, "mem_monitor")
BIN_DIR = os.path.join(PROJECT_ROOT, "bin")

# C allocator constants
ALLOC_MAGIC_HEADER = 0xDEADBEEF
ALLOC_MAGIC_FOOTER = 0xBEEFDEAD
MMAP_THRESHOLD = 128 * 1024  # 128 KB


class AllocatorStats(ctypes.Structure):
    _fields_ = [
        ("total_allocated", ctypes.c_size_t),
        ("total_freed", ctypes.c_size_t),
        ("current_heap_size", ctypes.c_size_t),
        ("mmap_allocations", ctypes.c_size_t),
        ("sbrk_allocations", ctypes.c_size_t),
    ]


def ensure_binaries_built():
    """Ensure libmyalloc.so and mem_monitor exist, building them via make all if needed."""
    if not os.path.isfile(LIBMYALLOC_PATH) or not os.path.isfile(MEM_MONITOR_PATH):
        subprocess.run(["make", "all"], cwd=PROJECT_ROOT, check=True, capture_output=True)


def load_allocator(so_path=None):
    """Load libmyalloc.so with properly defined signatures."""
    path = so_path or LIBMYALLOC_PATH
    if not os.path.isfile(path):
        ensure_binaries_built()
    if not os.path.isfile(path):
        raise FileNotFoundError(f"Shared library not found at: {path}")

    lib = ctypes.CDLL(path, use_errno=True)

    lib.allocator_init.restype = None
    lib.allocator_init.argtypes = []

    lib.allocator_destroy.restype = None
    lib.allocator_destroy.argtypes = []

    lib.allocator_get_stats.restype = AllocatorStats
    lib.allocator_get_stats.argtypes = []

    lib.allocator_verify_integrity.restype = ctypes.c_int
    lib.allocator_verify_integrity.argtypes = []

    lib.my_malloc.restype = ctypes.c_void_p
    lib.my_malloc.argtypes = [ctypes.c_size_t]

    lib.my_free.restype = None
    lib.my_free.argtypes = [ctypes.c_void_p]

    lib.my_calloc.restype = ctypes.c_void_p
    lib.my_calloc.argtypes = [ctypes.c_size_t, ctypes.c_size_t]

    lib.my_realloc.restype = ctypes.c_void_p
    lib.my_realloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t]

    return lib


def run_command(cmd, env=None, timeout=15, input_data=None):
    """Run an arbitrary command and return (exit_code, stdout_str, stderr_str)."""
    full_env = os.environ.copy()
    if env:
        full_env.update(env)

    stdin_arg = subprocess.PIPE if input_data is not None else None
    input_bytes = input_data.encode("utf-8") if isinstance(input_data, str) else input_data

    for attempt in range(5):
        try:
            proc = subprocess.Popen(
                cmd,
                cwd=PROJECT_ROOT,
                env=full_env,
                stdin=stdin_arg,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            break
        except PermissionError:
            if attempt < 4:
                time.sleep(0.1)
            else:
                raise
    try:
        stdout_str, stderr_str = proc.communicate(input=input_data, timeout=timeout)
        return proc.returncode, stdout_str, stderr_str
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout_str, stderr_str = proc.communicate()
        return -999, stdout_str, stderr_str + "\n[ERROR: Process timed out]"


def run_under_preload(cmd, env_extra=None, timeout=15, input_data=None):
    """Run a command with LD_PRELOAD set to libmyalloc.so."""
    preload_env = {"LD_PRELOAD": LIBMYALLOC_PATH}
    if env_extra:
        preload_env.update(env_extra)
    return run_command(cmd, env=preload_env, timeout=timeout, input_data=input_data)


def run_isolated_python(snippet, env=None, timeout=10):
    """
    Run a Python code snippet in an isolated subprocess.
    Essential for tests verifying fatal abort() signals (SIGABRT)
    which terminate the entire process.
    """
    cmd = [sys.executable, "-c", snippet]
    return run_command(cmd, env=env, timeout=timeout)


def compile_c_code(source_code, output_path, extra_flags=None):
    """Compile C code to an executable binary using gcc."""
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as f:
        f.write(source_code)
        src_path = f.name

    try:
        flags = [
            "gcc",
            "-Wall",
            "-Wextra",
            "-std=c11",
            "-D_GNU_SOURCE",
            f"-I{os.path.join(PROJECT_ROOT, 'include')}",
            src_path,
            "-o",
            output_path,
            "-pthread",
        ]
        if extra_flags:
            flags.extend(extra_flags)

        res = subprocess.run(flags, cwd=PROJECT_ROOT, capture_output=True, text=True)
        if res.returncode != 0:
            raise RuntimeError(f"Failed to compile C code:\n{res.stderr}")
        return output_path
    finally:
        if os.path.exists(src_path):
            os.remove(src_path)


def get_free_port():
    """Find and return an unused local TCP port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        s.listen(1)
        return s.getsockname()[1]


class ServerContext:
    """Context manager managing the lifecycle of the mem_monitor daemon."""

    def __init__(self, port=None, token="test-e2e-token-12345", bind="127.0.0.1"):
        self.port = port or get_free_port()
        self.token = token
        self.bind = bind
        self.proc = None

    def __enter__(self):
        ensure_binaries_built()
        cmd = [
            MEM_MONITOR_PATH,
            "--headless",
            "--bind",
            self.bind,
            "--port",
            str(self.port),
            "--token",
            self.token,
        ]
        for attempt in range(5):
            try:
                self.proc = subprocess.Popen(
                    cmd,
                    cwd=PROJECT_ROOT,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                break
            except PermissionError:
                if attempt < 4:
                    time.sleep(0.15)
                else:
                    raise

        # Poll HTTP endpoint until reachable (up to 3 seconds)
        deadline = time.time() + 3.0
        connected = False
        url = f"http://{self.bind}:{self.port}/api/metrics"
        while time.time() < deadline:
            if self.proc.poll() is not None:
                out, err = self.proc.communicate()
                raise RuntimeError(
                    f"mem_monitor terminated prematurely with code {self.proc.returncode}:\n{err}\n{out}"
                )
            try:
                req = urllib.request.Request(url, method="GET")
                with urllib.request.urlopen(req, timeout=0.2) as resp:
                    if resp.status == 200:
                        connected = True
                        break
            except Exception:
                time.sleep(0.05)

        if not connected:
            self.proc.kill()
            out, err = self.proc.communicate()
            raise TimeoutError(f"Failed to connect to mem_monitor on port {self.port} within deadline:\n{err}\n{out}")

        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=1.0)
        if self.proc:
            if self.proc.stdout:
                self.proc.stdout.close()
            if self.proc.stderr:
                self.proc.stderr.close()


def raw_http_exchange(port, method, path, headers=None, body="", host="127.0.0.1", timeout=5):
    """Execute low-overhead HTTP request matching gui_server's custom socket model."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        req_headers = {
            "Host": f"{host}:{port}",
            "Connection": "close",
        }
        if body:
            req_headers["Content-Length"] = str(len(body.encode("utf-8") if isinstance(body, str) else body))
        if headers:
            req_headers.update(headers)

        header_str = "".join(f"{k}: {v}\r\n" for k, v in req_headers.items())
        req = f"{method} {path} HTTP/1.1\r\n{header_str}\r\n"
        req_bytes = req.encode("utf-8") + (body.encode("utf-8") if isinstance(body, str) else body)

        s.sendall(req_bytes)

        chunks = []
        while True:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
            except OSError:
                break
    finally:
        s.close()

    resp_raw = b"".join(chunks).decode("utf-8", errors="replace")
    if not resp_raw:
        return 0, "", {}

    parts = resp_raw.split("\r\n\r\n", 1)
    header_part = parts[0]
    body_part = parts[1] if len(parts) > 1 else ""

    lines = header_part.split("\r\n")
    status_line = lines[0] if lines else ""
    tokens = status_line.split()
    status_code = int(tokens[1]) if len(tokens) > 1 and tokens[1].isdigit() else 0

    parsed_headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            parsed_headers[k.strip().lower()] = v.strip()

    try:
        parsed_body = json.loads(body_part)
        return status_code, parsed_body, parsed_headers
    except (ValueError, TypeError):
        return status_code, body_part, parsed_headers


def http_get(port, path, host="127.0.0.1", timeout=5):
    """Perform HTTP GET request and return (status_code, body_dict_or_str, raw_headers)."""
    return raw_http_exchange(port, "GET", path, host=host, timeout=timeout)


def http_post(port, path, headers=None, json_data=None, raw_data=None, host="127.0.0.1", timeout=5):
    """Perform HTTP POST request and return (status_code, body_dict_or_str, raw_headers)."""
    post_headers = {"Content-Type": "application/json"}
    if headers:
        post_headers.update(headers)

    if json_data is not None:
        body = json.dumps(json_data)
    elif raw_data is not None:
        body = raw_data
    else:
        body = ""

    return raw_http_exchange(port, "POST", path, headers=post_headers, body=body, host=host, timeout=timeout)
