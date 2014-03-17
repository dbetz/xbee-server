''*************************************************
''* Xbee EEPROM Loader                            *
''*  Copyright (c) 2014 David Betz                *
''* Portions adapted from code in sdspiFemto.spin *
''*  Copyright (c) 2009 Michael Green             *
''* Released under the MIT License                *
''*************************************************

CON

  ' program preamble
  PREAMBLE0 = $0000
  PREAMBLE1 = $0004
  PREAMBLE2 = $0008
  PREAMBLE3 = $000c

  ' spin program preamble offsets
  clkfreqVal = $0000                                ' Current CLKFREQ value stored here
  clksetVal  = $0004                                ' Current CLKSET value stored here
  
  ' maximum eeprom write
  MAX_EEPROM_XFER = 128
  
  ' i2c function codes
  I2C_EEPROM_ADDR = $a0
  I2C_READ = 1
  I2C_WRITE = 0

DAT

'
'
' Entry
'
entry                   jmp     #entry1

i2cDataSet              long    0                   ' Minumum data setup time (ticks)
i2cClkLow               long    0                   ' Minimum clock low time (ticks)
i2cClkHigh              long    0                   ' Minimum clock high time (ticks)
eeprom_addr             long    0                   ' EEPROM address
hub_addr                long    0                   ' Hub address
count                   long    0                   ' Byte count

entry1                  ' from sdspiFemto.spin
                        rdlong  SaveClkFreq,#clkfreqVal ' Save clock frequency and mode
                        rdbyte  SaveClkMode,#clksetVal
                        
                        call    #i2cReset
                        
                        ' read up to 128 bytes at a time
:outer                  mov     t1, count wz
                if_z    jmp     #:done
                        cmp     t1, #MAX_EEPROM_XFER wz, wc
                if_a    mov     t1, #MAX_EEPROM_XFER
                
                        ' setup the read address
                        call    #i2cStart
                        mov     i2cData, #I2C_EEPROM_ADDR
                        or      i2cData, #I2C_WRITE
                        call    #i2cWrite
                        mov     i2cData, eeprom_addr
                        shr     i2cData, #8
                        call    #i2cWrite
                        mov     i2cData, eeprom_addr
                        call    #i2cWrite
                        
                        ' read a block from EEPROM
                        call    #i2cStart
                        mov     i2cData, #I2C_EEPROM_ADDR
                        or      i2cData, #I2C_READ
                        call    #i2cWrite
:inner                  cmp     count,#2 wc         ' Carry true if this is the last byte
                        call    #i2cRead
                        wrbyte  i2cData, hub_addr
                        add     eeprom_addr, #1
                        add     hub_addr, #1
                        sub     count, #1
                        djnz    t1, #:inner
                        call    #i2cStop
                        
                        jmp     #:outer
:done
                        
'' Adapted from code in sdspiFemto.spin
'' After reading is finished for a boot, the stack marker is added below dbase
'' and memory is cleared between that and vbase (the end of the loaded program).
'' Memory beyond the stack marker is not cleared.  Note that if ioNoStore is set,
'' we go through the motions, but don't actually change memory or the clock.

                        rdlong  t1,#PREAMBLE2
                        shr     t1,#16             ' Get dbase value
                        sub     t1,#4
                        wrlong  StackMark,t1       ' Place stack marker at dbase
                        sub     t1,#4
                        wrlong  StackMark,t1
                        rdlong  t2,#PREAMBLE2      ' Get vbase value
                        and     t2,WordMask
                        sub     t1,t2
                        shr     t1,#2         wz   ' Compute number of longs between
:zeroIt         if_nz   wrlong  zero,t2            '  vbase and below stack marker
                if_nz   add     t2,#4
                if_nz   djnz    t1,#:zeroIt        ' Zero that space (if any)
                        rdlong  t1,#PREAMBLE0
                        cmp     t1,SaveClkFreq wz  ' Is the clock frequency the same?
                        rdlong  t1,#PREAMBLE1
                        and     t1,#$FF            ' Is the clock mode the same also?
                if_ne   jmp     #:changeClock
                        cmp     t1,SaveClkMode wz  ' If both same, just go start COG
                if_e    jmp     #:justStartUp
:changeClock            and     t1,#$F8            ' Force use of RCFAST clock while
                        clkset  t1                 '  letting requested clock start
                        mov     t1,time_xtal
:startupDelay           djnz    t1,#:startupDelay  ' Allow 20ms@20MHz for xtal/pll to settle
                        rdlong  t1,#PREAMBLE1
                        and     t1,#$FF            ' Then switch to selected clock
                        clkset  t1
:justStartUp            cogid   t1
                        or      t1, interpreter
                        coginit t1

debug                   or      outa, debug_leds
                        or      dira, debug_leds
                        jmp     #$
debug_leds              long    (1 << 26) | (1 << 27)

'' Low level I2C routines.  These are designed to work either with a standard I2C bus
'' (with pullups on both SCL and SDA) or the Propellor Demo Board (with a pullup only
'' on SDA).  Timing can be set by the caller to 100KHz or 400KHz.

CON
  i2cBootSCL  = 28                             ' Boot EEPROM SCL pin
  i2cBootSDA  = 29                             ' Boot EEPROM SDA pin

DAT

'' Do I2C Reset Sequence.  Clock up to 9 cycles.  Look for SDA high while SCL
'' is high.  Device should respond to next Start Sequence.  Leave SCL high.

i2cReset                andn    dira,i2cSDA             ' Pullup drive SDA high
                        mov     i2cBitCnt,#9            ' Number of clock cycles
                        mov     i2cTime,i2cClkLow
                        add     i2cTime,cnt             ' Allow for minimum SCL low
:i2cResetClk            andn    outa,i2cSCL             ' Active drive SCL low
                        or      dira,i2cSCL            
                        waitcnt i2cTime,i2cClkHigh
                        test    i2cBootSCLm,i2cSCL wz   ' Check for boot I2C bus
              if_nz     or      outa,i2cSCL             ' Active drive SCL high
              if_nz     or      dira,i2cSCL
              if_z      andn    dira,i2cSCL             ' Pullup drive SCL high
                        waitcnt i2cTime,i2cClkLow       ' Allow minimum SCL high
                        test    i2cSDA,ina         wz   ' Stop if SDA is high
              if_z      djnz    i2cBitCnt,#:i2cResetClk ' Stop after 9 cycles
i2cReset_ret            ret                             ' Should be ready for Start      

'' Do I2C Start Sequence.  This assumes that SDA is a floating input and
'' SCL is also floating, but may have to be actively driven high and low.
'' The start sequence is where SDA goes from HIGH to LOW while SCL is HIGH.

i2cStart                andn    dira,i2cSDA             ' Pullup drive SDA high
                        andn    outa,i2cSDA             ' SDA set to drive low
                        mov     i2cTime,i2cClkLow
                        add     i2cTime,cnt             ' Allow for bus free time
                        waitcnt i2cTime,i2cClkHigh
                        test    i2cBootSCLm,i2cSCL wz   ' Check for boot I2C bus
              if_nz     or      outa,i2cSCL             ' Active drive SCL high
              if_nz     or      dira,i2cSCL
              if_z      andn    dira,i2cSCL             ' Pullup drive SCL high
                        waitcnt i2cTime,i2cClkHigh      ' Allow for start setup time
                        or      dira,i2cSDA             ' Active drive SDA low
                        waitcnt i2cTime,#0              ' Allow for start hold time
                        andn    outa,i2cSCL             ' Active drive SCL low
                        or      dira,i2cSCL
i2cStart_ret            ret                             

'' Do I2C Stop Sequence.  This assumes that SCL is low and SDA is indeterminant.
'' The stop sequence is where SDA goes from LOW to HIGH while SCL is HIGH.
'' i2cStart must have been called prior to calling this routine for initialization.
'' The state of the (c) flag is maintained so a write error can be reported.

i2cStop                 or      dira,i2cSDA             ' Active drive SDA low
                        mov     i2cTime,i2cClkLow
                        add     i2cTime,cnt             ' Wait for minimum clock low
                        waitcnt i2cTime,i2cClkLow
                        test    i2cBootSCLm,i2cSCL wz   ' Check for boot I2C bus
              if_nz     or      outa,i2cSCL             ' Active drive SCL high
              if_nz     or      dira,i2cSCL
              if_z      andn    dira,i2cSCL             ' Pullup drive SCL high
                        waitcnt i2cTime,i2cClkHigh      ' Wait for minimum setup time
                        andn    dira,i2cSDA             ' Pullup drive SDA high
                        waitcnt i2cTime,#0              ' Allow for bus free time
                        andn    dira,i2cSCL             ' Leave SCL and SDA high
i2cStop_ret             ret

'' Write I2C data.  This assumes that i2cStart has been called and that SCL is low,
'' SDA is indeterminant. The (c) flag will be set on exit from ACK/NAK with ACK == false
'' and NAK == true. Bytes are handled in "little-endian" order so these routines can be
'' used with words or longs although the bits are in msb..lsb order.

i2cWrite                mov     i2cBitCnt,#8
                        mov     i2cMask,#%10000000
                        mov     i2cTime,i2cClkLow
                        add     i2cTime,cnt             ' Wait for minimum SCL low
:i2cWriteBit            waitcnt i2cTime,i2cDataSet
                        test    i2cData,i2cMask    wz
              if_z      or      dira,i2cSDA             ' Copy data bit to SDA
              if_nz     andn    dira,i2cSDA
                        waitcnt i2cTime,i2cClkHigh      ' Wait for minimum setup time
                        test    i2cBootSCLm,i2cSCL wz   ' Check for boot I2C bus
              if_nz     or      outa,i2cSCL             ' Active drive SCL high
              if_nz     or      dira,i2cSCL
              if_z      andn    dira,i2cSCL             ' Pullup drive SCL high
                        waitcnt i2cTime,i2cClkLow
                        andn    outa,i2cSCL             ' Active drive SCL low
                        or      dira,i2cSCL
                        ror     i2cMask,#1              ' Go do next bit if not done
                        djnz    i2cBitCnt,#:i2cWriteBit
                        andn    dira,i2cSDA             ' Switch SDA to input and
                        waitcnt i2cTime,i2cClkHigh      '  wait for minimum SCL low
                        test    i2cBootSCLm,i2cSCL wz   ' Check for boot I2C bus
              if_nz     or      outa,i2cSCL             ' Active drive SCL high
              if_nz     or      dira,i2cSCL
              if_z      andn    dira,i2cSCL             ' Pullup drive SCL high
                        waitcnt i2cTime,#0              ' Wait for minimum high time
                        test    i2cSDA,ina         wc   ' Sample SDA (ACK/NAK) then
                        andn    outa,i2cSCL             '  active drive SCL low
                        or      dira,i2cSCL
                        or      dira,i2cSDA             ' Leave SDA low
                        rol     i2cMask,#16             ' Prepare for multibyte write
i2cWrite_ret            ret

'' Read I2C data.  This assumes that i2cStart has been called and that SCL is low,
'' SDA is indeterminant.  ACK/NAK will be copied from the (c) flag on entry with
'' ACK == low and NAK == high.  Bytes are handled in "little-endian" order so these
'' routines can be used with words or longs although the bits are in msb..lsb order.

i2cRead                 mov     i2cBitCnt,#8
                        mov     i2cMask,#%10000000
                        andn    dira,i2cSDA             ' Make sure SDA is set to input
                        mov     i2cTime,i2cClkLow
                        add     i2cTime,cnt             ' Wait for minimum SCL low
:i2cReadBit             waitcnt i2cTime,i2cClkHigh
                        test    i2cBootSCLm,i2cSCL wz   ' Check for boot I2C bus
              if_nz     or      outa,i2cSCL             ' Active drive SCL high
              if_nz     or      dira,i2cSCL
              if_z      andn    dira,i2cSCL             ' Pullup drive SCL high
                        waitcnt i2cTime,i2cClkLow       ' Wait for minimum clock high
                        test    i2cSDA,ina         wz   ' Sample SDA for data bits
                        andn    outa,i2cSCL             ' Active drive SCL low
                        or      dira,i2cSCL
              if_nz     or      i2cData,i2cMask         ' Accumulate data bits
              if_z      andn    i2cData,i2cMask
                        ror     i2cMask,#1              ' Shift the bit mask and
                        djnz    i2cBitCnt,#:i2cReadBit  '  continue until done
                        waitcnt i2cTime,i2cDataSet      ' Wait for end of SCL low
              if_c      andn    dira,i2cSDA             ' Copy the ACK/NAK bit to SDA
              if_nc     or      dira,i2cSDA
                        waitcnt i2cTime,i2cClkHigh      ' Wait for minimum setup time
                        test    i2cBootSCLm,i2cSCL wz   ' Check for boot I2C bus
              if_nz     or      outa,i2cSCL             ' Active drive SCL high
              if_nz     or      dira,i2cSCL
              if_z      andn    dira,i2cSCL             ' Pullup drive SCL high
                        waitcnt i2cTime,#0              ' Wait for minimum clock high
                        andn    outa,i2cSCL             ' Active drive SCL low
                        or      dira,i2cSCL
                        or      dira,i2cSDA             ' Leave SDA low
                        rol     i2cMask,#16             ' Prepare for multibyte read
i2cRead_ret             ret

i2cSCL                  long    |<i2cBootSCL            ' Bit mask for SCL
i2cSDA                  long    |<i2cBootSDA            ' Bit mask for SDA
i2cBootSCLm             long    |<i2cBootSCL            ' Bit mask for pin 28 SCL use
i2cTime                 long    0                       ' Used for timekeeping
i2cData                 long    0                       ' Data to be transmitted / received
i2cMask                 long    0                       ' Bit mask for bit to be tx / rx
i2cBitCnt               long    0                       ' Number of bits to tx / rx

'
' Initialized data
'

zero                    long    0

' taken from sdspiFemto.spin
StackMark               long    $FFF9FFFF               ' Two of these mark the base of the stack
interpreter             long    ($0004 << 16) | ($F004 << 2) | %0000
WordMask                long    $0000FFFF
time_xtal               long    20 * 20000 / 4 / 1      ' 20ms (@20MHz, 1 inst/loop)
SaveClkFreq             long    0                       ' Initial clock frequency (clkfreqVal)
SaveClkMode             long    0                       ' Initial clock mode value (clksetVal)

'
' Uninitialized data
'
t1                      res     1
t2                      res     1

                        fit     496
