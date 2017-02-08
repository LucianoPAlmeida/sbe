# IBM_PROLOG_BEGIN_TAG
# This is an automatically generated prolog.
#
# $Source: src/test/testcases/testMemUtil.py $
#
# OpenPOWER sbe Project
#
# Contributors Listed Below - COPYRIGHT 2017
#
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied. See the License for the specific language governing
# permissions and limitations under the License.
#
# IBM_PROLOG_END_TAG
import sys
import os
import struct
sys.path.append("targets/p9_nimbus/sbeTest" )
import testUtil
err = False

def gethalfword(dataInInt):
    hex_string = '0'*(4-len(str(hex(dataInInt))[2:])) + str(hex(dataInInt))[2:]
    return list(struct.unpack('<BB',hex_string.decode('hex')))
def getsingleword(dataInInt):
    hex_string = '0'*(8-len(str(hex(dataInInt))[2:])) + str(hex(dataInInt))[2:]
    return list(struct.unpack('<BBBB',hex_string.decode('hex')))
def getdoubleword(dataInInt):
    hex_string = '0'*(16-len(str(hex(dataInInt))[:18][2:])) + str(hex(dataInInt))[:18][2:]
    return list(struct.unpack('<BBBBBBBB',hex_string.decode('hex')))

def addItagEcc(arr, itag, ecc, eccVal=0):
    arrs = []
    while len(arr) > 8:
        pice = arr[:8]
        arrs += pice
        if(itag):
            arrs += [1]
        if(ecc):
            arrs += [eccVal]
        arr   = arr[8:]
    arrs += arr
    if(itag):
        arrs += [1]
    if(ecc):
        arrs += [eccVal]
    return arrs

def putmem(addr, data, flags, ecc=0):
    totalLen = 5 + len(data)/4
    req = (getsingleword(totalLen)
          +[ 0,0,0xA4,0x02]
          +[0, ecc]
          +gethalfword(flags)
          #0,0,0x0,0xA5] #CoreChipletId/EccByte/Flags -> NoEccOverride/CacheInhibit/FastMode/NoTag/NoEcc/AutoIncr/Adu/Proc
          + getdoubleword(addr)
          + getsingleword(len(data))  # length of data
          + data)
    testUtil.writeUsFifo(req)
    testUtil.writeEot( )
    testUtil.runCycles( 10000000 )
    lenWritten = len(data)
    if(flags & 0x0008):
        lenWritten += int(len(data)/8)
    if(flags & 0x0010):
        lenWritten += int(len(data)/8)
    expData = (getsingleword(lenWritten)
               +[0xc0,0xde,0xa4,0x02,
                 0x0,0x0,0x0,0x0,
                 0x00,0x0,0x0,0x03])
    testUtil.readDsFifo(expData)
    testUtil.readEot( )

def getmem(addr, len, flags):
    req = (getsingleword(6)
         + [0, 0, 0xA4, 0x01]
         + getsingleword(flags)
            #[0,0,0x0,0xA5]
         + getdoubleword(addr)
         + getsingleword(len))
    testUtil.writeUsFifo(req)
    testUtil.writeEot( )

    # read data
    data = []
    lenExp = len
    if(flags & 0x0008):
        lenExp += int(len/8)
    if(flags & 0x0010):
        lenExp += int(len/8)
    for i in range(0, int(-(-float(lenExp)//4))):
        data += list(testUtil.readDsEntryReturnVal())

    readLen = testUtil.readDsEntryReturnVal()
    if(getsingleword(lenExp) != list(readLen)):
        print getsingleword(lenExp)
        print list(readLen)
        raise Exception("Invalid Length")

    expResp =  [0xc0,0xde,0xa4,0x01,
                0x0,0x0,0x0,0x0,
                0x00,0x0,0x0,0x03];
    testUtil.readDsFifo(expResp)
    testUtil.readEot( )
    return data[:lenExp]
