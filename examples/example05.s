        DSK test
        MX %11
        ORG $6000
        
        LDX #$ff
COUNT   PHA
        PHA
        DEX
        BNE COUNT
