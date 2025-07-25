#include <avr/io.h>
#define F_CPU 8000000UL
#include <util/delay.h>


// Existing pin definitions
#define LED_PIN     PC0  // Pin 22 (Output)
#define CONTROL_PIN PA0  // Pin 40 (Input)

// LCD pin definitions (using PORTB and PD2)
#define LCD_RS PB0
#define LCD_EN PB1
#define LCD_D4 PB2
#define LCD_D5 PB3
#define LCD_D6 PB4
#define LCD_D7 PD2       // Using PD2 instead of PB5 to avoid SPI conflict

void LCD_Command(unsigned char cmnd) {
	// Handle upper nibble (D4-D7)
	PORTB = (PORTB & 0xC3) | ((cmnd & 0b11110000) >> 2); // D4-D6 to PB2-PB4
	if(cmnd & 0x80) PORTD |= (1<<PD2);  // D7 to PD2
	else PORTD &= ~(1<<PD2);
	
	PORTB &= ~(1<<LCD_RS); // RS=0 for command
	PORTB |= (1<<LCD_EN);  // EN=1
	_delay_us(1);
	PORTB &= ~(1<<LCD_EN); // EN=0
	_delay_us(200);
	
	// Handle lower nibble (D4-D7)
	PORTB = (PORTB & 0xC3) | ((cmnd & 0x0F) << 2); // D4-D6 to PB2-PB4
	if(cmnd & 0x08) PORTD |= (1<<PD2);  // D7 to PD2
	else PORTD &= ~(1<<PD2);
	
	PORTB |= (1<<LCD_EN);
	_delay_us(1);
	PORTB &= ~(1<<LCD_EN);
	_delay_ms(2);
}

void LCD_Char(unsigned char data) {
	// Upper nibble
	PORTB = (PORTB & 0xC3) | ((data & 0xF0) >> 2); // D4-D6 to PB2-PB4
	if(data & 0x80) PORTD |= (1<<PD2);  // D7 to PD2
	else PORTD &= ~(1<<PD2);
	
	PORTB |= (1<<LCD_RS); // RS=1 for data
	PORTB |= (1<<LCD_EN);
	_delay_us(1);
	PORTB &= ~(1<<LCD_EN);
	_delay_us(200);
	
	// Lower nibble
	PORTB = (PORTB & 0xC3) | ((data & 0x0F) << 2); // D4-D6 to PB2-PB4
	if(data & 0x08) PORTD |= (1<<PD2);  // D7 to PD2
	else PORTD &= ~(1<<PD2);
	
	PORTB |= (1<<LCD_EN);
	_delay_us(1);
	PORTB &= ~(1<<LCD_EN);
	_delay_ms(2);
}

void LCD_Init() {
	// Configure LCD pins
	DDRB |= 0x1F;  // PB0-PB4 as outputs (PB5 left alone for SPI)
	DDRD |= (1<<PD2); // PD2 as output for LCD_D7
	
	_delay_ms(20); // LCD power-up delay
	
	// LCD initialization sequence
	LCD_Command(0x02); // 4-bit mode
	LCD_Command(0x28); // 2 lines, 5x7 matrix
	LCD_Command(0x0C); // Display on, cursor off
	LCD_Command(0x06); // Increment cursor
	LCD_Command(0x01); // Clear display
	_delay_ms(2);
}

void LCD_String(char *str) {
	for(int i=0; str[i]!=0; i++) {
		LCD_Char(str[i]);
	}
}

int main(void) {
	// Your existing setup
	DDRC |= (1 << LED_PIN);       // Set LED as output
	DDRA &= ~(1 << CONTROL_PIN);  // Set CONTROL_PIN as input
	
	// Initialize LCD
	LCD_Init();
	LCD_Command(0x80); // Move cursor to first line
	LCD_String("Hello");
	
	while (1) {
		// Your existing control logic
		if (PINA & (1 << CONTROL_PIN)) {
			PORTC |= (1 << LED_PIN);  // LED ON if PA0 = 1
			} else {
			PORTC &= ~(1 << LED_PIN); // LED OFF if PA0 = 0
		}
		
		// Add any LCD updates here if needed
	}
}
