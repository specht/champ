        DSK test
        MX %11
        ORG $6000
    
FOO     EQU $8000

        JSR TEST
        BRK
    
TEST    LDA #64         ; load 64 into accumulator
        ASL             ; multiply by two @Au 
        STA FOO         ; store result @Au
        RTS
