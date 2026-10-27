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
PIPE_A = $6000
PIPE_B = $6400
OUT_HIGH = $64
IN_HIGH = $65
POS_HIGH = $66
STREAM_PTR = $68
FILTER_LEFT = $6A
MATCH_PTR = $6C
FILTER_PTR = $6E
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
FS_FIND = $2006
FS_MOVE = $2021
FS_PUT_WIDE = $2024
FS_GET_WIDE = $2027
LENGTH_HI = $57
LARGE_BUFFER = $1000
LARGE_PTR = $60
LARGE_LEFT = $62
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
    lda #<CONTENTS
    sta DATA
    lda #>CONTENTS
    sta DATA+1
    jsr FS_PWD
    bcs @invalid
    lda #<CONTENTS
    sta TEXT
    lda #>CONTENTS
    sta TEXT+1
    jsr puts
    jmp @suffix
@invalid:
    lda #'?'
    jsr putc
@suffix:
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
    lda OUT_HIGH
    cmp #4
    bcc @store
    lda #8
    sta STATUS
    jmp @restore
@store:
    lda OUT_PTR
    sta STREAM_PTR
    lda OUT_PTR+1
    clc
    adc OUT_HIGH
    sta STREAM_PTR+1
    tsx
    lda $0103,x
    sta (STREAM_PTR),y
    inc OUT_SIZE
    bne @restore
    inc OUT_HIGH
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
    lda POS_HIGH
    cmp IN_HIGH
    bne @read
    ldy IN_POS
    cpy IN_SIZE
    beq @eof
@read:
    lda IN_PTR
    sta STREAM_PTR
    lda IN_PTR+1
    clc
    adc POS_HIGH
    sta STREAM_PTR+1
    ldy IN_POS
    lda (STREAM_PTR),y
    pha
    inc IN_POS
    bne @value
    inc POS_HIGH
@value:
    pla
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
    sta IN_HIGH
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
    lda LINE,x
    cmp #'>'
    bne @destination_start
    inc REDIRECT
    inx
@destination_start:
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
    jne @abort
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
    sta OUT_HIGH
    sta IN_POS
    sta POS_HIGH
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
    lda OUT_HIGH
    sta IN_HIGH
    lda IN_PTR+1
    ldx OUT_PTR+1
    sta OUT_PTR+1
    stx IN_PTR+1
    jmp @run
@commit:
    lda REDIRECT
    beq @done
    cmp #2
    bne @replace
    jsr append_output
    sta STATUS
    jmp @abort
@replace:
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
    lda OUT_HIGH
    sta LENGTH_HI
    jsr FS_PUT_WIDE
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

append_output:
    lda #<DESTINATION
    sta NAME
    lda #>DESTINATION
    sta NAME+1
    jsr load_large
    bcc @size
    cmp #1
    bne @return
    lda #0
    sta LENGTH
    sta LENGTH_HI
@size:
    lda LENGTH
    clc
    adc OUT_SIZE
    sta $0C10
    lda LENGTH_HI
    adc OUT_HIGH
    sta $0C11
    cmp #16
    bcc @fits
    bne @overflow
    lda $0C10
    bne @overflow
@fits:
    lda LENGTH
    sta LARGE_PTR
    lda LENGTH_HI
    clc
    adc #>LARGE_BUFFER
    sta LARGE_PTR+1
    lda OUT_PTR
    sta MATCH_PTR
    lda OUT_PTR+1
    sta MATCH_PTR+1
    lda OUT_SIZE
    sta FILTER_LEFT
    lda OUT_HIGH
    sta FILTER_LEFT+1
    ldy #0
@copy:
    lda FILTER_LEFT
    ora FILTER_LEFT+1
    beq @write
    lda (MATCH_PTR),y
    sta (LARGE_PTR),y
    inc MATCH_PTR
    bne @dest
    inc MATCH_PTR+1
@dest:
    inc LARGE_PTR
    bne @left
    inc LARGE_PTR+1
@left:
    lda FILTER_LEFT
    bne @low
    dec FILTER_LEFT+1
@low:
    dec FILTER_LEFT
    jmp @copy
@write:
    lda $0C10
    sta LENGTH
    lda $0C11
    sta LENGTH_HI
    jmp FS_PUT_WIDE
@overflow:
    lda #8
    sec
@return:
    rts

load_large:
    lda #<LARGE_BUFFER
    sta DATA
    lda #>LARGE_BUFFER
    sta DATA+1
    jsr FS_FIND
    bcs @return
    lda ENTRY
    cmp #$FF
    beq @type_error
    ldy #9
    lda (ENTRY),y
    cmp #1
    bne @type_error
    ldy #11
    lda (ENTRY),y
    cmp #16
    bcc @load
    bne @too_large
    ldy #8
    lda (ENTRY),y
    bne @too_large
@load:
    lda #<LARGE_BUFFER
    sta DATA
    lda #>LARGE_BUFFER
    sta DATA+1
    jmp FS_GET_WIDE
@type_error:
    lda #9
    sec
    rts
@too_large:
    lda #4
    sec
@return:
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
    jeq @ok
    cpx #56
    beq @wc
    cpx #60
    beq @grep
    cpx #48
    jeq @transfer
    cpx #52
    jeq @transfer
    cpx #16
    jeq @write
    cpx #20
    jeq @cat
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
    jeq @ls
    lda (ARG),y
    jne @invalid
    jeq @ok
@mutate:
    lda STAGES
    cmp #1
    jne @invalid
    lda REDIRECT
    jne @invalid
    cpx #28
    jeq @ok
    cpx #40
    jne @single
    lda (ARG),y
    jeq @ok
    jmp @single
@wc:
    lda (ARG),y
    jne @single
    lda STAGE
    jeq @invalid
    jmp @ok
@grep:
    lda STAGE
    bne @pipe_pattern
    jsr parse_filename
    jcs @invalid
    jmp @single
@pipe_pattern:
    lda (ARG),y
    jeq @invalid
    jmp @ok
@transfer:
    lda STAGES
    cmp #1
    jne @invalid
    lda REDIRECT
    jne @invalid
    jsr parse_filename
    jcs @invalid
    jsr valid_filename
    jcs @invalid
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
    lda IN_HIGH
    sta LENGTH_HI
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
    lda STAGE
    bne @wide
    lda #0
    sta LENGTH_HI
@wide:
    jsr FS_PUT_WIDE
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
    jmp @large
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
@large:
    jsr load_large
    jcs command_error
    lda DATA
    sta LARGE_PTR
    lda DATA+1
    sta LARGE_PTR+1
    lda LENGTH
    sta LARGE_LEFT
    lda LENGTH_HI
    sta LARGE_LEFT+1
    ldy #0
@large_loop:
    lda LARGE_LEFT
    ora LARGE_LEFT+1
    beq @large_done
    lda (LARGE_PTR),y
    jsr putc
    inc LARGE_PTR
    bne @remaining
    inc LARGE_PTR+1
@remaining:
    lda LARGE_LEFT
    bne @low
    dec LARGE_LEFT+1
@low:
    dec LARGE_LEFT
    jmp @large_loop
@large_done:
    lda CAPTURE
    bne @large_return
    jmp newline
@large_return:
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
    lda LENGTH_HI
    sta $0C13
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
    ldx $0C13
    beq @small_size
    sta $0C12
    jsr decimal_wide
    jmp @newline
@small_size:
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

cmd_wc:
    ldy #0
    lda (ARG),y
    beq @input
    jsr single_filename
    jcs command_error
    jsr load_large
    jcs command_error
    lda DATA
    sta IN_PTR
    lda DATA+1
    sta IN_PTR+1
    lda LENGTH
    sta IN_SIZE
    lda LENGTH_HI
    sta IN_HIGH
@input:
    lda #0
    sta IN_POS
    sta POS_HIGH
    sta $0C00
    sta $0C01
    sta $0C02
    sta $0C03
    sta $0C04
    sta $0C06
    sta $0C07
    sta $0C08
@read:
    jsr stream_get
    bcs @print
    inc $0C02
    bne @byte_counted
    inc $0C08
@byte_counted:
    sta $0C05
    cmp #13
    beq @cr
    cmp #10
    bne @other
    lda $0C04
    bne @space
    inc $0C00
    bne @lf_counted
    inc $0C06
@lf_counted:
    jmp @space
@cr:
    inc $0C00
    bne @cr_counted
    inc $0C06
@cr_counted:
    lda #1
    sta $0C04
    lda #0
    sta $0C03
    jmp @read
@other:
    lda #0
    sta $0C04
    lda $0C05
    cmp #32
    beq @space
    cmp #9
    bcc @word
    cmp #14
    bcc @space
@word:
    lda $0C03
    bne @read
    inc $0C01
    bne @word_counted
    inc $0C07
@word_counted:
    lda #1
    sta $0C03
    jmp @read
@space:
    lda #0
    sta $0C03
    sta $0C04
    jmp @read
@print:
    lda $0C00
    ldx $0C06
    jsr filter_decimal
    lda #' '
    jsr putc
    lda $0C01
    ldx $0C07
    jsr filter_decimal
    lda #' '
    jsr putc
    lda $0C02
    ldx $0C08
    jsr filter_decimal
    jmp newline

filter_decimal:
    cpx #0
    bne @wide
    jmp decimal
@wide:
    sta $0C12
    stx $0C13
    jmp decimal_wide

cmd_grep:
    lda STAGE
    bne @pipe
    jsr parse_filename
    jcs command_error
    ldy #0
@file_pattern:
    lda FILENAME,y
    sta $0E00,y
    beq @file_pattern_done
    iny
    bne @file_pattern
@file_pattern_done:
    sty $0C26
    jsr single_filename
    jcs command_error
    jsr load_large
    jcs command_error
    lda DATA
    sta IN_PTR
    lda DATA+1
    sta IN_PTR+1
    lda LENGTH
    sta IN_SIZE
    lda LENGTH_HI
    sta IN_HIGH
    jmp @setup
@pipe:
    ldy #0
@pattern:
    lda (ARG),y
    sta $0E00,y
    beq @pattern_done
    iny
    bne @pattern
@pattern_done:
    sty $0C26
@setup:
    lda IN_PTR
    sta FILTER_PTR
    lda IN_PTR+1
    sta FILTER_PTR+1
    lda IN_SIZE
    sta FILTER_LEFT
    lda IN_HIGH
    sta FILTER_LEFT+1
@line:
    lda FILTER_LEFT
    ora FILTER_LEFT+1
    jeq @done
    lda FILTER_PTR
    sta $0C20
    lda FILTER_PTR+1
    sta $0C21
@end:
    lda FILTER_PTR
    sta $0C22
    lda FILTER_PTR+1
    sta $0C23
    lda FILTER_LEFT
    ora FILTER_LEFT+1
    beq @line_end
    ldy #0
    lda (FILTER_PTR),y
    cmp #13
    beq @delimiter
    cmp #10
    beq @delimiter
    jsr filter_advance
    jmp @end
@delimiter:
    jsr filter_advance
    cmp #13
    bne @line_end
    lda FILTER_LEFT
    ora FILTER_LEFT+1
    beq @line_end
    lda (FILTER_PTR),y
    cmp #10
    bne @line_end
    jsr filter_advance
@line_end:
    lda FILTER_PTR
    sta $0C24
    lda FILTER_PTR+1
    sta $0C25
    lda $0C20
    sta MATCH_PTR
    lda $0C21
    sta MATCH_PTR+1
@candidate:
    lda MATCH_PTR
    clc
    adc $0C26
    sta $0C28
    lda MATCH_PTR+1
    adc #0
    cmp $0C23
    bcc @compare_start
    jne @next
    lda $0C28
    cmp $0C22
    bcc @compare_start
    jne @next
@compare_start:
    ldy #0
@compare:
    lda (MATCH_PTR),y
    cmp $0E00,y
    bne @mismatch
    iny
    cpy $0C26
    bne @compare
    lda $0C20
    sta MATCH_PTR
    lda $0C21
    sta MATCH_PTR+1
@emit:
    lda MATCH_PTR+1
    cmp $0C25
    bne @emit_byte
    lda MATCH_PTR
    cmp $0C24
    beq @next
@emit_byte:
    ldy #0
    lda (MATCH_PTR),y
    jsr putc
    inc MATCH_PTR
    bne @emit
    inc MATCH_PTR+1
    jmp @emit
@mismatch:
    inc MATCH_PTR
    bne @candidate
    inc MATCH_PTR+1
    jmp @candidate
@next:
    jmp @line
@done:
    rts

filter_advance:
    pha
    inc FILTER_PTR
    bne @left
    inc FILTER_PTR+1
@left:
    lda FILTER_LEFT
    bne @low
    dec FILTER_LEFT+1
@low:
    dec FILTER_LEFT
    pla
    rts

cmd_cp:
    lda #0
    sta $0BDA
    jmp transfer_file
cmd_mv:
    lda #1
    sta $0BDA
transfer_file:
    jsr parse_filename
    jcs command_error
    ldy #0
@save_source:
    lda FILENAME,y
    sta $0B00,y
    beq @source
    iny
    bne @save_source
@source:
    jsr FS_FIND
    jcs command_error
    lda ENTRY
    cmp #$FF
    jeq @type_error
    ldy #9
    lda (ENTRY),y
    cmp #1
    jne @type_error
    lda ENTRY
    sta $0BD8
    lda ENTRY+1
    sta $0BD9
    ldy #0
@basename:
    lda (ENTRY),y
    sta $0BC0,y
    iny
    cpy #8
    bne @basename
    lda #0
    sta $0BC8
    jsr single_filename
    jcs command_error
    ldy #0
@save_target:
    lda FILENAME,y
    sta $0B60,y
    beq @target
    iny
    bne @save_target
@target:
    jsr FS_FIND
    bcc @exists
    cmp #1
    jne command_error
    jmp @copy
@exists:
    lda ENTRY
    cmp #$FF
    beq @directory
    ldy #9
    lda (ENTRY),y
    cmp #2
    beq @directory
    jmp @same
@directory:
    ldy #0
@length:
    lda $0B60,y
    beq @append
    iny
    bne @length
@append:
    cpy #95
    jcs @path_error
    lda #'/'
    sta $0B60,y
    iny
    ldx #0
@append_name:
    cpy #96
    jcs @path_error
    lda $0BC0,x
    sta $0B60,y
    beq @resolve_target
    iny
    inx
    bne @append_name
@resolve_target:
    lda #$60
    sta NAME
    lda #$0B
    sta NAME+1
    jsr FS_FIND
    bcc @same
    cmp #1
    jne command_error
    jmp @copy
@same:
    lda ENTRY
    cmp $0BD8
    bne @copy
    lda ENTRY+1
    cmp $0BD9
    bne @copy
    rts
@copy:
    lda #0
    sta NAME
    lda #$0B
    sta NAME+1
    lda $0BDA
    bne @move
    jsr load_large
    jcs command_error
    lda #$60
    sta NAME
    lda #$0B
    sta NAME+1
    jsr FS_PUT_WIDE
    jmp command_error
@move:
    lda #$60
    sta DATA
    lda #$0B
    sta DATA+1
    jsr FS_MOVE
    jmp command_error
@type_error:
    lda #9
    jmp command_error
@path_error:
    lda #3
    jmp command_error

decimal_wide:
    lda #<divisors
    sta LARGE_PTR
    lda #>divisors
    sta LARGE_PTR+1
    lda #5
    sta $0C14
@digit:
    ldx #'0'
@subtract:
    ldy #0
    lda $0C12
    sec
    sbc (LARGE_PTR),y
    sta $0C15
    iny
    lda $0C13
    sbc (LARGE_PTR),y
    bcc @emit
    sta $0C13
    lda $0C15
    sta $0C12
    inx
    jmp @subtract
@emit:
    txa
    jsr putc
    inc LARGE_PTR
    bne @increment
    inc LARGE_PTR+1
@increment:
    inc LARGE_PTR
    bne @next
    inc LARGE_PTR+1
@next:
    dec $0C14
    bne @digit
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
syntax_text: .byte "SYNTAX ERROR",13,0
line_long: .byte "LINE TOO LONG",13,0
help_text:
    .byte "HELP - SHOW COMMANDS",13
    .byte "ECHO TEXT - PRINT TEXT",13
    .byte "MEM - SHOW MEMORY BUDGET",13
    .byte "LS [PATH] - LIST DIRECTORY",13
    .byte "WRITE FILE [TEXT] - SAVE TEXT",13
    .byte "CAT [FILE] - PRINT FILE OR PIPE",13
    .byte "RM FILE - REMOVE FILE",13
    .byte "MKDIR PATH - CREATE DIRECTORY",13
    .byte "RMDIR PATH - REMOVE EMPTY DIRECTORY",13
    .byte "CD [PATH] - CHANGE DIRECTORY",13
    .byte "PWD - PRINT CURRENT DIRECTORY",13
    .byte "CP SRC DEST - COPY FILE",13
    .byte "MV SRC DEST - MOVE OR RENAME FILE",13
    .byte "WC [FILE] - COUNT LINES WORDS BYTES",13
    .byte "GREP TEXT [FILE] - FILTER LINES",13
    .byte "FORMAT YES - ERASE RAM FILESYSTEM",13
    .byte "> FILE REPLACE / >> FILE APPEND",13
    .byte "| SEND OUTPUT TO NEXT COMMAND",13,0
memory_text: .byte "RAM 24K; RAMFS 12K; CHAINED FILES",13,0
divisors: .word 10000,1000,100,10,1
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
cp_name: .byte "CP",0
mv_name: .byte "MV",0
wc_name: .byte "WC",0
grep_name: .byte "GREP",0
commands:
    .word help_name, cmd_help, echo_name, cmd_echo, mem_name, cmd_mem
    .word ls_name, cmd_ls, write_name, cmd_write, cat_name, cmd_cat
    .word rm_name, cmd_rm, format_name, cmd_format
    .word mkdir_name, cmd_mkdir, rmdir_name, cmd_rmdir, cd_name, cmd_cd
    .word pwd_name, cmd_pwd, cp_name, cmd_cp, mv_name, cmd_mv
    .word wc_name, cmd_wc, grep_name, cmd_grep, 0

.segment "VECTORS"
    .word reset, reset, reset