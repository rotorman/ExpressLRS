#!/usr/bin/python

from enum import Enum
import shutil
import os

from elrs_helpers import ElrsUploadResult
import BFinitPassthrough
import ETXinitPassthrough
import serials_find
from firmware import DeviceType, FirmwareOptions, MCUType

import sys
from os.path import dirname
sys.path.append(dirname(__file__) + '/external/esptool')

from external.esptool import esptool
sys.path.append(dirname(__file__) + "/external")

class UploadMethod(Enum):
    uart = 'uart'
    edgetx = 'etx'
    stock = 'stock'
    dir = 'dir'

    def __str__(self):
        return self.value

def upload_esp32_uart(args):
    if args.port == None:
        args.port = serials_find.get_serial_port()
    try:
        dir = os.path.dirname(args.file.name)
        cmd = ['--chip', args.platform.replace('-', ''), '--port', args.port, '--baud', str(args.baud), '--after', 'hard_reset', 'write_flash']
        if args.erase: cmd.append('--erase-all')
        start_addr = '0x0000' if args.platform.startswith('esp32-') else '0x1000'
        cmd.extend(['-z', '--flash_mode', 'dio', '--flash_freq', '40m', '--flash_size', 'detect', start_addr, os.path.join(dir, 'bootloader.bin'), '0x8000', os.path.join(dir, 'partitions.bin'), '0xe000', os.path.join(dir, 'boot_app0.bin'), '0x10000', args.file.name])
        esptool.main(cmd)
    except:
        return ElrsUploadResult.ErrorGeneral
    return ElrsUploadResult.Success

def upload_esp32_etx(args):
    if args.port == None:
        args.port = serials_find.get_serial_port()
    ETXinitPassthrough.etx_passthrough_init(args.port, args.baud)
    try:
        dir = os.path.dirname(args.file.name)
        cmd = ['--chip', args.platform.replace('-', ''), '--port', args.port, '--baud', str(args.baud), '--before', 'no_reset', '--after', 'hard_reset', 'write_flash']
        if args.erase: cmd.append('--erase-all')
        start_addr = '0x0000' if args.platform.startswith('esp32-') else '0x1000'
        cmd.extend(['-z', '--flash_mode', 'dio', '--flash_freq', '40m', '--flash_size', 'detect', start_addr, os.path.join(dir, 'bootloader.bin'), '0x8000', os.path.join(dir, 'partitions.bin'), '0xe000', os.path.join(dir, 'boot_app0.bin'), '0x10000', args.file.name])
        esptool.main(cmd)
    except:
        return ElrsUploadResult.ErrorGeneral
    return ElrsUploadResult.Success

def upload_dir(mcuType, args):
    if mcuType == MCUType.ESP32:
        shutil.copy2(args.file.name, args.out)

def upload(options: FirmwareOptions, args):
    if args.baud == 0:
        args.baud = 460800

    if args.flash == UploadMethod.dir or args.flash == UploadMethod.stock:
        return upload_dir(options.mcuType, args)
    else:
        if options.mcuType == MCUType.ESP32:
            if args.flash == UploadMethod.edgetx:
                return upload_esp32_etx(args)
            elif args.flash == UploadMethod.uart:
                return upload_esp32_uart(args)
    print("Invalid upload method for firmware")
    return ElrsUploadResult.ErrorGeneral
