# Smarthome IOT

Ini adalah repository iot smarthome.   
smarthome ini menggunakan PIR untuk mendeteksi jumlah orang, bila ada orang yang masuk maka sitem akan menyala bila tidak orang maka sistem mati otomatis. sistem menggunakan kipas dan lampu sebagai outputnya. Selain itu smarthome juga dapat dikontrol dan dimonitoring dari jarak jauh. menggunakan node red dan mqtt sebagai perantara.    

## Flowchart

![ini Flowchart Smarthom IoT](./src/FlowChart.jpg "FlowChart Smarthom IoT")


## Rangkaian

![ini Flowchart Smarthom IoT](./src/Rangkaian.jpg "FlowChart Smarthom IoT")


## Instalisasi
Pemograman dilakukan menggunakan Arduino Ide   
__Library external yang digunakan :__   
 - __MQTT__ by Joel Gaehwiler
 - __Adafruit Unified__ Sensor by Adafruit
 - __ESP32Servo__ by Kevin Harrington
 
 

## Usage
- buka node red dengan menjalankan comand berikut, pada Direktori utama:
  ```
  npx node-red -u .\node-red
  ```
- lalu buka website local host dengan port 1880 (default):
