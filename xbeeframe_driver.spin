''*********************************************
''* Xbee Frame Driver                         *
''*  (C) 2014 David Betz                      *
''* Based on:                                 *
''*  Full-Duplex Serial Driver v1.1 Extended  *
''*   (C) 2006 Parallax, Inc.                 *
''* Released under the MIT License            *
''*********************************************

'#define FIFO_DEBUG
'#define RTS_DEBUG

{
  init structure:
        long mailbox       '0: mailbox address
        long rx_pin        '1: receive pin
        long tx_pin        '2: transmit pin
        long rts_pin       '3: rts pin
        long bit_ticks     '4: baud rate
        long rxlength      '5: size of receive frame data buffer
        long buffers       '6: rxlength*2 size buffer

  mailbox structure:
        long rxframe       '0: rx frame data buffer
        long rxlength      '1: rx frame data length
        long rxstatus      '2: rx status
        long txframe       '3: tx frame data buffer
        long txlength      '4: tx frame data length
        long txstatus      '5: tx status
        long ticks         '6: number of ticks per bit
        long cog           '7: cog running driver
}

CON

  ' status codes
  #0
  STATUS_IDLE   ' must be zero
  STATUS_BUSY
  
  ' character codes
  START = $7e   ' start of a frame

  #0
  STATE_START
  STATE_LEN_HI
  STATE_LEN_LO
  STATE_DATA
  STATE_CHKSUM
  STATE_WAIT

PUB dummy

DAT

'***********************************
'* Assembly language serial driver *
'***********************************

                        org
'
'
' Entry
'
entry                   mov     t1, par              'get init structure address

                        rdlong  t2, t1               'get the mailbox address
                        mov     rx_frame_ptr, t2
                        add     t2, #4
                        mov     rx_length_ptr, t2
                        add     t2, #4
                        mov     rx_status_ptr, t2
                        add     t2, #4
                        mov     tx_frame_ptr, t2
                        add     t2, #4
                        mov     tx_length_ptr, t2
                        add     t2, #4
                        mov     tx_status_ptr, t2

                        add     t1, #4                'get rx_pin
                        rdlong  t2, t1
                        mov     rxmask, #1
                        shl     rxmask, t2

                        add     t1, #4                'get tx_pin
                        rdlong  t2, t1
                        mov     txmask, #1
                        shl     txmask, t2

                        add     t1, #4                'get rts_pin
                        rdlong  t2, t1
                        mov     rtsmask, #1
                        shl     rtsmask, t2

                        add     t1, #4                'get bit_ticks
                        rdlong  bitticks, t1

                        add     t1, #4                'get rxsize
                        rdlong  rcv_max, t1
                        
                        mov     rxtxmode, #0          'force the rxtxmode to zero

                        add     t1, #4                'get buffer address
                        rdlong  rcv_buf1, t1
                        mov     rcv_buf2, rcv_buf1
                        add     rcv_buf2, rcv_max
                        
                        mov     rcv_head, #0          'setup the circular receive buffer
                        mov     rcv_tail, #0
                        mov     rcv_count, #0
                        mov     rcv_mask, rcv_max
                        sub     rcv_mask, #1

                        mov     t1, #STATUS_IDLE      'no frame available yet
                        wrlong  t1, rx_status_ptr

                        mov     t1, #0                'signal end of initialization
                        wrlong  t1, par

                        test    rxtxmode,#%100  wz    'if_nz = open drain Tx
                        test    rxtxmode,#%010  wc    'if_c = inverted output
        if_z_ne_c       or      outa,txmask
        if_z            or      dira,txmask
        
                        andn    outa, rtsmask
                        or      dira, rtsmask

                        mov     rxcode,#receive       'initialize ping-pong multitasking
                        mov     txcode,#transmit

                        mov     rcv_state, #STATE_START 'initialize the frame receive state
                        mov     xmt_state, #STATE_START 'initialize the frame transmit state

'
'
' Receive
'
receive                 jmpret  rxcode,txcode         'run a chunk of transmit code, then return

                        test    rxtxmode,#%001  wz    'wait for start bit on rx pin
                        test    rxmask,ina      wc
        if_z_eq_c       jmp     #assemble

                        mov     rxbits,#9             'ready to receive byte
                        mov     rxcnt,bitticks
                        shr     rxcnt,#1              'half a bit tick
                        add     rxcnt,cnt             '+ the current clock             

:bit                    add     rxcnt,bitticks        'ready for the middle of the bit period

:wait                   jmpret  rxcode,txcode         'run a chuck of transmit code, then return

                        mov     t1,rxcnt              'check if bit receive period done
                        sub     t1,cnt
                        cmps    t1,#0           wc
        if_nc           jmp     #:wait

                        test    rxmask,ina      wc    'receive bit on rx pin into carry
                        rcr     rxdata,#1             'shift carry into receiver
                        djnz    rxbits,#:bit          'go get another bit till done

                        test    rxtxmode,#%001  wz    'find out if rx is inverted
        if_z_ne_c       jmp     #receive              'abort if no stop bit

                        shr     rxdata,#32-9          'justify and trim received byte
                        and     rxdata,#$FF
        if_nz           xor     rxdata,#$FF           'if rx inverted, invert byte
        
                        ' we have received a byte at this point
        
                        mov     t1, rcv_head
                        add     t1, rcv_buf2
                        wrbyte  rxdata, t1            'write the byte to the receive buffer
                        add     rcv_head, #1
                        and     rcv_head, rcv_mask
                        cmp     rcv_count, rcv_max wz, wc
        if_b            add     rcv_count, #1
        if_ae           add     rcv_tail, #1
        if_ae           and     rcv_tail, rcv_mask
#ifdef FIFO_DEBUG
        if_ae           or      outa, led
        if_ae           or      dira, led
#endif
        
                        cmp     rcv_count, high_water wz, wc
        if_a            or      outa, rtsmask
#ifdef RTS_DEBUG
        if_a            or      outa, led2
        if_a            or      dira, led2
#endif
        
assemble                cmp     rcv_state, #STATE_WAIT wz
        if_z            jmp     #do_rcv_wait
        
                        cmp     rcv_count, #0 wz
        if_z            jmp     #receive
        
                        mov     t1, rcv_tail
                        add     t1, rcv_buf2
                        rdbyte  rxdata, t1
                        add     rcv_tail, #1
                        and     rcv_tail, rcv_mask
                        sub     rcv_count, #1
                                
                        cmp     rcv_count, low_water wz, wc
        if_b            andn    outa, rtsmask

                        mov     t1, rcv_state
                        add     t1, #rcv_dispatch
                        jmp     t1
                        
#ifdef FIFO_DEBUG
led                     long    1 << 26
#endif

#ifdef RTS_DEBUG
led2                    long    1 << 27
#endif

high_water              long    1000
low_water               long    100

rcv_dispatch            jmp     #do_rcv_start
                        jmp     #do_rcv_len_hi
                        jmp     #do_rcv_len_lo
                        jmp     #do_rcv_data
                        jmp     #do_rcv_chksum
                        jmp     #do_rcv_wait

do_rcv_start            cmp     rxdata, #START wz
              if_z      mov     rcv_state, #STATE_LEN_HI
                        jmp     #receive              'byte done, receive next byte

do_rcv_len_hi           mov     rcv_length, rxdata
                        shl     rcv_length, #8
                        mov     rcv_state, #STATE_LEN_LO
                        jmp     #receive              'byte done, receive next byte

do_rcv_len_lo           or      rcv_length, rxdata
                        cmp     rcv_length, rcv_max wz, wc
              if_a      jmp     #look_for_frame
                        mov     rcv_chksum, #0
                        mov     rcv_cnt, rcv_length wz
              if_z      mov     rcv_state, #STATE_CHKSUM
              if_nz     mov     rcv_state, #STATE_DATA
                        mov     rcv_ptr, rcv_buf1
                        jmp     #receive              'byte done, receive next byte

do_rcv_data             add     rcv_chksum, rxdata    'update the checksum
                        wrbyte  rxdata, rcv_ptr
                        add     rcv_ptr, #1
                        djnz    rcv_cnt, #receive
                        mov     rcv_state, #STATE_CHKSUM
                        jmp     #receive              'byte done, receive next byte

do_rcv_chksum           add     rcv_chksum, rxdata    'update the checksum
                        and     rcv_chksum, #$ff
                        cmp     rcv_chksum, #$ff wz   'check the checksum
              if_nz     jmp     #look_for_frame
              
                        wrlong  rcv_length, rx_length_ptr 'pass the frame to the application
                        wrlong  rcv_buf1, rx_frame_ptr
                        mov     t1, #STATUS_BUSY
                        wrlong  t1, rx_status_ptr
                        mov     rcv_state, #STATE_WAIT

do_rcv_wait             rdlong  t1, rx_status_ptr wz
              if_nz     jmp     #receive
                        
look_for_frame          mov     rcv_state, #STATE_START
                        jmp     #receive              'byte done, receive next byte

'
' Transmit
'
transmit                jmpret  txcode,rxcode         'run a chunk of mailbox code, then return

                        mov     t1, xmt_state
                        add     t1, #xmt_dispatch
                        jmp     t1

xmt_dispatch            jmp     #do_xmt_start
                        jmp     #do_xmt_len_hi
                        jmp     #do_xmt_len_lo
                        jmp     #do_xmt_data
                        jmp     #do_xmt_chksum
                        jmp     #do_xmt_wait

do_xmt_wait             mov     t1, #STATUS_IDLE
                        wrlong  t1, tx_status_ptr
                        mov     xmt_state, #STATE_START

do_xmt_start            rdlong  t1, tx_status_ptr wz
        if_z            jmp     #transmit
                        rdlong  xmt_ptr, tx_frame_ptr
                        rdlong  xmt_cnt, tx_length_ptr
                        mov     xmt_state, #STATE_LEN_HI
                        mov     txdata, #START
                        jmp     #tx_start

do_xmt_len_hi           mov     xmt_state, #STATE_LEN_LO
                        mov     txdata, xmt_cnt
                        shr     txdata, #8
                        and     txdata, #$ff
                        jmp     #tx_start

do_xmt_len_lo           mov     xmt_state, #STATE_DATA
                        mov     txdata, xmt_cnt
                        and     txdata, #$ff
                        mov     xmt_chksum, #0
                        jmp     #tx_start

do_xmt_data             tjz     xmt_cnt, #do_xmt_chksum
                        rdbyte  txdata, xmt_ptr
                        add     xmt_chksum, txdata
                        add     xmt_ptr, #1
                        sub     xmt_cnt, #1
                        jmp     #tx_start

do_xmt_chksum           mov     xmt_state, #STATE_WAIT
                        mov     txdata, #$ff
                        and     xmt_chksum, #$ff
                        sub     txdata, xmt_chksum
                        
tx_start                or      txdata,#$100          'or in a stop bit
                        shl     txdata,#2
                        or      txdata,#1             'or in a idle line state and a start bit
                        mov     txbits,#11
                        mov     txcnt,cnt

:bit                    test    rxtxmode,#%100  wz    'output bit on tx pin according to mode
                        test    rxtxmode,#%010  wc
        if_z_and_c      xor     txdata,#1
                        shr     txdata,#1       wc
        if_z            muxc    outa,txmask        
        if_nz           muxnc   dira,txmask
                        add     txcnt,bitticks        'ready next cnt

:wait                   jmpret  txcode,rxcode         'run a chunk of mailbox code, then return

                        mov     t1,txcnt              'check if bit transmit period done
                        sub     t1,cnt
                        cmps    t1,#0           wc
        if_nc           jmp     #:wait

                        djnz    txbits,#:bit          'another bit to transmit?

                        jmp     #transmit             'byte done, transmit next byte

'
'
' Initialized data
'
'
zero                    long    0
word_mask               long    $ffff

'
' Uninitialized data
'
t1                      res     1
t2                      res     1

rxtxmode                res     1
bitticks                res     1

rxmask                  res     1
rxdata                  res     1
rxbits                  res     1
rxcnt                   res     1
rxcode                  res     1

txmask                  res     1
txdata                  res     1
txbits                  res     1
txcnt                   res     1
txcode                  res     1

rtsmask                 res     1

rcv_buf1                res     1
rcv_buf2                res     1
rcv_state               res     1
rcv_length              res     1  'frame length
rcv_max                 res     1  'maximum frame data length
rcv_ptr                 res     1  'data buffer pointer
rcv_cnt                 res     1  'data buffer count
rcv_chksum              res     1

rcv_head                res     1  'receive buffer head
rcv_tail                res     1  'receive buffer tail
rcv_count               res     1  'number of bytes in the receive buffer
rcv_mask                res     1  'receive buffer mask for wrapping

xmt_state               res     1
xmt_ptr                 res     1
xmt_cnt                 res     1
xmt_chksum              res     1

rx_frame_ptr            res     1
rx_length_ptr           res     1
rx_status_ptr           res     1

tx_frame_ptr            res     1
tx_length_ptr           res     1
tx_status_ptr           res     1


                        fit     496