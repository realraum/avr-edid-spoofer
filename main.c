#include "projInfo.h"	//Don't include in main.h 'cause that's included in other .c's?
#include "main.h"


//Need to save memory space for the Tiny45...
// (Actually, now it fits.
//  FADER just uses the three remaining pins to cyclicly fade three LEDs
//  You can just ignore/disable it.)
#define FADER_ENABLED FALSE //FALSE

#if (defined(FADER_ENABLED) && FADER_ENABLED)
#include _SINETABLE_HEADER_
#endif


#if !defined(__AVR_ARCH__)
 #error "__AVR_ARCH__ not defined!"
#endif

//#include <string.h> //for strcmp

// WTF I left this as 15 bytes but wrote 256 via usbTinyI2c 
// and had no errors (in version 20)?!
//uint8_t edidArray[15]; 
// = {0x00,0xe1,0xd2,0xc3,0xb4,0xa5,0x96,0x87,8,9};



//R,G,B
volatile uint8_t ledState[3] = {0,0,0};
uint8_t ledIndex = 0;


#define EDIDARRAYLENGTH	128 //256

// change by sinyria - this is the working confirmed EDID by sally
uint8_t edidArray[EDIDARRAYLENGTH] =
{
0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x09,0xD1,
0x01,0x84,0x3A,0x00,0x00,0x00,0x09,0x11,0x01,0x03,
0x80,0x00,0x00,0x78,0x2A,0x5F,0x9F,0xA9,0x55,0x50,
0x92,0x24,0x0D,0x4A,0x4B,0xA5,0x4A,0x00,0x61,0x59,
0xD1,0xC0,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
0x01,0x01,0x01,0x01,0x02,0x3A,0x80,0x18,0x71,0x38,
0x2D,0x40,0x58,0x2C,0x45,0x00,0x00,0x00,0x00,0x00,
0x00,0x1E,0x00,0x00,0x00,0xFD,0x00,0x30,0x58,0x18,
0x51,0x0F,0x00,0x0A,0x20,0x20,0x20,0x20,0x20,0x20,
0x00,0x00,0x00,0xFE,0x00,0x42,0x65,0x6E,0x51,0x0A,
0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x00,
0x00,0xFC,0x00,0x57,0x31,0x30,0x30,0x30,0x30,0x0A,
0x20,0x20,0x20,0x20,0x20,0x20,0x01,0x2E,
};

uint8_t edidArrayIndex = 0;
//Called immediately by the i2c interrupts when a byte is received
static __inline__
void processReceivedByte(uint8_t receivedByte, uint8_t byteNum)
	__attribute__((__always_inline__));
//Called immediately by the i2c interrupts when a byte is to be loaded
// for transmission
static __inline__
uint8_t nextByteToTransmit(uint8_t masterACKed)
	__attribute__((__always_inline__));;




#define USICNT_MASK ((1<<USICNT3)|(1<<USICNT2)|(1<<USICNT1)|(1<<USICNT0))
// There are four basic states: 
//    Awaiting Start Condition
//    ACK Transmission
//		Address
//    Data (read/write)
// AWAITING values are set... others are read... (helps for readability)
#define USI_STATE_AWAITING_START		0
#define USI_STATE_AWAITING_START_SCL 1
#define USI_STATE_START_SCL_RECEIVED USI_STATE_AWAITING_START_SCL
#define USI_STATE_AWAITING_ADDRESS	2
#define USI_STATE_ADDRESS_RECEIVED 	USI_STATE_AWAITING_ADDRESS
#define USI_STATE_AWAITING_ACK		3
#define USI_STATE_ACK_COMPLETE		USI_STATE_AWAITING_ACK
#define USI_STATE_AWAITING_BYTE	   4
#define USI_STATE_BYTE_COMPLETE		USI_STATE_AWAITING_BYTE

//uint8_t usi_i2c_deviceAddressReceived = FALSE;
uint8_t usi_i2c_byteToTransmit = 0xff; //0xA5;
uint8_t usi_i2c_state = USI_STATE_AWAITING_START;
uint8_t usi_i2c_readFromSlave = 0;
uint8_t usi_i2c_requestedAddres = 0;
uint8_t usi_i2c_receivedByte = 0;

#define SDA_PIN	PB0
#define SCL_PIN	PB2
#define SDAPORT	PORTB
#define SCLPORT	PORTB

// This can be called in two cases:
//   upon INIT
//   and when a different slave has been addressed
//      from an overflow interrupt
void usi_i2c_awaitStart(void)
{
	usi_i2c_state = USI_STATE_AWAITING_START;

   //Clear the interrupt flags, etc. first...
	// NOTE This will release SCL hold (i.e. different slave addressed)
   USISR = (1<<USISIF) //Start Condition Interrupt Flag
                       //  (ONLY cleared when written 1)
         | (1<<USIOIF) //Counter Overflow Interrupt Flag
                       //  (ONLY cleared when written 1)
         | (1<<USIPF)  //Stop Condition Flag (not an interrupt)
         | (0<<USIDC)  //Data Collision Flag (Read-Only, not an interrupt)
         | (USICNT_MASK & 0); //Clear the USI counter


   //Configure the USI to look for Start-Condition
   USICR = (1<<USISIE) //Enable the start-condition interrupt
         | (0<<USIOIE) //Disable the counter-overflow interrupt
         | (1<<USIWM1) //Enable two-wire mode 
							  // with SCL hold during start-condition
         | (0<<USIWM0) // without SCL hold during overflow
							  // will be set later
         | (1<<USICS1) //Select external clocking
         | (0<<USICS0) //  positive-edge
         | (0<<USICLK) //  4-bit counter counts on both external edges
         | (0<<USITC); // DON'T toggle the clock pin 
                       // (this should always be 0)

	setpinPORT(SDA_PIN, SDAPORT);
	setpinPORT(SCL_PIN, SCLPORT);

	setinPORT(SDA_PIN, SDAPORT);
	//Enable SCL-hold during the start-condition 
	setoutPORT(SCL_PIN, SCLPORT);
}



//Initialize the counter to count
//Clear the counter-overflow flag to
//Release SCL
// 16 clock edges will signal 8 bits received (count=0)
// 2 clock edges will signal ACK transmitted (count=14)
#define USI_I2C_OVERFLOW_RELEASE_SCL_AND_SET_COUNTER(count) \
   (USISR = ((1<<USIOIF) | (USICNT_MASK & (count))))

//This is called after a start-condition
void usi_i2c_awaitStartSCL(void)
{
	usi_i2c_state = USI_STATE_AWAITING_START_SCL;

	//Before enabling the counter-overflow interrupt
	// make sure the flag is clear
	// this will also clear the counter... watch out!
	USISR = (1<<USIOIF);

   //Configure the USI to look for counter-overflow (start SCL received)
   USICR = (1<<USISIE) //Enable the start-condition interrupt
         | (1<<USIOIE) //Enable the counter-overflow interrupt
                       // WE'RE READY!!!
         | (1<<USIWM1) //Enable two-wire mode
         | (1<<USIWM0) // with SCL hold during overflow
         | (1<<USICS1) //Select external clocking
         | (0<<USICS0) //  positive-edge
         | (0<<USICLK) //  4-bit counter counts on both external edges
         | (0<<USITC); // DON'T toggle the clock pin 
                       // (this should always be 0)

	//Don't clear the USISIF, SCL should be released AFTER it's received!
	// ONE SCL change (high to low) should be counted before we're
	//   before we're ready for 16 edges signalling address+r/w received
	//APPARENTLY the USI_START_vect ISR is called until USISIF is cleared
	// Unless the start-SCL occurs before the overflow interrupt is enabled
	//  this should have the same effect (the overflow interrupt stalls SCL)
	// However, if the start-SCL is too fast to detect, then we're screwed
	// all of, what, an interrupt-jump and a few instructions?
	//  Maybe revisit the zipped version (using a while loop instead of
	//   states)
	// also inline this function...
	USISR = (1<<USISIF) | (USICNT_MASK & 15);
	
//	USISR = (USICNT_MASK & 15);
//	static uint8_t callCount = 0;
//	if(callCount < 15)
//		callCount++;
//	set_heartBlink(callCount);
}



void usi_i2c_slaveInit(void)
{
	usi_i2c_awaitStart();
}

uint8_t heartBlinkInternal = 0;

ISR(USI_START_vect)
{
	// The start condition interrupt occurs when SDA is pulled low
	//   WHEN SCL is high...
	// Which means that the device will count an edge when SCL goes low...
	//   Thus, the number of clock edges will be 17 for the first 8 bits!!!
	//THIS IS A HACK AND SHOULD NOT BE IMPLEMENTED:
//	while(getpinPORT(SCL_PIN, SCLPORT))
//	{};
	// Should be fixed now, with the new AWAITING_START_SCL state

	heartBlinkInternal++;
	//set_heartBlink(heartBlinkInternal);

	//A Start-Detection has occurred
	// The slaves (including this device) have pulled SCL low
	// (from the manual):
	//  The start detector will hold the SCL line low after the master has
	//  forced a negative edge on this line. This allows the slave to wake
	//  up from sleep or complete other tasks before setting up the USI 
	//  Data Register to receive the address. This is done by clearing the 
	//  start condition flag and resetting the counter. 

	//NOT CERTAIN this is necessary
	// Shouldn't be, since the pin is set as an input...
	//   unless a START is received in the middle of a slave-write???
	// From the diagram showing the connections of the shift-registers
	// it seems we need this to prevent data collision
	//setinPORT(SDA_PIN, SDAPORT);
	//USIDR = 0xff;	

	//Set the pin directions...
	// Make sure, just in case a start-condition interrupts a slave-write...
	setinPORT(SDA_PIN, SDAPORT);
	// Leave the CLOCK pin active for SCL-hold at the first overflow
	// (once we've received address/direction)
	setoutPORT(SCL_PIN, SDAPORT);

	//Indicate that the address hasn't yet been received
	// (it should be the first byte transmitted)
	usi_i2c_awaitStartSCL();

	//heartClear();
}

//shifted right once to account for R/W bit...
#define USI_I2C_MYEDIDADDRESS 0x50
#define USI_I2C_MYLEDADDRESS	0x60

#define usi_i2c_isMyAddress(addr) \
	( ((addr) == USI_I2C_MYEDIDADDRESS)\
	| ((addr) == USI_I2C_MYLEDADDRESS) )


// In case this device handles multiple addresses... (NYI)
uint8_t usi_i2c_requestedAddress = 0;

uint8_t byteNum = 0;

//Basically, everything after the start-condition
// ACK and bytes...
ISR(USI_OVF_vect)
{
	//The USI counter has overflowed... this occurs in a couple cases..
	// First: After the address and r/w bit have been received
	// Second: After the ACK bit has been transmitted (by the slave)
	// Third: After the first data-byte has been received
	// Fourth: After the ACK bit has been transmitted for it...
	// and so on...

	//The counter-overflow also pulls SCL Low (from the manual):
	// After eight bits containing slave address and data direction 
	// (read or write) have been transferred, the slave counter overflows 
	// and the SCL line is forced low. 

	// If the slave is not the one the 
	// master has addressed, it releases the SCL line and waits for a new 
	// start condition.


	//These cases are the same as AWAITING, except if we're here,
	// then we're no longer awaiting them...
	switch(usi_i2c_state)
	{
		// The start condition interrupt occurs immediately when SDA goes low
		//  while SCL is high. The SCL line needs to go low once before
		//  data can be transmitted (this was a BITCH to find)
		//  in other words, there are 17 clock edges between the start-
		//  condition and having fully received the r/w bit
		case USI_STATE_START_SCL_RECEIVED:
	//		usi_i2c_awaitAddress();
			usi_i2c_state = USI_STATE_AWAITING_ADDRESS;

		   //Clear the Start-Condition flag to release SCL
			// ALSO the Overflow flag for the same reason
			// ALSO: set the counter
			//   16 clock edges will signal 8 bits received (address+r/w)
			USISR = (1<<USISIF) //Start Condition Interrupt Flag
										//  (ONLY cleared when written 1)
					| (1<<USIOIF)	//Overflow flag 
		         | (USICNT_MASK & 0); //Clear the USI counter

			break;
		case USI_STATE_ADDRESS_RECEIVED:
			{};

			byteNum = 0;
			//heartClear();
			//Check to see if it's ours...
			uint8_t udrTemp = USIDR;
			//1 = master-read (slave-writes to SDA)
			//0 = master-write (slave-reads from SDA)
			usi_i2c_readFromSlave = udrTemp & 0x01;
			usi_i2c_requestedAddress = udrTemp >> 1;
			
			//From the manual:
			// When the slave is addressed, it holds the SDA line low during 
			// the acknowledgment cycle before holding the SCL line low again
			if( usi_i2c_isMyAddress(usi_i2c_requestedAddress) )
			{
				// Send ACK:
				// This is a hack:
				//  We get to this point on a falling-edge
				//  the USIDR bit 7 is loaded 0
				//  then the next rising edge it changes to bit 6
				//  the next falling edge triggers the AWAITING_ACK case below
				//  which releases bit 6 from the SDA pin
				//  The master pulls it low again for a STOP condition
				// It might be wiser to just set the PORT value...
				//USIDR = 0x3f; 
				USIDR = 0x00;
				setoutPORT(SDA_PIN, SDAPORT);

	         usi_i2c_state = USI_STATE_AWAITING_ACK;

	         //FTM:
	         // (The USI Counter Register is set to 14 before releasing SCL)
				USI_I2C_OVERFLOW_RELEASE_SCL_AND_SET_COUNTER(14);
			}
			else //Another slave was addressed
				usi_i2c_awaitStart();

			break;
		//ACK has been transmitted from/to this device...
		case USI_STATE_ACK_COMPLETE:
			if(!usi_i2c_readFromSlave)	//slaveRead
			{
				//Release SDA from the ACK (we'll be reading...)
				setinPORT(SDA_PIN, SDAPORT);
				//Shouldn't be necessary with above, but can't hurt...
				//UDR = 0xff; 
			}
			else //slaveWrite
			{
				uint8_t masterACK;
				// First ACK will be this device's response to address+r/w
				// Read the master's ACK?
				// I guess it'd be stupid to send a read request without
				// actually reading a byte, but I've done it for testing...
				// but if that's the case, then the byteToTransmit will be 
				// decremented anyhow... Not sure how to handle this yet...
				// OTOH: if a read is requested a byte MUST be transferred
				// otherwise a bit-7 = 0 loaded would prevent a master-stop
				masterACK = !(USIDR & 0x01);

				if(byteNum)// && !(USIDR & 0x01))
				{
					heartBlinkInternal+= 0x10;
					//set_heartBlink(heartBlinkInternal);
				}

				//Load the byte to write to the master...
				//USIDR = usi_i2c_byteToTransmit;

				//if we load the next byte and its bit7 is 0
				// it will hold SDA low when the master tries to 
				// pull it high for a stop-condition!
				if(masterACK)
					USIDR = nextByteToTransmit(masterACK);
				else
					USIDR = 0xff;

				//Decrement IF the master sent ACK...
				//if(masterACK)
				//	usi_i2c_byteToTransmit--;
				
				setoutPORT(SDA_PIN, SDAPORT);
			}

			usi_i2c_state = USI_STATE_AWAITING_BYTE;

			USI_I2C_OVERFLOW_RELEASE_SCL_AND_SET_COUNTER(0);
			//heartClear();
			break;
		case USI_STATE_BYTE_COMPLETE:  //BYTE transmitted/received
			byteNum++;
			// Get the byte (if receiving)
			if(!usi_i2c_readFromSlave)
			{
				processReceivedByte(USIDR, byteNum);
				//usi_i2c_receivedByte = USIDR;
				heartBlinkInternal = usi_i2c_receivedByte;
				//set_heartBlink(heartBlinkInternal);
//				heartClear();

				// Also, we need to send an ACK...
				USIDR = 0x3f;
				setoutPORT(SDA_PIN, SDAPORT);
			}
			else //slaveWrite
			{
				// The master sends the ACK in this case...
				setinPORT(SDA_PIN, SDAPORT);
				USIDR = 0xff;
//				heartClear();
			}

			//Reload the counter to interrupt after the ACK
			USI_I2C_OVERFLOW_RELEASE_SCL_AND_SET_COUNTER(14);

			usi_i2c_state = USI_STATE_AWAITING_ACK;
			//heartClear();
         break;
		//Shouldn't get here...
		default:
			usi_i2c_state = 0x77;
			//set_heartBlink(0x77);
			break;
	}


	//I want it to blink if it's not yet read the EDID...
	if((usi_i2c_readFromSlave)
		 && (usi_i2c_state == 4)
		 && (usi_i2c_requestedAddress == USI_I2C_MYEDIDADDRESS))
		set_heartBlink(0);
	//set_heartBlink((!(usi_i2c_readFromSlave)<<4) | usi_i2c_state);
}

uint8_t ledsControlled = FALSE;

#define incrementEDIDIndex()	\
{\
	edidArrayIndex++;\
	if(edidArrayIndex == EDIDARRAYLENGTH) \
		edidArrayIndex = 0; \
}

#define setEDIDIndex(index) \
{\
	edidArrayIndex = index;\
	edidArrayIndex%=EDIDARRAYLENGTH;\
}

#define incrementLEDIndex() \
{\
	ledIndex++;\
	if(ledIndex == 3)\
		ledIndex = 0;\
}


//Interesting I haven't run into this before...
//	if(blah)
//		setLEDIndex(blah);
//	else
//	{ blah }
// APPARENTLY the semicolon causes the if statement to be closed (?)
#define setLEDIndex(index) \
{\
	ledIndex = index;\
	ledIndex %= 3;\
}

void processReceivedByte(uint8_t receivedByte, uint8_t byteNum)
{
  if(usi_i2c_requestedAddress == USI_I2C_MYEDIDADDRESS)
  {
	if(byteNum == 1)
	{
		// Could be that the EDID request doesn't send a byte-index...?
		//ledsControlled = FALSE;
		setEDIDIndex(receivedByte);
//		edidArrayIndex = receivedByte;
	}
	else
	{
		edidArray[edidArrayIndex] = receivedByte;

		incrementEDIDIndex();
		//edidArrayIndex++;
		//edidArrayIndex &= 0x07;
	}
  }
  else if (usi_i2c_requestedAddress == USI_I2C_MYLEDADDRESS)
  {
	
	ledsControlled = TRUE;

	if(byteNum == 1)
	{
		setLEDIndex(receivedByte);
	}
	else
	{
		ledState[ledIndex] = receivedByte;
		incrementLEDIndex();
	}	
  }
}

uint8_t nextByteToTransmit(uint8_t masterACKed)
{
//	static uint8_t temp = 0;

//	return temp++;

  if(usi_i2c_requestedAddress == USI_I2C_MYEDIDADDRESS)
  {
	uint8_t temp = edidArray[edidArrayIndex];

	ledsControlled = FALSE;
	if(masterACKed)
	{
		incrementEDIDIndex();
//		edidArrayIndex++;
	}

//	edidArrayIndex &= 0x07;

	return temp;
  }
  else if (usi_i2c_requestedAddress == USI_I2C_MYLEDADDRESS)
  {
	uint8_t temp = ledState[ledIndex];

	if(masterACKed)
	{
		incrementLEDIndex();
	}

	return temp;
  }

  //Should only get here if we're not address
  // in which case we shouldn't even get here.
  return 0xff;
}


void edid_checkSummer(void)
{
	uint8_t i;
	uint8_t sum = 0;

	// Don't include the garbage checksum value...
	// it will be overwritten here...
	for(i=0; i<0x7f; i++)
	{
		sum += edidArray[i];
	}

	//Load the calculated checksum
	edidArray[0x7f] = (uint8_t)((uint8_t)0 - (uint8_t)(sum));
}




#if FADER_ENABLED
//uint8_t ledsControlled = FALSE;

#define  LED_R PB4				// D4
#define  LED_G PB3				//	D5
#define	LED_B HEART_PINNUM   //	D2 after trace-change PB1 //NYI
#define LED_PORT PORTB


hfm_t ledHFM[3];


void updateLEDs(void);

void updateLEDFader(void)
{
	static uint8_t delay=0;
	static theta_t theta=2; //test = 2;

	delay++;
	if(delay == 0xff)
		theta++;

	if(theta == SINE_PI*3)
		theta = 0;



	if(theta < SINE_2PI)
		ledState[0] = \
			(uint8_t)((int16_t)sineRaw8(theta - SINE_PI_2) + (int16_t)127); 

	if((theta > SINE_PI) && (theta < SINE_PI*3))
	  	ledState[1] = \
		 	(uint8_t)((int16_t)sineRaw8(theta - SINE_PI_2 - SINE_PI) 
				+ (int16_t)127);	  

	if(theta > SINE_2PI)
		ledState[2] = \
			(uint8_t)((int16_t)sineRaw8(theta - SINE_PI_2 - SINE_2PI)
				+ (int16_t)127);
	else if(theta < SINE_PI)
		ledState[2] = \
			(uint8_t)((int16_t)sineRaw8(theta - SINE_PI_2 + SINE_PI) 
				+ (int16_t)127);
//	ledState[1] = 0x07;
	
//	ledState[2] = (uint8_t)((int16_t)sineRaw8(theta - SINE_PI_2) + (int16_t)127);

		//(uint32_t)(sineRaw(theta - SINE_PI_2))*127/SINE_MAX + 127;

	/*
	if(theta == 0xff)
	{
		theta=0;
		ledState[2]++; // = test; //2; //0x07; //theta;
	}
*/
//	ledState[0] = 0x07; //theta;

//	if(theta >= SINE_2PI) //*3)
//		theta -= SINE_2PI; //*3;

//	if(theta == SINE_2PI*8)
//	{
//		theta = 0;
//		togglebit(7, ledState[1]);
//	}

//	if(theta < SINE_2PI)
//		ledState[0] = (int32_t)theta*(int32_t)255/SINE_2PI;
//			(((int32_t)sineRaw(theta) * (int32_t)127) / 
//						 (int32_t)SINE_MAX) + 127;
		//((int8_t)(sineRaw(theta - SINE_PI_2)>>8) + (int8_t)127);

/*	if((theta > SINE_PI) && (theta < SINE_PI*3))
		ledState[1] = ((sineRaw(theta - SINE_PI_2 - SINE_PI)>>8) + 127);

	if(theta > SINE_2PI)
		ledState[2] = ((sineRaw(theta - SINE_PI_2 - SINE_2PI)>>8) + 127);
	else if(theta < SINE_PI)
		ledState[2] = ((sineRaw(theta - SINE_PI_2 + SINE_PI)>>8) + 127);
*/
	updateLEDs();
}

void updateLEDs(void)
{
	static uint8_t lastState[3] = {0,0,0};

	uint8_t i;
	for(i=0; i<3; i++)
	{
	//	if(ledState[i] != lastState[i])
		{
		//	ledsControlled = TRUE;
			lastState[i] = ledState[i];
			hfm_setPower(&(ledHFM[i]), ledState[i]);
		}
	}
	//hfm_setPower(&led_r, ledState[0]);
	//hfm_setPower(&led_g, ledState[1]);


	if(hfm_nextOutput(&(ledHFM[0])))
		clrpinPORT(LED_R, LED_PORT);
	else
		setpinPORT(LED_R, LED_PORT);

	if(hfm_nextOutput(&(ledHFM[1])))
		clrpinPORT(LED_G, LED_PORT);
	else
		setpinPORT(LED_G, LED_PORT);

	if(hfm_nextOutput(&(ledHFM[2])))
		clrpinPORT(LED_B, LED_PORT);
	else
		setpinPORT(LED_B, LED_PORT);

	//NYI:
	//if(ledState[2])

//	togglepinPORT(LED_R, LED_PORT);
//	togglepinPORT(LED_G, LED_PORT);
}
#endif

int main(void)
{
#if FADER_ENABLED
	uint8_t i;
	for(i=0; i<3; i++)
		hfm_setup(&(ledHFM[i]), 0, 255);
	//hfm_setup(&led_g, 0, 255);
#endif

//	uint16_t i;
//	for(i=0; i<256; i++)
//		edidArray[i] = i;

	//Found experimentally: assuming the free-running ADC is always 13
	// cycles per interrupt...
	// The default value was read to be 0x9f
	// This is of course device-specific
//	OSCCAL = 0x9a;

	//*** Initializations ***

	//!!! WDT could cause problems... this probably should be inited earlier and called everywhere...
	//INIT_HEARTBEAT(HEARTBEATPIN, HEARTBEAT, HEARTCONNECTION);


	init_heartBeat();

	setHeartRate(0);	

	//Blink until the EDID is read...
	//This is hokey...
	//set_heartBlink(1);

	// edid_checkSummer();

	usi_i2c_slaveInit();

#if FADER_ENABLED
	setoutPORT(LED_R, LED_PORT);
	setoutPORT(LED_G, LED_PORT);
	setpinPORT(LED_R, LED_PORT);
	setpinPORT(LED_G, LED_PORT);
#endif

//	setoutPORT(LED_B, LED_PORT);

	//This was only necessary for debugging timer initialization bugs...
	// which have been resolved
//	set_heartBlink(retVal);

	while(1)
	{
#if FADER_ENABLED
		if(!ledsControlled)
		{
			extern uint8_t heartBlink; 

			if(heartBlink)
				heartUpdate();
			else
				updateLEDFader();
		}
		//This was not previously elsed... how did the heart work at all?!
		// at one point it didn't (and neither did updateLEDs()
		// but somehow it started up again
		// a/o v49: May have been a result of the hfm bug...
		// 			(how did it start up again? Something to do with an
		//           uninitialized value in HFM?)
		else
		{
			// The heartbeat may have been in input (off) mode when switched
			setoutPORT(LED_B, LED_PORT);

			updateLEDs();
		}
#else
		heartUpdate();
#endif
	}

}


