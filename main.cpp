//=====[Libraries]=============================================================

#include "mbed.h"
#include "arm_book_lib.h"

//=====[Defines]===============================================================

#define NUMBER_OF_KEYS                           4
#define BLINKING_TIME_GAS_ALARM               1000
#define BLINKING_TIME_OVER_TEMP_ALARM          500
#define BLINKING_TIME_GAS_AND_OVER_TEMP_ALARM  100
#define NUMBER_OF_AVG_SAMPLES                   100
#define OVER_TEMP_LEVEL                         50
#define TIME_INCREMENT_MS                       10

//=====[Declaration and initialization of public global objects]===============

DigitalIn enterButton(BUTTON1);     //enterButton es el objeto de la clase DigitalIn, el constructor es DigitalIn() e inicializa la estructura gpio()
DigitalIn alarmTestButton(D2);      //Los métodos son read(), mode() y is_connected()
DigitalIn aButton(D4);              //Idem para los demás
DigitalIn bButton(D5);
DigitalIn cButton(D6);
DigitalIn dButton(D7);
DigitalIn mq2(PE_12);

DigitalOut alarmLed(LED1);          //alarmLED es el objeto de la clase DigitalOut, el constructor es DigitalOut() e inicializa la estructura gpio()
DigitalOut incorrectCodeLed(LED3);  //Los métodos son write(), read(), y is_connected()
DigitalOut systemBlockedLed(LED2);  //Idem para los demás

DigitalInOut sirenPin(PE_10);       //sirenPin es el objeto de la clase DigitalInOut, el constructor es DigitalInOut() e inicializa la estructura gpio()
                                    //Los métodos son read(), mode(), write(), output(), input(), y is_connected()
UnbufferedSerial uartUsb(USBTX, USBRX, 115200);     //Leer en uartTask()

AnalogIn potentiometer(A0); //potentiometer es el objeto de la clase AnalogIn, el constructor es AnalogIn() y pide memoria para la estructura PinMap o PinName según corresponda
AnalogIn lm35(A1);          //Los métodos son read(), read_u16(), read_voltage(), set_reference_voltage(), get_reference_voltage()

//=====[Declaration and initialization of public global variables]=============

bool alarmState    = OFF;
bool incorrectCode = false;
bool overTempDetector = OFF;

int numberOfIncorrectCodes = 0;
int buttonBeingCompared    = 0;
int codeSequence[NUMBER_OF_KEYS]   = { 1, 1, 0, 0 };       //Clave de reinicio del estado de alarma
int buttonsPressed[NUMBER_OF_KEYS] = { 0, 0, 0, 0 };       //Array donde se guardará el estado de los pulsadores (a-b-c-d) presionados
int accumulatedTimeAlarm = 0;

bool gasDetectorState          = OFF;
bool overTempDetectorState     = OFF;

float potentiometerReading = 0.0;
float lm35ReadingsAverage  = 0.0;
float lm35ReadingsSum      = 0.0;
float lm35ReadingsArray[NUMBER_OF_AVG_SAMPLES];
float lm35TempC            = 0.0;

//=====[Declarations (prototypes) of public functions]=========================

void inputsInit();
void outputsInit();

void alarmActivationUpdate();
void alarmDeactivationUpdate();

void uartTask();
void availableCommands();
bool areEqual();
float celsiusToFahrenheit( float tempInCelsiusDegrees );
float analogReadingScaledWithTheLM35Formula( float analogReading );

//=====[Main function, the program entry point after power on or reset]========

int main()
{
    inputsInit();
    outputsInit();
    while (true) {
        alarmActivationUpdate();
        alarmDeactivationUpdate();
        uartTask();
        delay(TIME_INCREMENT_MS);
    }
}

//=====[Implementations of public functions]===================================

void inputsInit()       //inicializa entradas
{
    alarmTestButton.mode(PullDown);
    aButton.mode(PullDown);
    bButton.mode(PullDown);
    cButton.mode(PullDown);
    dButton.mode(PullDown);
    sirenPin.mode(OpenDrain);
    sirenPin.input();
}

void outputsInit()  //inicializa las salidas
{
    alarmLed = OFF;
    incorrectCodeLed = OFF;
    systemBlockedLed = OFF;
}

/*Los pines analógicos se monitorean utilizando el método read().
Sin embargo también están los métodos read_u16(), read_voltage(), set_reference_voltage() y get_reference_voltage().
El objeto será "lm35" o "potentiometer" respectivamente, la clase será AnalogIn, y posee 3 constructores públicos con el mismo nombre que la clase.
Hereda PinMap de pnmap.h que es una estructura. La interfaz utiliza los métodos, constructores y destructores de este objeto*/
void alarmActivationUpdate()
{
    static int lm35SampleIndex = 0;
    int i = 0;

    lm35ReadingsArray[lm35SampleIndex] = lm35.read();   //Guarda muestras del valor de tensión y las guarda en el array lm35ReadingsArray
    lm35SampleIndex++;
    if ( lm35SampleIndex >= NUMBER_OF_AVG_SAMPLES) {
        lm35SampleIndex = 0;
    }
    
       lm35ReadingsSum = 0.0;
    for (i = 0; i < NUMBER_OF_AVG_SAMPLES; i++) {
        lm35ReadingsSum = lm35ReadingsSum + lm35ReadingsArray[i]; //Realiza la media móvil para disminuir error por ruido
    }
    lm35ReadingsAverage = lm35ReadingsSum / NUMBER_OF_AVG_SAMPLES;
    lm35TempC = analogReadingScaledWithTheLM35Formula ( lm35ReadingsAverage ); //Escala el valor medio y obtiene el valor de temperatura   
    
    if ( lm35TempC > OVER_TEMP_LEVEL ) {    //Verifica sobretemperatura
        overTempDetector = ON;
    } else {
        overTempDetector = OFF;
    }

    if( !mq2) {                 //Verifica estado del sensor de gas
        gasDetectorState = ON;
        alarmState = ON;
    }
    if( overTempDetector ) {
        overTempDetectorState = ON;
        alarmState = ON;
    }
    if( alarmTestButton ) {     //Verifica estado del Test Button        
        overTempDetectorState = ON;
        gasDetectorState = ON;
        alarmState = ON;
    }    
    if( alarmState ) {  //Si el estado de la alarma es "activo" entonces enciende la sirena
        accumulatedTimeAlarm = accumulatedTimeAlarm + TIME_INCREMENT_MS;
        sirenPin.output();                                     
        sirenPin = LOW;                                        
    
        if( gasDetectorState && overTempDetectorState ) {   //Controla el parpadeo del LED de alarma
            if( accumulatedTimeAlarm >= BLINKING_TIME_GAS_AND_OVER_TEMP_ALARM ) {
                accumulatedTimeAlarm = 0;
                alarmLed = !alarmLed;
            }
        } else if( gasDetectorState ) {
            if( accumulatedTimeAlarm >= BLINKING_TIME_GAS_ALARM ) {
                accumulatedTimeAlarm = 0;
                alarmLed = !alarmLed;
            }
        } else if ( overTempDetectorState ) {
            if( accumulatedTimeAlarm >= BLINKING_TIME_OVER_TEMP_ALARM  ) {
                accumulatedTimeAlarm = 0;
                alarmLed = !alarmLed;
            }
        }
    } else{
        alarmLed = OFF;                 //Si el sistema no detecta ninguna falla entonces los estados de alarma, gasDetector y overTemp serán apagados
        gasDetectorState = OFF;         //La sirena se apaga configurando el pin sirenPin como entrada ( sirenPin.input() )
        overTempDetectorState = OFF;
        sirenPin.input();                                  
    }
}

void alarmDeactivationUpdate()
{
    if ( numberOfIncorrectCodes < 5 ) {
        if ( aButton && bButton && cButton && dButton && !enterButton ) { //Si no se presionan los pulsadores no enciende el LED de código incorrecto
            incorrectCodeLed = OFF;
        }
        if ( enterButton && !incorrectCodeLed && alarmState ) { //Si presionan el botón "enterButton" entonces lee los valores de los otros pulsadores
            buttonsPressed[0] = aButton;                        //Si el código es correcto desactiva la alarma, sino incrementa el número de códigos incorrectos
            buttonsPressed[1] = bButton;                        //y enciende el LED de "código incorrecto"
            buttonsPressed[2] = cButton;
            buttonsPressed[3] = dButton;
            if ( areEqual() ) {
                alarmState = OFF;
                numberOfIncorrectCodes = 0;
            } else {
                incorrectCodeLed = ON;
                numberOfIncorrectCodes++;
            }
        }
    } else {
        systemBlockedLed = ON;  //Si el número de códigos incorrectos es 5 bloquea el sistema
    }
}

/*Para la comunicación serie con el USB se utiliza el objeto "uartUsb" de clase SerialBase. Posee dos constructores de nombre "SerialBase" protegido con sus métodos
y hereda la estructura serial_pinmap_t de "serial_api.h". Posee métodos privados y púbicos, donde en el método privado hereda la clase "NonCopyable" de "NonCopyable.h"
para deshabilitar la copia de objetos por fuera de la jerarquía del objeto, esto implica que solo él puede tener acceso al constructor. Como métodos públicos tenemos
baud(), format(), radable(), writeable(), attach(), set_break(), clear_break(), send_break(), eneable_input(), eneable_output().
A su vez, depende la configuración, podemos acceder a otros métodos cómo read() -Hay 2-, abort_read(), write() -Hay 2-, abort_write(), set_dma_usage_tx(), set_dma_usage_rx().
*/

/*printf() emplea UnbufferedSerial.cpp para poder enviar los datos via USB, para ello emplea la clase SerialBase y asocia a su stdout el bus serie.
UnbufferedSerial será una estructrua con el método "write()" que tendrá las mismas características que el método write() de SerialBase. Dentro de este método
se envía caracter por caracter cada uno de los caracteres empleando putc(). Éste último método emplea serial_putc() que 
verifica que el puerto serie esté disponible para imprimir y envía el caracter cargado. Sin embargo es bloqueante, dado que posee la línea 
"while (!serial_writable(obj));" que si no tiene el puerto disponible para escribir queda esperando hasta tenerlo. En cambio write() utiliza interrupciones.
*/

void uartTask()
{
    char receivedChar = '\0';
    char str[100];
    int stringLength;
    if( uartUsb.readable() ) {      //Si uartUsb tiene algo en el buffer entonces lee un caracter y selecciona la opción seleccionada
        uartUsb.read( &receivedChar, 1 ); //En cada caso imprime el mensaje correspondiente y/o el valor de la variable involucrada
        switch (receivedChar) {
        case '1':
            if ( alarmState ) {
                uartUsb.write( "The alarm is activated\r\n", 24);
            } else {
                uartUsb.write( "The alarm is not activated\r\n", 28);
            }
            break;

        case '2':
            if ( !mq2 ) {
                uartUsb.write( "Gas is being detected\r\n", 22);
            } else {
                uartUsb.write( "Gas is not being detected\r\n", 27);
            }
            break;

        case '3':
            if ( overTempDetector ) {
                uartUsb.write( "Temperature is above the maximum level\r\n", 40);
            } else {
                uartUsb.write( "Temperature is below the maximum level\r\n", 40);
            }
            break;
            
        case '4':                                                               //El caso 4 involucra la verificación del código de reset de alarma via comunicación
            uartUsb.write( "Please enter the code sequence.\r\n", 33 );         //serie, se deben ingresar los valores 1 o 0 correspondientes a los pulsadores a-b-c-d
            uartUsb.write( "First enter 'A', then 'B', then 'C', and ", 41 );   //en orden para que detecte el código correcto o incorrecto
            uartUsb.write( "finally 'D' button\r\n", 20 );
            uartUsb.write( "In each case type 1 for pressed or 0 for ", 41 );
            uartUsb.write( "not pressed\r\n", 13 );
            uartUsb.write( "For example, for 'A' = pressed, ", 32 );
            uartUsb.write( "'B' = pressed, 'C' = not pressed, ", 34);
            uartUsb.write( "'D' = not pressed, enter '1', then '1', ", 40 );
            uartUsb.write( "then '0', and finally '0'\r\n\r\n", 29 );

            incorrectCode = false;

            for ( buttonBeingCompared = 0;
                  buttonBeingCompared < NUMBER_OF_KEYS;
                  buttonBeingCompared++) {

                uartUsb.read( &receivedChar, 1 );
                uartUsb.write( "*", 1 );

                if ( receivedChar == '1' ) {
                    if ( codeSequence[buttonBeingCompared] != 1 ) {
                        incorrectCode = true;
                    }
                } else if ( receivedChar == '0' ) {
                    if ( codeSequence[buttonBeingCompared] != 0 ) {
                        incorrectCode = true;
                    }
                } else {
                    incorrectCode = true;
                }
            }

            if ( incorrectCode == false ) {
                uartUsb.write( "\r\nThe code is correct\r\n\r\n", 25 );
                alarmState = OFF;
                incorrectCodeLed = OFF;
                numberOfIncorrectCodes = 0;
            } else {
                uartUsb.write( "\r\nThe code is incorrect\r\n\r\n", 27 );
                incorrectCodeLed = ON;
                numberOfIncorrectCodes++;
            }                
            break;

        case '5':                                                                   //Esta opción permite cambiar el código de reset solicitando los datos de forma
            uartUsb.write( "Please enter new code sequence\r\n", 32 );              //serie de una manera similar al caso 4
            uartUsb.write( "First enter 'A', then 'B', then 'C', and ", 41 );
            uartUsb.write( "finally 'D' button\r\n", 20 );
            uartUsb.write( "In each case type 1 for pressed or 0 for not ", 45 );
            uartUsb.write( "pressed\r\n", 9 );
            uartUsb.write( "For example, for 'A' = pressed, 'B' = pressed,", 46 );
            uartUsb.write( " 'C' = not pressed,", 19 );
            uartUsb.write( "'D' = not pressed, enter '1', then '1', ", 40 );
            uartUsb.write( "then '0', and finally '0'\r\n\r\n", 29 );

            for ( buttonBeingCompared = 0; 
                  buttonBeingCompared < NUMBER_OF_KEYS; 
                  buttonBeingCompared++) {

                uartUsb.read( &receivedChar, 1 );
                uartUsb.write( "*", 1 );

                if ( receivedChar == '1' ) {
                    codeSequence[buttonBeingCompared] = 1;
                } else if ( receivedChar == '0' ) {
                    codeSequence[buttonBeingCompared] = 0;
                }
            }

            uartUsb.write( "\r\nNew code generated\r\n\r\n", 24 );
            break;
 
        case 'p':
        case 'P':
            potentiometerReading = potentiometer.read();
            sprintf ( str, "Potentiometer: %.2f\r\n", potentiometerReading );
            stringLength = strlen(str);
            uartUsb.write( str, stringLength );
            break;

        case 'c':
        case 'C':
            sprintf ( str, "Temperature: %.2f \xB0 C\r\n", lm35TempC );
            stringLength = strlen(str);
            uartUsb.write( str, stringLength );
            break;

        case 'f':
        case 'F':
            sprintf ( str, "Temperature: %.2f \xB0 F\r\n", 
                celsiusToFahrenheit( lm35TempC ) );
            stringLength = strlen(str);
            uartUsb.write( str, stringLength );
            break;

        default:
            availableCommands();
            break;

        }
    }
}

void availableCommands()
{
    uartUsb.write( "Available commands:\r\n", 21 );
    uartUsb.write( "Press '1' to get the alarm state\r\n", 34 );
    uartUsb.write( "Press '2' to get the gas detector state\r\n", 41 );
    uartUsb.write( "Press '3' to get the over temperature detector state\r\n", 54 );
    uartUsb.write( "Press '4' to enter the code sequence\r\n", 38 );
    uartUsb.write( "Press '5' to enter a new code\r\n", 31 );
    uartUsb.write( "Press 'P' or 'p' to get potentiometer reading\r\n", 47 );
    uartUsb.write( "Press 'f' or 'F' to get lm35 reading in Fahrenheit\r\n", 52 );
    uartUsb.write( "Press 'c' or 'C' to get lm35 reading in Celsius\r\n\r\n", 51 );
}

bool areEqual() //compara la clave cargada y la configurada internamente
{
    int i;

    for (i = 0; i < NUMBER_OF_KEYS; i++) {
        if (codeSequence[i] != buttonsPressed[i]) {
            return false;
        }
    }

    return true;
}

float analogReadingScaledWithTheLM35Formula( float analogReading )
{
    return ( analogReading * 3.3 / 0.01 );
}

float celsiusToFahrenheit( float tempInCelsiusDegrees )
{
    return ( tempInCelsiusDegrees * 9.0 / 5.0 + 32.0 );
}

float celsiusToFahrenheit( float tempInCelsiusDegrees )
{
    return ( tempInCelsiusDegrees * 9.0 / 5.0 + 32.0 );
}
