"""Windows process ownership for the daily driver (Windows 10+, nested jobs).

The launcher's job owns every helper, so even abrupt launcher termination closes
the job and kills the children. Each helper owns a nested job for its command:
connector exit/restart or UI closure also kills that command's descendants.
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
    (kernel.GetCurrentProcess, [], W.HANDLE),
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
        return self

    def __exit__(self, *args):
        self.job.close()

    def launch(self, command, **options):
        process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), *map(str, command)],
                                   stdin=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW, **options)
        try:
            # Popen retains this exact process handle; opening by PID could race
            # with exit/reuse. Only this platform adapter uses CPython's handle.
            self.job.assign(int(process._handle))
            process.stdin.write(b'G')
            process.stdin.close()
            process.stdin = None
            return process
        except BaseException:
            process.kill()
            process.wait()
            if process.stdin is not None:
                process.stdin.close()
            raise


def run_child(command):
    if sys.stdin.buffer.read(1) != b'G':
        raise RuntimeError('daily-driver parent did not authorize process startup')
    sys.stdin.close()
    job = Job()
    job.assign(kernel.GetCurrentProcess())
    # This handle deliberately lives until process exit. Closing it ourselves
    # would terminate this helper before it could return the command's status.
    # The OS closes it on exit, killing any surviving command descendants.
    return subprocess.call(command, stdin=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)


if __name__ == '__main__':
    try:
        sys.exit(run_child(sys.argv[1:]))
    except (OSError, RuntimeError) as error:
        print(f'error: cannot start daily-driver child: {error}', file=sys.stderr)
        sys.exit(1)
