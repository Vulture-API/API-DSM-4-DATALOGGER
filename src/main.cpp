#include <Arduino.h>
#include <DHT.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "intervalo.h"
#include "leitura.h"
#include "payload.h"
#include "pinos.h"
#include "secrets.h"
#include "status.h"

using namespace coleta;

namespace {

DHT dht(PINO_DHT, DHT22);
WiFiClient rede;
PubSubClient mqtt(rede);
Preferences preferencias;

Agendador agendador;
StatusColeta status;
const Identificadores identificadores{ID_SENSOR_TEMPERATURA, ID_SENSOR_UMIDADE};

char mac[18];
char topico[48];
char clientId[32];

constexpr uint32_t ESPERA_INICIAL_MS = 20000;

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

void coletarEPublicar() {
  const Leitura leitura = avaliarLeitura(dht.readTemperature(), dht.readHumidity());

  if (leitura.vazia()) {
    status.registrar(Evento::FalhaSensor);
    escreverLog("ERRO", status.sensorComProblema() ? "DHT22 sem leitura ha 3 coletas" : "DHT22 sem leitura");
    return;
  }
  status.registrar(leitura.completa() ? Evento::ColetaCompleta : Evento::ColetaParcial);
  if (!leitura.completa()) {
    escreverLog("WARN", leitura.temperaturaOk ? "umidade invalida, enviada so a temperatura"
                                      : "temperatura invalida, enviada so a umidade");
  }

  char payload[160];
  const Rejeicao motivo = montarPayload(mac, static_cast<uint32_t>(time(nullptr)), leitura,
                                        identificadores, payload, sizeof payload);
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
    status.registrar(Evento::Publicada);
    escreverLog("INFO", payload);
  } else {
    status.registrar(Evento::FalhaPublicacao);
    escreverLog("ERRO", "publish falhou");
  }
}

// Comandos pela serial: "intervalo <segundos>" e "status".
void lerComando() {
  if (!Serial.available()) return;
  String linha = Serial.readStringUntil('\n');
  linha.trim();
  char msg[280];
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
    snprintf(msg, sizeof msg, "estacao=%s intervalo=%lus wifi=%s mqtt=%s %s", mac,
             static_cast<unsigned long>(agendador.intervaloS()),
             WiFi.status() == WL_CONNECTED ? "ok" : "off", mqtt.connected() ? "ok" : "off", resumo);
    escreverLog("INFO", msg);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  dht.begin();
  carregarIntervalo();

  conectarWifi();
  strncpy(mac, WiFi.macAddress().c_str(), sizeof mac - 1);
  montarTopico(mac, topico, sizeof topico);
  snprintf(clientId, sizeof clientId, "datalogger-%s", mac);

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  mqtt.setServer(MQTT_HOST, MQTT_PORT);

  char msg[96];
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
}

void loop() {
  manterMqtt();
  mqtt.loop();
  lerComando();
  if (agendador.deveColetar(millis())) coletarEPublicar();
  delay(50);
}
