
#include "stdint.h"
#include "mcu_coro.hpp"
#include "awaitable.hpp"

#include "Arduino.h"

#define LED_PIN PB_14

mcucoro::awaitable<void> led_blinker()
{
	pinMode(LED_PIN, OUTPUT);
	int s = 0;
	for(;;)
	{
		s = !s;
		digitalWrite(LED_PIN, s);
		co_await coro_delay_ms(1240);
	}
}

void setup()
{
	mcucoro::post([](){});
}

void loop()
{
	mcucoro::executor::system_executor().poll();
}
