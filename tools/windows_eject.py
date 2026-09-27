#!/usr/bin/env python3
"""Windows safe eject and bootloader transition; invoked by Return-ToArduPilot.ps1."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import re
import subprocess

from readerctl import identify, exchange, await_bootloader


def verify_volume(port_name, drive):
    # Validate before interpolating into the fixed PowerShell inventory query.
    if not re.fullmatch(r'COM[0-9]+', port_name) or not re.fullmatch(r'[A-Z]:', drive):
        raise ValueError('Expected explicit COM number and drive letter')
    script = """
$ErrorActionPreference='Stop'
$s=@(Get-CimInstance Win32_SerialPort | Where-Object DeviceID -eq '%s')
$v=Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='%s'"
if ($s.Count -ne 1 -or !$v) { throw 'Port or volume not found' }
$p=@(Get-CimAssociatedInstance -InputObject $v -Association Win32_LogicalDiskToPartition)
if ($p.Count -ne 1) { throw 'Ambiguous partition' }
$d=@(Get-CimAssociatedInstance -InputObject $p[0] -Association Win32_DiskDriveToDiskPartition)
if ($d.Count -ne 1 -or $d[0].InterfaceType -ne 'USB') { throw 'Expected USB disk' }
$all=@(Get-CimAssociatedInstance -InputObject $d[0] -Association Win32_DiskDriveToDiskPartition)
if ($all.Count -ne 1) { throw 'Multiple partitions unsupported' }
$a=(Get-PnpDeviceProperty -InstanceId $s[0].PNPDeviceID -KeyName DEVPKEY_Device_ContainerId).Data
$b=(Get-PnpDeviceProperty -InstanceId $d[0].PNPDeviceID -KeyName DEVPKEY_Device_ContainerId).Data
if (!$a -or $a -eq [guid]::Empty -or $a -ne $b) { throw 'Disk/port identity mismatch' }
@{container="$a";disk=$d[0].PNPDeviceID} | ConvertTo-Json -Compress
""" % (port_name, drive)
    result = subprocess.run(['powershell.exe', '-NoProfile', '-Command', script],
                            check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def eject_and_reboot(port_name, drive):
    import serial
    with serial.Serial(port_name, 115200, timeout=2, write_timeout=2) as port:
        identity = identify(port)
        device = verify_volume(port_name, drive)
        if (Path(drive + '/') / 'ardupilot.abin').exists():
            raise RuntimeError('SD auto-flash image present; move it before bootloader entry')
        print('Verified', identity, device, flush=True)
        k = c.WinDLL('kernel32', use_last_error=True)
        k.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, c.c_void_p, w.DWORD, w.DWORD, w.HANDLE]
        k.CreateFileW.restype = w.HANDLE
        k.DeviceIoControl.argtypes = [w.HANDLE, w.DWORD, c.c_void_p, w.DWORD, c.c_void_p, w.DWORD, c.POINTER(w.DWORD), c.c_void_p]
        k.CloseHandle.argtypes = [w.HANDLE]
        k.FlushFileBuffers.argtypes = [w.HANDLE]

        class SPT(c.Structure):
            _fields_ = [('Length', w.WORD), ('ScsiStatus', c.c_ubyte),
                        ('PathId', c.c_ubyte), ('TargetId', c.c_ubyte), ('Lun', c.c_ubyte),
                        ('CdbLength', c.c_ubyte), ('SenseInfoLength', c.c_ubyte), ('DataIn', c.c_ubyte),
                        ('DataTransferLength', w.ULONG), ('TimeOutValue', w.ULONG),
                        ('DataBufferOffset', c.c_size_t), ('SenseInfoOffset', w.ULONG), ('Cdb', c.c_ubyte * 16)]

        handle = k.CreateFileW('\\\\.\\' + drive, 0xc0000000, 3, None, 3, 0, None)
        if handle == w.HANDLE(-1).value:
            raise c.WinError(c.get_last_error())
        try:
            returned = w.DWORD()
            def control(code):
                if not k.DeviceIoControl(handle, code, None, 0, None, 0, c.byref(returned), None):
                    raise c.WinError(c.get_last_error())
            control(0x90018)  # Lock: fails if other applications own files.
            if not k.FlushFileBuffers(handle):
                raise c.WinError(c.get_last_error())
            control(0x90020)  # Dismount while retaining exclusive volume ownership.
            # Explicit allow-removal, cache sync, logical eject. Windows EJECT_MEDIA
            # caused a USB reset/reload in the tested configuration.
            for command in ([0x1e, 0, 0, 0, 0, 0], [0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0], [0x1b, 0, 0, 0, 2, 0]):
                buffer = c.create_string_buffer(c.sizeof(SPT) + 32)
                spt = SPT.from_buffer(buffer)
                spt.Length = c.sizeof(SPT)
                spt.CdbLength = len(command)
                spt.SenseInfoLength = 32
                spt.DataIn = 2  # No data phase.
                spt.TimeOutValue = 5
                spt.SenseInfoOffset = c.sizeof(SPT)
                for index, byte in enumerate(command):
                    spt.Cdb[index] = byte
                if not k.DeviceIoControl(handle, 0x4d004, buffer, len(buffer), buffer, len(buffer), c.byref(returned), None):
                    raise c.WinError(c.get_last_error())
                if spt.ScsiStatus:
                    raise RuntimeError('SCSI eject/sync failed: ' + bytes(buffer)[c.sizeof(SPT):].hex())
            response = exchange(port, 'bootloader')
            print(response, flush=True)
            if not response.startswith('OK '):
                raise RuntimeError('Reader refused reset: ' + response)
        finally:
            k.CloseHandle(handle)
    print('Bootloader confirmed on ' + await_bootloader(identity['serial']), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--drive', required=True)
    args = parser.parse_args()
    eject_and_reboot(args.port.upper(), args.drive.upper())


if __name__ == '__main__':
    main()
