#pragma once

#include <cstddef>

namespace coleta {

// De onde vem a leitura de cada sensor da estação (SCRUM-603). O datalogger
// envia o valor bruto; a conversão para a unidade do parâmetro é do sistema,
// com o ganho e o offset cadastrados para o sensor.
enum class Fonte {
  Dht22Temperatura,  // °C
  Dht22Umidade,      // %
  Analogico,         // ADC de 12 bits: 0 a 4095
  Digital,           // 0 ou 1
};

struct Sensor {
  const char* identificador;  // local_identifier cadastrado no sistema
  Fonte fonte;
  int pino;
};

struct Medida {
  const char* identificador;
  float valor;
  bool ok;
};

constexpr size_t MAXIMO_SENSORES = 8;

// Só letras, números, "_" e "-": o identificador vai entre aspas no JSON.
bool identificadorValido(const char* identificador);

// No ESP32 o ADC2 não funciona com o Wi-Fi ligado: entrada analógica só nos
// pinos do ADC1 (GPIO 32 a 39).
bool pinoAnalogicoValido(int pino);

enum class ErroConfiguracao {
  Nenhum,
  SemSensores,
  SensoresDemais,
  IdentificadorInvalido,
  IdentificadorReservado,
  IdentificadorRepetido,
  PinoAnalogicoInvalido,
};

const char* descrever(ErroConfiguracao erro);

ErroConfiguracao validarConfiguracao(const Sensor* sensores, size_t quantidade);

// Valor fora da faixa física da fonte (ou NaN) é falha de leitura.
bool valorValido(Fonte fonte, float valor);

Medida avaliarMedida(const Sensor& sensor, float bruto);

size_t contarValidas(const Medida* medidas, size_t quantidade);

}  // namespace coleta
