/* The blood sweat and tears of 2 less than fortunate EE's that programmed this. */

/* DriverLib Includes */
#include "driverlib.h"
#include "msp432.h"
#include "GPS.h"
#include "PinSetup.h"

/**
 * main.c
 */

//Flash Globals
volatile _Bool NoFixFlag = 0; //In the event that the GPS did not get a fix within the alloted time, this flag is set to a 1.
volatile _Bool GPSStringClassifyGo = 0;

<<<<<<<

=======
//PC Hardwire Globals
volatile char PCdataString[99]; //Puts the characters from the buffer into the string and increments so we can get more than one character
//at a time and get a useful message
volatile char PCString[99]; //Puts the raw string from the buffer into a new location for parsing. Gets updated when the PCdataString has a complete string.
volatile int PCindex = 0; //Increments within the string and puts the characters in and increments to the next position
volatile int PCflag = 0; //Used for when there has been the start character detected and the characters on the buffer are to be put into PCdataString
volatile int PCStringExtractGo = 0; //When a complete string has been formed and needs to be parsed, this flag gets set.
volatile _Bool USBPresentFlag = 0; //When the USB is connected, this flag is set and used within the code.
volatile _Bool USBPresentOns = 1; //One shot to initialize the PC UART channel
volatile _Bool MagnetRemovedFlag = 0; //Used for when the magnet has been removed and the "start up" procedure can take place.

// PC GUI globals
struct PC_Set_Time {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t month;
    uint8_t day;
    uint16_t year;
} pc_set_time = {0,0,0,0,0,0};
struct PC_GPS_Settings {
    uint8_t num_hours;
    uint8_t fix_quality;
} pc_gps_settings = {0,0};
struct PC_Sat_Settings {
    uint8_t upload_day;
    uint8_t hour_connect;
    uint8_t am_pm;
    uint8_t frequency;
    uint8_t retries;
} pc_sat_settings = {0,0,0,0,0};
struct PC_VHF_Settings {
    uint8_t start_hour;
    uint8_t start_am_pm;
    uint8_t end_hour;
    uint8_t end_am_pm;
} pc_vhf_settings = {0,0,0,0};
int LED_STATE = 0;
int LED_CHANGE = 0;

volatile char PDOPString[6];


>>>>>>>
//UART ISR Globals
volatile uint8_t RXData = 0; //These are where the characters obtained on the UART buffer for each channel are stored.
volatile int index = 0;

char GPSString[100];

void initClocks(void)
{
    //Halting WDT
    MAP_WDT_A_holdTimer();

    CS_setExternalClockSourceFrequency(32768, 48000000);
    MAP_PCM_setCoreVoltageLevel(PCM_VCORE1);
    CS_startHFXT(false);
    //Starting HFXT
    //MAP_CS_initClockSignal(CS_SMCLK, CS_HFXTCLK_SELECT, CS_CLOCK_DIVIDER_16);
    MAP_CS_initClockSignal(CS_MCLK, CS_HFXTCLK_SELECT, CS_CLOCK_DIVIDER_16);
    //Starting LFXT
    MAP_CS_startLFXT(CS_LFXT_DRIVE3);
    MAP_CS_initClockSignal(CS_BCLK, CS_LFXTCLK_SELECT, CS_CLOCK_DIVIDER_1);

    // Configuring WDT to timeout after 128M iterations of SMCLK, at 3MHz,
    // this will roughly equal 42 seconds
    MAP_SysCtl_setWDTTimeoutResetType(SYSCTL_SOFT_RESET);
    MAP_WDT_A_initWatchdogTimer(WDT_A_CLOCKSOURCE_SMCLK,
    WDT_A_CLOCKITERATIONS_128M);
}

void ClassifyString(void)
{
    /*if(NoFixFlag == 0){ //used to pass commands once at startup
        GPS_puts("$PMTK161,1*29\r\n");
        GPS_puts("$PMTK104*39\r\n");
        GPS_puts("$PMTK353,1,1,0,0,*2B\r\n");

        NoFixFlag = 1;
    }*/

    GPSStringClassifyGo = 0;
    printf("%s", GPSString);
}


void main(void)
{
    //Halting WDT
    MAP_WDT_A_holdTimer();

    //Initializing IO on the MSP, leaving nothing floating
    IOSetup();

    initClocks(); //Initializing all of the timing devices on the MSP

    initGPSUART();

    //GPS On
    GPIO_setOutputLowOnPin(GPIO_PORT_P3, GPIO_PIN0);

    MAP_Interrupt_enableMaster();

    while(1)
        //GPS_puts("hello\n");
        if(GPSStringClassifyGo)
            ClassifyString();
}


/* EUSCI A2 UART ISR - Echoes data back to PC host (from GPS to monitor on serial monitor) */
void EUSCIA2_IRQHandler(void)
{
    uint32_t status = MAP_UART_getEnabledInterruptStatus(EUSCI_A2_BASE);

    MAP_UART_clearInterruptFlag(EUSCI_A2_BASE, status);

    if (status & EUSCI_A_UART_RECEIVE_INTERRUPT_FLAG)
    {
        RXData = MAP_UART_receiveData(EUSCI_A2_BASE);
        //printf("%c", RXData);
        switch (RXData)
        {
        case '\n': //looks for a new line character which is the end of the NMEA string
            GPSString[index] = '\0'; //puts a NULL at the end of the string again because strncpy doesn't
            index = 0;
            GPSStringClassifyGo = 1;
            break;

        default:
            GPSString[index] = RXData; //puts what was in the buffer into an index in dataString
            index++; //increments the index so the next character received in the buffer can be stored
            //into dataString
            break;
        }
    }
}

