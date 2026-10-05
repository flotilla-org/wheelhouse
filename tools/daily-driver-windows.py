"""Windows process ownership for the daily driver (Windows 10+, nested jobs).

The launcher's root job owns every helper. It also retains a nested job for each
command: closing that job stops the helper and all command descendants. Abrupt
launcher termination closes the retained jobs automatically.
The stdin gate prevents a helper spawning anything before the launcher owns it.
"""
import ctypes as C
from ctypes import wintypes as W
from pathlib import Path
import subprocess
import sys

kernel = C.WinDLL('kernel32', use_last_error=True)


class BasicLimits(C.Structure):
    _fields_ = [('process_time', C.c_int64), ('job_time', C.c_int64), ('flags', W.DWORD),
                ('minimum_working_set', C.c_size_t), ('maximum_working_set', C.c_size_t),
                ('active_processes', W.DWORD), ('affinity', C.c_size_t),
                ('priority', W.DWORD), ('scheduling', W.DWORD)]


class IoCounters(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ['read_ops', 'write_ops', 'other_ops',
                                             'read_bytes', 'write_bytes', 'other_bytes']]


class ExtendedLimits(C.Structure):
    _fields_ = [('basic', BasicLimits), ('io', IoCounters),
                ('process_memory', C.c_size_t), ('job_memory', C.c_size_t),
                ('peak_process_memory', C.c_size_t), ('peak_job_memory', C.c_size_t)]


for function, args, result in [
    (kernel.CreateJobObjectW, [C.c_void_p, W.LPCWSTR], W.HANDLE),
    (kernel.SetInformationJobObject, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD], W.BOOL),
    (kernel.AssignProcessToJobObject, [W.HANDLE, W.HANDLE], W.BOOL),
    (kernel.CloseHandle, [W.HANDLE], W.BOOL),
]:
    function.argtypes, function.restype = args, result


def checked(value):
    if not value:
        raise C.WinError(C.get_last_error())
    return value


class Job:
    def __init__(self):
        self.handle = checked(kernel.CreateJobObjectW(None, None))
        limits = ExtendedLimits(basic=BasicLimits(flags=0x2000))  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        try:
            # 9 = JobObjectExtendedLimitInformation.
            checked(kernel.SetInformationJobObject(self.handle, 9, C.byref(limits), C.sizeof(limits)))
        except BaseException:
            self.close()
            raise

    def assign(self, process_handle):
        checked(kernel.AssignProcessToJobObject(self.handle, process_handle))

    def close(self):
        if self.handle is not None:
            kernel.CloseHandle(self.handle)
            self.handle = None


class WindowsProcesses:
    def __enter__(self):
        self.job = Job()
        self.children = {}
        return self

    def __exit__(self, *args):
        for job in self.children.values():
            job.close()
        self.children.clear()
        self.job.close()

    def launch(self, command, **options):
        child_job = Job()
        process = None
        try:
            process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), *map(str, command)],
                                       stdin=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW, **options)
            # Popen retains this exact process handle; opening by PID could race
            # with exit/reuse. Only this platform adapter uses CPython's handle.
            handle = getattr(process, '_handle', None)
            if handle is None:
                raise RuntimeError('Windows daily driver requires CPython subprocess handles')
            self.job.assign(int(handle))
            child_job.assign(int(handle))
            self.children[process] = child_job
            process.stdin.write(b'G')
            process.stdin.close()
            process.stdin = None
            return process
        except BaseException:
            child_job.close()
            if process is not None:
                self.children.pop(process, None)
                process.kill()
                process.wait()
                if process.stdin is not None:
                    process.stdin.close()
            raise

    def stop(self, process):
        # Force-close the entire command job, even when the helper has already
        # exited or is stuck. Other command jobs (notably the UI) stay alive.
        self.children.pop(process).close()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(f'Windows child {process.pid} did not exit after job termination') from error


def run_child(command):
    if sys.stdin.buffer.read(1) != b'G':
        raise RuntimeError('daily-driver parent did not authorize process startup')
    sys.stdin.close()
    # Both jobs already own this helper before the gate opens. The launcher
    # retains their handles and closes this command's job on exit/restart.
    return subprocess.call(command, stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)


if __name__ == '__main__':
    try:
        sys.exit(run_child(sys.argv[1:]))
    except (OSError, RuntimeError) as error:
        print(f'error: cannot start daily-driver child: {error}', file=sys.stderr)
        sys.exit(1)
