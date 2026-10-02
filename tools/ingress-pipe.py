"""Windows byte-pipe adapter for http.client; verifies ADR 0011 server identity."""
import ctypes as C
from ctypes import wintypes as W
import io
import msvcrt
import os
import time

kernel = C.WinDLL('kernel32', use_last_error=True)
security = C.WinDLL('advapi32', use_last_error=True)
# Explicit signatures prevent truncation of 64-bit handles.
for function, args, result in [
    (kernel.CreateFileW, [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
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


class PipeSocket:
    def __init__(self, name, timeout):
        deadline = time.monotonic() + timeout
        while True:
            # SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION: server cannot
            # impersonate this client to act on its behalf.
            handle = kernel.CreateFileW(name, 0xC0000000, 0, None, 3, 0x110000, None)
            if handle != W.HANDLE(-1).value:
                break
            error = C.get_last_error()
            if error not in (2, 231) or time.monotonic() >= deadline:
                raise C.WinError(error)
            time.sleep(.01)
        try:
            pid = W.ULONG()
            checked(kernel.GetNamedPipeServerProcessId(handle, C.byref(pid)))
            process = checked(kernel.OpenProcess(0x1000, False, pid.value))
            try:
                remote_buffer, remote_sid = owner(process)
                local_buffer, local_sid = owner(kernel.GetCurrentProcess())
                if not security.EqualSid(remote_sid, local_sid):
                    raise PermissionError('named-pipe server belongs to another user')
            finally:
                kernel.CloseHandle(process)
            fd = msvcrt.open_osfhandle(handle, os.O_RDWR | os.O_BINARY)
        except BaseException:
            kernel.CloseHandle(handle)
            raise
        self.file = os.fdopen(fd, 'r+b', buffering=0)

    def sendall(self, data):
        remaining = memoryview(data)
        while remaining:
            written = self.file.write(remaining)
            if not written:
                raise BrokenPipeError('pipe write made no progress')
            remaining = remaining[written:]

    def makefile(self, mode):
        return io.BufferedReader(os.fdopen(os.dup(self.file.fileno()), 'rb', buffering=0))

    def close(self):
        self.file.close()
