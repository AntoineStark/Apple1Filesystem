; Woz Monitor for Apple 1
; Written by Steve Wozniak, 1976
; Assembled with xa (xa65)

; Zero page storage
XAML    = $24           ; Last "opened" location low
XAMH    = $25           ; Last "opened" location high
STL     = $26           ; Store address low
STH     = $27           ; Store address high
L       = $28           ; Hex value parsing low
H       = $29           ; Hex value parsing high
YSAV    = $2A           ; Saved Y for hex test
MODE    = $2B           ; $00=XAM, $74=STOR, $AE=BLOCK XAM

; Input buffer ($0200-$027F)
IN      = $0200

; PIA registers
KBD     = $D010
KBDCR   = $D011
DSP     = $D012
DSPCR   = $D013

; Special characters (bit 7 always set on Apple 1)
BS      = $DF           ; Backspace / left-arrow
CR      = $8D           ; Carriage return
ESC     = $9B           ; Escape
PROMPT  = $DC           ; '\' prompt character

        * = $FF00

; ── RESET ────────────────────────────────────────────────────────────────────
RESET:
        CLD                     ; Clear decimal mode
        CLI                     ; Enable interrupts
        LDY #$7F                ; Mask for DSP data direction register
        STY DSP                 ; Set DSP DDR (output mode)
        LDA #$A7                ; KBD+DSP control register value
        STA KBDCR               ; Enable KBD, set CA1 positive edge
        STA DSPCR               ; Enable DSP, set CB1 positive edge
; Y still = $7F → falls into NOTCR → INY makes Y=$80 (negative) → auto-ESC

; ── GETLINE / input loop ──────────────────────────────────────────────────────
NOTCR:
        CMP #BS                 ; Backspace?
        BEQ BACKSPACE
        CMP #ESC                ; Escape?
        BEQ ESCAPE
        INY                     ; Advance input index
        BPL NEXTCHAR            ; Loop if not overflow (< 128 chars)

ESCAPE:
        LDA #PROMPT             ; Print '\'
        JSR ECHO

GETLINE:
        LDA #CR                 ; Print CR
        JSR ECHO
        LDY #$01                ; New line: start at index 1

BACKSPACE:
        DEY                     ; Back up index
        BMI GETLINE             ; Reinit if already at start

NEXTCHAR:
        LDA KBDCR               ; Wait for keypress (b7 set when ready)
        BPL NEXTCHAR
        LDA KBD                 ; Read key, clears KBDCR b7
        STA IN,Y                ; Store in input buffer
        JSR ECHO                ; Echo it
        CMP #CR                 ; Was it CR?
        BNE NOTCR               ; No: keep reading

; ── Parse input line ──────────────────────────────────────────────────────────
        LDY #$FF                ; Reset index to -1
        LDA #$00                ; Default mode = XAM
        TAX                     ; X = 0

SETSTOR:
        ASL                     ; Shift left (turns ':' $BA → $74, sets STOR mode)

SETMODE:
        STA MODE                ; Save mode

BLSKIP:
        INY                     ; Next character

NEXTITEM:
        LDA IN,Y                ; Get character
        CMP #CR
        BEQ GETLINE             ; CR: done, get new line
        CMP #$AE                ; '.' (with b7): BLOCK XAM delimiter
        BCC BLSKIP              ; Below '.': skip (treat as blank)
        BEQ SETMODE             ; '.' sets BLOCK XAM mode ($AE)
        CMP #$BA                ; ':' (with b7): STOR delimiter
        BEQ SETSTOR
        CMP #$D2                ; 'R' (with b7): RUN
        BEQ RUN
        STX L                   ; Clear hex value (X=0)
        STX H
        STY YSAV                ; Save Y to detect empty hex

; Parse hex digits
NEXTHEX:
        LDA IN,Y                ; Get char
        EOR #$B0                ; Map '0'-'9' → 0-9
        CMP #$0A                ; Decimal digit?
        BCC DIG                 ; Yes
        ADC #$88                ; Map 'A'-'F' → $FA-$FF
        CMP #$FA                ; Hex letter?
        BCC NOTHEX              ; No: stop hex parsing

DIG:
        ASL                     ; Shift hex digit to top nibble
        ASL
        ASL
        ASL
        LDX #$04                ; 4-bit shift

HEXSHIFT:
        ASL
        ROL L                   ; Shift into L
        ROL H                   ; Carry into H
        DEX
        BNE HEXSHIFT
        INY                     ; Next char
        BNE NEXTHEX             ; Always (Y never 0 in valid input)

NOTHEX:
        CPY YSAV                ; Any hex digits parsed?
        BEQ ESCAPE              ; No: error → ESC

        BIT MODE                ; Test mode: V=b6, N=b7
        BVC NOTSTOR             ; b6=0 → not STOR (XAM or BLOCK XAM)

; ── STOR: deposit byte ────────────────────────────────────────────────────────
        LDA L                   ; Low byte of data
        STA (STL,X)             ; Store at STL/STH (X=0)
        INC STL
        BNE NEXTITEM
        INC STH

TONEXTITEM:
        JMP NEXTITEM

; ── RUN ──────────────────────────────────────────────────────────────────────
RUN:
        JMP (XAML)              ; Jump to last opened address

; ── NOTSTOR: XAM or BLOCK XAM ────────────────────────────────────────────────
NOTSTOR:
        BMI XAMNEXT             ; b7=1 → BLOCK XAM (continue from last addr)

; Copy address to XAM index and STOR index
        LDX #$02

SETADR:
        LDA L-1,X               ; L-1,X: X=2→H, X=1→L
        STA STL-1,X             ; → STH, STL
        STA XAML-1,X            ; → XAMH, XAML
        DEX
        BNE SETADR              ; Z cleared by DEX=2→1; set by DEX=1→0

; Print address + data
NXTPRNT:
        BNE PRDATA              ; Z set by DEX/AND: if ≠0 skip address line
        LDA #CR
        JSR ECHO
        LDA XAMH
        JSR PRBYTE
        LDA XAML
        JSR PRBYTE
        LDA #$BA                ; ':' with b7
        JSR ECHO

PRDATA:
        LDA #$A0                ; ' ' with b7
        JSR ECHO
        LDA (XAML,X)            ; Fetch byte (X=0)
        JSR PRBYTE

XAMNEXT:
        STX MODE                ; X=0: reset to XAM mode
        LDA XAML
        CMP L                   ; Compare low bytes
        LDA XAMH
        SBC H                   ; Compare high bytes (with borrow)
        BCS TONEXTITEM          ; XAML/H >= L/H: done

        INC XAML
        BNE MOD8CHK
        INC XAMH

MOD8CHK:
        LDA XAML
        AND #$07                ; Mod 8 → clears b7, so BPL always taken
        BPL NXTPRNT

; ── PRBYTE: print A as 2 hex digits ──────────────────────────────────────────
        * = $FFDC
PRBYTE:
        PHA
        LSR
        LSR
        LSR
        LSR
        JSR PRHEX
        PLA

; ── PRHEX: print low nibble of A as hex digit ────────────────────────────────
        * = $FFE5
PRHEX:
        AND #$0F
        ORA #$B0                ; '0' | $80
        CMP #$BA                ; > '9' (with b7)?
        BCC ECHO
        ADC #$06                ; Adjust to 'A'-'F'

; ── ECHO: output character in A to terminal ──────────────────────────────────
        * = $FFEF
ECHO:
        BIT DSP                 ; Wait until DSP ready (b7=0)
        BMI ECHO
        STA DSP                 ; Write character, sets DSP b7
        RTS

; ── Vectors ──────────────────────────────────────────────────────────────────
        * = $FFF8
        .byte $00, $00          ; Unused
        .word $0F00             ; NMI  → $0F00
        .word RESET             ; RESET → $FF00
        .word $0000             ; IRQ  → $0000
