#   CDROM Drive
#### Playstation CDROM I/O Ports
[CDROM Drive Overview](cdromdrive-overview.md)<br/>
[CDROM Controller I/O Ports](#cdrom-controller-io-ports)<br/>

#### Playstation CDROM Commands
[CDROM Controller Command Summary](#cdrom-controller-command-summary)<br/>
[CDROM - Control Commands](#cdrom-control-commands)<br/>
[CDROM - Seek Commands](#cdrom-seek-commands)<br/>
[CDROM - Read Commands](#cdrom-read-commands)<br/>
[CDROM - Status Commands](#cdrom-status-commands)<br/>
[CDROM - CD Audio Commands](#cdrom-cd-audio-commands)<br/>
[CDROM - Test Commands](#cdrom-test-commands)<br/>
[CDROM - Secret Unlock Commands](#cdrom-secret-unlock-commands)<br/>
[CDROM - Video CD Commands](#cdrom-video-cd-commands)<br/>
[CDROM - Mainloop/Responses](#cdrom-mainloopresponses)<br/>
[CDROM - Response Timings](#cdrom-response-timings)<br/>
[CDROM - Response/Data Queueing](#cdrom-responsedata-queueing)<br/>

#### General CDROM Disk Format
[CDROM Format](cdromformat.md)<br/>
[CDROM File Formats](cdromfileformats/index.md)<br/>
[CDROM Video CDs (VCD)](cdromvideocdsvcd.md)<br/>

#### Playstation CDROM Coprocessor
[CDROM Internal Info on PSX CDROM Controller](cdrominternalinfoonpsxcdromcontroller.md)<br/>



##   CDROM Controller I/O Ports
The CD-ROM drive is made up of several chips. The CPU only has direct access to
the sector buffer/decoder chip's "host" interface, which provides mailboxes to
communicate with the drive's microcontroller, a data port for reading sectors
and audio configuration registers. The interface is bank switched and consists
of four banks of four 8-bit registers each.

The following registers are available when reading:

| Bank | `0x1f801800`                              | `0x1f801801`                                  | `0x1f801802`                                  | `0x1f801803`                                        |
| ---: | :---------------------------------------- | :-------------------------------------------- | :-------------------------------------------- | :-------------------------------------------------- |
| 0, 2 | [`HSTS`](#0x1f801800-read-all-banks-hsts) | [`RESULT`](#0x1f801801-read-all-banks-result) | [`RDDATA`](#0x1f801802-read-all-banks-rddata) | [`HINTMSK`](#0x1f801803-read-banks-0-and-2-hintmsk) |
| 1, 3 | [`HSTS`](#0x1f801800-read-all-banks-hsts) | [`RESULT`](#0x1f801801-read-all-banks-result) | [`RDDATA`](#0x1f801802-read-all-banks-rddata) | [`HINTSTS`](#0x1f801803-read-banks-1-and-3-hintsts) |

The following registers are available when writing:

| Bank | `0x1f801800`                                     | `0x1f801801`                                       | `0x1f801802`                                       | `0x1f801803`                                       |
| ---: | :----------------------------------------------- | :------------------------------------------------- | :------------------------------------------------- | :------------------------------------------------- |
|    0 | [`ADDRESS`](#0x1f801800-write-all-banks-address) | [`COMMAND`](#0x1f801801-write-bank-0-command)      | [`PARAMETER`](#0x1f801802-write-bank-0-parameter)  | [`HCHPCTL`](#0x1f801803-write-bank-0-hchpctl)      |
|    1 | [`ADDRESS`](#0x1f801800-write-all-banks-address) | [`WRDATA`](#0x1f801801-write-bank-1-wrdata)        | [`HINTMSK`](#0x1f801802-write-bank-1-hintmsk)      | [`HCLRCTL`](#0x1f801803-write-bank-1-hclrctl)      |
|    2 | [`ADDRESS`](#0x1f801800-write-all-banks-address) | [`CI`](#0x1f801801-write-bank-2-ci)                | [`ATV0`](#0x1f801802-write-bank-2-atv0-l-l-volume) | [`ATV1`](#0x1f801803-write-bank-2-atv1-l-r-volume) |
|    3 | [`ADDRESS`](#0x1f801800-write-all-banks-address) | [`ATV2`](#0x1f801801-write-bank-3-atv2-r-r-volume) | [`ATV3`](#0x1f801802-write-bank-3-atv3-r-l-volume) | [`ADPCTL`](#0x1f801803-write-bank-3-adpctl)        |

Official documentation for these registers is available in the "host interface"
section of the CXD1199 decoder's datasheet. Later console revisions use
different decoders, however they all seem to be variants of the CXD1199 (just
merged with other CD-ROM chips, and possibly trimmed down by removing unused
features such as the sound map functionality).

#### Register ownership
Every register is in the decoder, but they are used by different parties:<br/>
```
  Register          Used by
  HSTS.BUSYSTS      set by the decoder on a COMMAND write, cleared by the HC05
  HSTS.RSLRRDY      result FIFO state; the HC05 fills the FIFO
  HSTS (others)     decoder: parameter FIFO, data transfer, ADPCM state
  ADDRESS           decoder only (register bank)
  COMMAND           read by the HC05 on its next pass
  PARAMETER         read by the HC05 when it takes the command
  RESULT            written by the HC05
  HINTSTS bits 0-2  written by the HC05, which also reads them back to see
                    whether the CPU has cleared them
  HINTSTS bits 3-4  decoder only (sound map)
  HINTMSK           decoder only, gates IRQ2 (the HC05 cannot read it)
  HCLRCTL           CLRINT clears the bits written by the HC05; CHPRST resets
                    the decoder and flags the reset to the HC05; the other
                    bits are decoder only
  HCHPCTL           decoder only (sector buffer transfers, sound map)
  RDDATA, WRDATA    decoder only
  CI                decoder only (sound map)
  ATV0..ATV3        decoder only
  ADPCTL            decoder only
```
The Mute and Demute commands do not use ADPCTL. The HC05 sets or clears the
decoder's own CD-DA and ADPCM mute bits, which are separate from ADPMUTE.<br/>


#### `0x1f801800` (read, all banks): `HSTS`
#### `0x1f801800` (write, all banks): `ADDRESS`
```
  0-1 RA       Current register bank (R/W)
  2   ADPBUSY  ADPCM busy            (R, 1=playing XA-ADPCM)
  3   PRMEMPT  Parameter empty       (R, 1=parameter FIFO empty)
  4   PRMWRDY  Parameter write ready (R, 1=parameter FIFO not full)
  5   RSLRRDY  Result read ready     (R, 1=result FIFO not empty)
  6   DRQSTS   Data request          (R, 1=one or more RDDATA reads or WRDATA writes pending)
  7   BUSYSTS  Busy status           (R, 1=command written, not yet acknowledged by the HC05)
```
Writing a value to the low 2 bits of this address changes the bank to said value. 
Likewise, the low 2 bits of this address can be read to get the current bank.

#### `0x1f801801` (write, bank 0): `COMMAND`
```
  0-7  Command Byte
```
Writing to this address stores the command byte in the decoder and sets
BUSYSTS. The HC05 picks it up the next time its main loop reads the decoder
(see [Command path](cdromdrive-overview.md#command-path)), drains the parameter FIFO, processes the
command, pushes any return values into the result FIFO and posts the
acknowledge cause (or the error cause if an error occurs).<br/>
Command/Parameter processing is indicated by BUSYSTS.<br/>
When that bit gets zero, the response can be read immediately (immediately for
MOST commands, but not ALL commands; so better wait for the IRQ).<br/>
Alternately, you can wait for an IRQ (which seems to take place MUCH later),
and then read the response.<br/>
If there are any pending cdrom interrupts from a previous command, for example an
acknowledge, these should be cleared before sending a new command. The HC05
does not take a command while a cause is set in HINTSTS, so a command sent early
sits in the command register unhandled, BUSYSTS stays set, and the next command
write overwrites it. On top of all of this, the new command may possibly take precedence
over the execution of the previously submitted command (seems to be related to the 
specific combination of commands sent). Overall, this can just be avoided by just
servicing the previous commands interrupts first.<br/>

#### `0x1f801802` (write, bank 0): `PARAMETER`
```
  0-7  Parameter Byte(s) to be used for next Command
```
Before sending a command, write any parameter byte(s) to this address. The FIFO
can hold up to 16 bytes; once full, the decoder will clear the PRMWRDY flag.<br/>
Note: the CXD1199 datasheet incorrectly states the parameter FIFO is 8 bytes
deep, however the longest CD-ROM command has a 13-byte parameter.<br/>

#### `0x1f801803` (write, bank 0): `HCHPCTL`
```
  0-4 -    Reserved                                    (should be 0)
  5   SMEN Sound map (manual XA-ADPCM playback) enable
  6   BFWR Request sector buffer write                 (1=prepare for writes to WRDATA)
  7   BFRD Request sector buffer read                  (1=prepare for reads from RDDATA)
```
Note: in the original nocash documentation, SMEN is described as "Want Command
Start Interrupt on Next Command". This is actually a side effect to the decoder
firing the BFWRDY interrupt, not an intended feature.<br/>

#### `0x1f801802` (read, all banks): `RDDATA`
After ReadS/ReadN commands have generated the data ready cause, software must set the BFRD flag,
then wait until DRQSTS is set, the datablock (disk sector) can be then read from
this register.<br/>
```
  0-7  Data 8bit  (one byte), or alternately,
  0-15 Data 16bit (LSB=First byte, MSB=Second byte)
```
The PSX hardware allows to read 800h-byte or 924h-byte sectors, indexed as
\[000h..7FFh\] or \[000h..923h\], when trying to read further bytes, then the PSX
will repeat the byte at index \[800h-8\] or \[924h-4\] as padding value.<br/>
RDDATA can be accessed with 8bit or 16bit reads (ie. to read a
2048-byte sector, one can use 2048 load-byte opcodes, or 1024 load halfword
opcodes, or, more conventionally, a 512 word DMA transfer; the actual CDROM
databus is only 8bits wide, so the CPU's bus interface handles splitting the
reads).<br/>

#### `0x1f801801` (read, all banks): `RESULT`
```
  0-7  Response Byte(s) received after sending a Command
```
The result FIFO can hold up to 16 bytes (most or all responses are less than 16
bytes). The decoder clears RSLRRDY after the last byte of the HC05's response is
read from this register.<br/>
When reading further bytes: The buffer is padded with 00h's to the end of the
16-bytes, and does then restart at the first response byte (that, without
receiving a new response, so it'll always return the same 16 bytes, until a new
command/response has been sent/received).<br/>

#### `0x1f801803` (read, banks 1 and 3): `HINTSTS`
```
  0-2 INTSTS Interrupt "flags" from HC05
  3   BFEMPT Sound map XA-ADPCM buffer empty       (1=decoder ran out of sectors to play)
  4   BFWRDY Sound map XA-ADPCM buffer write ready (1=decoder is ready for next sector)
  5-7 -      Reserved                              (always 1)
```
Bits 0-2 are supposed to be used as three separate IRQ flags, however the HC05
misuses them as a single 3-bit "interrupt type" value, which always assumes one
of the following values:<br/>
```
  0    -            -     CdlNoIntr       No interrupt pending
  1    Data ready   INT1  CdlDataReady    New sector (ReadN/ReadS) or report packet (Play) available
  2    Complete     INT2  CdlComplete     Command finished processing (some commands, after the acknowledge cause is fired)
  3    Acknowledge  INT3  CdlAcknowledge  Command received and acknowledged (all commands)
  4    End          INT4  CdlDataEnd      Reached end of disc (or end of track if auto-pause enabled)
  5    Error        INT5  CdlDiskError    Command error, read error, license string error or lid opened
  6-7  -
```
The first column is the name used throughout this page. INT1 to INT5 are the
names most emulators use, and the `Cdl` constants are the ones defined in the
official libcd headers.<br/>
The response interrupts are queued. For example, if the first response is an
acknowledge and the second an error, the acknowledge is delivered first and the
error is not delivered until the CPU has cleared the acknowledge (the causes are
NOT ORed together to produce 7). BFEMPT and BFWRDY however can be ORed with the lower
bits (i.e. BFWRDY plus the acknowledge cause (3) gives 13h).<br/>
All interrupts are always fired in response to a command with the exception of
the error cause, which may also be triggered at any time by opening the lid.<br/>

#### `0x1f801803` (read, banks 0 and 2): `HINTMSK`
#### `0x1f801802` (write, bank 1): `HINTMSK`
```
  0-2 ENINT    Enable IRQ on respective INTSTS bits
  3   ENBFEMPT Enable IRQ on BFEMPT
  4   ENBFWRDY Enable IRQ on BFWRDY
  5-7 -        Reserved (should be 0 when written, always 1 when read)
```
The CD-ROM drive fires an interrupt whenever (HINTMSK & HINTSTS) is non-zero.
This register is typically set to 1Fh, allowing any of the flags to trigger an
IRQ (even though BFEMPT and BFWRDY are never used).<br/>

#### `0x1f801803` (write, bank 1): `HCLRCTL`
```
  0-2 CLRINT     Acknowledge HC05 interrupt "flags" (0=no change, 1=clear)
  3   CLRBFEMPT  Acknowledge BFEMPT                 (0=no change, 1=clear)
  4   CLRBFWRDY  Acknowledge BFWRDY                 (0=no change, 1=clear)
  5   SMADPCLR   Clear sound map XA-ADPCM buffer    (0=no change, 1=clear/stop playback)
  6   CLRPRM     Clear parameter FIFO               (0=no change, 1=clear)
  7   CHPRST     Reset decoder chip                 (0=no change, 1=reset)
```
Setting bits 0-4 resets the corresponding flags in HINTSTS; normally one should
write 07h to reset the HC05 interrupt flags, or 1Fh to acknowledge all IRQs.
Acknowledging individual HC05 flags (e.g. writing 01h to change the acknowledge cause (3) to the complete cause (2)) is
possible, if completely useless. After acknowledge, the result FIFO is drained
and if there's been a pending command, then that command gets send to the
controller.<br/>
Setting CHPRST will result in a complete reset of the decoder. Unclear if this
also reboots the HC05 and CD-ROM DSP (the decoder has an "external reset" pin
which is pulled low when setting CHPRST).<br/>

#### Caution - Unstable IRQ Flag polling
IRQ flag changes aren't synced with the MIPS CPU clock. If more than one bit
gets set (and the CPU is reading at the same time) then the CPU does
occasionally see only one of the newly set bits:<br/>
```
  0 ----------> 3   ;99.9%  normal case acknowledge causes
  0 ----------> 5   ;99%    normal case error causes
  0 ---> 1 ---> 3   ;0.1%   glitch: occurs about once per thousands of acknowledge causes
  0 ---> 4 ---> 5   ;1%     glitch: occurs about once per hundreds of error causes
```
As workaround, do something like:<br/>
```
 @@polling_lop:
  irq_flags = [1F801803h] AND 07h       ;<-- 1st read (may be still unstable)
  if irq_flags = 00h then goto @@polling_lop
  irq_flags = [1F801803h] AND 07h       ;<-- 2nd read (should be stable now)
  handle irq_flags and acknowledge them
```
The problem applies only when manually polling the IRQ flags (an actual IRQ
handler will get triggered when the flags get nonzero, and the flags will have
stabilized once when the IRQ handler is reading them) (except, a combination of
IRQ10h followed by IRQ3 can also have unstable LSBs within the IRQ handler).<br/>
The problem occurs only on older consoles (like LATE-PU-8), not on newer
consoles (like PSone).<br/>

#### `0x1f801802` (write, bank 2): `ATV0` (L->L volume)
#### `0x1f801803` (write, bank 2): `ATV1` (L->R volume)
#### `0x1f801801` (write, bank 3): `ATV2` (R->R volume)
#### `0x1f801802` (write, bank 3): `ATV3` (R->L volume)
Allows to configure the CD for mono/stereo output (eg. values "80h,0,80h,0"
produce normal stereo volume, values "40h,40h,40h,40h" produce mono output of
equivalent volume).<br/>
When using bigger values, the hardware does have some incomplete saturation
support; the saturation works up to double volume (eg. overflows that occur on
"FFh,0,FFh,0" or "80h,80h,80h,80h" are clipped to min/max levels), however, the
saturation does NOT work properly when exceeding double volume (eg. mono with
quad-volume "FFh,FFh,FFh,FFh").<br/>
```
  0-7  Volume Level (00h..FFh) (00h=Off, FFh=Max/Double, 80h=Default/Normal)
```
After changing these registers, the CHNGATV flag in ADPCTL must be set.<br/>
Spyro the Dragon sets volume using these registers depending on whether mono or
stereo sound is selected in the options menu. The stereo setting sets ATV0, ATV2
to 7Fh and ATV1, ATV3 to 00h. The mono setting sets every register to 3Fh. 
Resident Evil 2 uses these ports to produce fade-in/fade-out effects (although,
for that purpose, it should be much easier to use Port 1F801DB0h).<br/>

#### `0x1f801803` (write, bank 3): `ADPCTL`
```
  0    ADPMUTE Mute XA-ADPCM           (1=mute)
  1-4  -       Reserved                (should be 0)
  5    CHNGATV Apply ATV0-ATV3 changes (0=no change, 1=apply)
  6-7  -       Reserved                (should be 0)
```

#### `0x1f801801` (write, bank 1): `WRDATA`
```
  0-7  Data
```
Used to upload sectors to the decoder for sound map XA-ADPCM playback.<br/>
This register seems to be restricted to 8bit bus, unknown if/how the PSX DMA
controller can write to it (it might support only 16bit data for CDROM).<br/>

#### `0x1f801801` (write, bank 2): `CI`
```
  0    S/M      Channel count   (0=mono, 1=stereo)
  1    -        Reserved        (should be 0)
  2    FS       Sample rate     (0=37800Hz, 1=18900Hz)
  3    -        Reserved        (should be 0)
  4    BITLNGTH Bits per sample (0=4bit, 1=8bit)
  5    -        Reserved        (should be 0)
  6    EMPHASIS Emphasis filter (0=off, 1=on)
  7    -        Reserved        (should be 0)
```
Used to configure the decoder for sound map XA-ADPCM playback (does not affect
playback of XA-ADPCM sectors from the disc). Uses the same format as the
"codinginfo" field in XA sector headers.<br/>

#### BUSYSTS flag
Indicates ready-to-send-new-command,<br/>
```
  0=Ready to send a new command
  1=Command written, not yet taken and acknowledged by the HC05
```
The decoder sets this bit when COMMAND is written. Only the HC05 clears it,
just before it posts the acknowledge or error cause.<br/>
Trying to send a new command in the Busy-phase causes malfunction (the older
command seems to get lost, the newer command executes and returns its results
and triggers an interrupt, but, thereafter, the controller seems to hang). So,
always at least wait until BUSYSTS goes off before sending a command.<br/>
When BUSYSTS goes off, a new command may be sent immediately (even if the
response from the previous command wasn't received yet), however, as previously
noted, the behavior doing this is unpredictable. The new command MAY stay in 
the Busy-phase until the IRQ from the previous command is acknowledged, 
at that point the actual transmission of the new command starts. However,
some command combinations may cause the new command to take precedence.
This behavior is unpredictable, so one should instead just wait for the interrupt
status bits in HINTSTS to be 0 first before sending a new command.<br/>

```
Pause -> Wait for acknowledge IRQ -> clear IRQ (write 0x1f to HCLRCTL) -> SetMode/Pause/Stop/SetMode/SeekL/... <br/>
ReadN/ReadS -> Wait for acknowledge IRQ -> clear IRQ (write 0x1f to HCLRCTL) -> SetMode/SetLoc/... <br/>
```
Will not drop any of the two commands, thus execute sequentially.<br/>
<br/>

```
Stop -> Wait for acknowledge IRQ -> clear IRQ (write 0x1f to HCLRCTL) -> SetMode/Pause/...<br/>
```
Will drop the second response of Stop(), and then execute the next command.<br/>




#### Misc
Performing a 32-bit read from 1F801800h will return the HSTS register's value
repeated four times, as the "auto increment" flag in the BIU configuration
register for the CD-ROM (at 1F801018h) is disabled by default. Enabling it will
restore the correct behavior but will also break CD-ROM DMA reads, which rely on
the bus interface splitting each 32-bit word transfer into four sequential byte
reads from RDDATA.<br/>

#### To init the CD
```
  -Flush all IRQs
  -HCHPCTL=0
  -Com_Delay=4901 (=1325h) (Port 1F801020h) (means 16bit or 32bit write?)
     (the write seems to be 32bit, clearing the upper16bit of the register)
  -Send two Nop commands
  -Send Command 0Ah (Init)
  -Demute
```

#### Seek-Busy Phase
Warning: most or all of the info in the sentence below appear to incorrect
(either that, or I didn't understand that rather confusing sentence).<br/>
REPORTEDLY:<br/>
"You should not send some commands while the CD is seeking (ie. Nop returns
with bit6 set). Thing is that stat only gets updated after a new command. I
haven't tested this for other command, but for the play command (03h) you can
just keep repeating the \[which?\] command and checking stat returned by that,
for bit6 to go low (and bit7 to go high in this case). If you don't and try to
do a getloc \[GetlocP and/or GetlocL?\] directly after the play command reports
it's done \[what done? meaning sending start-to-play was "done"? or meaning play
reached end-of-disc?\], the CD will stop. (I guess the CD can't get it's current
location while it's seeking, so the logic stops the seek to get an exact fix,
but never restarts..)"<br/>

#### Sound Map Flowchart
Sound Map mode allows to output XA-ADPCM from Main RAM (rather than from
CDROM).<br/>
```
  SPU: Init Master Volume Left/Right (Port 1F801D80h/1F801D82h)
  SPU: Init CD Audio Volume Left/Right (Port 1F801DB0h/1F801DB2h)
  SPU: Enable CD Audio (Port 1F801DAAh.Bit0=1)
  CDROM/CMD: send Stop command (probably better to avoid conflicts)
  CDROM/CMD: send Demute command (if muted) (but works only if disc inserted)
  CDROM/HOST: init CI register with XA-ADPCM coding info
  CDROM/HOST: enable ADPCM (ADPMUTE=0)  ;probably needed?
  ... set dummy addr/len with DISHXFRC=1 ?  <-- NOT required !
  for each block:
    set HCHPCTL=60h (SMEN and BFWR, both required)
    transfer 900h bytes (same format as ADPCM sectors) (WRDATA)
    wait for BFWRDY, then acknowledge it
  Note: Before sending a byte, one should wait for DRQSTS
  Note: ADPCM output doesn't start until the last (900h'th) byte is transferred
```
HCHPCTL must be written again before every block: if it is written only once,
the first block plays and the decoder then reports BFEMPT. DRQSTS stays set for
exactly 900h bytes per block. With SMEN alone, BFWRDY is set by the HCHPCTL
write but DRQSTS never is; with BFWR alone the bytes go to the sector buffer
(1000h of them are accepted) and nothing is played. Writing E0h instead of 60h
behaves the same.<br/>

The firmware version alone does not decide whether sound map produces output:
two SCPH-5501 units with the same firmware (97-01-10) give opposite results.
The cause is unknown.<br/>
```
  Console     Firmware   Sound map output
  SCPH-1000   95-05-16   yes
  SCPH-1001   95-07-24   silent (handshake completes, no audio)
  SCPH-5501   97-01-10   yes on one unit, silent on another
  SCPH-7001   97-08-14   yes
  SCPH-9002   99-02-01   yes
```
Sound Map mode may be very useful for testing XA-ADPCM directly from within an
exe file (without needing a cdrom with ADPCM sectors). And, Sound Map supports
both 4bit and 8bit compression (the SPU supports only 4bit).<br/>
Caution: If ADPCM wasn't playing, and one sends one 900h-byte block, then it
will get stored in one of three 900h-byte slots in SRAM, and one would expect
that slot to be played when the ADPCM output starts - however, actually, the
hardware will more or less randomly play one of the three slots; not
necessarily the slot that was updated most recently.<br/>



##   CDROM Controller Command Summary
#### Command Summary
| Opcode      | Command      | Parameters        | Acknowledge response                                    | Completion response                           | Notes                                            |
| ----------: | :----------- | :---------------- | :------------------------------------------------------ | :-------------------------------------------- | :----------------------------------------------- |
|      `0x00` | _Unused_     |                   | error: `0x11`, `0x40`                                    |                                               |                                                  |
|      `0x01` | `Nop`        |                   | acknowledge: status                                            |                                               |                                                  |
|      `0x02` | `Setloc`     | min, sec, frame   | acknowledge: status                                            |                                               |                                                  |
|      `0x03` | `Play`       | track (optional)  | acknowledge: status                                            |                                               |                                                  |
|      `0x04` | `Forward`    |                   | acknowledge: status                                            |                                               | Error if disc is spun down                       |
|      `0x05` | `Backward`   |                   | acknowledge: status                                            |                                               | Error if disc is spun down                       |
|      `0x06` | `ReadN`      |                   | acknowledge: status                                            |                                               |                                                  |
|      `0x07` | `Standby`    |                   | acknowledge: status                                            | complete: status                                  |                                                  |
|      `0x08` | `Stop`       |                   | acknowledge: status                                            | complete: status                                  |                                                  |
|      `0x09` | `Pause`      |                   | acknowledge: status                                            | complete: status                                  |                                                  |
|      `0x0a` | `Init`       |                   | acknowledge: status (late)                                     | complete: status                                  |                                                  |
|      `0x0b` | `Mute`       |                   | acknowledge: status                                            |                                               |                                                  |
|      `0x0c` | `Demute`     |                   | acknowledge: status                                            |                                               |                                                  |
|      `0x0d` | `Setfilter`  | file, channel     | acknowledge: status                                            |                                               |                                                  |
|      `0x0e` | `Setmode`    | mode              | acknowledge: status                                            |                                               |                                                  |
|      `0x0f` | `Getparam`   |                   | acknowledge: status, mode, `0x00`, file, channel               |                                               |                                                  |
|      `0x10` | `GetlocL`    |                   | acknowledge: min, sec, frame, mode, file, channel, sm, ci      |                                               | Error if disc is spun down                       |
|      `0x11` | `GetlocP`    |                   | acknowledge: track, index, rmin, rsec, rframe, min, sec, frame |                                               | Error if disc is spun down                       |
|      `0x12` | `Setsession` | session           | acknowledge: status                                            | complete: status                                  |                                                  |
|      `0x13` | `GetTN`      |                   | acknowledge: status, first, last                               |                                               |                                                  |
|      `0x14` | `GetTD`      | track             | acknowledge: status, min, sec                                  |                                               |                                                  |
|      `0x15` | `SeekL`      |                   | acknowledge: status                                            | complete: status                                  |                                                  |
|      `0x16` | `SeekP`      |                   | acknowledge: status                                            | complete: status                                  |                                                  |
| `0x17-0x18` | _Unused_     |                   | error: `0x11`, `0x40`                                    |                                               |                                                  |
|      `0x19` | `Test` \*    | sub, ...          | acknowledge: ...                                               |                                               |                                                  |
|      `0x1a` | `GetID` \*   |                   | acknowledge: status                                            | complete/error: status, flag, type, atip, `"SCEx"` |                                                  |
|      `0x1b` | `ReadS`      |                   | acknowledge: status                                            |                                               |                                                  |
|      `0x1c` | `Reset`      |                   | acknowledge: status                                            |                                               | Reboots HC05, requires delay after sending       |
|      `0x1d` | `GetQ` \*    | adr, point        | acknowledge: status                                            | complete: subq\[10\], peakl                       | Version `0xc1`+, error if disc is spun down      |
|      `0x1e` | `ReadTOC` \* |                   | acknowledge: status (late)                                     | complete: status                                  | Version `0xc1`+                                  |
|      `0x1f` | `VideoCD` \* | sub, ...          | acknowledge: status, ...                                       |                                               | SCPH-5903 only                                   |
| `0x20-0x4f` | _Unused_     |                   | error: `0x11`, `0x40`                                    |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x50` | `Unlock0` \* |                   | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x51` | `Unlock1` \* | `"Licensed by"`   | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x52` | `Unlock2` \* | `"Sony"`          | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x53` | `Unlock3` \* | `"Computer"`      | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x54` | `Unlock4` \* | `"Entertainment"` | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x55` | `Unlock5` \* | `"<region>"`      | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x56` | `Unlock6` \* |                   | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
|      `0x57` | `Lock` \*    |                   | error: `0x11`, `0x40` (even when successful)             |                                               | Version `0xc1`+, does nothing on Japanese models |
| `0x58-0x5f` | _Unused_     |                   |                                                         |                                               | Crashes the HC05                                 |
| `0x60-0xff` | _Unused_     |                   | error: `0x11`, `0x40`                                    |                                               |                                                  |

The following commands generate additional responses while reading:

| Opcode | Command    | Data responses                                                       |
| -----: | :--------- | :------------------------------------------------------------------- |
| `0x03` | `Play`     | data ready: status, track, index, (r)min, (r)sec, (r)frame, peakl, peakh   |
| `0x04` | `Forward`  | data ready: status, track, index, (r)min, (r)sec, (r)frame, peakl, peakh   |
| `0x05` | `Backward` | data ready: status, track, index, (r)min, (r)sec, (r)frame, peakl, peakh   |
| `0x06` | `ReadN`    | data ready: status (sector data must be read separately via RDDATA or DMA) |
| `0x1b` | `ReadS`    | data ready: status (sector data must be read separately via RDDATA or DMA) |

\* denotes commands that are not officially documented.

#### sub\_function numbers (for command 19h)
Test commands are invoked with command number 19h, followed by a sub\_function
number as first parameter byte. The Kernel seems to be using only sub\_function
20h (to detect the CDROM Controller version).<br/>
```
  sub  params  response           ;Effect
  00h      -   acknowledge(stat)         ;Force motor on, clockwise, even if door open
  01h      -   acknowledge(stat)         ;Force motor on, anti-clockwise, super-fast
  02h      -   acknowledge(stat)         ;Force motor on, anti-clockwise, super-fast
  03h      -   acknowledge(stat)         ;Force motor off (ignored during spin-up)
  04h      -   acknowledge(stat)         ;Start SCEx reading and reset counters
  05h      -   acknowledge(total,success);Stop SCEx reading and get counters
  06h *    n   acknowledge(old)  ;\early ;Adjust balance in RAM, send CX(30+n XOR 7)
  07h *    n   acknowledge(old)  ; PSX   ;Adjust gain in RAM, send CX(38+n XOR 7)
  08h *    n   acknowledge(old)  ;/only  ;Adjust balance in RAM only
  06h..0Fh -   error(11h,10h)      ;N/A (11h,20h when NONZERO number of params)
  10h      -   acknowledge(stat) ;CX(..) ;Force motor on, anti-clockwise, super-fast
  11h      -   acknowledge(stat) ;CX(03) ;Move Lens Up (leave parking position)
  12h      -   acknowledge(stat) ;CX(02) ;Move Lens Down (enter parking position)
  13h      -   acknowledge(stat) ;CX(28) ;Move Lens Outwards
  14h      -   acknowledge(stat) ;CX(2C) ;Move Lens Inwards
  15h      -   acknowledge(stat) ;CX(22) ;If motor on: Move outwards,inwards,motor off
  16h      -   acknowledge(stat) ;CX(23) ;No effect?
  17h      -   acknowledge(stat) ;CX(E8) ;Force motor on, clockwise, super-fast
  18h      -   acknowledge(stat) ;CX(EA) ;Force motor on, anti-clockwise, super-fast
  19h      -   acknowledge(stat) ;CX(25) ;No effect?
  1Ah      -   acknowledge(stat) ;CX(21) ;No effect?
  1Bh..1Fh -   error(11h,10h)      ;N/A (11h,20h when NONZERO number of params)
  20h      -   acknowledge(yy,mm,dd,ver) ;Get cdrom BIOS date/version (yy,mm,dd,ver)
  21h      -   acknowledge(n)            ;Get Drive Switches (bit0=POS0, bit1=DOOR)
  22h ***  -   acknowledge("for ...")    ;Get Region ID String
  23h ***  -   acknowledge("CXD...")     ;Get Chip ID String for Servo Amplifier
  24h ***  -   acknowledge("CXD...")     ;Get Chip ID String for Signal Processor
  25h ***  -   acknowledge("CXD...")     ;Get Chip ID String for Decoder/FIFO
  26h..2Fh -   error(11h,10h)      ;N/A (11h,20h when NONZERO number of params)
  30h *    i,x,y     acknowledge(stat)       ;Prototype/Debug stuff   ;\supported on
  31h *    x,y       acknowledge(stat)       ;Prototype/Debug stuff   ; early PSX only
  4xh *    i         acknowledge(x,y)        ;Prototype/Debug stuff   ;/
  30h..4Fh ..        error(11h,10h)    ;N/A always 11h,10h (no matter of params)
  50h      a[,b[,c]] acknowledge(stat)       ;Servo/Signal send CX(a:b:c)
  51h **   39h,xx    acknowledge(stat,hi,lo) ;Servo/Signal send CX(39xx) with response
  51h..5Fh -         error(11h,10h)    ;N/A
  60h      lo,hi     acknowledge(databyte)   ;HC05 SUB-CPU read RAM and I/O ports
  61h..70h -         error(11h,10h)    ;N/A
  71h ***  adr       acknowledge(databyte)   ;Decoder Read one register
  72h ***  adr,dat   acknowledge(stat)       ;Decoder Write one register
  73h ***  adr,len   acknowledge(databytes..);Decoder Read multiple registers, bugged
  74h ***  adr,len,..acknowledge(stat)       ;Decoder Write multiple registers, bugged
  75h ***  -         acknowledge(lo,hi,lo,hi);Decoder Get Host Xfer Info Remain/Addr
  76h ***  a,b,c,d   acknowledge(stat)       ;Decoder Prepare Transfer to/from SRAM
  77h..FFh -         error(11h,10h)    ;N/A
  80h..8Fh a,b       ?                ;seem to do something on PS2
```
\* sub\_functions 06h..08h, 30h..31h, and 4xh are supported only in vC0 and vC1.<br/>
\*\* sub\_function 51h is supported only in BIOS version vC2 and up.<br/>
\*\*\* sub\_functions 22h..25h, 71h..76h supported only in BIOS version vC1 and up.<br/>

#### Unsupported GetQ,VCD,SecretUnlock (command 1Dh,1Fh,5xh)
An error cause will be returned if the command is unsupported. That, WITHOUT removing the
Parameters from the FIFO, so the parameters will be accidentally passed to the
NEXT command. To avoid that: clear the parameter FIFO by setting CLRPRM in
HCLRCTL after receiving the error cause.<br/>



##   CDROM - Control Commands
#### Sync - Command 00h --\> INTx(stat+1,40h) (?)
Reportedly "command does not succeed until all other commands complete. This
can be used for synchronization - hence the name."<br/>
Uh, actually, returns error code 40h = Invalid Command...?<br/>

#### Setfilter - Command 0Dh,file,channel --\> acknowledge(stat)
Automatic ADPCM (CD-ROM XA) filter ignores sectors except those which have the
same channel and file numbers in their subheader. This is the mechanism used to
select which of multiple songs in a single .XA file to play.<br/>
Setfilter does not affect actual reading (sector reads still occur for all
sectors).<br/>
More precisely, the filter only applies to real-time ADPCM sectors (Real-Time
bit set in the subheader submode). All other sectors (Form 1 and Form 2 data,
and audio sectors without the Real-Time bit) are delivered to the host whatever
their file and channel numbers, with the filter (Setmode bit 3) on or off.
Real-time ADPCM sectors themselves are never delivered to the host while
Setmode bit 3 or bit 6 is set, whether their file and channel match or not.<br/>

#### Setmode - Command 0Eh,mode --\> acknowledge(stat)
```
  7   Speed       (0=Normal speed, 1=Double speed)
  6   XA-ADPCM    (0=Off, 1=Send XA-ADPCM sectors to SPU Audio Input)
  5   Sector Size (0=800h=DataOnly, 1=924h=WholeSectorExceptSyncBytes)
  4   Ignore Bit  (0=Normal, 1=Ignore Sector Size and Setloc position)
  3   XA-Filter   (0=Off, 1=Process only XA-ADPCM sectors that match Setfilter)
  2   Report      (0=Off, 1=Enable Report-Interrupts for Audio Play)
  1   AutoPause   (0=Off, 1=Auto Pause upon End of Track) ;for Audio Play
  0   CDDA        (0=Off, 1=Allow to Read CD-DA Sectors; ignore missing EDC)
```
The "Ignore Bit" does reportedly force a sector size of 2328 bytes (918h),
however, that doesn't seem to be true. Instead, Bit4 seems to cause the
controller to ignore the sector size in Bit5 (instead, the size is kept from
the most recent Setmode command which didn't have Bit4 set). Also, Bit4 seems
to cause the controller to ignore the \<exact\> Setloc position (instead,
data is randomly returned from the "Setloc position minus 0..3 sectors"). And,
Bit4 causes the data ready cause to return status.Bit3=set (IdError). Purpose of Bit4 is
unknown?<br/>

#### Init - Command 0Ah --\> acknowledge(stat) --\> complete(stat)
Multiple effects at once. Sets mode=20h, activates drive motor, Standby, abort
all commands.<br/>
If an Init command is already in progress (its second response is still pending),
a new Init command is silently dropped with no response (neither acknowledge nor error
is generated).<br/>

#### Reset - Command 1Ch,(...) --\> acknowledge(stat) --\> Delay(1/8 seconds)
```
  Caution: Not supported on DTL-H2000 (v01)
```
Resets the drive controller, reportedly, same as opening and closing the drive
door. The command executes no matter if/how many parameters are used (tested
with 0..7 params). The acknowledge cause indicates that the command was started, but there's no
interrupt that would indicate when the command is finished, so, before sending any
further commands, a delay of 1/8 seconds (or 400000h clock cycles) must be
issued by software.<br/>
Note: Executing the command produces a click sound in the drive mechanics,
maybe it's just a rapid motor on/off, but it might something more serious, like
ignoring the /POS0 signal...?<br/>

#### MotorOn - Command 07h --\> acknowledge(stat) --\> complete(stat)
Activates the drive motor, works ONLY if the motor was off (otherwise fails
with error(stat,20h); that error code would normally indicate "wrong number of
parameters", but means "motor already on" in this case).<br/>
If no disc is present, the command fails with error(stat,80h).<br/>
Commands like Read, Seek, and Play are automatically starting the Motor when
needed (which makes the MotorOn command rather useless, and it's rarely used by
any games).<br/>
Myth: Older homebrew docs are referring to MotorOn as "Standby", claiming that
it would work similar as "Pause", that is wrong: the command does NOT pause
anything (if the motor is on, then it does simply trigger the error cause, but without
pausing reading or playing).<br/>
Note: The game "Nightmare Creatures 2" does actually attempt to use MotorOn to
"pause" after reading files, but the hardware does simply ignore that attempt
(aside from doing the error thing).<br/>

#### Stop - Command 08h --\> acknowledge(stat) --\> complete(stat)
Stops motor with magnetic brakes (stops within a second or so) (unlike
power-off where it'd keep spinning for about 10 seconds), and moves the drive
head to the begin of the first track. Official way to restart is command 0Ah,
but almost any command will restart it.<br/>
The first response returns the current status (this already with bit5 cleared),
the second response returns the new status (with bit1 cleared).<br/>

#### Pause - Command 09h --\> acknowledge(stat) --\> complete(stat)
Aborts Reading and Playing, the motor is kept spinning, and the drive head
maintains the current location within reasonable error.<br/>
The first response returns the current status (still with bit5 set if a Read
command was active), the second response returns the new status (with bit5
cleared).<br/>
During certain phases of a seek operation, Pause will fail with
error(stat,80h). This applies to explicit SeekL/SeekP seeks, and also to the
implicit seek at the beginning of ReadN/ReadS/Play.<br/>

#### Data/ADPCM Sector Filtering/Delivery
The PSX CDROM BIOS is first trying to send sectors to the ADPCM decoder, and,
if that didn't work out, then it's trying to send them to the main CPU (and if
that didn't work out either, then it's silently ignoring the sector).<br/>
```
 try_deliver_as_adpcm_sector:
  reject if CD-DA AUDIO format
  reject if sector isn't MODE2 format
  reject if adpcm_disabled(setmode.6)
  reject if filter_enabled(setmode.3) AND selected file/channel doesn't match
  reject if submode isn't audio+realtime (bit2 and bit6 must be both set)
  deliver: send sector to xa-adpcm decoder when passing above cases
 try_deliver_as_data_sector:
  reject data-delivery if "try_deliver_as_adpcm_sector" did do adpcm-delivery
  reject if filter_enabled(setmode.3) AND submode is audio+realtime (bit2+bit6)
  1st delivery attempt: send data ready+data, unless there's another INT pending
  delay, and retry at later time... but this time with file/channel checking!
  reject if filter_enabled(setmode.3) AND selected file/channel doesn't match
  2nd delivery attempt: send data ready+data, unless there's another INT pending
```
BUG: Note that the data delivery is done in two different attempts: The first
one regardless of file/channel, and the second one only on matching
file/channel (if filtering is enabled).<br/>



##   CDROM - Seek Commands
#### Setloc - Command 02h,amm,ass,asect --\> acknowledge(stat)
Sets the seek target - but without yet starting the seek operation. The actual
seek is invoked by certain commands: SeekL (Data) and SeekP (Audio) are doing
plain seeks (and do Pause after completion). ReadN/ReadS are similar to SeekL
(and do start reading data after the seek operation). Play is similar to SeekP
(and does start playing audio after the seek operation).<br/>
The amm,ass,asect parameters refer to the entire disk (not to the current
track). Note that each of these parameters is encoded as BCD values, not binary.
To seek to a specific location within a specific track, use GetTD to
get the start address of the track, and add the desired time offset to it.<br/>
All three parameters must be valid packed BCD, with ass &lt; 60h and asect &lt; 75h;
invalid or out-of-range values return error(stat,10h).<br/>

#### SeekL - Command 15h --\> acknowledge(stat) --\> complete(stat)
Seek to Setloc's location in data mode (using data sector header position data,
which works/exists only on Data tracks, not on CD-DA Audio tracks).<br/>
After the seek, the disk stays on the seeked location forever (namely: when
seeking sector N, it does stay at around N-8..N-0 in single speed mode, or at
around N-5..N+2 in double speed mode). This command will stop any current or pending ReadN or ReadS.<br/>
Trying to use SeekL on Audio CDs passes okay on the first response, but (after
two seconds or so) the second response will return an error (stat+4,04h), and
stop the drive motor... that error doesn't appear ALWAYS though... works in
some situations... such like when previously reading data sectors or so...?<br/>

#### SeekP - Command 16h --\> acknowledge(stat) --\> complete(stat)
Seek to Setloc's location in audio mode (using the Subchannel Q position data,
which works on both Audio on Data disks).<br/>
After the seek, the disk stays on the seeked location forever (namely: when
seeking sector N, it does stay at around N-9..N-1 in single speed mode, or at
around N-2..N in double speed mode). This command will stop any current or pending ReadN or ReadS. <br/>
Note: Some older docs claim that SeekP would recurse only "MM:SS" of the
"MM:SS:FF" position from Setloc - that is wrong, it does seek to MM:SS:FF
(verified on a PSone).<br/>
After the seek, status is stat.bit7=0 (ie. audio playback off), until sending a
new Play command (without parameters) to start playback at the seeked location.<br/>

#### SetSession - Command 12h,session --\> acknowledge(stat) --\> complete(stat)
Seeks to session (ie. moves the drive head to the session, with stat bit6 set
during the seek phase).<br/>
When issued during active-read or active-play, the command returns error code 80h.<br/>
When issued during play-spin-up, play is aborted.<br/>
```
  ___Errors___
  session = 00h causes error code 10h.     ;error(03h,10h), no 2nd/3rd response
  ___On a non-multisession-disk___
  session = 01h passes okay.               ;acknowledge(stat), and once complete(stat)
  session = 02h or higher cause seek error ;acknowledge(stat), and twice error(06h,40h)
  ___On a multisession-disk with N sessions___
  session = 01h..N+1 passes okay   ;where N+1 moves to the END of LAST session
  session = N+2 or higher cause seek error  ;2nd response = error(06h,20h)
```
after seek error --\> disk stops spinning at 2nd response, then restarts
spinning for 1 second or so, then stops spinning forever... and following
gettn/gettd/getid/getlocl/getlocp fail with error 80h...<br/>
On a SCPH-9002 with a two-session CD-R (session 2 holding a single Mode 2 track),
the outcomes are less tidy than the table above:
```
  session = 01h  acknowledge, complete after about 1.9 seconds
  session = 02h  acknowledge, then one of (14 attempts on the same disc):
                   complete after 3.7..6.7 s, TOC switched to session 2   (5x)
                   complete after 2.4 s, TOC still the session 1 one      (1x)
                   error(06h,20h) after 3.6..5.4 s                        (8x)
  session = 03h  acknowledge, then error(06h,20h) after 6.8..7.7 s (every attempt while spinning)
  session = 09h  acknowledge, then error(06h,40h) after ~9.9 s, or (06h,20h) after ~4 s
```
So on this drive N+1 did not pass, and the error code for a session past the last
one varies between 20h and 40h; only "acknowledge(stat), then an error with stat
06h" was consistent. After a switch to session 2, GetTN returns 02h,02h, GetTD(0)
returns the lead-out of session 2, GetTD(1) fails with error 10h, and GetTD(2)
returns the start of the session 2 track. After a seek error, the drive did not
always stop: GetTN/GetTD kept failing with error 80h, but a Setloc/ReadN into
session 2 still returned the correct sectors in 5 of 7 cases, while in the other 2
the motor was off (stat 01h) and every later command failed with error 80h.<br/>
The command does automatically read the TOC of the new session. BUG: Older CD
Firmwares (16 May 1995 and older) don't clear the old TOC when loading Session
1, in that case SetSession(1) may update some (not all) TOC entries; ending up
with a mixup of old and new TOC entries.<br/>
There seems to be no way to determine the current sessions number (via Getparam
or so), and more important, no way to determine if the disk is a multi-session
disk or not... except by trial... which would stop the drive motor on seek
errors on single-session disks...?<br/>
For setloc, one must probably specify minutes within the 1st track of the new
session (the 1st track of 1st session usually/always starts at 00:02:00, but
for other sessions one would need to use GetTD)...?<br/>



##   CDROM - Read Commands
#### ReadN - Command 06h --\> acknowledge(stat) --\> data ready(stat) --\> datablock
Read with retry. The command responds once with "stat,acknowledge", and then it's
repeatedly sending "stat,data ready --\> datablock", that is continued even after a
successful read has occurred; use the Pause command to terminate the repeated
data ready responses.<br/>
If you are reading an unlicensed disk without a modchip or first unlocking the drive, 
this command will first trigger an error cause, without triggering acknowledge or data ready.
This error cause is accompanied with the stat byte 0x3 and the following error byte 0x40,
indicating an invalid command. This can be avoided by first unlocking the drive.<br/>
Unknown which responses are sent in case of read errors?<br/>
====<br/>
Sectors on Audio CDs can be read only when CDDA is enabled
via Setmode (otherwise error code 40h is returned). Similarly, discs whose
region does not match the console region will also return error code 40h unless
CDDA mode is enabled.<br/>
====<br/>
Actually, Read seems to work on unlicensed CD-R's, but the returned data is the
whole sector or so (the 2048 data bytes preceded by a 12byte header, and
probably/maybe followed by error-correction info; in fact the total received
data in the Data Fifo is 4096 bytes; the last some bytes probably being
garbage) (however error correction is NOT performed by hardware, so the 2048
data bytes may be trashy) (however, if the error correction info IS received,
then error correction could be performed by software) (also Setloc doesn't seem
to work accurately on unlicensed CD-R's).<br/>
====<br/>
```
     ;Read occasionally returns 11h,40h ..? when TOC isn't loaded?
```
After receiving data ready, the Kernel does,<br/>
```
  [1F801800h]=00h
  00h=[1F801800h]
  [1F801803h]=00h
  00h=[1F801803h]
  [1F801800h]=00h
  [1F801803h]=80h
```
and then,<br/>
```
  [1F801018h]=00020943h  ;cdrom_delay
  [1F801020h]=0000132Ch  ;com_delay
```
then,<br/>
```
  x=[1F8010F4h] AND 00FFFFFFh   ;result is 00840000h
  [1F8010F4h] = x OR 00880000h
  [1F8010F0h] = [1F8010F0h] OR 00008000h
  [1F8010B0h] = A0010000h ;addr
  [1F8010B4h] = 00010200h ;LSBs=num words, MSBs=ignored/bullshit
  [1F8010B4h] = 11000000h ;DMA control
```
thereafter,<br/>
```
  [1F801800h]=01h
  [1F801803h]=40h    ;reset parameter fifo
  [0]=00000000h
  [0]=00000001h
  [0]=00000002h
  [0]=00000003h
  [1F801800h]=00h
  [1F801801h]=09h    ;command9 (pause)
```

#### ReadS - Command 1Bh --\> acknowledge(stat) --\> data ready(stat) --\> datablock
Read without automatic retry. Not sure what that means... does WHAT on errors?
Maybe intended for continuous streaming video output (to skip bad frames, rather
than to interrupt the stream by performing read-retrys).<br/>

#### ReadN/ReadS
Both ReadN/ReadS are reading data sequentially, starting at the sector
specified with Setloc, and then automatically reading the following sectors.<br/>

#### CDROM Incoming Data / Buffer Overrun Timings
The Read commands are continuously receiving 75 sectors per second (or 150
sectors at double speed), and, basically, the software must be fast enough to
process that amount of incoming data. The drive does not queue finished
sectors for the host: only one data ready interrupt can be pending, and when it
is acknowledged, the next one delivers the newest decoded sector, skipping any
sectors decoded in between. Leaving a data ready interrupt unacknowledged
for up to one sector period (1/75 second at single speed) loses nothing; every
further period skips one sector (after k periods, the next sector delivered is
the previous one plus k).<br/>
The decoded sectors themselves sit in a ring in the drive's buffer RAM, and a
pending sector is only readable until that ring comes back around to it. On
SCPH-9002 consoles the ring holds 8 sectors: the pending sector's data starts
being overwritten by the sector 8 positions later about 6 sector periods after
its data ready interrupt, the end of the sector about one period later. Other board revisions
(with a separate 32Kx8 sector SRAM) have not been measured.<br/>
The relevant steps for receiving data are:<br/>
```
  Wait for Interrupt Request (data ready)    ;indicates that data is available
  Send Data Request (BFRD=1)                 ;accept data
  Acknowledge data ready                     ;
  Copy Data to Main RAM (via I/O or DMA)     ;read data
```
The Data Request accepts the data for the currently pending interrupt, it
should be usually issued between receiving/acknowledging data ready (however, it can
be also issued shortly after the acknowledge; even if a newer sector is already
waiting, the next interrupt follows the acknowledge only after a short delay
(0.4 to 1.4 ms), and Data Requests during that period seem to be still treated
as belonging to the old interrupt). A Data Request issued after that next
interrupt has arrived reads the newer sector.<br/>
If a buffer overrun has occurred \<before\> issuing the Data Request, then
wrong data will be received, ie. some sectors will be skipped (the hardware
doesn't seem to support a buffer-overrun error flag? Anyways, see GetlocL
description for a possible way to detect buffer-overruns).<br/>
Issuing the Data Request does not lock the sector's data in the buffer RAM. On
SCPH-9002, when the transfer is done more than about 6 sector periods after the
data ready interrupt, only the first few bytes (presumably those the Data
Request already moved into the data FIFO) belong to the requested sector; the rest comes from the
sector 8 positions later. Read the data promptly after the Data Request.<br/>

#### ReadTOC - Command 1Eh --\> acknowledge(stat) --\> complete(stat)
```
  Caution: Supported only in BIOS version vC1 and up. Not supported in vC0.
```
Reread the Table of Contents of current session without reset. The command is
rather slow, the second response appears after about 1 second delay. The
command itself returns only status information (to get the actual TOC info, use
GetTD and GetTN commands).<br/>
Note: The TOC contains information about the tracks on the disk (not file names
or so, that kind of information is obtained via Read commands). The TOC is read
automatically on power-up, when opening/closing the drive door, and when
changing sessions (so, normally, it isn't required to use this command).<br/>

#### Setloc, Read, Pause
A normal CDROM access (such like reading a file) consists of three commands:<br/>
```
  Setloc, Read, Pause
```
Normally one shouldn't mess up the ordering of those commands, but if one does,
following rules do apply:<br/>
Setloc is memorizing the wanted target, and marks it as unprocessed, and has no
other effect (it doesn't start reading or seeking, and doesn't interrupt or
redirect any active reads).<br/>
If Read is issued with an unprocessed Setloc, then the drive is automatically
seeking the Setloc location (and marks Setloc as processed).<br/>
If Read is issued without an unprocessed Setloc, the following happens: If
reading is already in progress then it just continues reading. If Reading was
Paused, then reading resumes one sector before the most recently received
sector (eg. Pause after receiving 00:02:21, and the next Read returns 00:02:20
first, then 00:02:21 again).<br/>



##   CDROM - Status Commands
#### Status code (stat)
The 8bit status code is returned by Nop command (and many other commands),
the meaning of the separate stat bits is:<br/>
```
  7  Play          Playing CD-DA         ;\only ONE of these bits can be set
  6  Seek          Seeking               ; at a time (ie. Read/Play won't get
  5  Read          Reading data sectors  ;/set until after Seek completion)
  4  ShellOpen     Once shell open (0=Closed, 1=Is/was Open)
  3  IdError       (0=Okay, 1=GetID denied) (also set when Setmode.Bit4=1)
  2  SeekError     (0=Okay, 1=Seek error)     (followed by Error Byte)
  1  Spindle Motor (0=Motor off, or in spin-up phase, 1=Motor on)
  0  Error         Invalid Command/parameters (followed by Error Byte)
```
If the shell is closed, then bit4 is automatically reset to zero after reading
stat with the Nop command (most or all other commands do not reset that bit
after reading). If stat bit0 or bit2 is set, then the normal response(s) and
interrupt(s) are not send, and, instead, an error cause occurs, and an error-byte is send
as second response byte, with the following values:<br/>
```
  ___These values appear in the FIRST response; with stat.bit0 set___
  10h - Invalid Sub_function (for command 19h), or invalid parameter value
  20h - Wrong number of parameters
  40h - Invalid command
  80h - Cannot respond yet (eg. required info was not yet read from disk yet)
           (namely, TOC not-yet-read or so)
           (also appears if no disk inserted at all)
  ___These values appear in the SECOND response; with stat.bit2 set___
  04h - Seek failed (when trying to use SeekL on Audio CDs)
  ___These values appear even if no command was sent; with stat.bit2 set___
  08h - Drive door became opened
```
80h appears on some commands (02h..09h, 0Bh..0Dh, 10h..16h, 1Ah, 1Bh?, and 1Dh)
when the disk is missing, or when the drive unit is disconnected from the
mainboard.<br/>

When the shell is opened, the error cause is triggered regardless of whether a command was
executing or not. When this happens, all bits except shell open and error are cleared
in the status register. The error byte in the error cause is set to 08h.<br/>

Some games send a Stop command before changing discs, but others just wait for the
user to open the shell, causing the disc to stop. The game can then send Nop commands,
looping until bit 4 is cleared to detect when the new disc has been inserted.<br/>

#### Stat Seek/Play/Read bits
There is only max ONE of the three Seek/Play/Read bits set at a time, ie.
during Seek, ONLY the seek bit is set (and Read or Play doesn't get until seek
completion), that is important for Gran Turismo 1, which checks for seek
completion by waiting for READ getting set (rather than waiting for SEEK
getting cleared).<br/>

#### Nop - Command 01h --\> acknowledge(stat)
Returns stat (like many other commands), and additionally does reset the shell
open flag (for the following commands; unless the shell is still opened). This
is different as for most or all other commands (which may return stat, but
which do not reset the shell open flag).<br/>
In official docs, the command is eventually referred to as "Nop", believing that
it does nothing than returning stat (ignoring the fact that it's having the
special shell open reset feature).<br/>

#### Getparam - Command 0Fh --\> acknowledge(stat,mode,null,file,channel)
Returns stat (see Nop above), mode (see Setmode), a null byte (always 00h),
and file/channel filter values (see Setfilter).<br/>

#### GetlocL - Command 10h --\> acknowledge(amm,ass,asect,mode,file,channel,sm,ci)
Retrieves 4-byte sector header, plus 4-byte subheader of the current sector.
GetlocL can be send during active Read commands (but, mind that the
GetlocL-acknowledge-response can't be received until any pending Read-data-ready causes are
acknowledged).<br/>
The data ready handler receives the sector that was the newest one when its
interrupt was raised, while the GetlocL command returns the header and
subheader of the newest sector decoded so far. Note: If the returned sector
number is bigger than the one the handler expected next, sectors have been
skipped (see "CDROM Incoming Data / Buffer Overrun Timings").<br/>
GetlocL fails (with error code 80h) when playing Audio CDs (or Audio Tracks on
Data CDs). These errors occur because Audio sectors don't have any
header/subheader (instead, equivalent data is stored in Subchannel Q, which can
be read with GetlocP).<br/>
GetlocL also fails (with error code 80h) when the drive is in Seek phase (such
like shortly after a new ReadN/ReadS command). In that case one can retry
issuing GetlocL (until it passes okay, ie. until the seek has completed).
During Seek, the drive seems to decode only Subchannel position data (but no
header/subheader data), accordingly GetlocL won't work during seek (however,
GetlocP does work during Seek).<br/>

#### GetlocP - Command 11h - acknowledge(track,index,mm,ss,sect,amm,ass,asect)
Retrieves 8 bytes of position information from Subchannel Q with ADR=1. Mainly
intended for displaying the current audio position during Play. All results are
in BCD.<br/>
```
  track:  track number (AAh=Lead-out area) (FFh=unknown, toc, none?)
  index:  index number (Usually 01h)
  mm:     minute number within track (00h and up)
  ss:     second number within track (00h to 59h)
  sect:   sector number within track (00h to 74h)
  amm:    minute number on entire disk (00h and up)
  ass:    second number on entire disk (00h to 59h)
  asect:  sector number on entire disk (00h to 74h)
```
Note: GetlocP is also used for reading the LibCrypt protection data:<br/>
[CDROM Protection - LibCrypt](cdromformat.md#cdrom-protection-libcrypt)<br/>

#### GetTN - Command 13h --\> acknowledge(stat,first,last) ;BCD
Get first track number, and last track number in the TOC of the current
Session. The number of tracks in the current session can be calculated as
(last-first+1). The first track number is usually 01h in the first (or only)
session, and "last track of previous session plus 1" in further sessions.<br/>

#### GetTD - Command 14h,track --\> acknowledge(stat,mm,ss) ;BCD
For a disk with NN tracks, parameter values 01h..NNh return the start of the
specified track, parameter value 00h returns the end of the last track, and
parameter values bigger than NNh return error code 10h. Non-BCD parameter
values also return error code 10h.<br/>
The GetTD values are relative to Index=1 and are rounded down to second
boundaries (eg. if track=N Index=0 starts at 12:34:56, and Track=N Index=1
starts at 12:36:56, then GetTD(N) will return 12:36, ie. the sector number is
truncated, and the Index=0 region is skipped).<br/>

#### GetQ - Command 1Dh,adr,point --\> acknowledge(stat) --\> complete(10bytesSubQ,peak\_lo)
```
  Caution: Supported only in BIOS version vC1 and up. Not supported in vC0.
  Caution: When unsupported, Parameter Fifo isn't cleared after the command.
```
Allows to read 10 bytes from Subchannel Q in Lead-In (see CDROM Subchannels
chapter for details). Unlike GetTD, this command allows to receive the exact
MM:SS:FF address of the point'ed Track (GetTD reads a memorized MM:SS value
from RAM, whilst GetQ reads the full MM:SS:FF from the disk, which is slower
than GetTD, due to the disk-access).<br/>
With ADR=1, point can be a any point number for ADR=1 in Lead-in (eg.
01h..99h=Track N, A2h=Lead-Out). The returned 10 bytes are raw SubQ data
(starting with the ADR/Control value; of which the lower 4bits are always
ADR=1).<br/>
The 11th returned byte is the Peak LSB (similar as in Play+Report, but in this
case only the LSB is transferred, which is apparently a bug in CDROM BIOS, the
programmer probably wanted to send 10 bytes without peak, or 12 bytes with full
peak; although peak wouldn't be too useful, as it should always zero during
Lead-In... b