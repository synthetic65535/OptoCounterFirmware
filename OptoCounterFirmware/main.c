#include <avr/io.h>

// -------- Константы --------

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

// -------- Переменные --------

uint16_t counter_temp; // Значение счетчика для разбора
uint8_t digits[3]; // Counter разобранный на цифры
uint8_t display[3], display_c[3], display_d[3]; // Что нужно выводить на порты дисплея
uint8_t segment_number; // Номер общего провода
uint8_t i; // Итератор
uint8_t button_state; // Состояние кнопки
uint8_t button_integral; // Интеграл значений с кнопке (время зажатия)

// -------- Функции --------

// Инициализация аппаратного счетчика
void t1_init(void)
{
	TCCR1A = 0x00; // Нормальный счет вперед
	TCCR1B = (1 << CS12) | (1 << CS11) | (1 << CS10); // T1 on rising edge
	TCNT1 = 0x00; // Обнуляем счетчик
	OCR1A = 0x00;
	OCR1B = 0x00;
	ICR1 = 0x00;
	TIMSK = 0x00;
	// TODO: прерывание по достижению 999 и автоматическое обнуление
}

// -------- -------- --------

// Инициализация прерываний
void interrupt_init(void)
{
}

// -------- -------- --------

// Инициализация портов ввода-вывода
void gpio_init(void)
{
	// Отключаем подтягивающие резисторы на всех портах
	PORTB = 0;
	PORTC = 0;
	PORTD = 0;
	
	// Включаем необходимые подтягивающие резисторы
	PORTD |= (1 << PIND3); // Подтягивающий резистор для кнопки
	
	// По-умолчанию функция всех выводов - вход
	DDRB = 0;
	DDRC = 0;
	DDRD = 0;
	
	// Настраиваем необходимые пины на выход
	DDRB |= (1 << PINB0) | (1 << PINB1) | (1 << PINB2); // Общие выводы DIG1..DIG3
	DDRC |= (1 << PINC0) | (1 << PINC1) | (1 << PINC2) | (1 << PINC3) | (1 << PINC4) | (1 << PINC5); // Сегменты
	DDRD |= (1 << PIND0) | (1 << PIND1); // Сегменты
}

// -------- -------- --------

// Обновить значения для каждого сегмента
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
		// ERR - переполнение счетчика
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
	
	// Удалить первые нули
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
	// Инициализация
	gpio_init();
	interrupt_init();
	t1_init();
	
    while (1) 
    {
	    // -------- Обнуление счетчика --------
		
	    if (button_state == BUTTON_PRESSED)
	    {
		    COUNTER = 0;
	    }
		
		// -------- Обновление дисплея --------
		
		if (COUNTER > 999)
			COUNTER = 0;
	    
		update_display();
		
		// -------- Переключение сегментов --------
		
		if (++segment_number > 2)
			segment_number = 0;
		
		PORTB = 0x00; // Выключаем общие выводы чтобы избежать мерцания
		PORTC = display_c[segment_number]; // Подаем напряжение на сегменты
		PORTD = display_d[segment_number] | (1 << PIND3) /* Подтягивающий резистор для кнопки */;
		PORTB = (1 << segment_number); // Включаем нужный общий вывод
		
		// -------- Обработка кнопки --------
		
		if (IS_BUTTON_PRESSED)
		{
			if (button_integral < 0xff)
				button_integral++;
			
			if (button_integral >= 0xff)
			{
				if (button_state == BUTTON_RELEASED)
				{
					// Кнопка нажата
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
					// Кнопка отжата
					button_state = BUTTON_RELEASED;
				}
			}
		}

		// -------- Сброс Watchdog Timer-а --------
		
		asm("wdr");
    }
}

