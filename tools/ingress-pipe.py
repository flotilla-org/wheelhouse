"""Windows byte-pipe adapter for http.client; verifies ADR 0011 server identity."""
import ctypes as C
from ctypes import wintypes as W
import io
import time

class Overlapped(C.Structure):
    _fields_ = [('internal', C.c_size_t), ('internal_high', C.c_size_t),
                ('offset', W.DWORD), ('offset_high', W.DWORD), ('event', W.HANDLE)]


kernel = C.WinDLL('kernel32', use_last_error=True)
security = C.WinDLL('advapi32', use_last_error=True)
# Explicit signatures prevent truncation of 64-bit handles.
for function, args, result in [
    (kernel.CreateFileW, [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
    (kernel.CreateEventW, [C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR], W.HANDLE),
    (kernel.WaitForSingleObject, [W.HANDLE, W.DWORD], W.DWORD),
    (kernel.ReadFile, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(Overlapped)], W.BOOL),
    (kernel.WriteFile, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(Overlapped)], W.BOOL),
    (kernel.GetOverlappedResult, [W.HANDLE, C.POINTER(Overlapped), C.POINTER(W.DWORD), W.BOOL], W.BOOL),
    (kernel.CancelIoEx, [W.HANDLE, C.POINTER(Overlapped)], W.BOOL),
    (kernel.CloseHandle, [W.HANDLE], W.BOOL),
    (kernel.GetNamedPipeServerProcessId, [W.HANDLE, C.POINTER(W.ULONG)], W.BOOL),
    (kernel.OpenProcess, [W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
    (kernel.GetCurrentProcess, [], W.HANDLE),
    (security.OpenProcessToken, [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)], W.BOOL),
    (security.GetTokenInformation, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)], W.BOOL),
    (security.EqualSid, [C.c_void_p, C.c_void_p], W.BOOL),
]:
    function.argtypes, function.restype = args, result


def checked(value):
    if not value:
        raise C.WinError(C.get_last_error())
    return value


def owner(process):
    token = W.HANDLE()
    checked(security.OpenProcessToken(process, 8, C.byref(token)))
    try:
        size = W.DWORD()
        security.GetTokenInformation(token, 1, None, 0, C.byref(size))
        buffer = C.create_string_buffer(size.value)
        checked(security.GetTokenInformation(token, 1, buffer, size, C.byref(size)))
        # TOKEN_USER begins with a SID pointer; keep buffer alive with the SID.
        return buffer, C.cast(buffer, C.POINTER(C.c_void_p))[0]
    finally:
        kernel.CloseHandle(token)


def verify_server(handle, expected_sid=None):
    pid = W.ULONG()
    checked(kernel.GetNamedPipeServerProcessId(handle, C.byref(pid)))
    # Keep the queried process handle alive through token inspection. PID reuse
    # can race between querying the pipe and opening the process (ADR 0011);
    # the connected pipe and same-user trust policy bound this check.
    process = checked(kernel.OpenProcess(0x1000, False, pid.value))
    try:
        remote_buffer, remote_sid = owner(process)
        local_buffer, local_sid = owner(kernel.GetCurrentProcess())
        if not security.EqualSid(remote_sid, local_sid if expected_sid is None else expected_sid):
            raise PermissionError('named-pipe server belongs to another user')
    finally:
        kernel.CloseHandle(process)


class PipeReader(io.RawIOBase):
    def __init__(self, pipe):
        super().__init__()
        self.pipe = pipe

    def readable(self):
        return True

    def readinto(self, data):
        # BufferedReader owns only this reader; HTTPConnection owns the pipe.
        buffer = (C.c_char * len(data)).from_buffer(data)
        return self.pipe.transfer(kernel.ReadFile, buffer, len(data))


class PipeSocket:
    def __init__(self, name, timeout):
        self.timeout = timeout
        self.handle = None
        deadline = time.monotonic() + timeout
        while True:
            # OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION:
            # bounded I/O; server cannot impersonate this client to act as it.
            handle = kernel.CreateFileW(name, 0xC0000000, 0, None, 3, 0x40110000, None)
            if handle != W.HANDLE(-1).value:
                break
            error = C.get_last_error()
            if error not in (2, 231) or time.monotonic() >= deadline:
                raise C.WinError(error)
            time.sleep(.01)
        try:
            verify_server(handle)
        except BaseException:
            kernel.CloseHandle(handle)
            raise
        # Single ownership transfer: no CRT fd or duplicated-handle failure path.
        self.handle = handle

    def transfer(self, function, buffer, size):
        event = checked(kernel.CreateEventW(None, True, False, None))
        pending = Overlapped(event=event)
        count = W.DWORD()
        try:
            if not function(self.handle, buffer, size, C.byref(count), C.byref(pending)):
                error = C.get_last_error()
                if function is kernel.ReadFile and error in (109, 232):
                    return 0
                if error != 997:  # ERROR_IO_PENDING
                    raise C.WinError(error)
                waited = kernel.WaitForSingleObject(event, max(1, int(self.timeout * 1000)))
                if waited != 0:  # WAIT_OBJECT_0
                    # Settle cancellation before releasing OVERLAPPED and buffer.
                    kernel.CancelIoEx(self.handle, C.byref(pending))
                    kernel.GetOverlappedResult(self.handle, C.byref(pending), C.byref(count), True)
                    if waited == 258:  # WAIT_TIMEOUT
                        raise TimeoutError('named-pipe I/O timed out')
                    raise C.WinError(C.get_last_error())
                if not kernel.GetOverlappedResult(self.handle, C.byref(pending), C.byref(count), False):
                    error = C.get_last_error()
                    if function is kernel.ReadFile and error in (109, 232):
                        return 0
                    raise C.WinError(error)
            return count.value
        finally:
            kernel.CloseHandle(event)

    def sendall(self, data):
        data = bytes(data)
        buffer = C.create_string_buffer(data)
        offset = 0
        while offset < len(data):
            written = self.transfer(kernel.WriteFile, C.byref(buffer, offset), len(data) - offset)
            if not written:
                raise BrokenPipeError('pipe write made no progress')
            offset += written

    def makefile(self, mode):
        return io.BufferedReader(PipeReader(self))

    def close(self):
        if self.handle is not None:
            kernel.CloseHandle(self.handle)
            self.handle = None
