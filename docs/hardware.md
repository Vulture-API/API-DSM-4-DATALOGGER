# Hardware da estação (SCRUM-578, 579, 580)

## Componentes

| Componente | Modelo | Uso |
| --- | --- | --- |
| Microcontrolador | ESP32 DevKit V1 (ESP-WROOM-32) | coleta, Wi-Fi e MQTT |
| Sensor | DHT22 / AM2302 | temperatura do ar (-40 a 80 °C, ±0,5 °C) e umidade relativa (0 a 100 %, ±2 %) |
| Resistor | 10 kΩ | pull-up da linha de dados (dispensável no módulo com placa, que já traz) |
| Alimentação | cabo USB 5 V | ESP32 e, pelo pino 3V3, o sensor |
| Buzzer | ativo 3,3 V a 5 V (só liga/desliga) | alarme de falha persistente (SCRUM-619) |
| Transistor | BC547 (NPN) + resistor de 1 kΩ | aciona o buzzer sem puxar corrente do GPIO |
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

## Buzzer (SCRUM-619)

O buzzer toca só falha que dura: queda curta de Wi-Fi ou sensor que perde uma coleta fica no log e no `status`, sem som.

| Ligação | ESP32 |
| --- | --- |
| GPIO25 → 1 kΩ → base do BC547 | GPIO25 |
| buzzer `+` | 3V3 (ou VIN, 5 V, para buzzer de 5 V) |
| buzzer `-` → coletor do BC547 | — |
| emissor do BC547 | GND |

Buzzer pequeno de 3,3 V que puxa menos de 20 mA pode ir direto: `+` no GPIO25 e `-` no GND. Módulo de 3 pinos que apita com o I/O em nível baixo: `BUZZER_LIGA_EM_ALTO = false` em `include/pinos.h`.

| Falha | Quando toca | Bipes |
| --- | --- | --- |
| sensor | algum sensor sem leitura válida por 30 min | 2 |
| conexão | Wi-Fi ou MQTT fora do ar por 30 min | 3 |
| hora | relógio sem NTP por 30 min (nada é publicado) | 4 |
| configuração | `config.h` inválido, na inicialização | 5 |

Com mais de uma falha os códigos tocam em sequência, com 1 s entre eles. O toque se repete a cada 30 min enquanto a falha durar e de novo na hora se outra falha passar dos 30 min. Quando tudo volta, um bipe longo. Os tempos ficam em `lib/coleta/src/alarme.h` (`PERSISTENCIA_MS` e `REPETICAO_MS`).

Pela serial: `buzzer teste` toca os quatro códigos; `buzzer off` e `buzzer on` desligam e religam o som (grava na flash, o alarme continua no log); `status` mostra `buzzer=on|off` e `alarmes=` com as falhas confirmadas.

## Registro da montagem

Montagem validada na bancada em 14/11/2026.

| Item | Valor |
|---|---|
| Placa | ESP32 WROOM-32 DevKit (Arduino IDE 2.3.11, core esp32 3.3.12, Dev Module, COM3) |
| MAC / `estacao_id` | `AC:67:B2:3C:2B:78` |
| DHT22 | dados no GPIO4 (identificadores `temp` e `umid`) |
| LDR (opcional) | divisor 3V3 → LDR → GPIO34 → 10 kΩ → GND (identificador `lux`, ganho `0,000805860805`) |
| Tópico | `estacoes/AC:67:B2:3C:2B:78/dados` |
| Upload | velocidade 115200 (921600 falhou na verificação da flash) |

Resultado do `status` com a estação coletando:

```
estacao=AC:67:B2:3C:2B:78 sensores=2 intervalo=60s wifi=ok mqtt=ok coletas=23 parciais=0
```

Comandos seriais: `intervalo 20` aceito (o `status` seguinte mostrou 20 s); `intervalo 5` recusado com "use de 10 a 3600 s".

Ponta a ponta: broker → Redis (`telemetry:ingest`) → `readings` no PostgreSQL → página da estação no site, com temperatura e umidade atualizando a cada coleta (28 °C e 73 % em média na bancada). Leituras enviadas antes de cadastrar os sensores foram para a quarentena com motivo `unknown_sensor`, como esperado.

LDR: o sensor `lux` chegou ao site, mas leu 0 a 0,1 V mesmo com luz. Aguardando verificação da ligação; o sensor ficou desativado no cadastro.
