#include "sensores.h"

#include <cctype>
#include <cmath>
#include <cstring>

#include "leitura.h"

namespace coleta {

namespace {
bool igualSemCaixa(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
      return false;
    }
  }
  return *a == *b;
}

bool dentro(float valor, float minimo, float maximo) {
  return std::isfinite(valor) && valor >= minimo && valor <= maximo;
}

bool eDht22(Fonte fonte) { return fonte == Fonte::Dht22Temperatura || fonte == Fonte::Dht22Umidade; }
}  // namespace

bool identificadorValido(const char* identificador) {
  if (identificador == nullptr || *identificador == '\0') return false;
  for (const char* c = identificador; *c; c++) {
    if (!std::isalnum(static_cast<unsigned char>(*c)) && *c != '_' && *c != '-') return false;
  }
  return true;
}

bool pinoAnalogicoValido(int pino) { return pino >= 32 && pino <= 39; }

bool pinoComPullUpValido(int pino) { return (pino >= 0 && pino <= 5) || (pino >= 12 && pino <= 27) || pino == 32 || pino == 33; }

const char* descrever(ErroConfiguracao erro) {
  switch (erro) {
    case ErroConfiguracao::Nenhum:
      return "ok";
    case ErroConfiguracao::SemSensores:
      return "sem_sensores";
    case ErroConfiguracao::SensoresDemais:
      return "sensores_demais";
    case ErroConfiguracao::IdentificadorInvalido:
      return "identificador_invalido";
    case ErroConfiguracao::IdentificadorReservado:
      return "identificador_reservado";
    case ErroConfiguracao::IdentificadorRepetido:
      return "identificador_repetido";
    case ErroConfiguracao::PinoAnalogicoInvalido:
      return "pino_analogico_invalido";
    case ErroConfiguracao::PinoSemPullUp:
      return "pino_sem_pull_up";
    case ErroConfiguracao::PinoRepetido:
      return "pino_repetido";
  }
  return "desconhecido";
}

ErroConfiguracao validarConfiguracao(const Sensor* sensores, size_t quantidade) {
  if (sensores == nullptr || quantidade == 0) return ErroConfiguracao::SemSensores;
  if (quantidade > MAXIMO_SENSORES) return ErroConfiguracao::SensoresDemais;
  for (size_t i = 0; i < quantidade; i++) {
    const char* id = sensores[i].identificador;
    if (!identificadorValido(id)) return ErroConfiguracao::IdentificadorInvalido;
    // Campos fixos do contrato de telemetria.
    if (igualSemCaixa(id, "estacao_id") || igualSemCaixa(id, "unix_time")) {
      return ErroConfiguracao::IdentificadorReservado;
    }
    // O sistema compara o identificador sem diferenciar maiúsculas.
    for (size_t j = 0; j < i; j++) {
      if (igualSemCaixa(id, sensores[j].identificador)) return ErroConfiguracao::IdentificadorRepetido;
    }
    if (sensores[i].fonte == Fonte::Analogico && !pinoAnalogicoValido(sensores[i].pino)) {
      return ErroConfiguracao::PinoAnalogicoInvalido;
    }
    if ((sensores[i].fonte == Fonte::Pulsos || sensores[i].fonte == Fonte::Digital) &&
        !pinoComPullUpValido(sensores[i].pino)) {
      return ErroConfiguracao::PinoSemPullUp;
    }
    // Temperatura e umidade saem do mesmo DHT22; fora isso, um pino por canal.
    for (size_t j = 0; j < i; j++) {
      if (sensores[j].pino == sensores[i].pino && !(eDht22(sensores[i].fonte) && eDht22(sensores[j].fonte))) {
        return ErroConfiguracao::PinoRepetido;
      }
    }
  }
  return ErroConfiguracao::Nenhum;
}

bool valorValido(Fonte fonte, float valor) {
  switch (fonte) {
    case Fonte::Dht22Temperatura:
      return dentro(valor, TEMPERATURA_MIN_C, TEMPERATURA_MAX_C);
    case Fonte::Dht22Umidade:
      return dentro(valor, UMIDADE_MIN, UMIDADE_MAX);
    case Fonte::Analogico:
      return dentro(valor, 0.0f, 4095.0f);
    case Fonte::Digital:
      return valor == 0.0f || valor == 1.0f;
    case Fonte::Pulsos:
      return dentro(valor, 0.0f, PULSOS_MAX) && valor == std::floor(valor);
  }
  return false;
}

Medida avaliarMedida(const Sensor& sensor, float bruto) {
  const bool ok = valorValido(sensor.fonte, bruto);
  return Medida{sensor.identificador, ok ? bruto : 0.0f, ok};
}

size_t contarValidas(const Medida* medidas, size_t quantidade) {
  size_t validas = 0;
  for (size_t i = 0; i < quantidade; i++) {
    if (medidas[i].ok) validas++;
  }
  return validas;
}

}  // namespace coleta
