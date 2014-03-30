#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <propeller.h>
#include <eeprom.h>

#define START               0x8000
//#define LENGTH              0x2674
#define LENGTH              60

#define EEPROM_ADDR         0xa0
#define EEPROM_BLOCK_SIZE   128

static EEPROM_BOOT eeprom_state;
static EEPROM *eeprom;

/* main - the main routine */
int main(void)
{
    uint8_t buf[EEPROM_BLOCK_SIZE];
    int remaining, cnt, i;
    uint32_t start;
    
    if ((eeprom = eepromBootOpen(&eeprom_state, EEPROM_ADDR)) == NULL) {
        printf("eepromOpen failed\n");
        return 1;
    }
            
    for (start = START, remaining = LENGTH; remaining > 0; remaining -= cnt) {
        if ((cnt = remaining) > EEPROM_BLOCK_SIZE)
            cnt = EEPROM_BLOCK_SIZE;
        if (eepromRead(eeprom, (uint32_t)start, buf, cnt) != 0)
            printf("eepromRead failed\n");
        for (i = 0; i < cnt; ++i) {
            if ((i & 15) == 0)
                printf("\n%07x  ", start - START + i);
            printf("  %02x", buf[i]);
        }
        start += cnt;
    }
    putchar('\n');

    return 0;
}

