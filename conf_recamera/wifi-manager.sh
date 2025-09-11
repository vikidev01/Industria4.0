#!/bin/sh

# Configuración NUEVA para modo HTTP (NO toca tu configuración existente)
HTTP_WIFI_SSID="ReCamNet"
HTTP_WIFI_PASSWORD="12345678"
HTTP_WIFI_CONFIG="/etc/wpa_supplicant_http.conf"  # ARCHIVO NUEVO

LOG_FILE="/var/log/wifi-manager.log"
WIFI_INTERFACE="wlan0"

# Crear configuración NUEVA para HTTP (NO modifica archivos existentes)
create_http_wifi_config() {
    echo "$(date) - Creating NEW WiFi config for HTTP mode" >> $LOG_FILE
    cat > "$HTTP_WIFI_CONFIG" << EOF
ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
update_config=1
country=US

network={
    ssid="$HTTP_WIFI_SSID"
    psk="$HTTP_WIFI_PASSWORD"
    key_mgmt=WPA-PSK
}
EOF
    echo "$(date) - New config created: $HTTP_WIFI_CONFIG" >> $LOG_FILE
}

# Función SEGURA para detener procesos (solo si existen)
stop_network_services() {
    echo "$(date) - Stopping network services..." >> $LOG_FILE
    
    # Verificar y detener wpa_supplicant si está corriendo
    if ps | grep -q "[w]pa_supplicant"; then
        echo "$(date) - Stopping wpa_supplicant" >> $LOG_FILE
        killall wpa_supplicant 2>/dev/null
        sleep 2
    fi
    
    # Verificar y detener udhcpc si está corriendo
    if ps | grep -q "[u]dhcpc"; then
        echo "$(date) - Stopping udhcpc" >> $LOG_FILE
        killall udhcpc 2>/dev/null
        sleep 2
    fi
}

# Función para cambiar la red WiFi (MODO SEGURO)
switch_wifi_network() {
    MODE=$1
    
    echo "$(date) - Switching WiFi to mode: $MODE" >> $LOG_FILE
    
    if [ "$MODE" = "http" ]; then
        # Crear configuración NUEVA para HTTP
        if [ ! -f "$HTTP_WIFI_CONFIG" ]; then
            create_http_wifi_config
        fi
        CONFIG_FILE="$HTTP_WIFI_CONFIG"
        echo "$(date) - Using NEW HTTP config" >> $LOG_FILE
    else
        # Modo MQTT - usar tu configuración ACTUAL (NO la modifica)
        CONFIG_FILE="/etc/wpa_supplicant.conf"
        echo "$(date) - Using EXISTING MQTT config" >> $LOG_FILE
    fi
    
    # Detener servicios de manera segura
    stop_network_services
    
    # Iniciar con la configuración elegida
    echo "$(date) - Starting WiFi with config: $CONFIG_FILE" >> $LOG_FILE
    wpa_supplicant -B -i $WIFI_INTERFACE -c "$CONFIG_FILE" >> $LOG_FILE 2>&1
    sleep 3
    
    # Obtener IP
    echo "$(date) - Getting IP address..." >> $LOG_FILE
    udhcpc -i $WIFI_INTERFACE -n >> $LOG_FILE 2>&1
    sleep 2
    
    # Verificar conexión
    if ifconfig $WIFI_INTERFACE | grep -q "inet addr"; then
        IP=$(ifconfig $WIFI_INTERFACE | grep "inet addr" | cut -d: -f2 | awk '{print $1}')
        echo "$(date) - WiFi connected successfully! IP: $IP" >> $LOG_FILE
        return 0
    else
        echo "$(date) - WiFi connection failed" >> $LOG_FILE
        return 1
    fi
}

# Función principal
case "$1" in
    switch-to)
        if [ $# -ge 2 ] && [ "$2" = "http" -o "$2" = "mqtt" ]; then
            switch_wifi_network "$2"
            exit $?
        else
            echo "Usage: $0 switch-to {http|mqtt}"
            exit 1
        fi
        ;;
    status)
        echo "Current WiFi status:"
        if ifconfig $WIFI_INTERFACE | grep -q "inet addr"; then
            IP=$(ifconfig $WIFI_INTERFACE | grep "inet addr" | cut -d: -f2 | awk '{print $1}')
            echo "Connected: Yes"
            echo "IP Address: $IP"
        else
            echo "Connected: No"
        fi
        ;;
    test-http)
        echo "Testing HTTP WiFi connection..."
        switch_wifi_network "http"
        if [ $? -eq 0 ]; then
            echo "SUCCESS: Connected to ReCamNet"
        else
            echo "FAILED: Could not connect to ReCamNet"
        fi
        ;;
    *)
        echo "Usage: $0 {switch-to|status|test-http} [http|mqtt]"
        echo "  switch-to {http|mqtt} - Change WiFi network safely"
        echo "  status                - Show current WiFi status"
        echo "  test-http             - Test HTTP WiFi connection"
        exit 1
        ;;
esac

exit 0
