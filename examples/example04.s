        DSK test
        MX %11
        ORG $6000
        
        LDX #$20
        JSR COUNT
        LDX #$30
        JSR COUNT
        LDX #$40
        JSR COUNT
        
        BRK
    
COUNT   DEX         ; @Xu(post) @cycles
        BNE COUNT
        RTS
