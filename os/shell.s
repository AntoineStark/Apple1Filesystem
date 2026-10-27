.setcpu "6502"

COMMAND = $20
TEXT = $22
ARG = $24
HANDLER = $26
LINE_SIZE = $28
TABLE_INDEX = $29
LIST_INDEX = $2A
FILE_SIZE = $2B
NAME = $40
DATA = $42
LENGTH = $44
ENTRY = $45
LINE = $0300
FILENAME = $0400
CONTENTS = $0500
FS_MOUNT = $2000
FS_FORMAT = $2003
FS_PUT = $2009
FS_GET = $200C
FS_REMOVE = $200F
FS_INFO = $2012

.macro print label
    lda #<label
    sta TEXT
    lda #>label
    sta TEXT+1
    jsr puts
.endmacro

.segment "CODE"
reset:
    sei
    cld
    ldx #$FF
    txs
    lda #0
    sta $D011
    sta $D013
    sta $D010
    lda #$7F
    sta $D012
    lda #$A7
    sta $D011
    sta $D013
    print banner
    jsr FS_MOUNT
    bcc prompt
    lda #0
    sta TEXT
    lda #$A0
    sta TEXT+1
    ldy #0
    lda #0
@blank:
    ora (TEXT),y
    iny
    bne @blank
    inc TEXT+1
    ldx TEXT+1
    cpx #$B0
    bne @blank
    cmp #0
    bne @invalid
    jsr FS_FORMAT
    print initialized
    jmp prompt
@invalid:
    print invalid_fs
prompt:
    print prompt_text
    jsr readline
    jsr dispatch
    jmp prompt

putc:
    pha
@wait:
    bit $D012
    bmi @wait
    sta $D012
    pla
    rts

newline:
    lda #13
    jmp putc

puts:
    ldy #0
@loop:
    lda (TEXT),y
    beq @done
    jsr putc
    inc TEXT
    bne @loop
    inc TEXT+1
    jmp @loop
@done:
    rts

readline:
    ldx #0
@read:
    lda $D011
    bpl @read
    lda $D010
    and #$7F
    cmp #13
    beq @done
    cmp #3
    beq @cancel
    cmp #27
    beq @cancel
    cmp #'_'
    beq @erase
    cmp #8
    beq @erase
    cmp #127
    beq @erase
    cmp #32
    bcc @read
    cmp #127
    bcs @read
    cmp #'a'
    bcc @store
    cmp #'z'+1
    bcs @store
    and #$DF
@store:
    cpx #239
    beq @overflow
    sta LINE,x
    inx
    jsr putc
    jmp @read
@erase:
    cpx #0
    beq @read
    dex
    lda #'_'
    jsr putc
    jmp @read
@cancel:
    ldx #0
    jmp @done
@overflow:
    lda $D011
    bpl @overflow
    lda $D010
    and #$7F
    cmp #13
    bne @overflow
    jsr newline
    print line_long
    ldx #0
@done:
    lda #0
    sta LINE,x
    stx LINE_SIZE
    jmp newline

skip_spaces:
    lda LINE,x
    cmp #' '
    bne @done
    inx
    bne skip_spaces
@done:
    rts

dispatch:
    ldx #0
    jsr skip_spaces
    lda LINE,x
    bne @command
    rts
@command:
    stx COMMAND
    lda #>LINE
    sta COMMAND+1
@scan:
    lda LINE,x
    beq @argument
    cmp #' '
    beq @split
    inx
    bne @scan
@split:
    lda #0
    sta LINE,x
    inx
    jsr skip_spaces
@argument:
    stx ARG
    lda #>LINE
    sta ARG+1
    lda #0
    sta TABLE_INDEX
@entry:
    ldx TABLE_INDEX
    lda commands,x
    sta TEXT
    lda commands+1,x
    sta TEXT+1
    ora TEXT
    beq @unknown
    ldy #0
@compare:
    lda (TEXT),y
    cmp (COMMAND),y
    bne @next
    cmp #0
    beq @matched
    iny
    bne @compare
@matched:
    lda commands+2,x
    sta HANDLER
    lda commands+3,x
    sta HANDLER+1
    jmp (HANDLER)
@next:
    txa
    clc
    adc #4
    sta TABLE_INDEX
    jmp @entry
@unknown:
    print unknown
    rts

cmd_help:
    print help_text
    rts
cmd_echo:
    lda ARG
    sta TEXT
    lda ARG+1
    sta TEXT+1
    jsr puts
    jmp newline
cmd_mem:
    print memory_text
    rts

parse_filename:
    ldy #0
@copy:
    lda (ARG),y
    beq @end
    cmp #' '
    beq @end
    cpy #8
    beq @bad
    sta FILENAME,y
    iny
    bne @copy
@end:
    cpy #0
    beq @bad
    lda #0
    sta FILENAME,y
    tya
    clc
    adc ARG
    tax
    jsr skip_spaces
    stx ARG
    lda #<FILENAME
    sta NAME
    lda #>FILENAME
    sta NAME+1
    clc
    rts
@bad:
    lda #3
    sec
    rts

report:
    beq @done
    pha
    print error_text
    pla
    clc
    adc #'0'
    jsr putc
    jsr newline
@done:
    rts

cmd_write:
    jsr parse_filename
    bcs report
    lda ARG
    sta DATA
    lda ARG+1
    sta DATA+1
    lda LINE_SIZE
    sec
    sbc ARG
    sta LENGTH
    jsr FS_PUT
    jmp report

single_filename:
    jsr parse_filename
    bcs @done
    ldy #0
    lda (ARG),y
    beq @done
    lda #3
    sec
@done:
    rts

cmd_cat:
    jsr single_filename
    bcs report
    lda #<CONTENTS
    sta DATA
    lda #>CONTENTS
    sta DATA+1
    jsr FS_GET
    bcs report
    ldx #0
@loop:
    cpx LENGTH
    beq @done
    lda CONTENTS,x
    jsr putc
    inx
    bne @loop
@done:
    jmp newline

cmd_rm:
    jsr single_filename
    bcs @error
    jsr FS_REMOVE
@error:
    jmp report

cmd_format:
    ldy #0
@compare:
    lda (ARG),y
    cmp yes_text,y
    bne @confirm
    cmp #0
    beq @format
    iny
    bne @compare
@format:
    jsr FS_FORMAT
    print initialized
    rts
@confirm:
    print confirm_text
    rts

cmd_ls:
    jsr FS_MOUNT
    bcc @start
    jmp report
@start:
    lda #0
    sta LIST_INDEX
@slot:
    lda LIST_INDEX
    jsr FS_INFO
    bcs @next
    lda LENGTH
    sta FILE_SIZE
    ldy #0
@name:
    lda (ENTRY),y
    beq @size
    jsr putc
    iny
    cpy #8
    bne @name
@size:
    lda #' '
    jsr putc
    lda FILE_SIZE
    jsr decimal
    jsr newline
@next:
    inc LIST_INDEX
    lda LIST_INDEX
    cmp #15
    bne @slot
    rts

decimal:
    ldx #'0'-1
@hundreds:
    inx
    sec
    sbc #100
    bcs @hundreds
    adc #100
    pha
    txa
    jsr putc
    pla
    ldx #'0'-1
@tens:
    inx
    sec
    sbc #10
    bcs @tens
    adc #10
    pha
    txa
    jsr putc
    pla
    clc
    adc #'0'
    jmp putc

.segment "RODATA"
banner: .byte 13,"A1OS",13,0
prompt_text: .byte "> ",0
initialized: .byte "RAMFS FORMATTED",13,0
invalid_fs: .byte "RAMFS INVALID; FORMAT YES TO ERASE",13,0
unknown: .byte "UNKNOWN COMMAND",13,0
line_long: .byte "LINE TOO LONG",13,0
help_text: .byte "HELP ECHO MEM LS WRITE CAT RM FORMAT",13,"WRITE NAME TEXT / FORMAT YES",13,0
memory_text: .byte "RAM 24K; RAMFS 4K; 15 X 240 BYTES",13,0
error_text: .byte "ERROR ",0
confirm_text: .byte "FORMAT YES TO ERASE RAMFS",13,0
yes_text: .byte "YES",0
help_name: .byte "HELP",0
echo_name: .byte "ECHO",0
mem_name: .byte "MEM",0
ls_name: .byte "LS",0
write_name: .byte "WRITE",0
cat_name: .byte "CAT",0
rm_name: .byte "RM",0
format_name: .byte "FORMAT",0
commands:
    .word help_name, cmd_help, echo_name, cmd_echo, mem_name, cmd_mem
    .word ls_name, cmd_ls, write_name, cmd_write, cat_name, cmd_cat
    .word rm_name, cmd_rm, format_name, cmd_format, 0

.segment "VECTORS"
    .word reset, reset, reset