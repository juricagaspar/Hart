/********************************* INFORMATION *********************************/
/*	 	
 * 	Author:				Jurica Gaspar
 * 	Version:			V1.0.0
 * 	Date:					28-10-2023 
 * 	Target:				STM32F103C8T6 (8MHz)
 * 	Note:					Tested(Working)
 *
 */
 
/********************************** INCLUDES ***********************************/
#include 	<stdio.h>
#include	<stdint.h>
#include	<string.h>
#include 	<stdlib.h> 			// rand();
#include 	<stdbool.h>
#include 	<math.h>				// gamma correction uses pow();

#include 	<stm32f10x.h>
#include	<stm32f10x_tim.h>
#include	<stm32f10x_rcc.h>
#include	<stm32f10x_gpio.h>
#include	<stm32f10x_dma.h>

/********************************* USER DEFINES ********************************/
#define	SetTimer(x,t)					x=osTime+t
#define	TimerExpired(x)				osTime>=x
#define	TimerNotExpired(x)		osTime<x


#define	TICK_TIME								1										/* 1ms  */
#define KEYBOARD_TIMER					(10/TICK_TIME)			/* 10ms */
#define	AUTOREAPEAT_START_DELAY	(600/TICK_TIME)
#define	AUTOREAPEAT_DELAY				(300/TICK_TIME)			
#define SECOND									1000/TICK_TIME
#define MINUTE									(SECOND*60)					/* sec * 60 */
#define HOUR										(MINUTE*60)					/* min * 60 */
#define LONG_PRESS							500
#define BLOCK_AUTOREPEAT_TIME		200

#define PLUS_PRESSED						0x0100							/*0b0000000100000000*/
#define ON_OFF_PRESSED					0x0200							/*0b0000001000000000*/
#define MINUS_PRESSED						0x0400							/*0b0000010000000000*/
#define MODE_PRESSED						0x0800							/*0b0000100000000000*/
#define PUSHBUTTON_BITS					0x0F00							/*0b0000111100000000*/

#define BLACK										0x000000
#define WHITE										0xFFFFFF
#define RED	 										0xFF0000  
#define ORANGE   								0xFF8000  
#define YELLOW   								0xFFFF00  
#define GREEN   								0x00FF00  
#define CYAN_GREEN   						0x00FF80  
#define CYAN   									0x00FFFF  
#define LIGHT_BLUE   						0x0080FF  
#define PURPLE   								0x8000FF  
#define PINKISH_MAGENTA   			0xFF00FF  
#define BRIGHT_PINK   					0xFF0080  

#define LED_NUM           			32
#define LED_BUFFER_SIZE     		24*LED_NUM+42

#define BRIGHTNESS_START				0.30
#define BRIGHTNESS_INCREMENT 		0.05
#define BRIGHTNESS_MIN					0.15
#define BRIGHTNESS_MAX					0.50	

#define LIST_SIZE								10
const uint32_t ColorList[LIST_SIZE] = 
{	
 RED,	 								 
 ORANGE,   						  
 YELLOW,   						     	    			  
 GREEN,   						    		 
 CYAN_GREEN,   				    				 
 CYAN,   							     				  
 LIGHT_BLUE,   				     		  							       			  
 PURPLE,   						    					  
 PINKISH_MAGENTA,   	  			 
 BRIGHT_PINK   			   					
};

#define MODE_NUMBER							(14-1)
typedef enum
{
 OFF_MODE = 0,
 RED_MODE,	 								 
 ORANGE_MODE,   						  
 YELLOW_MODE,   						     	    			  
 GREEN_MODE,   						    		 
 CYAN_GREEN_MODE,   				    				 
 CYAN_MODE,   							     				  
 LIGHT_BLUE_MODE,   				     		  							       			  
 PURPLE_MODE,   						    					  
 PINKISH_MAGENTA_MODE,   	  			 
 BRIGHT_PINK_MODE,	
 RAINBOW_MODE,
 COLOR_LOOP_MODE,		
 RUN_MODE
}tModes;	

typedef struct
{
	uint8_t 	mode;
	uint8_t 	last_mode;
	uint32_t	color;
	float 		brightness;	
	float 		gamma;
}tHartStruct;

/*********************************** VARIABLES *********************************/		 
					uint8_t 		LED_Color	[LED_NUM][3];				// LED color data 
					uint16_t 		LED_Data	[LED_BUFFER_SIZE];	// Transmition Buffer 	
volatile 	uint32_t 		osTime;
					tHartStruct Hart;
					
/***************************** FUNCTION PROTOTYPES *****************************/
		void 	HSI_Init(void);  
		void	InitSysTimer(void);
		void	SysTick_Handler(void);
		void	Wait(uint16_t ms);

		void	InitRCC(void);
		void	InitGPIO(void);
		void	InitPWM(void);
		void	InitDMA(void);
		void	InitStruct(void);
		void	InitHardware(void);

uint16_t 	ScanKeys(void);
uint16_t 	GetKey(void);

 uint8_t 	GammaCorrection(uint8_t value, float gamma);
		void	SetLED(uint8_t led, uint32_t color, float brightness);
		void	SetLEDs(uint8_t start_led, uint8_t finish_led, uint32_t color, float brightness);
		void	SetAll(uint32_t color, float brightness);
		void	SendData(void);
		
/********************************** FUNCTIONS **********************************/
void InitSysTimer(void)
{
	// Update SystemCoreClock value
	SystemCoreClockUpdate();

	// Configure the SysTick timer to overflow every 1 us
	// SysTick_Config input parameter is the number of ticks between two interrupts.
	//SysTick_Config(SystemCoreClock / 1000000);	 	

	// Configure the SysTick timer to overflow every 1 ms
	// SysTick_Config input parameter is the number of ticks between two interrupts.
	SysTick_Config(SystemCoreClock / 1000);	 		
}

// SysTick_Handler function will be called every 1 ms
void SysTick_Handler(void)
{
	osTime++;	
}

void Wait(uint16_t ms)
{
	uint32_t DelayTimer;

	SetTimer(DelayTimer, ms);
	while(!(TimerExpired(DelayTimer))) {}
}


/* ScanKeys() returnes pushbutton value every 10ms */
uint16_t ScanKeys(void)
{
	volatile uint32_t Timeout = 0;
	
	SetTimer(Timeout,KEYBOARD_TIMER);		// Timeout = 10ms	 
	while (!(TimerExpired(Timeout)));		//!(osTime>=Timeout) 
	return((~GPIO_ReadInputData(GPIOB)) & PUSHBUTTON_BITS);						
}

/* 	
	GetKey() is used when button autorepeat is needed, while button is  
	pressed it behaves like multiple presses.
	After initial press it takes 600ms start to autorepeat. 
*/

uint16_t GetKey(void)
{
	static uint32_t Autorepeat = 0;		
	static uint16_t LastKey = 0;		
																		
	uint16_t tmp;
	tmp = ScanKeys();	 						
	
	if((tmp!=0)&&(tmp==LastKey))						
	{
		if(osTime>=Autorepeat)					
		{
			Autorepeat = osTime+AUTOREAPEAT_DELAY;	
			return LastKey;								
		}
		else
		{
			return 0;								
		}
	}
	LastKey = tmp;									
	Autorepeat = osTime+AUTOREAPEAT_START_DELAY;			
	return tmp;														
}


void HSI_Init(void) 
{
	// Enable HSI
	RCC_HSICmd(ENABLE);
	
	// Wait until HSI is ready
	while (RCC_GetFlagStatus(RCC_FLAG_HSIRDY) == RESET);

	// Set HSI as the system clock
	RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
	
	// Wait until HSI is actually used as the system clock
	while (RCC_GetSYSCLKSource() != 0x00);
}

void InitRCC(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);					// Enable clock for GPIOA
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);					// Enable clock for GPIOB
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);					// Enable clock for TIM2
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);            // Enable clock for DMA1                   
}

void InitGPIO(void)
{
	GPIO_InitTypeDef 					GPIO_InitStruct;
	
	// Initialize PA2 as push-pull alternate function (PWM output) 
	GPIO_InitStruct.GPIO_Pin 		= GPIO_Pin_2;
	GPIO_InitStruct.GPIO_Mode 	= GPIO_Mode_AF_PP;			
	GPIO_InitStruct.GPIO_Speed 	= GPIO_Speed_2MHz;
	GPIO_Init(GPIOA, &GPIO_InitStruct);
	
	// Initialize PB8,PB9,PB10 and PB11 as pushbutton input with pull-up  
	GPIO_InitStruct.GPIO_Pin 		= GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11;
	GPIO_InitStruct.GPIO_Mode 	= GPIO_Mode_IPU;			
	GPIO_InitStruct.GPIO_Speed 	= GPIO_Speed_2MHz;
	GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void InitPWM(void)
{
	TIM_TimeBaseInitTypeDef		TIM_TimeBaseInitStruct;
	TIM_OCInitTypeDef 				TIM_OCInitStruct;
	
	// PWM_Frequency (Hz) = Timer_clock / ((TIM_Prescaler + 1)*(Tim_Period + 1))
	// PWM_Frequency (Hz) = 72000000Hz / (2 + 1)*(29 + 1) = 800kHz 

	// TIM_Period = ((Timer_clock / (TIM_Prescaler + 1))/ (PWM_Frequency)) - 1 
	// TIM_Period = ((72MHz / (2 + 1))/ (800kHz)) - 1 = 29
	
	// TIM_Prescaler = ((Timer_clock / (Period + 1))/PWM_Frequency) - 1 
	// TIM_Prescaler = ((72MHz / (29 + 1))/800kHz) - 1 = 2 
	
	TIM_TimeBaseInitStruct.TIM_Prescaler 			= 2;
	TIM_TimeBaseInitStruct.TIM_Period 				= 29; 									
	TIM_TimeBaseInitStruct.TIM_ClockDivision 	= TIM_CKD_DIV1;
	TIM_TimeBaseInitStruct.TIM_CounterMode 		= TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStruct);
	
	// PWM1 Mode configuration: Channel1 
	TIM_OCInitStruct.TIM_OCMode 				= TIM_OCMode_PWM1;
	TIM_OCInitStruct.TIM_OutputState 		= TIM_OutputState_Enable;
	TIM_OCInitStruct.TIM_Pulse 					= 0;
	TIM_OCInitStruct.TIM_OCPolarity 		= TIM_OCPolarity_High;
	TIM_OC3Init(TIM2, &TIM_OCInitStruct);

	// Start TIM2
	TIM_Cmd(TIM2, ENABLE);
	TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
}
	
	// Duty cycle equation:
	// TIM_Pulse = (((TIM_Period + 1) * duty_cycle) / 100) - 1
	// Ex. 32% duty cycle = 0.4us high and 0.85us low
	// Ex. 68% duty cycle = 0.85us high and 0.4us low
	// TIM_Pulse = (((29 + 1) * 32) / 100) - 1 = 9
	// TIM_Pulse = (((29 + 1) * 68) / 100) - 1 = 20	
	// Initialize PWM value with duty cycle of 0%
		
void InitDMA(void) 
{    
    DMA_InitTypeDef DMA_InitStruct;
    
		DMA_DeInit(DMA1_Channel1);                	                               	// Deinitialize DAM1 Channel 1 to their default reset values.
    DMA_InitStruct.DMA_PeripheralBaseAddr = 	(uint32_t)&TIM2->CCR3;            // Specifies Physical address of the peripheral in this case Timer 2 CCR1
    DMA_InitStruct.DMA_MemoryBaseAddr 		= 	(uint32_t)&LED_Data;							// Specifies the buffer memory address
    DMA_InitStruct.DMA_DIR 								= 	DMA_DIR_PeripheralDST;            // Data transfered from memory to peripheral
    DMA_InitStruct.DMA_BufferSize 				= 	LED_BUFFER_SIZE;                  // Specifies the buffer size
    DMA_InitStruct.DMA_PeripheralInc		  = 	DMA_PeripheralInc_Disable;        // Do not incrament the peripheral address
    DMA_InitStruct.DMA_MemoryInc 					= 	DMA_MemoryInc_Enable;             // Incrament the buffer index
    DMA_InitStruct.DMA_PeripheralDataSize = 	DMA_PeripheralDataSize_HalfWord;  // Specifies the peripheral data width
    DMA_InitStruct.DMA_MemoryDataSize 		= 	DMA_MemoryDataSize_HalfWord;      // Specifies the memory data width
    DMA_InitStruct.DMA_Mode 							= 	DMA_Mode_Normal;                  // Specifies the operation mode. Normal or Circular
    DMA_InitStruct.DMA_Priority 					= 	DMA_Priority_High;                // Specifies the software priority
    DMA_InitStruct.DMA_M2M 								= 	DMA_M2M_Disable;                  //
    
    DMA_Init(DMA1_Channel1, &DMA_InitStruct);                                   // Initialize DAM1 Channel 1 to values specified in the DMA_InitStruct structure.
    TIM_DMACmd(TIM2, TIM_DMA_CC3, ENABLE);                                      // Enables TIM2's DMA request. TIM_DMA_CC1 : TIM Capture Compare 1 DMA source 
}


void InitStruct(void)
{
	Hart.mode 				= OFF_MODE;
	Hart.brightness 	= BRIGHTNESS_START;
	Hart.gamma 				= 2.2;
}

void InitHardware(void)
{
	HSI_Init();
	InitSysTimer();
	InitRCC();
	InitGPIO();
	InitPWM();
	InitDMA();
	InitStruct();
}




// The human eye perceives brightness non-linearly, so gamma correction
// helps make the dimming or brightness adjustments appear more natural.
// Typical values range from 2.0 to 2.8. A common default is 2.2.

uint8_t GammaCorrection(uint8_t value, float gamma)
{
	return (uint8_t)(pow(value / 255.0, gamma) * 255);
}	

void SetLED(uint8_t led, uint32_t color, float brightness)
{
	uint8_t red, green, blue;
	
	red = (uint8_t)(color >> 16);
	green = (uint8_t)(color >> 8);
	blue = (uint8_t)color;

	// Scale brightness
	red = (uint8_t)(red * brightness);
  green	= (uint8_t)(green * brightness);
  blue = (uint8_t)(blue * brightness);

  // Gamma correction
  red = GammaCorrection(red, Hart.gamma);
  green = GammaCorrection(green, Hart.gamma);
  blue = GammaCorrection(blue, Hart.gamma);
	
	// G7-G0 | R7-R0 |	B7-B0
	LED_Color[led][0] = green;
	LED_Color[led][1] = red;
	LED_Color[led][2] = blue;
}

void SetLEDs(uint8_t start_led, uint8_t finish_led, uint32_t color, float brightness)
{
	for(uint8_t led = start_led; led<=finish_led; led++)
	{
		SetLED(led, color, brightness);
	}	
}

void SetAll(uint32_t color, float brightness)
{
	for(uint8_t led = 0; led < LED_NUM; led++) 	
	{
		SetLED(led, color, brightness);
	}
}

void SendData(void)
{
	uint8_t led, rgb, temp, bit;
	uint16_t mem_adr = 0;
	
	for(led = 0; led<LED_NUM; led++)
	{
		for(rgb = 0; rgb < 3; rgb++)
		{
			temp = LED_Color[led][rgb];			// Saves temp. r, g or b value 
			for(bit = 0; bit < 8; bit++)
			{
				// MSB is sent first  
				if(temp & 0x80) LED_Data[mem_adr++] = 2; 	//PWM_DATA_HIGH;
				else LED_Data[mem_adr++] = 1; 						//PWM_DATA_LOW;
				temp = temp << 1;
			}
		}
	}
	
	uint16_t buffersize = (24*LED_NUM)+42;
	while(mem_adr<buffersize) LED_Data[mem_adr++] = 0; // NO_PWM_DATA
	
	DMA_SetCurrDataCounter(DMA1_Channel1, LED_BUFFER_SIZE);		// Load number of bytes to be transferred
	DMA_Cmd(DMA1_Channel1, ENABLE);                         	// Enable DMA channel 1
	TIM_Cmd(TIM2, ENABLE);                                  	// Enable Timer 2
	while(!DMA_GetFlagStatus(DMA1_FLAG_TC1));               	// Wait until transfer complete
	TIM_Cmd(TIM2, DISABLE);                                   // Disable Timer 2
	DMA_Cmd(DMA1_Channel1, DISABLE);                          // Disable DMA channel 1
	DMA_ClearFlag(DMA1_FLAG_TC1);                             // Clear DMA1 Channel 1 transfer complete flag	
}

void HartStart(uint32_t color, float brightness) 
{
	uint8_t time_delay = 20;
	uint8_t start_led = 16;
	
	SetLED(start_led, color, brightness);
	Wait(time_delay);
	
	for(uint8_t i = 1; i<=16; i++)
	{
		SetLED(start_led-i, color, brightness);
		SendData();
		SetLED(start_led+i, color, brightness);
		SendData();
		Wait(time_delay);
	}
}

void HartBeat(uint32_t color, float brightness)
{
	static uint8_t State = 0;
	static uint32_t DelayTimer = 0;
	
	switch(State)
	{
		case 0:
		case 2:	
					if(TimerExpired(DelayTimer))		
					{
						SetTimer(DelayTimer, 150);
						SetAll(color, brightness);
						SendData();
						State++;
					}	
					break;
		
		case 1:
					if(TimerExpired(DelayTimer))		
					{
						SetTimer(DelayTimer, 100);
						SetAll(BLACK, 0);
						SendData();
						State++;
					}
					break;
							
		case 3:
					if(TimerExpired(DelayTimer))		
					{
						SetAll(BLACK, 0);
						SendData();
						SetTimer(DelayTimer, 600);
						State=0;
					}	
					break;

		default:
				break;
	
	}
}

// Helper function: Hue to RGB conversion
uint32_t HueToRGB(uint8_t hue)
{
	uint8_t r, g, b;  
	uint8_t region = hue / 43;           	// Hue divided into 6 regions (0–5)
	uint8_t remainder = (hue % 43) * 6; 	// Remainder for smooth transition
	uint8_t p = 0;                      	// Min value (we assume max brightness)
	uint8_t q = (255 - remainder);      	// Gradual transition value
	uint8_t t = remainder;              	// Gradual transition value

	// Assign RGB based on the hue region
	switch (region) 
	{
		case 0: r = 255; g = t;   b = p;   break; // Red to Yellow
		case 1: r = q;   g = 255; b = p;   break; // Yellow to Green
		case 2: r = p;   g = 255; b = t;   break; // Green to Cyan
		case 3: r = p;   g = q;   b = 255; break; // Cyan to Blue
		case 4: r = t;   g = p;   b = 255; break; // Blue to Magenta
		case 5: r = 255; g = p;   b = q;   break; // Magenta to Red
	}

	// Pack the RGB values into a 32-bit integer (0xRRGGBB)
	return (r << 16) | (g << 8) | b;
}

void HartRainbow(float brightness)
{
	static uint8_t hue = 0;
	
	for(uint8_t led = 0; led<=LED_NUM-1; led++)
	{
		uint32_t next_color = HueToRGB(hue);
		SetLED(led, next_color, brightness);
		SendData();
		hue=hue+8;
	}
}

void HartColorLoop(float brightness)
{
	static uint32_t DelayTimer = 0;
	static uint8_t hue = 0;

	if(TimerExpired(DelayTimer))
	{
		SetTimer(DelayTimer, 50);  // Set a 10ms delay
		// Convert the hue to an RGB color
		uint32_t next_color = HueToRGB(hue);
		// Set all LEDs to the current color, applying brightness
		SetAll(next_color, brightness);
		// Send updated data to the LEDs
		SendData();
		// Increment hue to cycle through rainbow colors
		hue++;
	}
}

void HartRunningLoop(float brightness)
{
	static uint8_t hue = 0;
	
	for(uint8_t led = 0; led<LED_NUM; led++)
	{
		uint32_t next_color = HueToRGB(hue);
		SetLED(led, next_color, brightness);
		SendData();
		hue=hue+1;
	}
}


int main(void)
{
	uint16_t NewKey;
	InitHardware();
	SetAll(BLACK, Hart.brightness);
	SendData();
	
	while(1)
	{	
		NewKey = GetKey();
		if(NewKey!=0)
		{	
			switch(NewKey)
			{
				case ON_OFF_PRESSED:
							while(ScanKeys()==ON_OFF_PRESSED){}
							if(Hart.mode!=OFF_MODE)
							{
								Hart.mode = OFF_MODE;
								SetAll(BLACK, 0);
								SendData();
							}	
							else 
							{	
								Hart.mode = RED_MODE;
								HartStart(RED, Hart.brightness);
							}	
							break;

				case PLUS_PRESSED:
							if(Hart.mode!=OFF_MODE)
							{
								if(Hart.brightness<BRIGHTNESS_MAX)	
								{
									Hart.brightness = Hart.brightness + BRIGHTNESS_INCREMENT;
									switch(Hart.mode)
									{									
										case RED_MODE:
										case ORANGE_MODE:
										case YELLOW_MODE:   						     	    			  
										case GREEN_MODE:   						    		 
										case CYAN_GREEN_MODE:   				    				 
										case CYAN_MODE:  							     				  
										case LIGHT_BLUE_MODE:   				     		  							       			  
										case PURPLE_MODE: 
										case PINKISH_MAGENTA_MODE:	
										case BRIGHT_PINK_MODE:
													SetAll(ColorList[Hart.mode-1], Hart.brightness);
													SendData();										
													break;
										
										case RAINBOW_MODE:
													HartRainbow(Hart.brightness);
													break;	
										
										default:
													break;
									}
								}								
							}	
							break;	
				
				case MINUS_PRESSED:
							if(Hart.mode!=OFF_MODE)
							{
								if(Hart.brightness>=BRIGHTNESS_MIN) 
								{	
									Hart.brightness = Hart.brightness - BRIGHTNESS_INCREMENT;
									switch(Hart.mode)
									{									
										case RED_MODE:
										case ORANGE_MODE:
										case YELLOW_MODE:   						     	    			  
										case GREEN_MODE:   						    		 
										case CYAN_GREEN_MODE:   				    				 
										case CYAN_MODE:  							     				  
										case LIGHT_BLUE_MODE:   				     		  							       			  
										case PURPLE_MODE: 
										case PINKISH_MAGENTA_MODE:	
										case BRIGHT_PINK_MODE:
													SetAll(ColorList[Hart.mode-1], Hart.brightness);
													SendData();
													break;
										
										case RAINBOW_MODE:
													HartRainbow(Hart.brightness);
													break;	
									
										default:
													break;
									}
								}
							}	
							break;

				case MODE_PRESSED:
							if(Hart.mode!=OFF_MODE)
							{
								if(Hart.mode == RUN_MODE) Hart.mode = RED_MODE;
								else Hart.mode++;	
								
								switch(Hart.mode)
								{									
									case RED_MODE:
									case ORANGE_MODE:
									case YELLOW_MODE:   						     	    			  
									case GREEN_MODE:   						    		 
									case CYAN_GREEN_MODE:   				    				 
									case CYAN_MODE:  							     				  
									case LIGHT_BLUE_MODE:   				     		  							       			  
									case PURPLE_MODE: 
									case PINKISH_MAGENTA_MODE:	
									case BRIGHT_PINK_MODE:
												HartStart(ColorList[Hart.mode-1], Hart.brightness);
												break;
									
									case RAINBOW_MODE:
												HartRainbow(Hart.brightness);
												break;	
									
									default:
												break;
								}
							}
							break;		
	
				default:
							break;			
			}
		}	
			
		switch(Hart.mode)
		{
			case COLOR_LOOP_MODE:		
						HartColorLoop(Hart.brightness);
						break;
			
			case RUN_MODE:
						HartRunningLoop(Hart.brightness);
						break;
			
			default:
						break;
		}		
	}
}

/******************************** END OF FILE *********************************/


