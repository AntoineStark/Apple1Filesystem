.setcpu "6502"
.macpack longbranch

COMMAND = $20
TEXT = $22
ARG = $24
HANDLER = $26
LINE_SIZE = $28
TABLE_INDEX = $29
LIST_INDEX = $2A
FILE_SIZE = $2B
CAPTURE = $2C
OUT_SIZE = $2D
IN_SIZE = $2E
IN_POS = $2F
STATUS = $30
STAGES = $31
STAGE = $32
REDIRECT = $33
CHECK_ONLY = $34
OUT_PTR = $35
IN_PTR = $37
SCAN = $39
BEGIN = $3A
REDIR_NAME = $3B
PATH_COUNT = $3C
OLD_CWD = $3D
STAGE_BEGIN = $0700
STAGE_END = $0710
DESTINATION = $0720
PIPE_A = $0800
PIPE_B = $0900
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
FS_MKDIR = $2015
FS_RMDIR = $2018
FS_CHDIR = $201B
FS_PWD = $201E
CWD = $4C

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
    sta CAPTURE
    lda #1
    sta CWD
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
    cpx #$D0
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
    jsr execute_line
    jmp prompt

putc:
    pha
    lda CAPTURE
    beq @console
    txa
    pha
    tya
    pha
    ldy OUT_SIZE
    cpy #240
    bcc @store
    lda #8
    sta STATUS
    jmp @restore
@store:
    tsx
    lda $0103,x
    sta (OUT_PTR),y
    inc OUT_SIZE
@restore:
    pla
    tay
    pla
    tax
    pla
    rts
@console:
    pla
console_putc:
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

stream_get:
    ldy IN_POS
    cpy IN_SIZE
    beq @eof
    lda (IN_PTR),y
    inc IN_POS
    clc
    rts
@eof:
    sec
    rts

execute_line:
    lda #0
    sta CAPTURE
    sta STATUS
    sta STAGES
    sta STAGE
    sta REDIRECT
    sta IN_SIZE
    sta CHECK_ONLY
    ldx #0
    jsr skip_spaces
    lda LINE,x
    bne @parse
    rts
@parse:
    stx BEGIN
@scan:
    lda LINE,x
    beq @last
    cmp #'|'
    beq @pipe
    cmp #'>'
    beq @redirect
    cmp #'<'
    jeq syntax_error
    cmp #'"'
    jeq syntax_error
    cmp #39
    jeq syntax_error
    inx
    bne @scan
@pipe:
    jsr save_stage
    jcs syntax_error
    inx
    jsr skip_spaces
    lda LINE,x
    jeq syntax_error
    jmp @parse
@redirect:
    jsr save_stage
    jcs syntax_error
    lda #1
    sta REDIRECT
    inx
    jsr skip_spaces
    stx REDIR_NAME
    stx ARG
    lda #>LINE
    sta ARG+1
    jsr single_filename
    jcs syntax_error
    jsr valid_filename
    jcs syntax_error
    ldy #0
@destination:
    lda FILENAME,y
    sta DESTINATION,y
    beq @validate
    iny
    bne @destination
@last:
    jsr save_stage
    jcs syntax_error
@validate:
    lda #1
    sta CHECK_ONLY
@check:
    jsr prepare_stage
    jsr dispatch
    lda STATUS
    bne @abort
    inc STAGE
    lda STAGE
    cmp STAGES
    bne @check
    lda #0
    sta CHECK_ONLY
    sta STAGE
    lda #<PIPE_A
    sta OUT_PTR
    sta IN_PTR
    lda #>PIPE_A
    sta OUT_PTR+1
    lda #>PIPE_B
    sta IN_PTR+1
@run:
    lda #0
    sta OUT_SIZE
    sta IN_POS
    sta CAPTURE
    lda STAGE
    clc
    adc #1
    cmp STAGES
    bne @capture
    lda REDIRECT
    beq @dispatch
@capture:
    lda #1
    sta CAPTURE
@dispatch:
    jsr prepare_stage
    jsr dispatch
    lda STATUS
    bne @abort
    inc STAGE
    lda STAGE
    cmp STAGES
    beq @commit
    lda OUT_SIZE
    sta IN_SIZE
    lda IN_PTR+1
    ldx OUT_PTR+1
    sta OUT_PTR+1
    stx IN_PTR+1
    jmp @run
@commit:
    lda REDIRECT
    beq @done
    lda #<DESTINATION
    sta NAME
    lda #>DESTINATION
    sta NAME+1
    lda OUT_PTR
    sta DATA
    lda OUT_PTR+1
    sta DATA+1
    lda OUT_SIZE
    sta LENGTH
    jsr FS_PUT
    sta STATUS
@abort:
    lda #0
    sta CAPTURE
    sta CHECK_ONLY
    lda STATUS
    cmp #7
    bne @report
    print unknown
    rts
@report:
    jmp report
@done:
    lda #0
    sta CAPTURE
    rts

syntax_error:
    print syntax_text
    rts

save_stage:
    stx SCAN
    cpx BEGIN
    beq @bad
    dex
@trim:
    lda LINE,x
    cmp #' '
    bne @save
    cpx BEGIN
    beq @bad
    dex
    jmp @trim
@save:
    inx
    ldy STAGES
    cpy #8
    beq @bad
    txa
    sta STAGE_END,y
    lda #0
    sta LINE,x
    lda BEGIN
    sta STAGE_BEGIN,y
    inc STAGES
    ldx SCAN
    clc
    rts
@bad:
    ldx SCAN
    sec
    rts

prepare_stage:
    ldx STAGE
    lda STAGE_END,x
    sta LINE_SIZE
    lda STAGE_BEGIN,x
    tax
    rts

valid_filename:
    ldy #0
    lda #0
    sta PATH_COUNT
@loop:
    lda FILENAME,y
    beq @ok
    cmp #'/'
    beq @slash
    inc PATH_COUNT
    ldx PATH_COUNT
    cpx #9
    bcs @bad
    cmp #'A'
    bcc @digit
    cmp #'Z'+1
    bcc @next
@digit:
    cmp #'0'
    bcc @punct
    cmp #'9'+1
    bcc @next
@punct:
    cmp #'.'
    beq @next
    cmp #'_'
    beq @next
    cmp #'-'
    bne @bad
@next:
    iny
    bne @loop
@slash:
    lda #0
    sta PATH_COUNT
    beq @next
@ok:
    clc
    rts
@bad:
    sec
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
    bne @letter
    lda (COMMAND),y
    beq @matched
    cmp #' '
    beq @matched
    bne @next
@letter:
    cmp (COMMAND),y
    bne @next
    iny
    bne @compare
@matched:
    lda CHECK_ONLY
    beq @valid
    jsr check_arguments
    bcc @checked
    sta STATUS
    rts
@valid:
    ldx TABLE_INDEX
    lda commands+2,x
    sta HANDLER
    lda commands+3,x
    sta HANDLER+1
    jmp (HANDLER)
@checked:
    rts
@next:
    txa
    clc
    adc #4
    sta TABLE_INDEX
    jmp @entry
@unknown:
    lda #7
    sta STATUS
    rts

check_arguments:
    ldy #0
    ldx TABLE_INDEX
    cpx #4
    beq @ok
    cpx #16
    beq @write
    cpx #20
    beq @cat
    cpx #24
    beq @mutate
    cpx #28
    beq @mutate
    cpx #32
    beq @mutate
    cpx #36
    beq @mutate
    cpx #40
    beq @mutate
    cpx #12
    beq @ls
    lda (ARG),y
    bne @invalid
    beq @ok
@mutate:
    lda STAGES
    cmp #1
    bne @invalid
    lda REDIRECT
    bne @invalid
    cpx #28
    beq @ok
    cpx #40
    bne @single
    lda (ARG),y
    beq @ok
    jmp @single
@ls:
    lda (ARG),y
    beq @ok
    jmp @single
@cat:
    lda (ARG),y
    bne @single
    lda STAGE
    beq @invalid
    bne @ok
@write:
    lda REDIRECT
    bne @invalid
    lda STAGE
    clc
    adc #1
    cmp STAGES
    bne @invalid
    jsr parse_filename
    bcs @invalid
    jsr valid_filename
    bcs @invalid
    lda STAGE
    beq @ok
    ldy #0
    lda (ARG),y
    bne @invalid
    beq @ok
@single:
    jsr single_filename
    bcs @invalid
    jsr valid_filename
    bcs @invalid
@ok:
    clc
    rts
@invalid:
    lda #3
    sec
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
    cpy #95
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
    cmp #0
    beq @done
    sta STATUS
    lda #0
    sta CAPTURE
    lda STATUS
    pha
    print error_text
    pla
    cmp #10
    bcc @digit
    sec
    sbc #10
    pha
    lda #'1'
    jsr putc
    pla
@digit:
    clc
    adc #'0'
    jsr putc
    jsr newline
@done:
    rts

cmd_write:
    jsr parse_filename
    jcs command_error
    lda STAGE
    beq @literal
    lda IN_PTR
    sta DATA
    lda IN_PTR+1
    sta DATA+1
    lda IN_SIZE
    sta LENGTH
    jmp @put
@literal:
    lda ARG
    sta DATA
    lda ARG+1
    sta DATA+1
    lda LINE_SIZE
    sec
    sbc ARG
    sta LENGTH
@put:
    jsr FS_PUT
    sta STATUS
    rts

command_error:
    sta STATUS
    rts

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
    ldy #0
    lda (ARG),y
    bne @file
@input:
    jsr stream_get
    bcs @done_input
    jsr putc
    jmp @input
@done_input:
    rts
@file:
    jsr single_filename
    jcs command_error
    lda #<CONTENTS
    sta DATA
    lda #>CONTENTS
    sta DATA+1
    jsr FS_GET
    jcs command_error
    ldx #0
@loop:
    cpx LENGTH
    beq @done
    lda CONTENTS,x
    jsr putc
    inx
    bne @loop
@done:
    lda CAPTURE
    bne @return
    jmp newline
@return:
    rts

cmd_rm:
    jsr single_filename
    bcs @error
    jsr FS_REMOVE
@error:
    jmp command_error

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
    jmp command_error
@start:
    lda CWD
    sta OLD_CWD
    ldy #0
    lda (ARG),y
    beq @list
    jsr single_filename
    jcs command_error
    jsr FS_CHDIR
    jcs command_error
@list:
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
    ldy #9
    lda (ENTRY),y
    cmp #2
    bne @file
    lda #'/'
    jsr putc
    jmp @newline
@file:
    lda #' '
    jsr putc
    lda FILE_SIZE
    jsr decimal
@newline:
    jsr newline
@next:
    inc LIST_INDEX
    lda LIST_INDEX
    cmp #16
    bne @slot
    lda OLD_CWD
    sta CWD
    rts

cmd_mkdir:
    jsr single_filename
    jcs command_error
    jsr FS_MKDIR
    jmp command_error

cmd_rmdir:
    jsr single_filename
    jcs command_error
    jsr FS_RMDIR
    jmp command_error

cmd_cd:
    ldy #0
    lda (ARG),y
    bne @path
    lda #1
    sta CWD
    rts
@path:
    jsr single_filename
    jcs command_error
    jsr FS_CHDIR
    jmp command_error

cmd_pwd:
    lda #<CONTENTS
    sta DATA
    lda #>CONTENTS
    sta DATA+1
    jsr FS_PWD
    jcs command_error
    lda #<CONTENTS
    sta TEXT
    lda #>CONTENTS
    sta TEXT+1
    jsr puts
    jmp newline

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
syntax_text: .byte "SYNTAX ERROR",13,0
line_long: .byte "LINE TOO LONG",13,0
help_text: .byte "HELP ECHO MEM LS WRITE CAT RM FORMAT",13,"MKDIR RMDIR CD PWD",13,"ECHO TEXT > PATH",13,"CAT PATH | WRITE COPY",13,"FORMAT YES ERASES RAMFS",13,0
memory_text: .byte "RAM 24K; RAMFS 12K; FILES 240 BYTES",13,0
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
mkdir_name: .byte "MKDIR",0
rmdir_name: .byte "RMDIR",0
cd_name: .byte "CD",0
pwd_name: .byte "PWD",0
commands:
    .word help_name, cmd_help, echo_name, cmd_echo, mem_name, cmd_mem
    .word ls_name, cmd_ls, write_name, cmd_write, cat_name, cmd_cat
    .word rm_name, cmd_rm, format_name, cmd_format
    .word mkdir_name, cmd_mkdir, rmdir_name, cmd_rmdir, cd_name, cmd_cd
    .word pwd_name, cmd_pwd, 0

.segment "VECTORS"
    .word reset, reset, reset