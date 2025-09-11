En -> /usr/local/bin/ debe encontrarse:

1) Ejecutable (Realtime_detection_http)
2) Modelo     (yolov.....cvi)
3) En esta carpeta se creará images_bak/ donde se almacenarán
las imágenes en caso de no contarse con SD

Originalmente las imagenes se guardan en /mnt/sd/images 

Los siguientes archivos deben ubicarse en estas rutas en la camera:

config.ini                    -> /usr/local/bin/
realtime-detection-wrapper.sh -> /usr/local/bin/
wifi-manager.sh               -> /usr/local/bin/
S99realtime-detection         -> /etc/init.d/
S98network-time-setup         -> /etc/init.d  
