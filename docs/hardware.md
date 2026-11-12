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

## Outros sensores (SCRUM-603)

Os sensores da estação ficam na tabela `SENSORES` de `include/config.h`. Cada linha vira um campo do payload, com o valor bruto lido. A conversão para a unidade do parâmetro é feita pelo sistema, com o ganho e o offset cadastrados para o sensor.

| Fonte | Leitura | Valor enviado | Pinos |
| --- | --- | --- | --- |
| `Dht22Temperatura` | temperatura do DHT22 | °C, de -40 a 80 | `PINO_DHT` |
| `Dht22Umidade` | umidade do DHT22 | %, de 0 a 100 | `PINO_DHT` |
| `Analogico` | tensão no pino, pelo ADC de 12 bits | 0 a 4095 (0 a 3,3 V) | só GPIO 32 a 39 |
| `Digital` | nível do pino | 0 ou 1 | qualquer GPIO de entrada |

Para incluir um sensor:

1. Ligue o sensor. Sinal analógico só nos GPIO 32 a 39: os outros pinos analógicos do ESP32 não leem com o Wi-Fi ligado. O sinal não pode passar de 3,3 V.
2. Acrescente uma linha em `SENSORES` com identificador, fonte e pino, e grave o firmware.
3. No sistema, cadastre o tipo (se ainda não existir) e o sensor na estação, com o **mesmo identificador**.
4. Informe o ganho e o offset do sensor para converter o valor bruto.

Exemplo, sensor de luminosidade (LDR em divisor de tensão no GPIO34):

```cpp
{"lux", coleta::Fonte::Analogico, 34},
```

No cadastro, um ganho de `0,000805860805` (3,3 / 4095) grava a leitura em volts; o ganho e o offset do fabricante convertem para lux.

Limites: até 8 sensores por estação; identificadores sem repetição, só com letras, números, `_` e `-`. Configuração inválida aparece na serial ao ligar e a estação não coleta. Sensor que falha fica fora da mensagem e os demais são enviados.

## Registro da montagem

Preencher depois de montar: foto da protoboard, MAC impresso na serial na inicialização e resultado do comando `status`.
