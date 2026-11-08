#include <unity.h>

#include <cstring>
#include <initializer_list>
#include <limits>

#include "intervalo.h"
#include "leitura.h"
#include "payload.h"
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
  return UNITY_END();
}
