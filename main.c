/* The blood sweat and tears of 2 less than fortunate EE's that programmed this. */

/* DriverLib Includes */
#include "driverlib.h"
#include "msp432.h"
#include <math.h>
#include <string.h>
#include "GPS.h"

#include "PinSetup.h"

/**
 * main.c
 */

extern void IOSetup(void);
extern void initGPSUART(void);
extern void disableGPSUART(void);

//Where Flash Bank 1 begins
#define START_LOC 0x00020000

//Flash Globals
volatile _Bool NoFixFlag = 0; //In the event that the GPS did not get a fix within the alloted time, this flag is set to a 1.
static volatile char CurrentFixSaveString[33]; //This is the string that is constructed that has the time, date, lat and long
//in it that came from the GPS when a fix was to be obtained. When there isn't a fix, it's populated properly before getting
//written to flash.
static volatile char FixRead[33]; //Stores the 32 bytes from flash that has a fix in it, it's 33 length for the end character
static volatile char SectorRead[4097]; //Same, but this one can store an entire sector.
volatile _Bool MemoryFull = 0; //Once the end of flash has been reached, this gets set to trigger the collar to be in longevity mode
//which means that it doesn't store anymore fixes into flash.
static volatile uint8_t FixMemoryLocator[2]; //FixMemoryLocator[0] stores position in the current sector, range 0-124. //??
//FixMemoryLocator[1] stores the current sector, range 0-31.
static volatile uint8_t MemPlaceholder[2]; //MemPlaceholder[0] stores position in the readout sector, range 0-128. //??
//MemPlaceholder[1] stores the readout sector, range 0-31

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

volatile char PDOPString[6];


//UART ISR Globals
volatile uint8_t RXData = 0; //These are where the characters obtained on the UART buffer for each channel are stored.
volatile uint8_t RX0Data = 0;
volatile uint8_t RX1Data = 0;


//Where Flash Bank 1 begins
#define START_LOC 0x00020000

//Cool structure I made to store all of the parameters obtained through the UART connection
typedef struct _Configparameters
{
    uint8_t COM; //Command program or read (program or read)
    uint8_t GPS; //GPS sample interval
    uint8_t WTM; //Wireless transmission mode (confirmed or spew)
    uint8_t WTD; //Wireless transmission day
    uint8_t WCT; //Wireless connection start time
    uint8_t WCW; //Wireless connection window
    uint8_t VST; //VHF broadcast start time
    uint8_t VET; //VHF broadcast end time
    uint8_t DOP; //PDOP Threshold
    uint8_t GTO; //GPS timeout
} Configparameters;
static volatile Configparameters Config;

//GPS Globals
volatile char dataString[300]; //Raw characters from the buffer are put in here
volatile char GPSString[300]; //When something needed to be parsed then it's put in this string
volatile int index = 0; //Same thing as PC string
volatile int flag = 0; // Look above for the comments on these, the naming stays the same
volatile int GPSStringClassifyGo = 0;
volatile int gpsFixFound = 0;
volatile _Bool FixAttemptFailed = 0;

static volatile GPSinfostructure GPSinfo;



//RTC Globals
static volatile RTC_C_Calendar SystemTime; //This is for within the system, it gets updated every time the RTC interrupt happens
static volatile RTC_C_Calendar SetTime; //Had to have a separate structure which got populated for updating the RTC time.  It was weird
//in that it wouldn't work with using the same structure, likely because the RTC handler was messing with it.

//How a second delay is created without having to use ACLK or another source besides what I know. Delay1ms is included in this
void parrotdelay(unsigned long ulCount)
{
    __asm ( "pdloop:  subs    r0, #1\n"
            "    bne    pdloop\n");
}

void Delay1ms(uint32_t n) //just burns CPU as it decrements this counter. It was scaled to 3MHz from what we had in another project.
{
    while (n)
    {
        parrotdelay(369);    // 1 msec, tuned at 48 MHz is 5901, 3MHz set to 369
        n--;
    }
}


void RTC_setup(void)
{
    /* Specify an interrupt to assert every hour */
//    MAP_RTC_C_setCalendarEvent(RTC_C_CALENDAREVENT_MINUTECHANGE);
    MAP_RTC_C_setCalendarEvent(RTC_C_CALENDAREVENT_HOURCHANGE);

    /* Enable interrupt for RTC Ready Status, which asserts when the RTC
     * Calendar registers are ready to read.
     * Also, enable interrupts for the Calendar alarm and Calendar event. */

    //delete the read_ready_interrupt once this is ready to be buttoned up more
    MAP_RTC_C_clearInterruptFlag(
    RTC_C_TIME_EVENT_INTERRUPT | RTC_C_CLOCK_READ_READY_INTERRUPT);
    /* MAP_RTC_C_enableInterrupt(
    RTC_C_TIME_EVENT_INTERRUPT | RTC_C_CLOCK_READ_READY_INTERRUPT); */

    /* Enable interrupts and go to sleep. */
    //MAP_Interrupt_enableInterrupt(INT_RTC_C);
}



void initClocks(void)
{
    //Halting WDT
    MAP_WDT_A_holdTimer();

    CS_setExternalClockSourceFrequency(32768, 48000000);
    MAP_PCM_setCoreVoltageLevel(PCM_VCORE1);
    MAP_FlashCtl_setWaitState(FLASH_BANK0, 2);
    MAP_FlashCtl_setWaitState(FLASH_BANK1, 2);
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
    if(FixAttemptFailed == 0){
        GPS_Send_Command("PMTK104");
        FixAttemptFailed = 1;
    }
    printf("%s\n", GPSString);
}

void GPS_Send_Command(char* command){
    int x = 0, str_len = strlen(command);
    for(x = 0; x < str_len; x++){
        uart_putc(command[x]);
    }
}

//----- UART_PUTC -------------------------------------------------------------
void uart_putc( uint8_t c )
{
    while(!MAP_UART_getInterruptStatus(EUSCI_A2_BASE,
        EUSCI_A_UART_TRANSMIT_INTERRUPT_FLAG)); // Wait for xmit buffer ready
    UART_transmitData(EUSCI_A2_BASE, c);        // Transmit the byte
} // End UART_PUTC



int main(void)

{
    //Halting WDT
    MAP_WDT_A_holdTimer();

    //Initializing IO on the MSP, leaving nothing floating
    IOSetup();

    initClocks(); //Initializing all of the timing devices on the MSP

    //initPCUART(); //delete once done with debug
    initGPSUART();

    //GPS On
    initClocks();
    GPIO_setOutputLowOnPin(GPIO_PORT_P3, GPIO_PIN0);
    GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN0);
    GPIO_setOutputHighOnPin(GPIO_PORT_P4, GPIO_PIN7);

    MAP_Interrupt_enableMaster();

    while(!gpsFixFound){

        if(GPSStringClassifyGo){
            ClassifyString();
            GPSStringClassifyGo = 0;
        }
    }

    disableGPSUART();
    //GPS Off
    GPIO_setOutputHighOnPin(GPIO_PORT_P3, GPIO_PIN0);
    MAP_Interrupt_disableMaster();
    return 0;
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
        //MAP_UART_transmitData(EUSCI_A0_BASE, RXData);
        int i;
        switch (RXData)
        {
        case '\n': //looks for a new line character which is the end of the NMEA string
            strncpy(GPSString, dataString, index); //copies what was in dataString into GPSString
            //so it doesn't get reset and GPSString can be used and parsed
            GPSString[index] = '\0'; //puts a NULL at the end of the string again because strncpy doesn't
            index = 0;
            GPSStringClassifyGo = 1;
            break;

        default:
            dataString[index] = RXData; //puts what was in the buffer into an index in dataString
            index++; //increments the index so the next character received in the buffer can be stored
            //into dataString
            break;
        }
    }

}

