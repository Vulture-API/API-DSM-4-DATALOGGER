#include <Arduino.h>
#include <DHT.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "alarme.h"
#include "canais.h"
#include "fila.h"
#include "config.h"
#include "intervalo.h"
#include "leitura.h"
#include "payload.h"
#include "pinos.h"
#include "pulsos.h"
#include "secrets.h"
#include "sensores.h"
#include "status.h"

using namespace coleta;

namespace {

WiFiClient rede;
PubSubClient mqtt(rede);
Preferences preferencias;

Agendador agendador;
StatusColeta status;
char mac[18];
char topico[48];
char clientId[32];
bool configuracaoOk = false;

// Canais da estação, remontados quando o DHT22 aparece ou some (SCRUM-621).
Sensor canais[MAXIMO_SENSORES];
char nomesCanais[MAXIMO_SENSORES][TAMANHO_NOME_CANAL];
size_t quantidadeCanais = 0;

DHT* dht = nullptr;
volatile int pinoDht = -1;
uint8_t falhasDht = 0;
uint32_t coletasSemDht = 0;
float temperaturaDht = NAN;
float umidadeDht = NAN;

// Estado por GPIO, atualizado pela tarefa de amostragem.
CanalPulsos pulsos[40];
volatile uint16_t ultimoAnalogico[40];
portMUX_TYPE travaPulsos = portMUX_INITIALIZER_UNLOCKED;
volatile bool amostragemPausada = false;

void registrarAmostra(int pino, bool alto) {
  portENTER_CRITICAL(&travaPulsos);
  pulsos[pino].amostrar(alto);
  portEXIT_CRITICAL(&travaPulsos);
}

void amostrar(void*) {
  TickType_t proxima = xTaskGetTickCount();
  for (;;) {
    if (!amostragemPausada) {
      for (int pino : PINOS_ANALOGICOS) {
        const int valor = analogRead(pino);
        ultimoAnalogico[pino] = static_cast<uint16_t>(valor);
        registrarAmostra(pino, nivelAnalogico(valor, pulsos[pino].alto));
      }
      for (int pino : PINOS_DIGITAIS) {
        if (pino != pinoDht) registrarAmostra(pino, digitalRead(pino) == HIGH);
      }
    }
    vTaskDelayUntil(&proxima, pdMS_TO_TICKS(PERIODO_AMOSTRAGEM_MS));
  }
}

Alarme alarme;
Bipador bipador;
bool buzzerLigado = true;
constexpr uint8_t CANAL_BUZZER = 0;  // canal LEDC do tom

uint16_t buzzerNota = 0;  // frequência tocando agora (0 = mudo)

// Toca a nota pedida (0 = silêncio). Com BUZZER_FREQUENCIA_HZ = 0 só liga/desliga.
void acionarBuzzer(uint16_t nota) {
  if (nota == buzzerNota) return;
  buzzerNota = nota;
  if (BUZZER_FREQUENCIA_HZ == 0) {
    digitalWrite(PINO_BUZZER, (nota != 0) == BUZZER_LIGA_EM_ALTO ? HIGH : LOW);
    return;
  }
  if (nota != 0) {
    ledcAttachPin(PINO_BUZZER, CANAL_BUZZER);
    ledcWriteTone(CANAL_BUZZER, nota);
  } else {
    ledcWriteTone(CANAL_BUZZER, 0);
    ledcDetachPin(PINO_BUZZER);
    pinMode(PINO_BUZZER, OUTPUT);
    digitalWrite(PINO_BUZZER, BUZZER_LIGA_EM_ALTO ? LOW : HIGH);  // repouso: mudo
  }
}

constexpr uint32_t ESPERA_INICIAL_MS = 20000;
// Cabe a mensagem com MAXIMO_SENSORES campos; o pacote MQTT leva também o tópico.
constexpr size_t TAMANHO_PAYLOAD = 768;
constexpr uint16_t TAMANHO_PACOTE_MQTT = 1024;

// 48 leituras: 48 min sem broker no intervalo padrão de 60 s.
constexpr size_t CAPACIDADE_FILA = 48;
constexpr int ENVIOS_POR_VOLTA = 4;
FilaEnvio<CAPACIDADE_FILA, TAMANHO_PAYLOAD> fila;

uint32_t tentativasMqtt = 0;
uint32_t proximaTentativaMs = 0;
bool estavaConectado = false;

void escreverLog(const char* nivel, const char* mensagem) {
  Serial.printf("[%lu] %s %s\n", static_cast<unsigned long>(millis()), nivel, mensagem);
}

void carregarIntervalo() {
  preferencias.begin("datalogger", true);
  const uint32_t salvo = preferencias.getUInt("intervalo_s", INTERVALO_PADRAO_S);
  preferencias.end();
  if (!agendador.configurar(salvo)) agendador.configurar(INTERVALO_PADRAO_S);
}

void carregarBuzzer() {
  preferencias.begin("datalogger", true);
  buzzerLigado = preferencias.getBool("buzzer", true);
  preferencias.end();
}

void salvarBuzzer(bool ligado) {
  buzzerLigado = ligado;
  preferencias.begin("datalogger", false);
  preferencias.putBool("buzzer", ligado);
  preferencias.end();
}

size_t listarFalhas(uint8_t mascara, char* destino, size_t tamanho) {
  size_t n = 0;
  destino[0] = '\0';
  for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) {
    const Falha falha = static_cast<Falha>(i);
    if ((mascara & mascaraDe(falha)) == 0) continue;
    const int escrito = snprintf(destino + n, tamanho - n, "%s%s", n ? "," : "", nomeDe(falha));
    if (escrito < 0 || static_cast<size_t>(escrito) >= tamanho - n) break;
    n += static_cast<size_t>(escrito);
  }
  if (n == 0) snprintf(destino, tamanho, "nenhum");
  return n;
}

// Toca o que o bipador manda a cada 5 ms, fora do loop: a coleta e a conexão
// MQTT seguram o loop por até alguns segundos e as melodias sairiam picotadas.
// No núcleo 0, junto da amostragem: no 1 ela atrapalharia a leitura do DHT22.
void tarefaBuzzer(void*) {
  for (;;) {
    acionarBuzzer(bipador.frequencia(millis()));
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// Falha curta não apita: o alarme só toca o que já dura PERSISTENCIA_MS.
void cuidarAlarme() {
  const uint32_t agora = millis();
  alarme.atualizar(Falha::Conexao, WiFi.status() != WL_CONNECTED || !mqtt.connected(), agora);
  alarme.atualizar(Falha::Hora, static_cast<uint32_t>(time(nullptr)) < UNIX_TIME_MINIMO, agora);

  const Aviso aviso = alarme.verificar(agora);
  if (!aviso.vazio()) {
    char falhas[48];
    listarFalhas(aviso.falhas, falhas, sizeof falhas);
    char msg[96];
    if (aviso.normalizou) {
      snprintf(msg, sizeof msg, "alarme encerrado: falhas normalizadas%s", buzzerLigado ? "" : " (buzzer desligado)");
    } else {
      snprintf(msg, sizeof msg, "alarme: %s%s", falhas, buzzerLigado ? "" : " (buzzer desligado)");
    }
    escreverLog(aviso.normalizou ? "INFO" : "WARN", msg);
    if (buzzerLigado) bipador.tocar(aviso, agora);
  }
}

void montarListaCanais() {
  quantidadeCanais = montarCanais(pinoDht, PINOS_ANALOGICOS, QUANTIDADE_ANALOGICOS, PINOS_DIGITAIS,
                                  QUANTIDADE_DIGITAIS, canais, nomesCanais, MAXIMO_SENSORES);
  const ErroConfiguracao erro = validarConfiguracao(canais, quantidadeCanais);
  configuracaoOk = erro == ErroConfiguracao::Nenhum;
  alarme.atualizar(Falha::Configuracao, !configuracaoOk, millis());
  if (!configuracaoOk) {
    char aviso[96];
    snprintf(aviso, sizeof aviso, "canais mal configurados em config.h (%s): nada sera coletado", descrever(erro));
    escreverLog("ERRO", aviso);
  }
}

// O DHT22 é lido com a amostragem parada: a resposta dele depende de tempo em
// microssegundos.
bool lerDht(DHT& sensor, float& temperatura, float& umidade) {
  const bool pausada = amostragemPausada;
  amostragemPausada = true;
  delay(PERIODO_AMOSTRAGEM_MS * 2);
  temperatura = sensor.readTemperature(false, true);
  umidade = sensor.readHumidity(false);
  amostragemPausada = pausada;
  return !isnan(temperatura) && !isnan(umidade);
}

// Duas respostas válidas seguidas: uma leitura solta não basta para dizer que
// o DHT22 está naquele pino.
bool respondeComoDht(int pino) {
  DHT teste(pino, DHT22);
  teste.begin();
  float t, u;
  if (!lerDht(teste, t, u)) return false;
  delay(2100);
  return lerDht(teste, t, u);
}

// Procura o DHT22 nos pinos digitais, começando pelo último em que foi achado.
// A amostragem só para durante cada leitura (lerDht): com o sensor ausente a
// varredura leva alguns segundos e um pulso nesse tempo não pode se perder.
void detectarDht() {
  preferencias.begin("datalogger", true);
  int salvo = preferencias.getInt("dht_pino", PINO_DHT);
  preferencias.end();
  // Pino gravado por um firmware com outra lista de pinos.
  if (!contemPino(PINOS_DIGITAIS, QUANTIDADE_DIGITAIS, salvo)) salvo = PINO_DHT;
  int achado = respondeComoDht(salvo) ? salvo : -1;
  for (size_t i = 0; achado < 0 && i < QUANTIDADE_DIGITAIS; i++) {
    if (PINOS_DIGITAIS[i] != salvo && respondeComoDht(PINOS_DIGITAIS[i])) achado = PINOS_DIGITAIS[i];
  }
  for (int pino : PINOS_DIGITAIS) pinMode(pino, INPUT_PULLUP);

  if (achado != pinoDht) {
    delete dht;
    dht = achado >= 0 ? new DHT(achado, DHT22) : nullptr;
    if (dht) dht->begin();
    if (pinoDht >= 0) pulsos[pinoDht] = CanalPulsos{};
    pinoDht = achado;
    if (achado >= 0) {
      preferencias.begin("datalogger", false);
      preferencias.putInt("dht_pino", achado);
      preferencias.end();
    }
    montarListaCanais();
    char msg[64];
    if (achado >= 0) {
      snprintf(msg, sizeof msg, "DHT22 no GPIO%d", achado);
    } else {
      snprintf(msg, sizeof msg, "DHT22 nao encontrado");
    }
    escreverLog(achado >= 0 ? "INFO" : "WARN", msg);
  }
}

bool salvarIntervalo(uint32_t segundos) {
  if (!agendador.configurar(segundos)) return false;
  preferencias.begin("datalogger", false);
  preferencias.putUInt("intervalo_s", segundos);
  preferencias.end();
  return true;
}

void conectarWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// Não bloqueia o loop: tenta de novo com espera crescente até 60 s.
void manterMqtt() {
  const bool conectado = mqtt.connected();
  if (estavaConectado && !conectado) {
    status.registrar(Evento::ConexaoPerdida);
    escreverLog("WARN", "conexao MQTT perdida");
  }
  estavaConectado = conectado;
  if (conectado || WiFi.status() != WL_CONNECTED) return;
  if (static_cast<int32_t>(millis() - proximaTentativaMs) < 0) return;

  status.registrar(Evento::TentativaReconexao);
  const bool ok = strlen(MQTT_USER) > 0 ? mqtt.connect(clientId, MQTT_USER, MQTT_PASSWORD)
                                         : mqtt.connect(clientId);
  if (ok) {
    tentativasMqtt = 0;
    estavaConectado = true;
    status.registrar(Evento::Reconectado);
    escreverLog("INFO", "MQTT conectado");
    return;
  }
  char msg[64];
  snprintf(msg, sizeof msg, "MQTT falhou (estado %d), tentativa %lu", mqtt.state(),
           static_cast<unsigned long>(tentativasMqtt + 1));
  escreverLog("WARN", msg);
  proximaTentativaMs = millis() + atrasoReconexaoMs(tentativasMqtt++);
}

// Publica a fila na ordem, poucas por volta do loop para não travar o resto.
void enviarFila() {
  static uint32_t descartadasAvisadas = 0;
  if (fila.descartadas() != descartadasAvisadas) {
    descartadasAvisadas = fila.descartadas();
    char aviso[80];
    snprintf(aviso, sizeof aviso, "fila de envio cheia: %lu leituras antigas descartadas",
             static_cast<unsigned long>(descartadasAvisadas));
    escreverLog("WARN", aviso);
  }
  for (int i = 0; i < ENVIOS_POR_VOLTA && !fila.vazia() && mqtt.connected(); i++) {
    if (!mqtt.publish(topico, fila.primeira())) {
      status.registrar(Evento::FalhaPublicacao);
      escreverLog("ERRO", "publish falhou; a leitura continua na fila");
      return;
    }
    status.registrar(Evento::Publicada);
    escreverLog("INFO", fila.primeira());
    fila.remover();
  }
}

float lerBruto(const Sensor& sensor) {
  switch (sensor.fonte) {
    case Fonte::Dht22Temperatura:
      return temperaturaDht;
    case Fonte::Dht22Umidade:
      return umidadeDht;
    case Fonte::Analogico:
      return static_cast<float>(ultimoAnalogico[sensor.pino]);
    case Fonte::Digital:
      return static_cast<float>(digitalRead(sensor.pino));
    case Fonte::Pulsos: {
      portENTER_CRITICAL(&travaPulsos);
      const uint32_t total = pulsos[sensor.pino].total();
      portEXIT_CRITICAL(&travaPulsos);
      return static_cast<float>(total);
    }
  }
  return NAN;
}

void coletarEPublicar() {
  // DHT22 sumido por 3 coletas, ou ausente há 10: procura de novo.
  if (pinoDht < 0 ? (++coletasSemDht % 10 == 0) : falhasDht >= 3) {
    falhasDht = 0;
    detectarDht();
  }
  if (!configuracaoOk) return;

  temperaturaDht = umidadeDht = NAN;
  if (dht && !lerDht(*dht, temperaturaDht, umidadeDht)) {
    // Uma nova tentativa depois do intervalo mínimo do sensor.
    delay(2100);
    lerDht(*dht, temperaturaDht, umidadeDht);
  }

  Medida medidas[MAXIMO_SENSORES];
  char msg[96];
  bool dhtRespondeu = false;
  for (size_t i = 0; i < quantidadeCanais; i++) {
    medidas[i] = avaliarMedida(canais[i], lerBruto(canais[i]));
    const bool doDht = canais[i].fonte == Fonte::Dht22Temperatura || canais[i].fonte == Fonte::Dht22Umidade;
    if (doDht && medidas[i].ok) dhtRespondeu = true;
    if (!medidas[i].ok) {
      snprintf(msg, sizeof msg, "sensor %s sem leitura valida", canais[i].identificador);
      escreverLog("WARN", msg);
    }
  }
  if (pinoDht >= 0) falhasDht = dhtRespondeu ? 0 : falhasDht + 1;

  const size_t validas = contarValidas(medidas, quantidadeCanais);
  alarme.atualizar(Falha::Sensor, validas < quantidadeCanais, millis());
  if (validas == 0) {
    status.registrar(Evento::FalhaSensor);
    escreverLog("ERRO", status.sensorComProblema() ? "nenhum sensor com leitura ha 3 coletas"
                                                   : "nenhum sensor com leitura");
    return;
  }
  status.registrar(validas == quantidadeCanais ? Evento::ColetaCompleta : Evento::ColetaParcial);

  char payload[TAMANHO_PAYLOAD];
  const Rejeicao motivo = montarPayload(mac, static_cast<uint32_t>(time(nullptr)), medidas,
                                        quantidadeCanais, payload, sizeof payload);
  if (motivo != Rejeicao::Nenhuma) {
    status.registrar(Evento::PayloadRejeitado);
    escreverLog("WARN", descrever(motivo));
    return;
  }
  if (!fila.guardar(payload)) {
    status.registrar(Evento::PayloadRejeitado);
    escreverLog("ERRO", "leitura maior que o item da fila de envio");
    return;
  }
  // Os pulsos agora estão na mensagem da fila; os que chegarem depois ficam
  // para a próxima coleta.
  for (size_t i = 0; i < quantidadeCanais; i++) {
    if (canais[i].fonte != Fonte::Pulsos || !medidas[i].ok) continue;
    portENTER_CRITICAL(&travaPulsos);
    pulsos[canais[i].pino].descontar(static_cast<uint32_t>(medidas[i].valor));
    portEXIT_CRITICAL(&travaPulsos);
  }
  if (!mqtt.connected()) {
    status.registrar(Evento::SemConexao);
    snprintf(msg, sizeof msg, "MQTT fora do ar: leitura guardada (%u na fila)", static_cast<unsigned>(fila.tamanho()));
    escreverLog("WARN", msg);
  }
  enviarFila();
}

// Valor atual de cada canal, para achar onde o sensor foi ligado.
void mostrarCanais() {
  if (dht) lerDht(*dht, temperaturaDht, umidadeDht);
  char linha[96];
  size_t usado = 0;
  for (size_t i = 0; i < quantidadeCanais; i++) {
    const float valor = lerBruto(canais[i]);
    const int n = snprintf(linha + usado, sizeof linha - usado, "%s%s=%.1f", usado ? " " : "", canais[i].identificador,
                           valor);
    if (n < 0 || static_cast<size_t>(n) >= sizeof linha - usado) {
      escreverLog("INFO", linha);
      usado = 0;
      i--;
      continue;
    }
    usado += static_cast<size_t>(n);
  }
  if (usado) escreverLog("INFO", linha);
}

// Comandos pela serial: "intervalo <segundos>", "status", "canais" e "buzzer on|off|teste|abertura|normalizou".
void lerComando() {
  if (!Serial.available()) return;
  String linha = Serial.readStringUntil('\n');
  linha.trim();
  char msg[360];
  if (linha.startsWith("intervalo ")) {
    const long segundos = linha.substring(10).toInt();
    if (segundos > 0 && salvarIntervalo(static_cast<uint32_t>(segundos))) {
      snprintf(msg, sizeof msg, "intervalo de coleta: %ld s", segundos);
    } else {
      snprintf(msg, sizeof msg, "intervalo invalido: use de %lu a %lu s",
               static_cast<unsigned long>(INTERVALO_MINIMO_S),
               static_cast<unsigned long>(INTERVALO_MAXIMO_S));
    }
    escreverLog("INFO", msg);
  } else if (linha == "status") {
    char resumo[180];
    status.resumo(resumo, sizeof resumo);
    char alarmes[48];
    listarFalhas(alarme.confirmadas(millis()), alarmes, sizeof alarmes);
    char dhtTexto[12];
    if (pinoDht >= 0) {
      snprintf(dhtTexto, sizeof dhtTexto, "GPIO%d", static_cast<int>(pinoDht));
    } else {
      snprintf(dhtTexto, sizeof dhtTexto, "nenhum");
    }
    snprintf(msg, sizeof msg, "estacao=%s canais=%u dht=%s intervalo=%lus wifi=%s mqtt=%s fila=%u buzzer=%s alarmes=%s %s", mac,
             static_cast<unsigned>(quantidadeCanais), dhtTexto, static_cast<unsigned long>(agendador.intervaloS()),
             WiFi.status() == WL_CONNECTED ? "ok" : "off", mqtt.connected() ? "ok" : "off",
             static_cast<unsigned>(fila.tamanho()), buzzerLigado ? "on" : "off", alarmes, resumo);
    escreverLog("INFO", msg);
  } else if (linha == "canais") {
    mostrarCanais();
  } else if (linha == "buzzer on" || linha == "buzzer off") {
    salvarBuzzer(linha == "buzzer on");
    escreverLog("INFO", buzzerLigado ? "buzzer ligado" : "buzzer desligado");
  } else if (linha == "buzzer teste") {
    Aviso todos;
    for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) todos.falhas |= mascaraDe(static_cast<Falha>(i));
    bipador.tocar(todos, millis());
    escreverLog("INFO", "buzzer teste: sirene + sensor 2, conexao 3, hora 4, configuracao 5 bipes");
  } else if (linha == "buzzer abertura") {
    bipador.tocarAbertura(millis());
    escreverLog("INFO", "buzzer abertura");
  } else if (linha == "buzzer normalizou") {
    Aviso aviso;
    aviso.normalizou = true;
    bipador.tocar(aviso, millis());
    escreverLog("INFO", "buzzer normalizou");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(PINO_BUZZER, OUTPUT);
  digitalWrite(PINO_BUZZER, BUZZER_LIGA_EM_ALTO ? LOW : HIGH);
  if (BUZZER_FREQUENCIA_HZ > 0) ledcSetup(CANAL_BUZZER, BUZZER_FREQUENCIA_HZ, 8);
  xTaskCreatePinnedToCore(tarefaBuzzer, "buzzer", 2048, nullptr, 1, nullptr, 0);
  carregarIntervalo();
  carregarBuzzer();

  for (int pino : PINOS_DIGITAIS) pinMode(pino, INPUT_PULLUP);
  montarListaCanais();

  conectarWifi();
  strncpy(mac, WiFi.macAddress().c_str(), sizeof mac - 1);
  montarTopico(mac, topico, sizeof topico);
  snprintf(clientId, sizeof clientId, "datalogger-%s", mac);

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(TAMANHO_PACOTE_MQTT);

  char msg[128];
  snprintf(msg, sizeof msg, "estacao %s, topico %s, intervalo %lus", mac, topico,
           static_cast<unsigned long>(agendador.intervaloS()));
  escreverLog("INFO", msg);

  // A primeira coleta sai logo no início do loop. Espera um pouco por Wi-Fi e
  // NTP para ela não ser descartada (e o DHT22 precisa de ~2 s depois de ligar).
  const uint32_t inicio = millis();
  while (millis() - inicio < ESPERA_INICIAL_MS &&
         (millis() - inicio < 2000 || WiFi.status() != WL_CONNECTED ||
          static_cast<uint32_t>(time(nullptr)) < UNIX_TIME_MINIMO)) {
    delay(200);
  }
  if (WiFi.status() != WL_CONNECTED) escreverLog("WARN", "Wi-Fi ainda nao conectou; seguindo e tentando em segundo plano");

  detectarDht();
  if (pinoDht < 0) escreverLog("WARN", "DHT22 nao encontrado");
  // No núcleo 0: no 1 (o do loop) a tarefa interrompia a leitura do DHT22, que
  // depende de tempo em microssegundos.
  xTaskCreatePinnedToCore(amostrar, "amostragem", 3072, nullptr, 1, nullptr, 0);
  snprintf(msg, sizeof msg, "%u canais; comando \"canais\" mostra o valor de cada um",
           static_cast<unsigned>(quantidadeCanais));
  escreverLog("INFO", msg);
  if (buzzerLigado) bipador.tocarAbertura(millis());  // avisa que a placa ligou
}

void loop() {
  manterMqtt();
  mqtt.loop();
  lerComando();
  if (agendador.deveColetar(millis())) {
    coletarEPublicar();
  } else if (!fila.vazia()) {
    enviarFila();
  }
  cuidarAlarme();
  delay(50);
}
