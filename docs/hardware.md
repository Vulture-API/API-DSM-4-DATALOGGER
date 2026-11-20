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

## Canais da placa (SCRUM-603, SCRUM-621)

O firmware lê todos os pinos livres em toda coleta e envia o valor bruto de cada um. Para ligar um sensor novo não é preciso mexer no firmware nem saber o GPIO do conector: ligue onde quiser, veja qual canal mudou e cadastre o sensor no sistema com esse identificador. Canal sem sensor cadastrado é ignorado pelo tratamento.

| Canal | O que é | Pinos | Valor enviado |
| --- | --- | --- | --- |
| `temp`, `umid` | DHT22, procurado sozinho nos pinos digitais | o primeiro que responder (começa pelo GPIO4) | °C e % |
| `anNN` | tensão no pino (ADC de 12 bits) | 34, 35, 36, 39 | 0 a 4095 (0 a 3,3 V) |
| `puNN` | pulsos (contato fechando) desde o último envio | 34, 35, 36, 39 e 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 26, 27, 32, 33 | inteiro |

- Os pulsos são contados por amostragem a cada 5 ms, então qualquer um desses pinos serve, inclusive 34 a 39. O nível só muda depois de 20 ms estável: quique do reed e vaivém do ímã não contam pulso a mais (conta até 25 pulsos por segundo).
- Nos pinos digitais (incluindo 32 e 33) o firmware liga o pull-up interno e lê o nível digital, que tem histerese: cabo longo de contato seco não conta pulso falso. Os GPIO 34 a 39 não têm pull-up: ali o contato seco precisa do resistor da placa.
- Pino analógico também conta pulsos: abaixo de 1/3 da escala é contato fechado, acima de 2/3 é aberto.
- Os pulsos só saem da contagem depois de publicados: com o Wi-Fi ou o MQTT fora do ar, o acumulado vai no próximo envio.
- O DHT22 é procurado ao ligar, a cada 10 coletas sem ele e depois de 3 coletas sem resposta. O pino achado fica gravado e sai da lista de pulsos.

### Achar o canal do sensor

1. Ligue o sensor e grave/reinicie a placa.
2. Na serial, mande `canais`: aparece o valor de cada canal. Mexa no sensor (vire a gangorra do pluviômetro, gire o anemômetro) e mande `canais` de novo: o canal que mudou é o do sensor. O mesmo aparece no payload de cada envio.
3. No sistema, cadastre o sensor na estação com esse identificador e a calibração: `valor = bruto × fator × ganho + offset`.

| Sensor | Ligação | Canal | Ganho | Resultado |
| --- | --- | --- | --- | --- |
| Pluviômetro basculante de 0,2 mm (2 fios) | GND e sinal (na placa da Fatec: GND e `Pluviometro` do conector J11; nunca 3V3 + GND) | o `puNN` que contar | `0.2` | mm de chuva no intervalo |
| Anemômetro de copo | GND e sinal | o `puNN` que contar | velocidade por pulso ÷ intervalo em s | velocidade média |
| LDR em divisor de tensão | sinal no pino analógico | o `anNN` que mudar | `0.000805860805` (3,3 / 4095) | volts |
| Sensor 0–3,3 V linear (ex.: 0–100 %) | sinal no pino analógico | o `anNN` | `0.0244200244` (100 / 4095) | % |

Pinos que o firmware não lê: 0 e 2 (boot e LED), 1 e 3 (serial), 6 a 11 (flash) e 25 (buzzer). Os GPIO 5, 12 e 15 são lidos só depois do boot.

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
