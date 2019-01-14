#include "Arduino.h"
#include "AzureIotHub.h"
#include "AZ3166WiFi.h"
#include "Sensor.h"
#include "SystemVersion.h"
#include "SystemTickCounter.h"
#include "EEPROMInterface.h"

#include "IoT_DevKit_HW.h"

static RGB_LED rgbLed;

static DevI2C *ext_i2c;
static LSM6DSLSensor *acc_gyro;
static HTS221Sensor *ht_sensor;
static LIS2MDLSensor *magnetometer;
static IRDASensor *IrdaSensor;
static LPS22HBSensor *pressureSensor;

static char connString[AZ_IOT_HUB_MAX_LEN + 1] = {'\0'};
static const char *boardName = NULL;
static const char *boardInfo = NULL;

static volatile uint64_t blinkTimeStart = 0;
static volatile int64_t blinkTime = -1;
static int ledStat = 0;
static int ledColor = 0;

static struct _tagRGB
{
    int red;
    int green;
    int blue;
} _rgb[] =
    {
        {255, 0, 0},
        {0, 255, 0},
        {0, 0, 255},
};

static bool initWiFi(void)
{
    if (WiFi.begin() == WL_CONNECTED)
    {
        return true;
    }
    else
    {
        LogError("Failed to initialize Wi-Fi.");
        return false;
    }
}

int initIoTDevKit(void)
{
    // Init the screen
    Screen.init();
    Screen.print(0, "IoT DevKit");
    Screen.print(2, "Initializing...");

    // Serial
    Serial.begin(115200);

    // Init pins
    pinMode(LED_WIFI, OUTPUT);
    pinMode(LED_AZURE, OUTPUT);
    pinMode(LED_USER, OUTPUT);

    // Turn off the RGB Led
    rgbLed.turnOff();

    // Init I2C bus
    if ((ext_i2c = new DevI2C(D14, D15)) == NULL)
    {
        LogError("Failed to initialize I2C.");
        return false;
    }

    // Init the gyroscope and accelerator sensor
    if ((acc_gyro = new LSM6DSLSensor(*ext_i2c, D4, D5)) == NULL)
    {
        LogError("Failed to initialize gyroscope and accelerator sensor.");
        return false;
    }
    acc_gyro->init(NULL);
    acc_gyro->enableAccelerator();
    acc_gyro->enableGyroscope();

    // Init the humidity and temperature sensor
    if ((ht_sensor = new HTS221Sensor(*ext_i2c)) == NULL)
    {
        LogError("Failed to initialize humidity and temperature sensor.");
        return false;
    }
    ht_sensor->init(NULL);

    // Init the magnetometer sensor
    if ((magnetometer = new LIS2MDLSensor(*ext_i2c)) == NULL)
    {
        LogError("Failed to initialize magnetometer sensor.");
        return false;
    }
    magnetometer->init(NULL);

    // Init Irda
    if ((IrdaSensor = new IRDASensor()) == NULL)
    {
        LogError("Failed to initialize IrDa sensor.");
        return false;
    }
    IrdaSensor->init();

    // Init pressure sensor
    if ((pressureSensor = new LPS22HBSensor(*ext_i2c)) == NULL)
    {
        LogError("Failed to initialize pressure sensor.");
        return false;
    }
    pressureSensor->init(NULL);

    // Init WiFi
    return initWiFi();
}

const char *getIoTHubConnectionString(void)
{
    if (connString[0] == '\0')
    {
        EEPROMInterface eeprom;
        eeprom.read((uint8_t *)connString, AZ_IOT_HUB_MAX_LEN, 0, AZ_IOT_HUB_ZONE_IDX);
    }
    return connString;
}

const char *getDevKitName(void)
{
    if (boardName == NULL)
    {
        int len = snprintf(NULL, 0, "MXChip IoT DevKit %s", GetBoardID()) + 1;
        boardName = (const char *)malloc(len);
        if (boardName == NULL)
        {
            LogError("No memory");
            return NULL;
        }
        snprintf((char *)boardName, len, "MXChip IoT DevKit %s", GetBoardID()) + 1;
    }
    return boardName;
}

const char *getDevKitSerialNumber(void)
{
    return GetBoardID();
}

float getDevKitHumidityValue(void)
{
    float humidity = 0;
    ht_sensor->getHumidity(&humidity);
    LogInfo(">>Humidity %f", humidity);
    return humidity;
}

float getDevKitTemperatureValue(int isFahrenheit)
{
    float temperature = 0;
    ht_sensor->getTemperature(&temperature);
    if (isFahrenheit)
    {
        //convert from C to F
        temperature = temperature * 1.8 + 32;
    }
    LogInfo(">>Temperature %f", temperature);
    return temperature;
}

float getDevKitPressureValue(void)
{
    float pressure = 0;
    pressureSensor->getTemperature(&pressure);
    LogInfo(">>Pressure %f", pressure);
    return pressure;
}

void getDevKitMagnetometerValue(int *x, int *y, int *z)
{
    int axes[3];
    magnetometer->getMAxes(axes);
    *x = axes[0];
    *y = axes[1];
    *z = axes[2];
    LogInfo(">>Magnetometer %d, %d, %d", *x, *y, *z);
}

void getDevKitGyroscopeValue(int *x, int *y, int *z)
{
    int axes[3];
    acc_gyro->getGAxes(axes);
    *x = axes[0];
    *y = axes[1];
    *z = axes[2];
    LogInfo(">>Gyroscope %d, %d, %d", *x, *y, *z);
}

void getDevKitAcceleratorValue(int *x, int *y, int *z)
{
    int axes[3];
    acc_gyro->getXAxes(axes);
    *x = axes[0];
    *y = axes[1];
    *z = axes[2];
    LogInfo(">>Accelerator %d, %d, %d", *x, *y, *z);
}

void blinkDevKitLED(int ms)
{
    blinkTimeStart = SystemTickCounterRead();
    blinkTime = ms;
    ledStat = 0;
    ledColor = 0;
    digitalWrite(LED_USER, ledStat);
}

void invokeDevKitSensors(void)
{
    if (blinkTime < 0)
    {
        return;
    }

    uint64_t ms = SystemTickCounterRead() - blinkTimeStart;
    if (ms >= 500)
    {
        ledStat = !ledStat;
        digitalWrite(LED_USER, ledStat);

        rgbLed.setColor(_rgb[ledColor].red, _rgb[ledColor].green, _rgb[ledColor].blue);
        ledColor = (ledColor + 1) % (sizeof(_rgb) / sizeof(struct _tagRGB));

        blinkTime -= ms;
        blinkTimeStart = SystemTickCounterRead();
        if (blinkTime == 0)
        {
            // Disable
            blinkTime = -1;
        }
        if (blinkTime < 0)
        {
            digitalWrite(LED_USER, 0);
            rgbLed.turnOff();
        }
    }
}