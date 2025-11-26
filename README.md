# Smarthome IOT

Ini adalah repository iot smarthome.   
smarthome ini menggunakan PIR untuk mendeteksi jumlah orang, bila ada orang yang masuk maka sitem akan menyala bila tidak orang maka sistem mati otomatis. sistem menggunakan kipas dan lampu sebagai outputnya. Selain itu smarthome juga dapat dikontrol dan dimonitoring dari jarak jauh. menggunakan node red dan mqtt sebagai perantara.    

## Flowchart

![ini Flowchart Smarthom IoT](./src/FlowChart.jpg "FlowChart Smarthom IoT")


## Rangkaian

![ini Flowchart Smarthom IoT](./src/Rangkaian.jpg "FlowChart Smarthom IoT")

## Prerequisite
__Komponen Yang digunakan:__
- DHT11
- sensor PIR 2
- Motor Servo 180° (LF20MG) 
- lampu
- Kipas PC 12V
- Motor Driver l298n
- step down LM2596
- Power Supply 12VDC 5A
- ESP32

__Software yang digunakan:__
- node-red
- Arduino Ide

__Library external Arduino IDE yang digunakan:__
 - __MQTT__ by Joel Gaehwiler
 - __Adafruit Unified__ Sensor by Adafruit
 - __ESP32Servo__ by Kevin Harrington


## Instalisasi


 
 

## Usage
- buka node red dengan menjalankan comand berikut, pada Direktori utama:
  ```
  npx node-red -u .\node-red
  ```
- lalu buka website local host dengan port 1880 (default):
