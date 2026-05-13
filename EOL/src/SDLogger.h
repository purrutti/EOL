#pragma once
#include <Arduino.h>
#include "CTD.h"
#include "PiSAMI_decode.h"

// Broche CS de la carte SD (SPI par défaut : CLK=18 MISO=19 MOSI=23)
#ifndef SD_CS_PIN
#  define SD_CS_PIN 5
#endif

// Initialise la carte SD et crée le répertoire /data si absent.
// Retourne false si la carte est introuvable.
bool sdLoggerBegin(uint8_t csPin = SD_CS_PIN);

// Écrit une ligne dans /data/YYYYMMDD_EOL_CTD.csv
// Nécessite que data.date soit renseigné (parseDatetime a réussi).
void sdLogCTD(const CTDData& data);

// Écrit une ligne dans /data/YYYYMMDD_EOL_SAMI.csv
// Utilise rec.unixTimestamp pour nommer le fichier et horodater la ligne.
void sdLogSAMI(const PiSAMI_Record& rec);
