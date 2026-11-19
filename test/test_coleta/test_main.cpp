#include <unity.h>

#include <cstring>
#include <initializer_list>
#include <limits>

#include "alarme.h"
#include "config.h"
#include "intervalo.h"
#include "leitura.h"
#include "payload.h"
#include "pulsos.h"
#include "status.h"

using namespace coleta;

namespace {
const char* MAC = "24:6F:28:AB:CD:EF";
const uint32_t AGORA = 1794100000;  // nov/2026
const float NAN_F = std::numeric_limits<float>::quiet_NaN();
}  // namespace

void setUp() {}
void tearDown() {}

// SCRUM-583: frequência de coleta

void test_intervalo_aceita_limites_e_rejeita_fora_da_faixa() {
  TEST_ASSERT_TRUE(intervaloValido(10));
  TEST_ASSERT_TRUE(intervaloValido(60));
  TEST_ASSERT_TRUE(intervaloValido(3600));
  TEST_ASSERT_FALSE(intervaloValido(9));
  TEST_ASSERT_FALSE(intervaloValido(0));
  TEST_ASSERT_FALSE(intervaloValido(3601));
}

void test_agendador_coleta_na_primeira_chamada() {
  Agendador agendador(60);
  TEST_ASSERT_TRUE(agendador.deveColetar(5000));
}

void test_agendador_nao_coleta_duas_vezes_no_mesmo_intervalo() {
  Agendador agendador(60);
  agendador.deveColetar(0);
  TEST_ASSERT_FALSE(agendador.deveColetar(1000));
  TEST_ASSERT_FALSE(agendador.deveColetar(59999));
  TEST_ASSERT_TRUE(agendador.deveColetar(60000));
  TEST_ASSERT_FALSE(agendador.deveColetar(60001));
}

void test_agendador_atrasado_coleta_uma_vez_e_reancora() {
  Agendador agendador(10);
  agendador.deveColetar(0);
  TEST_ASSERT_TRUE(agendador.deveColetar(45000));
  TEST_ASSERT_FALSE(agendador.deveColetar(46000));
  TEST_ASSERT_TRUE(agendador.deveColetar(55000));
}

void test_agendador_continua_certo_quando_millis_da_a_volta() {
  Agendador agendador(10);
  agendador.deveColetar(0xFFFFFFFFu - 4000);
  TEST_ASSERT_FALSE(agendador.deveColetar(1000));
  TEST_ASSERT_TRUE(agendador.deveColetar(6000));
}

void test_novo_intervalo_vale_para_as_proximas_coletas() {
  Agendador agendador(60);
  agendador.deveColetar(0);
  TEST_ASSERT_TRUE(agendador.configurar(10));
  TEST_ASSERT_EQUAL_UINT32(10, agendador.intervaloS());
  TEST_ASSERT_TRUE(agendador.deveColetar(10000));
}

void test_intervalo_invalido_mantem_o_anterior() {
  Agendador agendador(120);
  TEST_ASSERT_FALSE(agendador.configurar(5));
  TEST_ASSERT_EQUAL_UINT32(120, agendador.intervaloS());
}

void test_agendador_com_valor_inicial_invalido_usa_o_padrao() {
  Agendador agendador(0);
  TEST_ASSERT_EQUAL_UINT32(INTERVALO_PADRAO_S, agendador.intervaloS());
}

// SCRUM-581/585: leitura e validação

void test_leitura_valida_nos_limites_do_sensor() {
  const Leitura leitura = avaliarLeitura(-40.0f, 100.0f);
  TEST_ASSERT_TRUE(leitura.completa());
}

void test_nan_do_dht_vira_falha() {
  const Leitura leitura = avaliarLeitura(NAN_F, NAN_F);
  TEST_ASSERT_TRUE(leitura.vazia());
}

void test_valor_fora_da_faixa_fisica_vira_falha() {
  const Leitura leitura = avaliarLeitura(80.1f, -0.1f);
  TEST_ASSERT_TRUE(leitura.vazia());
}

void test_leitura_parcial_marca_so_o_sensor_que_falhou() {
  const Leitura leitura = avaliarLeitura(25.0f, NAN_F);
  TEST_ASSERT_TRUE(leitura.temperaturaOk);
  TEST_ASSERT_FALSE(leitura.umidadeOk);
  TEST_ASSERT_FALSE(leitura.completa());
  TEST_ASSERT_FALSE(leitura.vazia());
}

// SCRUM-582/585: contrato de telemetria

void test_payload_segue_o_contrato() {
  char buffer[160];
  const Rejeicao motivo =
      montarPayload(MAC, AGORA, avaliarLeitura(23.456f, 61.0f), Identificadores{}, buffer, sizeof buffer);
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::Nenhuma), static_cast<int>(motivo));
  TEST_ASSERT_EQUAL_STRING(
      "{\"estacao_id\":\"24:6F:28:AB:CD:EF\",\"unix_time\":1794100000,\"temp\":23.46,\"umid\":61.00}",
      buffer);
}

void test_payload_parcial_leva_so_a_leitura_valida() {
  char buffer[160];
  montarPayload(MAC, AGORA, avaliarLeitura(NAN_F, 55.5f), Identificadores{}, buffer, sizeof buffer);
  TEST_ASSERT_NULL(std::strstr(buffer, "temp"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"umid\":55.50"));
}

void test_payload_usa_os_identificadores_cadastrados() {
  char buffer[160];
  Identificadores ids;
  ids.temperatura = "temp_ar";
  ids.umidade = "umid_ar";
  montarPayload(MAC, AGORA, avaliarLeitura(20.0f, 50.0f), ids, buffer, sizeof buffer);
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"temp_ar\":20.00"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"umid_ar\":50.00"));
}

void test_sem_leitura_valida_nao_monta_payload() {
  char buffer[160] = "";
  const Rejeicao motivo =
      montarPayload(MAC, AGORA, avaliarLeitura(NAN_F, NAN_F), Identificadores{}, buffer, sizeof buffer);
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::SemLeituraValida), static_cast<int>(motivo));
  TEST_ASSERT_EQUAL_STRING("", buffer);
}

void test_relogio_nao_sincronizado_nao_publica() {
  char buffer[160];
  const Rejeicao motivo =
      montarPayload(MAC, 42, avaliarLeitura(20.0f, 50.0f), Identificadores{}, buffer, sizeof buffer);
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::RelogioNaoSincronizado), static_cast<int>(motivo));
}

void test_mac_invalido_nao_publica() {
  char buffer[160];
  const Leitura leitura = avaliarLeitura(20.0f, 50.0f);
  for (const char* mac : {"", "24:6f:28:ab:cd:ef", "24-6F-28-AB-CD-EF", "24:6F:28:AB:CD", "00:00:00:00:00:0G"}) {
    TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::MacInvalido),
                      static_cast<int>(montarPayload(mac, AGORA, leitura, Identificadores{}, buffer, sizeof buffer)));
  }
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::MacInvalido),
                    static_cast<int>(montarPayload(nullptr, AGORA, leitura, Identificadores{}, buffer, sizeof buffer)));
}

void test_buffer_pequeno_nao_corta_a_mensagem() {
  char buffer[20];
  const Rejeicao motivo =
      montarPayload(MAC, AGORA, avaliarLeitura(20.0f, 50.0f), Identificadores{}, buffer, sizeof buffer);
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::BufferPequeno), static_cast<int>(motivo));
}

void test_identificador_invalido_nao_monta_payload() {
  char buffer[160];
  Identificadores ids;
  ids.temperatura = "temp\"x";
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::IdentificadorInvalido),
                    static_cast<int>(montarPayload(MAC, AGORA, avaliarLeitura(20.0f, 50.0f), ids, buffer,
                                                   sizeof buffer)));
  ids.temperatura = "";
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::IdentificadorInvalido),
                    static_cast<int>(montarPayload(MAC, AGORA, avaliarLeitura(20.0f, 50.0f), ids, buffer,
                                                   sizeof buffer)));
}

void test_topico_segue_o_contrato() {
  char topico[48];
  TEST_ASSERT_TRUE(montarTopico(MAC, topico, sizeof topico));
  TEST_ASSERT_EQUAL_STRING("estacoes/24:6F:28:AB:CD:EF/dados", topico);
  TEST_ASSERT_FALSE(montarTopico("invalido", topico, sizeof topico));
}

// SCRUM-586: status e falhas

void test_status_separa_falha_de_sensor_de_falha_de_comunicacao() {
  StatusColeta status;
  status.registrar(Evento::FalhaSensor);
  status.registrar(Evento::SemConexao);
  status.registrar(Evento::FalhaPublicacao);
  TEST_ASSERT_EQUAL_UINT32(1, status.falhasSensor());
  TEST_ASSERT_EQUAL_UINT32(2, status.falhasComunicacao());
}

void test_sensor_com_problema_apos_tres_falhas_seguidas() {
  StatusColeta status;
  status.registrar(Evento::FalhaSensor);
  status.registrar(Evento::FalhaSensor);
  TEST_ASSERT_FALSE(status.sensorComProblema());
  status.registrar(Evento::FalhaSensor);
  TEST_ASSERT_TRUE(status.sensorComProblema());
}

void test_coleta_bem_sucedida_zera_as_falhas_seguidas() {
  StatusColeta status;
  for (int i = 0; i < 3; i++) status.registrar(Evento::FalhaSensor);
  status.registrar(Evento::ColetaParcial);
  TEST_ASSERT_FALSE(status.sensorComProblema());
  TEST_ASSERT_EQUAL_UINT32(3, status.falhasSensor());
}

void test_resumo_mostra_reconexoes() {
  StatusColeta status;
  status.registrar(Evento::ConexaoPerdida);
  status.registrar(Evento::TentativaReconexao);
  status.registrar(Evento::TentativaReconexao);
  status.registrar(Evento::Reconectado);
  status.registrar(Evento::Publicada);
  char resumo[200];
  status.resumo(resumo, sizeof resumo);
  TEST_ASSERT_NOT_NULL(std::strstr(resumo, "conexoes_perdidas=1"));
  TEST_ASSERT_NOT_NULL(std::strstr(resumo, "reconexoes=2"));
  TEST_ASSERT_NOT_NULL(std::strstr(resumo, "publicadas=1"));
}

void test_espera_de_reconexao_cresce_ate_60_segundos() {
  TEST_ASSERT_EQUAL_UINT32(1000, atrasoReconexaoMs(0));
  TEST_ASSERT_EQUAL_UINT32(2000, atrasoReconexaoMs(1));
  TEST_ASSERT_EQUAL_UINT32(32000, atrasoReconexaoMs(5));
  TEST_ASSERT_EQUAL_UINT32(60000, atrasoReconexaoMs(6));
  TEST_ASSERT_EQUAL_UINT32(60000, atrasoReconexaoMs(1000));
}

// SCRUM-603: sensores configuráveis

const Sensor DHT_TEMP{"temp", Fonte::Dht22Temperatura, 4};
const Sensor DHT_UMID{"umid", Fonte::Dht22Umidade, 4};
const Sensor LUX{"lux", Fonte::Analogico, 34};
const Sensor PORTA{"porta", Fonte::Digital, 27};

void test_configuracao_so_com_dht22_e_valida() {
  const Sensor sensores[] = {DHT_TEMP, DHT_UMID};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum), static_cast<int>(validarConfiguracao(sensores, 2)));
}

void test_configuracao_aceita_sensores_adicionais() {
  const Sensor sensores[] = {DHT_TEMP, DHT_UMID, LUX, PORTA};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum), static_cast<int>(validarConfiguracao(sensores, 4)));
}

void test_configuracao_vazia_ou_grande_demais_e_recusada() {
  Sensor muitos[MAXIMO_SENSORES + 1];
  const char* nomes[] = {"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12"};
  for (size_t i = 0; i <= MAXIMO_SENSORES; i++) muitos[i] = Sensor{nomes[i], Fonte::Digital, static_cast<int>(13 + i)};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::SemSensores), static_cast<int>(validarConfiguracao(muitos, 0)));
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::SemSensores), static_cast<int>(validarConfiguracao(nullptr, 2)));
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum),
                    static_cast<int>(validarConfiguracao(muitos, MAXIMO_SENSORES)));
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::SensoresDemais),
                    static_cast<int>(validarConfiguracao(muitos, MAXIMO_SENSORES + 1)));
}

void test_identificador_invalido_na_configuracao_e_recusado() {
  for (const char* id : {"", "com espaco", "aspas\"", "acentuação"}) {
    const Sensor sensores[] = {DHT_TEMP, Sensor{id, Fonte::Digital, 27}};
    TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::IdentificadorInvalido),
                      static_cast<int>(validarConfiguracao(sensores, 2)));
  }
  const Sensor semNome[] = {Sensor{nullptr, Fonte::Digital, 27}};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::IdentificadorInvalido),
                    static_cast<int>(validarConfiguracao(semNome, 1)));
}

void test_identificador_repetido_ou_reservado_e_recusado() {
  const Sensor repetido[] = {DHT_TEMP, Sensor{"TEMP", Fonte::Analogico, 34}};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::IdentificadorRepetido),
                    static_cast<int>(validarConfiguracao(repetido, 2)));
  for (const char* id : {"estacao_id", "unix_time", "Unix_Time"}) {
    const Sensor reservado[] = {Sensor{id, Fonte::Digital, 27}};
    TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::IdentificadorReservado),
                      static_cast<int>(validarConfiguracao(reservado, 1)));
  }
}

void test_entrada_analogica_so_nos_pinos_que_funcionam_com_wifi() {
  for (int pino : {32, 34, 36, 39}) {
    const Sensor sensores[] = {Sensor{"lux", Fonte::Analogico, pino}};
    TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum), static_cast<int>(validarConfiguracao(sensores, 1)));
  }
  for (int pino : {4, 25, 31, 40}) {
    const Sensor sensores[] = {Sensor{"lux", Fonte::Analogico, pino}};
    TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::PinoAnalogicoInvalido),
                      static_cast<int>(validarConfiguracao(sensores, 1)));
  }
  const Sensor digital[] = {Sensor{"porta", Fonte::Digital, 4}};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum), static_cast<int>(validarConfiguracao(digital, 1)));
}

void test_faixa_valida_depende_da_fonte() {
  TEST_ASSERT_TRUE(valorValido(Fonte::Dht22Temperatura, -40.0f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Dht22Temperatura, 80.1f));
  TEST_ASSERT_TRUE(valorValido(Fonte::Dht22Umidade, 100.0f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Dht22Umidade, -0.1f));
  TEST_ASSERT_TRUE(valorValido(Fonte::Analogico, 0.0f));
  TEST_ASSERT_TRUE(valorValido(Fonte::Analogico, 4095.0f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Analogico, 4096.0f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Analogico, -1.0f));
  TEST_ASSERT_TRUE(valorValido(Fonte::Digital, 0.0f));
  TEST_ASSERT_TRUE(valorValido(Fonte::Digital, 1.0f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Digital, 0.5f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Analogico, NAN_F));
}

void test_medida_guarda_o_identificador_e_o_valor_bruto() {
  const Medida boa = avaliarMedida(LUX, 2048.0f);
  TEST_ASSERT_TRUE(boa.ok);
  TEST_ASSERT_EQUAL_STRING("lux", boa.identificador);
  TEST_ASSERT_EQUAL_FLOAT(2048.0f, boa.valor);
  const Medida ruim = avaliarMedida(DHT_TEMP, NAN_F);
  TEST_ASSERT_FALSE(ruim.ok);
  TEST_ASSERT_EQUAL_STRING("temp", ruim.identificador);
}

void test_payload_com_varios_sensores_tem_um_campo_por_sensor() {
  const Medida medidas[] = {avaliarMedida(DHT_TEMP, 23.456f), avaliarMedida(DHT_UMID, 61.0f),
                            avaliarMedida(LUX, 2048.0f), avaliarMedida(PORTA, 1.0f)};
  char buffer[320];
  const Rejeicao motivo = montarPayload(MAC, AGORA, medidas, 4, buffer, sizeof buffer);
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::Nenhuma), static_cast<int>(motivo));
  TEST_ASSERT_EQUAL_STRING(
      "{\"estacao_id\":\"24:6F:28:AB:CD:EF\",\"unix_time\":1794100000,"
      "\"temp\":23.46,\"umid\":61.00,\"lux\":2048.00,\"porta\":1.00}",
      buffer);
}

void test_falha_de_um_sensor_nao_impede_o_envio_dos_demais() {
  const Medida medidas[] = {avaliarMedida(DHT_TEMP, NAN_F), avaliarMedida(DHT_UMID, NAN_F),
                            avaliarMedida(LUX, 300.0f)};
  char buffer[320];
  TEST_ASSERT_EQUAL(1, static_cast<int>(contarValidas(medidas, 3)));
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::Nenhuma),
                    static_cast<int>(montarPayload(MAC, AGORA, medidas, 3, buffer, sizeof buffer)));
  TEST_ASSERT_EQUAL_STRING("{\"estacao_id\":\"24:6F:28:AB:CD:EF\",\"unix_time\":1794100000,\"lux\":300.00}", buffer);
}

void test_nenhuma_medida_valida_nao_monta_payload() {
  const Medida medidas[] = {avaliarMedida(DHT_TEMP, NAN_F), avaliarMedida(LUX, 5000.0f)};
  char buffer[320] = "intacto";
  TEST_ASSERT_EQUAL(0, static_cast<int>(contarValidas(medidas, 2)));
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::SemLeituraValida),
                    static_cast<int>(montarPayload(MAC, AGORA, medidas, 2, buffer, sizeof buffer)));
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::SemLeituraValida),
                    static_cast<int>(montarPayload(MAC, AGORA, nullptr, 0, buffer, sizeof buffer)));
}

void test_payload_com_o_maximo_de_sensores_cabe_no_buffer_do_firmware() {
  const char* nomes[] = {"temperatura_ar", "umidade_ar", "luminosidade", "umidade_solo",
                         "temperatura_solo", "pressao_atm", "vento_veloc", "chuva_calha",
                         "radiacao_sol", "direcao_vent", "molhamento_f", "nivel_reserv"};
  Medida medidas[MAXIMO_SENSORES];
  for (size_t i = 0; i < MAXIMO_SENSORES; i++) medidas[i] = Medida{nomes[i], 4095.0f, true};
  char buffer[384];
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::Nenhuma),
                    static_cast<int>(montarPayload(MAC, AGORA, medidas, MAXIMO_SENSORES, buffer, sizeof buffer)));
  TEST_ASSERT_TRUE(std::strlen(buffer) < 384);
  char pequeno[64];
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::BufferPequeno),
                    static_cast<int>(montarPayload(MAC, AGORA, medidas, MAXIMO_SENSORES, pequeno, sizeof pequeno)));
  TEST_ASSERT_EQUAL_STRING("", pequeno);
}

void test_estacao_so_com_dht22_gera_o_mesmo_payload_de_antes() {
  char antigo[160];
  char novo[160];
  montarPayload(MAC, AGORA, avaliarLeitura(23.456f, 61.0f), Identificadores{}, antigo, sizeof antigo);
  const Medida medidas[] = {avaliarMedida(DHT_TEMP, 23.456f), avaliarMedida(DHT_UMID, 61.0f)};
  montarPayload(MAC, AGORA, medidas, 2, novo, sizeof novo);
  TEST_ASSERT_EQUAL_STRING(antigo, novo);
}

// Buzzer: alarme só para falha que persiste

namespace {
const uint32_t MINUTO = 60UL * 1000UL;
}  // namespace

void test_falha_curta_nao_toca() {
  Alarme alarme;
  alarme.atualizar(Falha::Conexao, true, 0);
  TEST_ASSERT_TRUE(alarme.verificar(29 * MINUTO).vazio());
  alarme.atualizar(Falha::Conexao, false, 29 * MINUTO);
  TEST_ASSERT_TRUE(alarme.verificar(31 * MINUTO).vazio());
}

void test_falha_persistente_toca_depois_de_30_minutos() {
  Alarme alarme;
  alarme.atualizar(Falha::Sensor, true, 0);
  TEST_ASSERT_TRUE(alarme.verificar(PERSISTENCIA_MS - 1).vazio());
  const Aviso aviso = alarme.verificar(PERSISTENCIA_MS);
  TEST_ASSERT_EQUAL_UINT8(mascaraDe(Falha::Sensor), aviso.falhas);
  TEST_ASSERT_FALSE(aviso.normalizou);
}

void test_falha_que_volta_reinicia_a_contagem() {
  Alarme alarme;
  alarme.atualizar(Falha::Hora, true, 0);
  alarme.atualizar(Falha::Hora, false, 20 * MINUTO);
  alarme.atualizar(Falha::Hora, true, 25 * MINUTO);
  TEST_ASSERT_TRUE(alarme.verificar(40 * MINUTO).vazio());
  TEST_ASSERT_EQUAL_UINT8(mascaraDe(Falha::Hora), alarme.verificar(55 * MINUTO).falhas);
}

void test_alarme_repete_a_cada_30_minutos_enquanto_durar() {
  Alarme alarme;
  alarme.atualizar(Falha::Conexao, true, 0);
  TEST_ASSERT_FALSE(alarme.verificar(30 * MINUTO).vazio());
  TEST_ASSERT_TRUE(alarme.verificar(31 * MINUTO).vazio());
  TEST_ASSERT_TRUE(alarme.verificar(59 * MINUTO).vazio());
  TEST_ASSERT_EQUAL_UINT8(mascaraDe(Falha::Conexao), alarme.verificar(60 * MINUTO).falhas);
}

void test_configuracao_invalida_toca_na_hora() {
  Alarme alarme;
  alarme.atualizar(Falha::Configuracao, true, 1000);
  TEST_ASSERT_EQUAL_UINT8(mascaraDe(Falha::Configuracao), alarme.verificar(1000).falhas);
}

void test_nova_falha_durante_o_alarme_toca_na_hora_com_as_duas() {
  Alarme alarme;
  alarme.atualizar(Falha::Sensor, true, 0);
  alarme.atualizar(Falha::Conexao, true, 10 * MINUTO);
  TEST_ASSERT_EQUAL_UINT8(mascaraDe(Falha::Sensor), alarme.verificar(30 * MINUTO).falhas);
  TEST_ASSERT_EQUAL_UINT8(mascaraDe(Falha::Sensor) | mascaraDe(Falha::Conexao), alarme.verificar(40 * MINUTO).falhas);
}

void test_avisa_uma_vez_quando_normaliza() {
  Alarme alarme;
  alarme.atualizar(Falha::Sensor, true, 0);
  alarme.verificar(30 * MINUTO);
  alarme.atualizar(Falha::Sensor, false, 35 * MINUTO);
  const Aviso aviso = alarme.verificar(35 * MINUTO);
  TEST_ASSERT_TRUE(aviso.normalizou);
  TEST_ASSERT_EQUAL_UINT8(0, aviso.falhas);
  TEST_ASSERT_TRUE(alarme.verificar(36 * MINUTO).vazio());
}

void test_falha_que_nao_chegou_a_tocar_nao_avisa_normalizacao() {
  Alarme alarme;
  alarme.atualizar(Falha::Sensor, true, 0);
  alarme.verificar(10 * MINUTO);
  alarme.atualizar(Falha::Sensor, false, 11 * MINUTO);
  TEST_ASSERT_TRUE(alarme.verificar(11 * MINUTO).vazio());
}

void test_alarme_continua_certo_quando_millis_da_a_volta() {
  Alarme alarme;
  const uint32_t inicio = 0xFFFFFFFFUL - 5 * MINUTO;
  alarme.atualizar(Falha::Conexao, true, inicio);
  TEST_ASSERT_TRUE(alarme.verificar(inicio + 29 * MINUTO).vazio());
  TEST_ASSERT_EQUAL_UINT8(mascaraDe(Falha::Conexao), alarme.verificar(inicio + 30 * MINUTO).falhas);
}

void test_cada_falha_tem_um_codigo_de_bipes() {
  TEST_ASSERT_EQUAL_UINT8(2, bipesDe(Falha::Sensor));
  TEST_ASSERT_EQUAL_UINT8(3, bipesDe(Falha::Conexao));
  TEST_ASSERT_EQUAL_UINT8(4, bipesDe(Falha::Hora));
  TEST_ASSERT_EQUAL_UINT8(5, bipesDe(Falha::Configuracao));
}

void test_bipador_toca_o_codigo_sem_bloquear() {
  Bipador bipador;
  Aviso aviso;
  aviso.falhas = mascaraDe(Falha::Sensor);
  bipador.tocar(aviso, 1000);
  TEST_ASSERT_TRUE(bipador.ligado(1000));
  TEST_ASSERT_TRUE(bipador.ligado(1000 + Bipador::BIPE_MS - 1));
  TEST_ASSERT_FALSE(bipador.ligado(1000 + Bipador::BIPE_MS));
  TEST_ASSERT_TRUE(bipador.ligado(1000 + Bipador::BIPE_MS + Bipador::PAUSA_BIPE_MS));
  const uint32_t fimDoSegundo = 1000 + 2 * Bipador::BIPE_MS + Bipador::PAUSA_BIPE_MS;
  TEST_ASSERT_FALSE(bipador.ligado(fimDoSegundo));
  TEST_ASSERT_TRUE(bipador.tocando(fimDoSegundo));
  TEST_ASSERT_FALSE(bipador.tocando(fimDoSegundo + Bipador::PAUSA_GRUPO_MS));
}

void test_bipador_normalizou_e_um_bipe_longo() {
  Bipador bipador;
  Aviso aviso;
  aviso.normalizou = true;
  bipador.tocar(aviso, 0);
  TEST_ASSERT_TRUE(bipador.ligado(Bipador::BIPE_NORMALIZOU_MS - 1));
  TEST_ASSERT_FALSE(bipador.ligado(Bipador::BIPE_NORMALIZOU_MS));
}

void test_bipador_parado_fica_desligado() {
  Bipador bipador;
  TEST_ASSERT_FALSE(bipador.ligado(0));
  TEST_ASSERT_FALSE(bipador.tocando(12345));
}

void test_bipador_com_todas_as_falhas_toca_os_14_bipes() {
  Bipador bipador;
  Aviso aviso;
  for (size_t i = 0; i < QUANTIDADE_FALHAS; i++) aviso.falhas |= mascaraDe(static_cast<Falha>(i));
  bipador.tocar(aviso, 0);
  int bipes = 0;
  bool anterior = false;
  for (uint32_t t = 0; t < bipador.duracaoMs(); t += 10) {
    const bool agora = bipador.ligado(t);
    if (agora && !anterior) bipes++;
    anterior = agora;
  }
  TEST_ASSERT_EQUAL_INT(14, bipes);
}

// SCRUM-621: canais de pulsos (pluviômetro basculante, anemômetro)

void test_pulsos_aceita_inteiro_nao_negativo_e_recusa_o_resto() {
  TEST_ASSERT_TRUE(valorValido(Fonte::Pulsos, 0.0f));
  TEST_ASSERT_TRUE(valorValido(Fonte::Pulsos, 37.0f));
  TEST_ASSERT_TRUE(valorValido(Fonte::Pulsos, PULSOS_MAX));
  TEST_ASSERT_FALSE(valorValido(Fonte::Pulsos, -1.0f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Pulsos, 2.5f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Pulsos, PULSOS_MAX + 1.0f));
  TEST_ASSERT_FALSE(valorValido(Fonte::Pulsos, NAN_F));
}

void test_pulsos_e_digital_so_em_pino_com_pull_up() {
  for (Fonte fonte : {Fonte::Pulsos, Fonte::Digital}) {
    for (int pino : {0, 4, 5, 12, 18, 26, 27, 32, 33}) {
      const Sensor s[] = {Sensor{"canal", fonte, pino}};
      TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum), static_cast<int>(validarConfiguracao(s, 1)));
    }
    for (int pino : {-1, 6, 11, 28, 34, 39, 40}) {
      const Sensor s[] = {Sensor{"canal", fonte, pino}};
      TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::PinoSemPullUp),
                        static_cast<int>(validarConfiguracao(s, 1)));
    }
  }
}

void test_um_pino_por_canal_exceto_o_dht22() {
  const Sensor dht[] = {DHT_TEMP, DHT_UMID};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum), static_cast<int>(validarConfiguracao(dht, 2)));
  const Sensor repetido[] = {Sensor{"pu26", Fonte::Pulsos, 26}, Sensor{"di26", Fonte::Digital, 26}};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::PinoRepetido),
                    static_cast<int>(validarConfiguracao(repetido, 2)));
  const Sensor comDht[] = {DHT_TEMP, Sensor{"di4", Fonte::Digital, 4}};
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::PinoRepetido),
                    static_cast<int>(validarConfiguracao(comDht, 2)));
}

// SCRUM-621: todos os canais da placa ficam ativos

void test_mapa_de_canais_da_placa_e_valido() {
  TEST_ASSERT_EQUAL(static_cast<int>(ErroConfiguracao::Nenhum),
                    static_cast<int>(validarConfiguracao(SENSORES, QUANTIDADE_SENSORES)));
  TEST_ASSERT_EQUAL(12, static_cast<int>(QUANTIDADE_SENSORES));
  TEST_ASSERT_TRUE(pinoLivre(PINO_BUZZER));
}

void test_mapa_tem_todos_os_tipos_de_canal() {
  size_t porFonte[5] = {};
  for (const Sensor& s : SENSORES) porFonte[static_cast<int>(s.fonte)]++;
  TEST_ASSERT_EQUAL(1, static_cast<int>(porFonte[static_cast<int>(Fonte::Dht22Temperatura)]));
  TEST_ASSERT_EQUAL(1, static_cast<int>(porFonte[static_cast<int>(Fonte::Dht22Umidade)]));
  TEST_ASSERT_EQUAL(6, static_cast<int>(porFonte[static_cast<int>(Fonte::Analogico)]));
  TEST_ASSERT_EQUAL(2, static_cast<int>(porFonte[static_cast<int>(Fonte::Pulsos)]));
  TEST_ASSERT_EQUAL(2, static_cast<int>(porFonte[static_cast<int>(Fonte::Digital)]));
}

void test_mapa_mantem_os_identificadores_do_dht22() {
  TEST_ASSERT_EQUAL_STRING("temp", SENSORES[0].identificador);
  TEST_ASSERT_EQUAL_STRING("umid", SENSORES[1].identificador);
}

void test_payload_com_todos_os_canais_no_maximo_cabe_no_buffer_do_firmware() {
  Medida medidas[QUANTIDADE_SENSORES];
  for (size_t i = 0; i < QUANTIDADE_SENSORES; i++) {
    float bruto = 0;
    switch (SENSORES[i].fonte) {
      case Fonte::Dht22Temperatura: bruto = -40.0f; break;
      case Fonte::Dht22Umidade: bruto = 100.0f; break;
      case Fonte::Analogico: bruto = 4095.0f; break;
      case Fonte::Digital: bruto = 1.0f; break;
      case Fonte::Pulsos: bruto = PULSOS_MAX; break;
    }
    medidas[i] = avaliarMedida(SENSORES[i], bruto);
  }
  TEST_ASSERT_EQUAL(static_cast<int>(QUANTIDADE_SENSORES), static_cast<int>(contarValidas(medidas, QUANTIDADE_SENSORES)));
  char buffer[384];
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::Nenhuma),
                    static_cast<int>(montarPayload(MAC, AGORA, medidas, QUANTIDADE_SENSORES, buffer, sizeof buffer)));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"pu26\":100000.00"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "\"an39\":4095.00"));
}

void test_contador_conta_pulsos_separados() {
  ContadorPulsos c;
  TEST_ASSERT_TRUE(c.registrar(1000));
  TEST_ASSERT_TRUE(c.registrar(5000));
  TEST_ASSERT_TRUE(c.registrar(9000));
  TEST_ASSERT_EQUAL_UINT32(3, c.total());
}

void test_contador_ignora_quique_do_reed() {
  ContadorPulsos c;
  TEST_ASSERT_TRUE(c.registrar(1000));
  TEST_ASSERT_FALSE(c.registrar(1003));
  TEST_ASSERT_FALSE(c.registrar(1049));
  TEST_ASSERT_TRUE(c.registrar(1050));
  TEST_ASSERT_EQUAL_UINT32(2, c.total());
}

void test_contador_primeiro_pulso_conta_mesmo_com_millis_perto_de_zero() {
  ContadorPulsos c;
  TEST_ASSERT_TRUE(c.registrar(0));
  TEST_ASSERT_EQUAL_UINT32(1, c.total());
}

void test_contador_continua_certo_quando_millis_da_a_volta() {
  ContadorPulsos c;
  TEST_ASSERT_TRUE(c.registrar(0xFFFFFFF0u));
  TEST_ASSERT_FALSE(c.registrar(0x00000005u));  // 21 ms depois
  TEST_ASSERT_TRUE(c.registrar(0x00000100u));
  TEST_ASSERT_EQUAL_UINT32(2, c.total());
}

void test_pulsos_publicados_saem_da_contagem_e_os_novos_ficam() {
  ContadorPulsos c;
  for (uint32_t t = 1000; t < 6000; t += 1000) c.registrar(t);  // 5 pulsos
  const uint32_t enviado = c.total();
  c.registrar(7000);  // chegou durante o envio
  c.descontar(enviado);
  TEST_ASSERT_EQUAL_UINT32(1, c.total());
}

void test_pulsos_nao_publicados_acumulam_para_o_proximo_envio() {
  ContadorPulsos c;
  c.registrar(1000);
  c.registrar(2000);
  // envio falhou: nada é descontado
  c.registrar(3000);
  TEST_ASSERT_EQUAL_UINT32(3, c.total());
  const Medida m = avaliarMedida(Sensor{"pu26", Fonte::Pulsos, 26}, static_cast<float>(c.total()));
  TEST_ASSERT_TRUE(m.ok);
  TEST_ASSERT_EQUAL_FLOAT(3.0f, m.valor);
}

void test_descontar_mais_que_o_pendente_nao_estoura() {
  ContadorPulsos c;
  c.registrar(1000);
  c.descontar(5);
  TEST_ASSERT_EQUAL_UINT32(0, c.total());
}

void test_payload_leva_os_pulsos_como_valor_bruto() {
  const Medida medidas[] = {Medida{"temp", 25.0f, true}, Medida{"pu26", 12.0f, true}};
  char payload[320];
  TEST_ASSERT_EQUAL(static_cast<int>(Rejeicao::Nenhuma),
                    static_cast<int>(montarPayload(MAC, AGORA, medidas, 2, payload, sizeof payload)));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"pu26\":12.00"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_intervalo_aceita_limites_e_rejeita_fora_da_faixa);
  RUN_TEST(test_agendador_coleta_na_primeira_chamada);
  RUN_TEST(test_agendador_nao_coleta_duas_vezes_no_mesmo_intervalo);
  RUN_TEST(test_agendador_atrasado_coleta_uma_vez_e_reancora);
  RUN_TEST(test_agendador_continua_certo_quando_millis_da_a_volta);
  RUN_TEST(test_novo_intervalo_vale_para_as_proximas_coletas);
  RUN_TEST(test_intervalo_invalido_mantem_o_anterior);
  RUN_TEST(test_agendador_com_valor_inicial_invalido_usa_o_padrao);
  RUN_TEST(test_leitura_valida_nos_limites_do_sensor);
  RUN_TEST(test_nan_do_dht_vira_falha);
  RUN_TEST(test_valor_fora_da_faixa_fisica_vira_falha);
  RUN_TEST(test_leitura_parcial_marca_so_o_sensor_que_falhou);
  RUN_TEST(test_payload_segue_o_contrato);
  RUN_TEST(test_payload_parcial_leva_so_a_leitura_valida);
  RUN_TEST(test_payload_usa_os_identificadores_cadastrados);
  RUN_TEST(test_sem_leitura_valida_nao_monta_payload);
  RUN_TEST(test_relogio_nao_sincronizado_nao_publica);
  RUN_TEST(test_mac_invalido_nao_publica);
  RUN_TEST(test_buffer_pequeno_nao_corta_a_mensagem);
  RUN_TEST(test_identificador_invalido_nao_monta_payload);
  RUN_TEST(test_topico_segue_o_contrato);
  RUN_TEST(test_status_separa_falha_de_sensor_de_falha_de_comunicacao);
  RUN_TEST(test_sensor_com_problema_apos_tres_falhas_seguidas);
  RUN_TEST(test_coleta_bem_sucedida_zera_as_falhas_seguidas);
  RUN_TEST(test_resumo_mostra_reconexoes);
  RUN_TEST(test_espera_de_reconexao_cresce_ate_60_segundos);
  RUN_TEST(test_configuracao_so_com_dht22_e_valida);
  RUN_TEST(test_configuracao_aceita_sensores_adicionais);
  RUN_TEST(test_configuracao_vazia_ou_grande_demais_e_recusada);
  RUN_TEST(test_identificador_invalido_na_configuracao_e_recusado);
  RUN_TEST(test_identificador_repetido_ou_reservado_e_recusado);
  RUN_TEST(test_entrada_analogica_so_nos_pinos_que_funcionam_com_wifi);
  RUN_TEST(test_faixa_valida_depende_da_fonte);
  RUN_TEST(test_medida_guarda_o_identificador_e_o_valor_bruto);
  RUN_TEST(test_payload_com_varios_sensores_tem_um_campo_por_sensor);
  RUN_TEST(test_falha_de_um_sensor_nao_impede_o_envio_dos_demais);
  RUN_TEST(test_nenhuma_medida_valida_nao_monta_payload);
  RUN_TEST(test_payload_com_o_maximo_de_sensores_cabe_no_buffer_do_firmware);
  RUN_TEST(test_estacao_so_com_dht22_gera_o_mesmo_payload_de_antes);
  RUN_TEST(test_falha_curta_nao_toca);
  RUN_TEST(test_falha_persistente_toca_depois_de_30_minutos);
  RUN_TEST(test_falha_que_volta_reinicia_a_contagem);
  RUN_TEST(test_alarme_repete_a_cada_30_minutos_enquanto_durar);
  RUN_TEST(test_configuracao_invalida_toca_na_hora);
  RUN_TEST(test_nova_falha_durante_o_alarme_toca_na_hora_com_as_duas);
  RUN_TEST(test_avisa_uma_vez_quando_normaliza);
  RUN_TEST(test_falha_que_nao_chegou_a_tocar_nao_avisa_normalizacao);
  RUN_TEST(test_alarme_continua_certo_quando_millis_da_a_volta);
  RUN_TEST(test_cada_falha_tem_um_codigo_de_bipes);
  RUN_TEST(test_bipador_toca_o_codigo_sem_bloquear);
  RUN_TEST(test_bipador_normalizou_e_um_bipe_longo);
  RUN_TEST(test_bipador_parado_fica_desligado);
  RUN_TEST(test_bipador_com_todas_as_falhas_toca_os_14_bipes);
  RUN_TEST(test_pulsos_aceita_inteiro_nao_negativo_e_recusa_o_resto);
  RUN_TEST(test_pulsos_e_digital_so_em_pino_com_pull_up);
  RUN_TEST(test_um_pino_por_canal_exceto_o_dht22);
  RUN_TEST(test_mapa_de_canais_da_placa_e_valido);
  RUN_TEST(test_mapa_tem_todos_os_tipos_de_canal);
  RUN_TEST(test_mapa_mantem_os_identificadores_do_dht22);
  RUN_TEST(test_payload_com_todos_os_canais_no_maximo_cabe_no_buffer_do_firmware);
  RUN_TEST(test_contador_conta_pulsos_separados);
  RUN_TEST(test_contador_ignora_quique_do_reed);
  RUN_TEST(test_contador_primeiro_pulso_conta_mesmo_com_millis_perto_de_zero);
  RUN_TEST(test_contador_continua_certo_quando_millis_da_a_volta);
  RUN_TEST(test_pulsos_publicados_saem_da_contagem_e_os_novos_ficam);
  RUN_TEST(test_pulsos_nao_publicados_acumulam_para_o_proximo_envio);
  RUN_TEST(test_descontar_mais_que_o_pendente_nao_estoura);
  RUN_TEST(test_payload_leva_os_pulsos_como_valor_bruto);
  return UNITY_END();
}
