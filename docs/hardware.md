# Hardware da estação (SCRUM-578, 579, 580)

## Componentes

| Componente | Modelo | Uso |
| --- | --- | --- |
| Microcontrolador | ESP32 DevKit V1 (ESP-WROOM-32) | coleta, Wi-Fi e MQTT |
| Sensor | DHT22 / AM2302 | temperatura do ar (-40 a 80 °C, ±0,5 °C) e umidade relativa (0 a 100 %, ±2 %) |
| Resistor | 10 kΩ | pull-up da linha de dados (dispensável no módulo com placa, que já traz) |
| Alimentação | cabo USB 5 V | ESP32 e, pelo pino 3V3, o sensor |
| Montagem | protoboard e jumpers | — |

O DHT22 funciona em 3,3 V e usa um fio de dados digital próprio (não é I²C), então liga direto no ESP32 sem conversor de nível. Os dois parâmetros já existem no catálogo do sistema (tipos "Temperatura" e "Umidade"); nenhum parâmetro novo foi criado.

## Ligações

| DHT22 | ESP32 |
| --- | --- |
| VCC (pino 1) | 3V3 |
| DATA (pino 2) | GPIO4, com 10 kΩ para 3V3 |
| NC (pino 3) | — |
| GND (pino 4) | GND |

O pino está em `include/pinos.h`. GPIO4 não interfere no boot do ESP32 (diferente de 0, 2, 12 e 15).

## Antes de energizar

- [ ] VCC do sensor no 3V3, não no 5V/VIN
- [ ] GND comum entre sensor e ESP32
- [ ] Pull-up entre DATA e 3V3 (ou módulo com resistor)
- [ ] Nenhum jumper encostando em pino vizinho

## Configuração no firmware

- Biblioteca `adafruit/DHT sensor library` (tipo `DHT22`), inicializada em `setup()`.
- Leitura a cada intervalo de coleta (mínimo 10 s; o DHT22 precisa de 2 s entre leituras).
- `NaN` ou valor fora da faixa do sensor é falha de leitura e não vai para o payload.

## Registro da montagem

Preencher depois de montar: foto da protoboard, MAC impresso na serial na inicialização e resultado do comando `status`.
