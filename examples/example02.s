        DSK test
        MX %11
        ORG $6000
    
FOO     EQU $8000       ; @u16

        JSR TEST
        BRK
    
TEST    LDA #0
        STA FOO
        LDA #$40
        STA FOO+1       ; @FOO(post)
        RTS
