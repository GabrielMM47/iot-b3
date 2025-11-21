#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "certificados.h"
#include <MQTT.h>
#include <RCSwitch.h>
#include <GFButton.h>
#include <Preferences.h>

//MUDAR DEPOIS
#define RF_PORT 2
#define B1_PORT 5
#define B2_PORT 6
#define B3_PORT 7
#define LDR_PIN 18

//Configurações do MQTT e Blynk
String MQTT_HOST = "ny3.blynk.cloud";
int MQTT_PORT = 8883;              // TLS
String MQTT_USER = "device";             // fixo
String BLYNK_AUTH_TOKEN = "xLNpnJ0e6f3JaeamhnZuKbRhxakC9lhK";   // password do MQTT

// Objetos globais
WiFiClientSecure conexaoSegura;
MQTTClient mqtt(1000);
Preferences preferencias;

//Botões
GFButton button1(B1_PORT);
GFButton button2(B2_PORT);
GFButton button3(B3_PORT);

//RF Switch
RCSwitch mySwitch = RCSwitch();
const unsigned long UP_CODE = 2448754;
const unsigned long DOWN_CODE = 2448756;
const unsigned long STOP_CODE = 2448760;
const int BITS = 24;

//Variaveis de controle integrado da persiana com a luz
int fechar = 0;
int luminosidadeAtual = 0;
float metaNum = 40;
int statusPersiana = 0;
int novoStatus = -1;
int comandoPersiana = 0;
int statusPersianaBlynk = 0;
int autoMode = 0; // 0 - manual, 1 - automático

//Variaveis de tempo
unsigned long instanteAnterior = 0; //Instante para checagem do sensor de luz
unsigned long instanteAnterior2 = 0; //Instante para checagem da persiana
unsigned long instanteAnteriorAjuste = 0;
unsigned long periodoDeChecagem = 10; //em Segundos



//Handlers dos botões
void upButtonPressed(GFButton& button) {
    Serial.println("Botão 1 Pressionado - Enviando código");
    mySwitch.send(UP_CODE, BITS);
}

void downButtonPressed(GFButton& button) {
    Serial.println("Botão 2 Pressionado - Enviando código");
    mySwitch.send(DOWN_CODE, BITS);
}

void stopButtonPressed(GFButton& button) {
    Serial.println("Botão 3 Pressionado - Enviando código");
    mySwitch.send(STOP_CODE, BITS);
}


void reconectarWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin("Projeto", "2022-11-07");

        Serial.print("Conectando ao WiFi...");
        while (WiFi.status() != WL_CONNECTED) {
            Serial.print(".");
            delay(1000);
        }
    Serial.print("conectado!\nEndereço IP: ");
    Serial.println(WiFi.localIP());
    }
}

void reconectarMQTT() {
    if (!mqtt.connected()) {
        Serial.print("Conectando MQTT...");
        while(!mqtt.connected()) {
            mqtt.connect("GabrielMm47-Esp32", "device", "xLNpnJ0e6f3JaeamhnZuKbRhxakC9lhK");
            Serial.print(".");
            delay(1000);
        }
        Serial.println(" conectado!");

        mqtt.subscribe("downlink/#");
    }
}

void ajustePersiana (const unsigned long code, int tempoDeMovimento) {
    unsigned long instanteInicioAjuste = millis();
    mySwitch.send(code, BITS);
    while (millis() < instanteInicioAjuste + tempoDeMovimento) {
    }//DELAY
    mySwitch.send(STOP_CODE, BITS);
}

void recebeuMensagem(String topico, String conteudo) {
    Serial.println("\n" + topico + ": " + conteudo);
    if (topico.endsWith(String("downlink/ds/LED"))) {
        int valor = conteudo.toInt();
        Serial.println("Valor recebido: " + String(valor));
        if (valor == 1) {
            rgbLedWrite(RGB_BUILTIN, 0, 50, 0);
        }
        else {
            rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
        }
    }

    if (topico.endsWith(String("downlink/ds/Persiana"))) {
        int comandoPersiana = conteudo.toInt();
        Serial.println("Valor recebido: " + String(comandoPersiana));
        if (comandoPersiana == 0) {
            mySwitch.send(STOP_CODE, BITS);
        }
        else if (comandoPersiana == 1) {
            mySwitch.send(UP_CODE, BITS);
        }
        else if (comandoPersiana == 2) {
            mySwitch.send(DOWN_CODE, BITS);
        }
    }

    if (topico.endsWith(String("downlink/ds/lightMeta"))) {
        metaNum = conteudo.toInt();
        preferencias.putInt("metaLum", metaNum);
        Serial.println("Nova meta de luminosidade: " + String(metaNum) + "%");
    }

    if (topico.endsWith(String("downlink/ds/blindStatus"))) {
        statusPersianaBlynk = conteudo.toInt();
        preferencias.putInt("statusPer", statusPersianaBlynk);
        Serial.println("Novo status da persiana via Blynk: " + String(statusPersianaBlynk) + "%");
        
        int diferenca = statusPersianaBlynk - statusPersiana;
        int tempoDeEspera = abs(diferenca) * 300;
        if (diferenca > 0) {//descer persiana
            ajustePersiana(DOWN_CODE, tempoDeEspera);
            statusPersiana = statusPersianaBlynk;
        }
        else if (diferenca < 0) {
            ajustePersiana(UP_CODE, tempoDeEspera);
            statusPersiana = statusPersianaBlynk;
        }
        else {
            Serial.println("Persiana já está na posição desejada.");
        }
    }

    if (topico.endsWith(String("downlink/ds/autoMode"))) {
        autoMode = conteudo.toInt();
        preferencias.putInt("autoMode", autoMode);
        Serial.println("Novo modo de operação recebido: " + String(autoMode));
        if (autoMode == 1) {
            Serial.println("Modo automático ativado.\n");
        }
        else {
            Serial.println("Modo automático desativado.\n");
        }
    }
}

void setup() {
    Serial.begin(115200); delay(500);
    reconectarWiFi();
    conexaoSegura.setCACert(certificado1);

    mqtt.begin("ny3.blynk.cloud", 8883, conexaoSegura);
    mqtt.onMessage(recebeuMensagem);
    mqtt.setKeepAlive(45);
    //mqtt.setWill("tópico da desconexão", "conteúdo");

    reconectarMQTT();

    mySwitch.enableTransmit(RF_PORT);

    button1.setPressHandler(upButtonPressed);
    button2.setPressHandler(downButtonPressed);
    button3.setPressHandler(stopButtonPressed);

    preferencias.begin("dadosPersiana");
    statusPersiana = preferencias.getInt("statusPer", 0);
    metaNum = preferencias.getInt("metaLum", -1);
    autoMode = preferencias.getInt("autoMode", 0);
}

void loop() {
    reconectarWiFi();
    reconectarMQTT();
    mqtt.loop();
    button1.process();
    button2.process();
    button3.process();

    unsigned long instanteAtual = millis();
    if (instanteAtual > instanteAnterior + 3000) {  
        int leitura = analogRead(LDR_PIN);
        luminosidadeAtual = map(leitura, 0, 4095, 0, 100);
        Serial.print("Luminosidade atual: " + String(luminosidadeAtual) + "%\n");
        
        //Envio da Luminosidade via MQTT para o Blynk
        String conteudo = String(luminosidadeAtual);
        mqtt.publish("ds/LightSensor", conteudo);

        if (metaNum == -1){//Não tem meta 
            Serial.println("Sem meta!");
        } 

        instanteAnterior = instanteAtual;
    }

     if ((instanteAtual > instanteAnterior2 + periodoDeChecagem*1000) && (metaNum != -1) && (autoMode == 1)){
        Serial.println("\nVerificando necessidade de ajuste na persiana...");
        Serial.println("Status atual da persiana: " + String(statusPersiana) + "%");
        Serial.println("Luminosidade atual: " + String(luminosidadeAtual) + "%");
        Serial.println("Meta de luminosidade: " + String(metaNum) + "%\n");
        
        if ((luminosidadeAtual < (metaNum + 5)) && (luminosidadeAtual > (metaNum -5))){// Estamos na meta!
            //nao faz nada...
            Serial.println("\nEstamos na meta!");
        }

        else { // se está fora do treshold

            if (( (statusPersiana == 0) && (metaNum > luminosidadeAtual)) || ( (statusPersiana == 100) && (metaNum < luminosidadeAtual) ) ){//Nesse caso não da mais para fazer nada
                Serial.println("\nInfelizmente nao sera possivel atingir a meta, a persiana já está no limite. :(");
            }
            else{
                if (luminosidadeAtual < metaNum){//Aqui devemos subir a persiana.
                    //Se 30 segundos é 100% da persiana, queremos subir 5%. Logo precisamos subir a persiana por 1,5 segundos
                    novoStatus = statusPersiana - 5;
                    if (novoStatus < 0){
                        statusPersiana = 0;
                    }
                    else{
                        statusPersiana = novoStatus;
                        // mySwitch.send(UP_CODE, BITS);
                        // delay(2000);//dando 500 milisegundos de folga - testar melhor...
                        // mySwitch.send(STOP_CODE,BITS);
                        ajustePersiana(UP_CODE, 2000);
                    }
                    mqtt.publish("ds/blindStatus", String(statusPersiana));
                    preferencias.putInt("statusPer", statusPersiana);
                }
                
                else{
                    //Se 30 segundos é 100% da persiana, queremos descer 5%. Logo precisamos subir a persiana por 1,5 segundos
                    novoStatus = statusPersiana + 5;
                    if (novoStatus > 100){
                        statusPersiana = 100;
                    }
                    else{
                        statusPersiana = novoStatus;
                        // mySwitch.send(DOWN_CODE, BITS);
                        // delay(2000); //dando 500 milisegundos de folga - testar melhor...
                        // mySwitch.send(STOP_CODE,BITS);
                        ajustePersiana(DOWN_CODE, 2000);
                    }   
                    mqtt.publish("ds/blindStatus", String(statusPersiana));
                    preferencias.putInt("statusPer", statusPersiana);
                }
            }
            Serial.println("Novo status da persiana: " + String(statusPersiana) + "%\n");
        }
        instanteAnterior2 = instanteAtual;
    }
}
