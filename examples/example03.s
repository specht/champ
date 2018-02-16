        DSK test
        MX %11
        ORG $6000
        
FOO     EQU $8000 ; @u8
    
        JSR TEST
        RTS
    
TEST    LDX #$FF
        LDA #1
LOOP    TAY
        TXA
        EOR #$FF
        LSR
        LSR
        STA FOO
        TYA
        CLC
        ADC FOO     ; @Au(post)
        DEX         ; @Xu(post) @Au,FOO(post)
        BNE LOOP
