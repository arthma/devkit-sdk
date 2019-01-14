#ifndef _IOT_DEVKIT_HW_H_
#define _IOT_DEVKIT_HW_H_

#ifdef __cplusplus
extern "C"
{
#endif

    const char *getIoTHubConnectionString(void);

    int initIoTDevKit(void);
    const char *getDevKitName(void);
    const char *getDevKitSerialNumber(void);

    float getDevKitHumidityValue(void);
    float getDevKitTemperatureValue(int isFahrenheit);
    float getDevKitPressureValue(void);
    void getDevKitMagnetometerValue(int *x, int *y, int *z);
    void getDevKitGyroscopeValue(int *x, int *y, int *z);
    void getDevKitAcceleratorValue(int *x, int *y, int *z);

    void blinkDevKitLED(int ms);

    void invokeDevKitSensors(void);

#ifdef __cplusplus
}
#endif // _IOT_DEVKIT_HW_H_

#endif
