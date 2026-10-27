.setcpu "6502"
.macpack longbranch
NAME=$40
DATA=$42
LENGTH=$44
ENTRY=$45
SLOT=$47
INDEX=$48
SAVED=$49
OTHER=$4A
CWD=$4C
DIR=$4D
POS=$4E
OFFSET=$4F
FREE=$50
MODE=$51
TEMP=$52
VALID_DIR=$54
VALID_OFFSET=$55
REF=$56
LENGTH_HI=$57
COPY_PTR=$58
REMAIN=$5A
BLOCKS=$5C
OLD_FIRST=$5D
CHAIN_INDEX=$5E
NEXT=$A090
CHAIN=$0D00
COMPONENT=$0A40
SEEN=$0A00
BASE=$A000
TYPES=$A010
PARENTS=$A050

.segment "CODE"
    jmp mount
    jmp format
    jmp find
    jmp put
    jmp get
    jmp remove
    jmp info
    jmp mkdir
    jmp rmdir
    jmp chdir
    jmp cwd_path
    jmp move_file
    jmp put_wide
    jmp get_wide

ok:
    lda #0
    clc
    rts
failure:
    sec
    rts
bad_fs:
    lda #5
    jmp failure
invalid:
    lda #3
    jmp failure
missing:
    lda #1
    jmp failure
full:
    lda #2
    jmp failure
wrong_type:
    lda #9
    jmp failure

pointer:
    lda #0
    sta ENTRY
    lda DIR
    clc
    adc #$A0
    sta ENTRY+1
    rts

valid_char:
    cmp #'A'
    bcc @digit
    cmp #'Z'+1
    bcc @ok
@digit:
    cmp #'0'
    bcc @punct
    cmp #'9'+1
    bcc @ok
@punct:
    cmp #'.'
    beq @ok
    cmp #'_'
    beq @ok
    cmp #'-'
    beq @ok
    sec
    rts
@ok:
    clc
    rts

mount:
    cld
    ldx #4
@signature:
    lda BASE,x
    cmp signature,x
    jne bad_fs
    dex
    bpl @signature
    lda TYPES
    cmp #3
    jne bad_fs
    lda TYPES+1
    cmp #2
    jne bad_fs
    lda PARENTS+1
    cmp #1
    jne bad_fs
    ldx #47
    lda #0
@clear:
    sta SEEN,x
    dex
    bpl @clear
    lda #1
    sta SEEN
    sta SEEN+1
    sta VALID_DIR
@directory:
    ldx VALID_DIR
    lda TYPES,x
    cmp #3
    jcs bad_fs
    cmp #2
    jne @next_dir
    lda #0
    sta VALID_OFFSET
@entry:
    lda VALID_DIR
    sta DIR
    jsr pointer
    ldy VALID_OFFSET
    lda (ENTRY),y
    jeq @next_entry
    ldx #0
@name:
    lda (ENTRY),y
    beq @padding
    jsr valid_char
    jcs bad_fs
    inx
    iny
    cpx #8
    bne @name
    beq @metadata
@padding:
    inx
    iny
    cpx #8
    beq @metadata
    lda (ENTRY),y
    jne bad_fs
    beq @padding
@metadata:
    ldy VALID_OFFSET
    lda (ENTRY),y
    cmp #'.'
    bne @fields
    iny
    lda (ENTRY),y
    jeq bad_fs
    cmp #'.'
    bne @fields
    iny
    lda (ENTRY),y
    jeq bad_fs
@fields:
    lda VALID_OFFSET
    clc
    adc #8
    tay
    lda (ENTRY),y
    sta TEMP
    lda VALID_OFFSET
    clc
    adc #11
    tay
    lda (ENTRY),y
    sta REMAIN+1
    cmp #47
    jcs bad_fs
    lda TEMP
    sta REMAIN
    lda VALID_OFFSET
    clc
    adc #9
    tay
    lda (ENTRY),y
    cmp #1
    beq @type_ok
    cmp #2
    jne bad_fs
    lda TEMP
    ora REMAIN+1
    jne bad_fs
@type_ok:
    lda (ENTRY),y
    sta MODE
    iny
    lda (ENTRY),y
    beq @empty
    cmp #2
    jcc bad_fs
    cmp #48
    jcs bad_fs
    tax
    lda SEEN,x
    jne bad_fs
    inc SEEN,x
    lda TYPES,x
    cmp MODE
    jne bad_fs
    lda MODE
    cmp #2
    bne @file_chain
    lda PARENTS,x
    cmp VALID_DIR
    jne bad_fs
    lda NEXT,x
    jne bad_fs
    jmp @duplicates
@empty:
    lda MODE
    cmp #1
    jne bad_fs
    lda TEMP
    ora REMAIN+1
    jne bad_fs
    jmp @duplicates
@file_chain:
    lda REMAIN
    ora REMAIN+1
    jeq bad_fs
    lda REMAIN+1
    beq @last_block
    dec REMAIN+1
    lda REMAIN
    ora REMAIN+1
    beq @last_link
    lda NEXT,x
    cmp #2
    jcc bad_fs
    cmp #48
    jcs bad_fs
    tax
    lda TYPES,x
    cmp #1
    jne bad_fs
    lda SEEN,x
    jne bad_fs
    inc SEEN,x
    jmp @file_chain
@last_block:
    lda REMAIN
    beq @last_link
@last_link:
    lda NEXT,x
    jne bad_fs
@duplicates:
    lda VALID_OFFSET
    clc
    adc #16
    sta OFFSET
    beq @next_entry
@duplicate:
    ldx #0
@compare:
    txa
    clc
    adc VALID_OFFSET
    tay
    lda (ENTRY),y
    sta TEMP
    txa
    clc
    adc OFFSET
    tay
    lda (ENTRY),y
    cmp TEMP
    bne @different
    inx
    cpx #8
    bne @compare
    jmp bad_fs
@different:
    lda OFFSET
    clc
    adc #16
    sta OFFSET
    bne @duplicate
@next_entry:
    lda VALID_OFFSET
    clc
    adc #16
    sta VALID_OFFSET
    jne @entry
@next_dir:
    inc VALID_DIR
    lda VALID_DIR
    cmp #48
    jne @directory
    ldx #2
@allocated:
    lda TYPES,x
    beq @free
    cmp #3
    jcs bad_fs
    lda SEEN,x
    cmp #1
    jne bad_fs
    lda TYPES,x
    cmp #2
    bne @advance
    stx REF
    ldy #15
@ancestors:
    lda PARENTS,x
    cmp #1
    beq @root
    cmp #2
    jcc bad_fs
    cmp #48
    jcs bad_fs
    tax
    lda TYPES,x
    cmp #2
    jne bad_fs
    dey
    bne @ancestors
    jmp bad_fs
@root:
    ldx REF
    jmp @advance
@free:
    lda SEEN,x
    jne bad_fs
@advance:
    inx
    cpx #48
    bne @allocated
    ldx CWD
    cpx #1
    jcc bad_fs
    cpx #48
    jcs bad_fs
    lda TYPES,x
    cmp #2
    jne bad_fs
    jmp ok

format:
    cld
    lda #0
    sta ENTRY
    lda #$A0
    sta ENTRY+1
    ldy #0
    lda #0
@clear:
    sta (ENTRY),y
    iny
    bne @clear
    inc ENTRY+1
    ldx ENTRY+1
    cpx #$D0
    bne @clear
    ldx #4
@header:
    lda signature,x
    sta BASE,x
    dex
    bpl @header
    lda #3
    sta TYPES
    lda #2
    sta TYPES+1
    lda #1
    sta PARENTS+1
    sta CWD
    jmp ok

component:
    ldx #0
@read:
    ldy POS
    lda (NAME),y
    beq @done
    cmp #'/'
    beq @done
    cpx #8
    jcs invalid
    jsr valid_char
    jcs invalid
    sta COMPONENT,x
    inx
    inc POS
    lda POS
    jeq invalid
    jmp @read
@done:
    cpx #0
    jeq invalid
    stx INDEX
    lda #0
@pad:
    sta COMPONENT,x
    inx
    cpx #8
    bcc @pad
    jmp ok

lookup:
    jsr pointer
    lda #0
    sta OFFSET
@entry:
    ldx #0
@compare:
    txa
    clc
    adc OFFSET
    tay
    lda (ENTRY),y
    cmp COMPONENT,x
    bne @next
    inx
    cpx #8
    bne @compare
    lda OFFSET
    sta ENTRY
    ldy #10
    lda (ENTRY),y
    sta SLOT
    jmp ok
@next:
    lda OFFSET
    clc
    adc #16
    sta OFFSET
    bne @entry
    jmp missing

resolve:
    jsr mount
    jcs @return
    lda CWD
    sta DIR
    lda #0
    sta POS
    ldy #0
    lda (NAME),y
    jeq invalid
    cmp #'/'
    bne @loop
    lda #1
    sta DIR
@slashes:
    inc POS
    ldy POS
    lda (NAME),y
    cmp #'/'
    beq @slashes
@loop:
    ldy POS
    lda (NAME),y
    beq @self
    jsr component
    bcs @return
    lda COMPONENT
    cmp #'.'
    bne @normal
    lda COMPONENT+1
    beq @dot
    cmp #'.'
    bne @normal
    lda COMPONENT+2
    bne @normal
    ldx DIR
    lda PARENTS,x
    sta DIR
@dot:
    ldy POS
    lda (NAME),y
    beq @self
    jmp @slashes
@normal:
    jsr lookup
    sta SAVED
    ldy POS
    lda (NAME),y
    beq @final
    lda SAVED
    bne @notfound
    ldy #9
    lda (ENTRY),y
    cmp #2
    bne @notdir
    lda SLOT
    sta DIR
    jmp @slashes
@final:
    lda SAVED
    beq @found
@notfound:
    lda #1
    jmp failure
@notdir:
    jmp wrong_type
@self:
    lda DIR
    sta SLOT
    lda #$FF
    sta ENTRY
    jmp ok
@found:
    jmp ok
@return:
    rts

find:
    jmp resolve

allocate:
    ldx #2
@loop:
    lda TYPES,x
    beq @found
    inx
    cpx #48
    bne @loop
    jmp full
@found:
    stx FREE
    jmp ok

create_entry:
    lda COMPONENT
    cmp #'.'
    bne @normal
    lda COMPONENT+1
    beq @invalid
    cmp #'.'
    bne @normal
    lda COMPONENT+2
    beq @invalid
@normal:
    jsr pointer
    ldy #0
@free:
    lda (ENTRY),y
    beq @found
    tya
    clc
    adc #16
    tay
    bne @free
    jmp full
@found:
    sty ENTRY
    jmp ok
@invalid:
    jmp invalid

put:
    lda LENGTH
    cmp #241
    bcc @valid
    lda #4
    jmp failure
@valid:
    lda #0
    sta LENGTH_HI
    jmp put_wide

get:
    lda #1
    sta MODE
    jmp read_file
get_wide:
    lda #0
    sta MODE
read_file:
    lda MODE
    pha
    jsr resolve
    pla
    sta MODE
    jcs @return
    lda ENTRY
    cmp #$FF
    jeq wrong_type
    ldy #9
    lda (ENTRY),y
    cmp #1
    jne wrong_type
    ldy #8
    lda (ENTRY),y
    sta LENGTH
    ldy #11
    lda (ENTRY),y
    sta LENGTH_HI
    lda MODE
    beq @copy_start
    lda LENGTH_HI
    bne @too_large
    lda LENGTH
    cmp #241
    bcc @copy_start
@too_large:
    lda #4
    jmp failure
@copy_start:
    jsr copy_setup
    ldx SLOT
@block:
    lda REMAIN
    ora REMAIN+1
    beq @done
    lda #0
    sta OTHER
    txa
    clc
    adc #$A0
    sta OTHER+1
    ldy #0
@copy:
    lda (OTHER),y
    sta (COPY_PTR),y
    jsr decrement_remaining
    beq @done
    iny
    bne @copy
    inc COPY_PTR+1
    lda NEXT,x
    tax
    jmp @block
@done:
    jmp ok
@return:
    rts

remove:
    jsr resolve
    bcs @return
    lda ENTRY
    cmp #$FF
    jeq wrong_type
    ldy #9
    lda (ENTRY),y
    cmp #1
    jne wrong_type
    jmp release
@return:
    rts
release:
    ldx SLOT
    jsr free_chain
    ldy #0
    lda #0
    sta (ENTRY),y
    jmp ok
free_chain:
    cpx #0
    beq @entry
    lda NEXT,x
    pha
    lda #0
    sta TYPES,x
    sta PARENTS,x
    sta NEXT,x
    pla
    tax
    jmp free_chain
@entry:
    rts

copy_setup:
    lda DATA
    sta COPY_PTR
    lda DATA+1
    sta COPY_PTR+1
    lda LENGTH
    sta REMAIN
    lda LENGTH_HI
    sta REMAIN+1
    rts
decrement_remaining:
    lda REMAIN
    bne @low
    dec REMAIN+1
@low:
    dec REMAIN
    lda REMAIN
    ora REMAIN+1
    rts

put_wide:
    lda LENGTH_HI
    cmp #47
    jcs @size_error
    jsr resolve
    bcc @existing
    cmp #1
    jne @return
    ldy POS
    lda (NAME),y
    jne missing
    jsr create_entry
    jcs @return
    lda #0
    sta SLOT
    jmp @plan
@existing:
    lda ENTRY
    cmp #$FF
    jeq wrong_type
    ldy #9
    lda (ENTRY),y
    cmp #1
    jne wrong_type
@plan:
    lda SLOT
    sta OLD_FIRST
    lda LENGTH_HI
    sta BLOCKS
    lda LENGTH
    beq @counted
    inc BLOCKS
@counted:
    ldy #0
    ldx SLOT
@old:
    cpx #0
    beq @free
    txa
    sta CHAIN,y
    iny
    lda NEXT,x
    tax
    jmp @old
@free:
    ldx #2
@scan:
    lda TYPES,x
    bne @next
    txa
    sta CHAIN,y
    iny
@next:
    inx
    cpx #48
    bne @scan
    cpy BLOCKS
    jcc full
    ldx OLD_FIRST
    jsr free_chain
    jsr copy_setup
    lda #0
    sta CHAIN_INDEX
    sta SLOT
    lda BLOCKS
    jeq @metadata
    lda CHAIN
    sta SLOT
@block:
    ldy CHAIN_INDEX
    lda CHAIN,y
    tax
    lda #1
    sta TYPES,x
    lda #0
    sta OTHER
    txa
    clc
    adc #$A0
    sta OTHER+1
    ldy #0
@copy:
    lda (COPY_PTR),y
    sta (OTHER),y
    jsr decrement_remaining
    beq @last
    iny
    bne @copy
    inc COPY_PTR+1
    inc CHAIN_INDEX
    ldy CHAIN_INDEX
    lda CHAIN,y
    sta NEXT,x
    jmp @block
@last:
    lda #0
    sta NEXT,x
@metadata:
    ldy #0
@name:
    lda COMPONENT,y
    sta (ENTRY),y
    iny
    cpy #8
    bne @name
    lda LENGTH
    sta (ENTRY),y
    iny
    lda #1
    sta (ENTRY),y
    iny
    lda SLOT
    sta (ENTRY),y
    iny
    lda LENGTH_HI
    sta (ENTRY),y
    jmp ok
@size_error:
    lda #4
    jmp failure
@return:
    rts

info:
    pha
    jsr mount
    pla
    bcs @return
    cmp #16
    bcc @valid
    lda #6
    jmp failure
@valid:
    asl
    asl
    asl
    asl
    sta OFFSET
    lda CWD
    sta DIR
    jsr pointer
    lda OFFSET
    sta ENTRY
    ldy #0
    lda (ENTRY),y
    jeq missing
    ldy #8
    lda (ENTRY),y
    sta LENGTH
    ldy #11
    lda (ENTRY),y
    sta LENGTH_HI
    ldy #10
    lda (ENTRY),y
    sta SLOT
    jmp ok
@return:
    lda #5
    jmp failure

mkdir:
    jsr resolve
    bcs @missing
    lda #10
    jmp failure
@missing:
    cmp #1
    bne @return
    ldy POS
    lda (NAME),y
    jne missing
    jsr create_entry
    bcs @return
    ldx DIR
    ldy #15
@depth:
    cpx #1
    beq @allocate
    lda PARENTS,x
    tax
    dey
    bne @depth
    lda #4
    jmp failure
@allocate:
    jsr allocate
    bcs @return
    ldx FREE
    lda DIR
    sta PARENTS,x
    lda #2
    sta TYPES,x
    lda #0
    sta OTHER
    txa
    clc
    adc #$A0
    sta OTHER+1
    ldy #0
    lda #0
@clear:
    sta (OTHER),y
    iny
    bne @clear
@name:
    lda COMPONENT,y
    sta (ENTRY),y
    iny
    cpy #8
    bne @name
    lda #0
    sta (ENTRY),y
    iny
    lda #2
    sta (ENTRY),y
    iny
    lda FREE
    sta (ENTRY),y
    iny
    lda #0
    sta (ENTRY),y
    jmp ok
@return:
    rts

rmdir:
    jsr resolve
    bcs @return
    lda ENTRY
    cmp #$FF
    jeq wrong_type
    ldy #9
    lda (ENTRY),y
    cmp #2
    jne wrong_type
    lda SLOT
    sta REF
    ldx CWD
@busy:
    cpx REF
    beq @notempty
    cpx #1
    beq @check
    lda PARENTS,x
    tax
    jmp @busy
@check:
    lda #0
    sta OTHER
    lda SLOT
    clc
    adc #$A0
    sta OTHER+1
    ldy #0
@scan:
    lda (OTHER),y
    bne @notempty
    tya
    clc
    adc #16
    tay
    bne @scan
    jmp release
@notempty:
    lda #11
    jmp failure
@return:
    rts

chdir:
    jsr resolve
    bcs @return
    lda ENTRY
    cmp #$FF
    beq @self
    ldy #9
    lda (ENTRY),y
    cmp #2
    jne wrong_type
@self:
    lda SLOT
    sta CWD
    jmp ok
@return:
    rts

cwd_path:
    jsr mount
    bcs @return
    lda #0
    sta LENGTH
    lda CWD
    jsr path_directory
    ldy LENGTH
    lda #0
    sta (DATA),y
    jmp ok
@return:
    rts
path_directory:
    cmp #1
    bne @child
    ldy LENGTH
    lda #'/'
    sta (DATA),y
    inc LENGTH
    rts
@child:
    pha
    tax
    lda PARENTS,x
    jsr path_directory
    pla
    sta REF
    tax
    lda PARENTS,x
    sta DIR
    jsr pointer
    ldy #10
@entry:
    tya
    sec
    sbc #10
    tay
    lda (ENTRY),y
    beq @skip
    tya
    clc
    adc #10
    tay
    lda (ENTRY),y
    cmp REF
    beq @found
    jmp @advance
@skip:
    tya
    clc
    adc #10
    tay
@advance:
    tya
    clc
    adc #16
    tay
    jmp @entry
@found:
    tya
    sec
    sbc #10
    sta ENTRY
    lda LENGTH
    cmp #1
    beq @name
    tay
    lda #'/'
    sta (DATA),y
    inc LENGTH
@name:
    ldx #0
@copy:
    txa
    tay
    lda (ENTRY),y
    beq @done
    ldy LENGTH
    sta (DATA),y
    inc LENGTH
    inx
    cpx #8
    bne @copy
@done:
    rts

move_file:
    lda DATA
    sta $0BE2
    lda DATA+1
    sta $0BE3
    jsr resolve
    jcs @return
    lda ENTRY
    cmp #$FF
    jeq wrong_type
    ldy #9
    lda (ENTRY),y
    cmp #1
    jne wrong_type
    lda ENTRY
    sta $0BE0
    lda ENTRY+1
    sta $0BE1
    lda $0BE2
    sta NAME
    lda $0BE3
    sta NAME+1
    jsr resolve
    bcc @existing
    cmp #1
    bne @return
    ldy POS
    lda (NAME),y
    jne missing
    jsr create_entry
    bcs @return
    jmp @move
@existing:
    lda ENTRY
    cmp #$FF
    jeq wrong_type
    ldy #9
    lda (ENTRY),y
    cmp #1
    jne wrong_type
    lda ENTRY
    cmp $0BE0
    bne @replace
    lda ENTRY+1
    cmp $0BE1
    jeq ok
@replace:
    ldx SLOT
    jsr free_chain
@move:
    lda $0BE0
    sta OTHER
    lda $0BE1
    sta OTHER+1
    ldy #8
@metadata:
    lda (OTHER),y
    sta (ENTRY),y
    iny
    cpy #16
    bne @metadata
    ldy #0
@name:
    lda COMPONENT,y
    sta (ENTRY),y
    iny
    cpy #8
    bne @name
    ldy #0
    lda #0
    sta (OTHER),y
    jmp ok
@return:
    rts

.segment "RODATA"
signature: .byte "A1FS", 3