#include <Arduino.h>
#include <DHT.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "alarme.h"
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

DHT dht(PINO_DHT, DHT22);
WiFiClient rede;
PubSubClient mqtt(rede);
Preferences preferencias;

Agendador agendador;
StatusColeta status;
char mac[18];
char topico[48];
char clientId[32];
bool configuracaoOk = false;

// Um contador por sensor de pulsos (índice de SENSORES), alimentado pela interrupção.
ContadorPulsos contadores[MAXIMO_SENSORES];
portMUX_TYPE travaPulsos = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR aoReceberPulso(void* arg) {
  portENTER_CRITICAL_ISR(&travaPulsos);
  static_cast<ContadorPulsos*>(arg)->registrar(static_cast<uint32_t>(millis()));
  portEXIT_CRITICAL_ISR(&travaPulsos);
}

Alarme alarme;
Bipador bipador;
bool buzzerLigado = true;

constexpr uint32_t ESPERA_INICIAL_MS = 20000;
// Cabe a mensagem com MAXIMO_SENSORES campos; o pacote MQTT leva também o tópico.
constexpr size_t TAMANHO_PAYLOAD = 384;
constexpr uint16_t TAMANHO_PACOTE_MQTT = 512;

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
  const bool ligar = bipador.ligado(agora);
  digitalWrite(PINO_BUZZER, ligar == BUZZER_LIGA_EM_ALTO ? HIGH : LOW);
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

float lerBruto(size_t indice, const Sensor& sensor) {
  switch (sensor.fonte) {
    case Fonte::Dht22Temperatura:
      return dht.readTemperature();
    case Fonte::Dht22Umidade:
      return dht.readHumidity();
    case Fonte::Analogico:
      return static_cast<float>(analogRead(sensor.pino));
    case Fonte::Digital:
      return static_cast<float>(digitalRead(sensor.pino));
    case Fonte::Pulsos:
      return static_cast<float>(contadores[indice].total());
  }
  return NAN;
}

void coletarEPublicar() {
  if (!configuracaoOk) return;

  Medida medidas[MAXIMO_SENSORES];
  char msg[96];
  for (size_t i = 0; i < QUANTIDADE_SENSORES; i++) {
    medidas[i] = avaliarMedida(SENSORES[i], lerBruto(i, SENSORES[i]));
    if (!medidas[i].ok) {
      snprintf(msg, sizeof msg, "sensor %s sem leitura valida", SENSORES[i].identificador);
      escreverLog("WARN", msg);
    }
  }

  const size_t validas = contarValidas(medidas, QUANTIDADE_SENSORES);
  alarme.atualizar(Falha::Sensor, validas < QUANTIDADE_SENSORES, millis());
  if (validas == 0) {
    status.registrar(Evento::FalhaSensor);
    escreverLog("ERRO", status.sensorComProblema() ? "nenhum sensor com leitura ha 3 coletas"
                                                   : "nenhum sensor com leitura");
    return;
  }
  status.registrar(validas == QUANTIDADE_SENSORES ? Evento::ColetaCompleta : Evento::ColetaParcial);

  char payload[TAMANHO_PAYLOAD];
  const Rejeicao motivo = montarPayload(mac, static_cast<uint32_t>(time(nullptr)), medidas,
                                        QUANTIDADE_SENSORES, payload, sizeof payload);
  if (motivo != Rejeicao::Nenhuma) {
    status.registrar(Evento::PayloadRejeitado);
    escreverLog("WARN", descrever(motivo));
    return;
  }
  if (!mqtt.connected()) {
    status.registrar(Evento::SemConexao);
    escreverLog("WARN", "leitura descartada: MQTT fora do ar");
    return;
  }
  if (mqtt.publish(topico, payload)) {
    // Só agora os pulsos publicados saem da contagem; os que chegaram durante
    // o envio (ou numa coleta que não publicou) continuam pendentes.
    for (size_t i = 0; i < QUANTIDADE_SENSORES; i++) {
      if (SENSORES[i].fonte != Fonte::Pulsos || !medidas[i].ok) continue;
      portENTER_CRITICAL(&travaPulsos);
      contadores[i].descontar(static_cast<uint32_t>(medidas[i].valor));
      portEXIT_CRITICAL(&travaPulsos);
    }
    status.registrar(Evento::Publicada);
    escreverLog("INFO", payload);
  } else {
    status.registrar(Evento::FalhaPublicacao);
    escreverLog("ERRO", "publish falhou");
  }
}

// Comandos pela serial: "intervalo <segundos>", "status" e "buzzer on|off|teste".
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
    snprintf(msg, sizeof msg, "estacao=%s sensores=%u intervalo=%lus wifi=%s mqtt=%s buzzer=%s alarmes=%s %s", mac,
             static_cast<unsigned>(QUANTIDADE_SENSORES), static_cast<unsigned long>(agendador.intervaloS()),
             WiFi.status() == WL_CONNECTED ? "ok" : "off", mqtt.connected() ? "ok" : "off",
             buzzerLigado ? "on" : "off", alarmes, resumo);
    escreverLog("INFO", msg);
  } else if (linha == "buzzer on" || linha == "buzzer off") {
    salvarBuzzer(linha == "buzzer on");
    escreverLog("INFO", buzzerLigado ? "buzzer ligado" : "buzzer desligado");
  } else if (linha == "buzzer teste") {
    Aviso todos;
    for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) todos.falhas |= mascaraDe(static_cast<Falha>(i));
    bipador.tocar(todos, millis());
    escreverLog("INFO", "buzzer teste: sensor 2, conexao 3, hora 4, configuracao 5 bipes");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(PINO_BUZZER, OUTPUT);
  digitalWrite(PINO_BUZZER, BUZZER_LIGA_EM_ALTO ? LOW : HIGH);
  carregarIntervalo();
  carregarBuzzer();

  const ErroConfiguracao erro = validarConfiguracao(SENSORES, QUANTIDADE_SENSORES);
  configuracaoOk = erro == ErroConfiguracao::Nenhum;
  alarme.atualizar(Falha::Configuracao, !configuracaoOk, millis());
  if (!configuracaoOk) {
    char aviso[96];
    snprintf(aviso, sizeof aviso, "canais mal configurados em config.h (%s): nada sera coletado", descrever(erro));
    escreverLog("ERRO", aviso);
  }
  for (size_t i = 0; configuracaoOk && i < QUANTIDADE_SENSORES; i++) {
    if (SENSORES[i].fonte == Fonte::Analogico) {
      pinMode(SENSORES[i].pino, INPUT);
    } else if (SENSORES[i].fonte == Fonte::Digital) {
      pinMode(SENSORES[i].pino, INPUT_PULLUP);
    } else if (SENSORES[i].fonte == Fonte::Pulsos) {
      pinMode(SENSORES[i].pino, INPUT_PULLUP);
      attachInterruptArg(digitalPinToInterrupt(SENSORES[i].pino), aoReceberPulso, &contadores[i], FALLING);
    }
  }

  conectarWifi();
  strncpy(mac, WiFi.macAddress().c_str(), sizeof mac - 1);
  montarTopico(mac, topico, sizeof topico);
  snprintf(clientId, sizeof clientId, "datalogger-%s", mac);

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(TAMANHO_PACOTE_MQTT);

  char msg[128];
  snprintf(msg, sizeof msg, "estacao %s, topico %s, intervalo %lus, %u sensores", mac, topico,
           static_cast<unsigned long>(agendador.intervaloS()), static_cast<unsigned>(QUANTIDADE_SENSORES));
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
}

void loop() {
  manterMqtt();
  mqtt.loop();
  lerComando();
  if (agendador.deveColetar(millis())) coletarEPublicar();
  cuidarAlarme();
  delay(50);
}
