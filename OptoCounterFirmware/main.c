#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>

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

#define EEPROM_MIRROR_REPEAT_COUNT 5 /* Насколько надёжно нужно сохранять данные в EEPROM */

// -------- Переменные --------

uint16_t counter_delayed; // Значение счетччика после задержки, чтобы избежать race conditioning между нисходящим фронтом на power_sense и нисходящим фронтом на count
uint8_t delay_for_counter;
uint8_t digits[3]; // Counter разобранный на цифры
uint8_t display[3], display_c[3], display_d[3]; // Что нужно выводить на порты дисплея
uint8_t segment_number; // Номер общего провода
uint8_t i; // Итератор
uint8_t button_state; // Состояние кнопки
uint8_t button_integral; // Интеграл значений с кнопке (время зажатия)

// -------- Функции --------

// -------- EEPROM --------

// Функция записи в EEPROM из даташита. Предполагается, что во время работы функции прерывания происходить не будут.
void eeprom_write(uint16_t address, uint8_t data)
{
	while (EECR & (1<<EEWE));
	EEAR = address;
	EEDR = data;
	EECR |= (1<<EEMWE);
	EECR |= (1<<EEWE);
}

// -------- -------- --------

// Функция чтения из EEPROM из даташита. Предполагается, что во время работы функции прерывания происходить не будут.
uint8_t eeprom_read(uint16_t address)
{
	while (EECR & (1<<EEWE));
	EEAR = address;
	EECR |= (1<<EERE);
	return EEDR;
}

// -------- -------- --------

// Надёжно считать данные из EEPROM
//  offset - смещение начала данных в EEPROM, указатель на начало
//  size - размер данных, байт
//  repeat_count - сколько раз данные повторяются в EEPROM
//  data - сами данные
void eeprom_get_data(uint16_t offset, uint8_t size, uint8_t repeat_count, uint8_t *data)
{
	uint8_t i;             // Универсальный итератор
	uint8_t data_byte;     // Номер байта в данных
	uint8_t data_pack;     // Номер пачки байтов с данными
	uint8_t cur_byte;      // Считанный байт
	uint8_t bits_count[8]; // Счетчики встречаемости битов

	cli(); // Выключаем прерывания

	// Перебираем номера байтов в массиве с солью
	for (data_byte = 0; data_byte < size; data_byte++)
	{
		// Очищаем счетчик битов
		for (i = 0; i < 8; i++)
		bits_count[i] = 0;

		// Заполняем счетчик битов
		for (data_pack = 0; data_pack < repeat_count; data_pack++)
		{
			// Считываем байт
			cur_byte = eeprom_read(offset + data_pack * size + data_byte);

			// Инкрементируем нужный счетчик
			for (i=0; i < 8; i++)
			{
				bits_count[i] += cur_byte & 0x01;
				cur_byte >>= 1;
			}
		}

		// Собираем байт по значениям счетчиков
		cur_byte = 0x00;
		for (i=0; i < 8; i++)
		{
			cur_byte <<= 1;
			cur_byte |= ((bits_count[7 - i] > (repeat_count >> 1)) ? 0x01 : 0x00);
		}

		// Записываем байт в соответствующее место в массиве с солью
		data[data_byte] = cur_byte;
	}

	sei(); // Включаем прерывания
}

// -------- -------- --------

// Надёжно записать данные в EEPROM
//  offset - смещение начала данных в EEPROM, указатель на начало
//  size - размер данных, байт
//  repeat_count - сколько раз данные повторяются в EEPROM
//  data - сами данные
void eeprom_put_data(uint16_t offset, uint8_t size, uint8_t repeat_count, uint8_t *data)
{
	cli(); // Выключаем прерывания
	volatile static uint8_t data_byte; // Номер байта в данных
	volatile static uint8_t data_pack; // Номер пачки байтов с данными
	volatile static uint8_t data_tmp;  // Временная переменная для данных
	volatile static uint16_t addr; // Временная переменная для адреса
	for (data_pack = 0; data_pack < repeat_count; data_pack++)
	for (data_byte = 0; data_byte < size; data_byte++)
	{
		addr = offset + data_pack * size + data_byte;
		data_tmp = eeprom_read(addr);
		// Записываем только отличающиеся байты для увеличения скорости и времени жизни eeprom
		if (data_tmp != data[data_byte])
		{
			eeprom_write(addr, data[data_byte]);
		}
	}
	sei(); // Включаем прерывания
}

// -------- -------- --------

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
	counter_delayed = COUNTER;
}

// -------- -------- --------

// Инициализация прерываний
void interrupt_init(void)
{
	// INT0 - falling edge, INT1 - falling edge
    MCUCR = (1 << ISC11) | (0 << ISC10) | (1 << ISC01) | (0 << ISC00);
	// INT0 - enable, INT1 - disable
	GICR = (0 << INT1) | (1 << INT0);
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
	PORTD |= (1 << PIND2); // Подтягивающий резистор для POWER_SENSE
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
	static uint16_t counter_temp;
	counter_temp = counter_delayed;
	
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

void shutdown_display(void)
{
	display[0] = CHAR_;
	display[1] = CHAR_;
	display[2] = CHAR_;
	for (i=0; i<3; i++)
	{
		display_c[i] = display[i] & 0b00111111;
		display_d[i] = ((display[i] & 0b11000000) >> 6);
	}
}

void eeprom_write_counter(void)
{
	cli(); // Отключаем прерывания
	shutdown_display(); // Выключаем дисплей на время сохранения для экономии энергии
	eeprom_put_data(0, sizeof(counter_delayed), EEPROM_MIRROR_REPEAT_COUNT, (uint8_t*)(&counter_delayed));
	sei(); // Включаем прерывания
}

void eeprom_read_counter(void)
{
	cli(); // Отключаем прерывания
	eeprom_get_data(0, sizeof(COUNTER), EEPROM_MIRROR_REPEAT_COUNT, (uint8_t*)(&COUNTER));
	counter_delayed = COUNTER;
	sei(); // Включаем прерывания
}

// Инициализация Watchdog Timer-а
void init_wdt(void)
{
	asm("wdr");
	WDTCR |= (1 << WDCE) | (1 << WDE);
	WDTCR = (1 << WDE) | (1 << WDP2) | (1 << WDP1) | (1 << WDP0); // 2 секундs
}

// -------- INT --------

ISR(INT0_vect)
{
	eeprom_write_counter();
}

// -------- MAIN --------

int main(void)
{
	// Инициализация
	init_wdt();
	gpio_init();
	interrupt_init();
	t1_init();
	eeprom_read_counter();
	sei();
	
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
	    
		if (COUNTER != counter_delayed)
		{
			delay_for_counter += 1;
			if (delay_for_counter >= 0xff)
			{
				counter_delayed = COUNTER;
			}
		} else {
			delay_for_counter = 0;
		}

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

// TODO: add pull-down resistor parallel to zener diode

