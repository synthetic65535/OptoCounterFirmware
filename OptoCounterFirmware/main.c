#include <avr/io.h>

// -------- ��������� --------

#define F_CPU 8000000

#define CHAR_0 0b00111111
#define CHAR_1 0b00000110
#define CHAR_2 0b01011011
#define CHAR_3 0b01001111
#define CHAR_4 0b01100110
#define CHAR_5 0b01101101
#define CHAR_6 0b01111101
#define CHAR_7 0b00000111
#define CHAR_8 0b01111111
#define CHAR_9 0b01101111
#define CHAR_  0b00000000
#define CHAR_E 0b01111001
#define CHAR_R 0b01010000

// Button State
#define BUTTON_RELEASED 0
#define BUTTON_PRESSED 1

#define IS_BUTTON_PRESSED ((PIND & (1 << PIND3)) == 0)

#define COUNTER TCNT1

// -------- ���������� --------

uint16_t counter_temp; // �������� �������� ��� �������
uint8_t digits[3]; // Counter ����������� �� �����
uint8_t display[3], display_c[3], display_d[3]; // ��� ����� �������� �� ����� �������
uint8_t segment_number; // ����� ������ �������
uint8_t i; // ��������
uint8_t button_state; // ��������� ������
uint8_t button_integral; // �������� �������� � ������ (����� �������)

// -------- ������� --------

// ������������� ����������� ��������
void t1_init(void)
{
	TCCR1A = 0x00; // ���������� ���� ������
	TCCR1B = (1 << CS12) | (1 << CS11) | (1 << CS10); // T1 on rising edge
	TCNT1 = 0x00; // �������� �������
	OCR1A = 0x00;
	OCR1B = 0x00;
	ICR1 = 0x00;
	TIMSK = 0x00;
	// TODO: ���������� �� ���������� 999 � �������������� ���������
}

// -------- -------- --------

// ������������� ����������
void interrupt_init(void)
{
}

// -------- -------- --------

// ������������� ������ �����-������
void gpio_init(void)
{
	// ��������� ������������� ��������� �� ���� ������
	PORTB = 0;
	PORTC = 0;
	PORTD = 0;
	
	// �������� ����������� ������������� ���������
	PORTD |= (1 << PIND3); // ������������� �������� ��� ������
	
	// ��-��������� ������� ���� ������� - ����
	DDRB = 0;
	DDRC = 0;
	DDRD = 0;
	
	// ����������� ����������� ���� �� �����
	DDRB |= (1 << PINB0) | (1 << PINB1) | (1 << PINB2); // ����� ������ DIG1..DIG3
	DDRC |= (1 << PINC0) | (1 << PINC1) | (1 << PINC2) | (1 << PINC3) | (1 << PINC4) | (1 << PINC5); // ��������
	DDRD |= (1 << PIND0) | (1 << PIND1); // ��������
}

// -------- -------- --------

// �������� �������� ��� ������� ��������
void update_display(void)
{
	counter_temp = COUNTER;
	
	digits[2] = counter_temp % 10;
	counter_temp /= 10;
	digits[1] = counter_temp % 10;
	counter_temp /= 10;
	digits[0] = counter_temp % 10;
	
	if (counter_temp > 9)
	{
		// ERR - ������������ ��������
		display[0] = CHAR_E;
		display[1] = CHAR_R;
		display[2] = CHAR_R;
	} else {
		for (i=0; i<3; i++)
		{
			switch (digits[i])
			{
				case 0: {display[i] = CHAR_0;} break;
				case 1: {display[i] = CHAR_1;} break;
				case 2: {display[i] = CHAR_2;} break;
				case 3: {display[i] = CHAR_3;} break;
				case 4: {display[i] = CHAR_4;} break;
				case 5: {display[i] = CHAR_5;} break;
				case 6: {display[i] = CHAR_6;} break;
				case 7: {display[i] = CHAR_7;} break;
				case 8: {display[i] = CHAR_8;} break;
				case 9: {display[i] = CHAR_9;} break;
				case 10: {display[i] = CHAR_;} break;
				case 11: {display[i] = CHAR_E;} break;
				case 12: {display[i] = CHAR_R;} break;
				default: {display[i] = CHAR_;} break;
			}
		}
	}
	
	// ������� ������ ����
	if (display[0] == CHAR_0)
	{
		display[0] = CHAR_;
		if (display[1] == CHAR_0)
		{
			display[1] = CHAR_;
		}
	}
	
	for (i=0; i<3; i++)
	{
		display_c[i] = display[i] & 0b00111111;
		display_d[i] = ((display[i] & 0b11000000) >> 6);
	}
}

// -------- MAIN --------

int main(void)
{
	// �������������
	gpio_init();
	interrupt_init();
	t1_init();
	
    while (1) 
    {
	    // -------- ��������� �������� --------
		
	    if (button_state == BUTTON_PRESSED)
	    {
		    COUNTER = 0;
	    }
		
		// -------- ���������� ������� --------
		
		if (COUNTER > 999)
			COUNTER = 0;
	    
		update_display();
		
		// -------- ������������ ��������� --------
		
		if (++segment_number > 2)
			segment_number = 0;
		
		PORTB = 0x00; // ��������� ����� ������ ����� �������� ��������
		PORTC = display_c[segment_number]; // ������ ���������� �� ��������
		PORTD = display_d[segment_number] | (1 << PIND3) /* ������������� �������� ��� ������ */;
		PORTB = (1 << segment_number); // �������� ������ ����� �����
		
		// -------- ��������� ������ --------
		
		if (IS_BUTTON_PRESSED)
		{
			if (button_integral < 0xff)
				button_integral++;
			
			if (button_integral >= 0xff)
			{
				if (button_state == BUTTON_RELEASED)
				{
					// ������ ������
					button_state = BUTTON_PRESSED;
				}
			}
		} else {
			if (button_integral > 0)
				button_integral--;

			if (button_integral == 0)
			{
				if (button_state == BUTTON_PRESSED)
				{
					// ������ ������
					button_state = BUTTON_RELEASED;
				}
			}
		}

		// -------- ����� Watchdog Timer-� --------
		
		asm("wdr");
    }
}

