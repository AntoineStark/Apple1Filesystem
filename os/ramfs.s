.setcpu "6502"

NAME = $40
DATA = $42
LENGTH = $44
ENTRY = $45
SLOT = $47
INDEX = $48
SAVED = $49
OTHER = $4A
BASE = $A000
COUNT = 15
CAPACITY = 240

.segment "CODE"
    jmp mount
    jmp format
    jmp find
    jmp put
    jmp get
    jmp remove
    jmp info

ok:
    lda #0
    clc
    rts
bad_fs:
    lda #5
failure:
    sec
    rts

mount:
    cld
    ldx #4
@header:
    lda BASE,x
    cmp signature,x
    bne bad_fs
    dex
    bpl @header
    lda #0
    sta ENTRY
    lda #$A1
    sta ENTRY+1
@slot:
    ldy #0
    lda (ENTRY),y
    beq @next
@name:
    lda (ENTRY),y
    beq @padding
    jsr valid_character
    bcs bad_fs
    iny
    cpy #8
    bne @name
    beq @length
@padding:
    iny
    cpy #8
    beq @length
    lda (ENTRY),y
    bne bad_fs
    beq @padding
@length:
    ldy #8
    lda (ENTRY),y
    cmp #CAPACITY+1
    bcs bad_fs
    lda #0
    sta OTHER
    lda ENTRY+1
    clc
    adc #1
    sta OTHER+1
@duplicate:
    lda OTHER+1
    cmp #$B0
    beq @next
    ldy #0
@compare:
    lda (ENTRY),y
    cmp (OTHER),y
    bne @different
    iny
    cpy #8
    bne @compare
    jmp bad_fs
@different:
    inc OTHER+1
    jmp @duplicate
@next:
    inc ENTRY+1
    lda ENTRY+1
    cmp #$B0
    bne @slot
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
    cpx #$B0
    bne @clear
    ldx #4
@header:
    lda signature,x
    sta BASE,x
    dex
    bpl @header
    jmp ok

valid_character:
    cmp #'A'
    bcc @digit
    cmp #'Z'+1
    bcc @valid
@digit:
    cmp #'0'
    bcc @punctuation
    cmp #'9'+1
    bcc @valid
@punctuation:
    cmp #'.'
    beq @valid
    cmp #'_'
    beq @valid
    cmp #'-'
    beq @valid
    sec
    rts
@valid:
    clc
    rts

check_name:
    ldy #0
@loop:
    lda (NAME),y
    beq @end
    cpy #8
    beq @invalid
    jsr valid_character
    bcs @invalid
    iny
    bne @loop
@end:
    cpy #0
    beq @invalid
    sty INDEX
    jmp ok
@invalid:
    lda #3
    jmp failure

find:
    jsr mount
    bcs @return
    jsr check_name
    bcs @return
    lda #0
    sta SLOT
@slot:
    jsr slot_pointer
    ldy #0
@compare:
    cpy INDEX
    beq @end
    lda (NAME),y
    cmp (ENTRY),y
    bne @next
    iny
    bne @compare
@end:
    cpy #8
    beq @found
    lda (ENTRY),y
    beq @found
@next:
    inc SLOT
    lda SLOT
    cmp #COUNT
    bne @slot
    lda #1
    jmp failure
@found:
    jmp ok
@return:
    rts

slot_pointer:
    lda #0
    sta ENTRY
    lda SLOT
    clc
    adc #$A1
    sta ENTRY+1
    rts

put:
    lda LENGTH
    cmp #CAPACITY+1
    bcc @lookup
    lda #4
    jmp failure
@lookup:
    jsr find
    bcc @write
    cmp #1
    bne @return
    lda #0
    sta SLOT
@free:
    jsr slot_pointer
    ldy #0
    lda (ENTRY),y
    beq @write
    inc SLOT
    lda SLOT
    cmp #COUNT
    bne @free
    lda #2
    jmp failure
@write:
    ldy #0
@name:
    cpy INDEX
    bcs @pad
    lda (NAME),y
    bne @store
@pad:
    lda #0
@store:
    sta (ENTRY),y
    iny
    cpy #8
    bne @name
    lda LENGTH
    sta (ENTRY),y
    lda #16
    sta ENTRY
    ldy #0
@data:
    cpy LENGTH
    beq @done
    lda (DATA),y
    sta (ENTRY),y
    iny
    bne @data
@done:
    lda #0
    sta ENTRY
    jmp ok
@return:
    rts

get:
    jsr find
    bcs @return
    ldy #8
    lda (ENTRY),y
    sta LENGTH
    lda #16
    sta ENTRY
    ldy #0
@copy:
    cpy LENGTH
    beq @done
    lda (ENTRY),y
    sta (DATA),y
    iny
    bne @copy
@done:
    lda #0
    sta ENTRY
    jmp ok
@return:
    rts

remove:
    jsr find
    bcs @return
    ldy #0
    lda #0
    sta (ENTRY),y
    jmp ok
@return:
    rts

info:
    sta SAVED
    jsr mount
    bcs @return
    lda SAVED
    cmp #COUNT
    bcc @valid
    lda #6
    jmp failure
@valid:
    sta SLOT
    jsr slot_pointer
    ldy #0
    lda (ENTRY),y
    bne @used
    lda #1
    jmp failure
@used:
    ldy #8
    lda (ENTRY),y
    sta LENGTH
    jmp ok
@return:
    rts

.segment "RODATA"
signature:
    .byte "A1FS", 1