WEBASTO controller on a STM32F103
==========================

Первый тестовый проект по управлению webasto через stm32 контроллер.

У нас есть 2 контакта на чтение\запись в шину K-Line, 2 диода на отображение статуса, кнопка на запуск\остановку.

Выражаю благодарность по мощь и разъяснение radist1982(drive2)

Расшифровка индикации на диодах: (з-зеленый диод, к - красный диод) 
```sed
START:
		  з-ON-300-OFF
GET_PARAM:
if error: k-ON-1000-OFF
else:     з-ON
START_HEADER:
if error: k-ON-2000-OFF
SUPPORT_HEAD:
if error: к-ON-500-OFFF {3}
	if key_press: 
         з-OFF-1000-ON-1000-OFF
    if T>50:
		 к-ON-1000-OFF-ON-1000-OFF
		 з-OFF
    if АКБ<12.3:
		 к-ON-2000-OFF-ON-2000-OFF
 	 	 з-OFF
 	 	 
POMP:
 if off: rled-on-500-off-500-on-500-off-500
```
Данные K-Line брались с расшифровки (см. файл webasto-wbus.txt)
 

Расшифровка пинов:


<img src="https://github.com/oditynet/webasto_peugeot/blob/main/STM32.png" title="withwords" width="500" />
