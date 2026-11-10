# Control GPS y ajuste de estación total (POSGAR 07 / Gauss-Krüger)

Script en Python (sin dependencias externas) para:

1. **Controlar los puntos GPS**: verifica que estén en la faja, calcula lat/lon y
   factores de escala, detecta puntos medidos dos veces que no coinciden, revisa
   la precisión y el tipo de solución, y compara contra las coordenadas de partida
   del comitente. La comparación se hace por diferencias absolutas, por distancias
   entre puntos y con una transformación de semejanza, para separar un **corrimiento
   sistemático** (problema de la base o del marco) de un **punto mal medido**.
2. **Diagnosticar el cierre de la estación total (ET)** contra el punto GPS de
   llegada: lo descompone en error longitudinal y transversal, calcula cuánto
   explica el **factor de escala combinado** (Gauss-Krüger × altura) y cuánto la
   **orientación inicial** sobre un punto GPS cercano, y lo compara con el error
   esperable de la poligonal.
3. **Ajustar todos los puntos de la ET** (estaciones y targets) a los puntos GPS de
   inicio y cierre:
   - `helmert`: transformación de semejanza 2D (giro + escala, con el inicio fijo).
     Corrige los dos errores sistemáticos típicos: la escala y la orientación.
   - `bowditch`: reparte el error proporcionalmente a la distancia recorrida
     (regla de la brújula). Necesita el orden de las estaciones.
   - Las cotas se compensan linealmente según el avance sobre la poligonal.
4. **Verificar** el ajuste con puntos medidos con la ET que también tienen
   coordenada GPS o de partida y no se usaron en el cálculo.

## Datos de entrada

Archivos CSV o TXT con separador `,` `;` tab o espacios. Con `;` se acepta la coma
decimal. Si no hay cabecera, el orden de columnas se indica con `--formato`
(`PENZ` por defecto). Si las columnas X/Y vienen con la convención GK (X = Norte),
se detecta y se corrige automáticamente.

| Archivo | Columnas | Obligatorio |
|---|---|---|
| `--gps` | Punto, Este, Norte, H [, HRMS, VRMS, Solución] | sí |
| `--control` | Punto, Este, Norte, H (coordenadas de partida) | no |
| `--et` | Punto, Este, Norte, H [, Estacion, Codigo] | para ajustar |

- La columna `Estacion` de la ET (desde qué estación se midió cada target) es
  opcional. Mejora el reparto de cotas y de Bowditch para los puntos radiados.
- Si un punto tiene distinto nombre en la ET y en el GPS, se indica `ET=GPS`,
  por ejemplo `--cierre T45=PF9`.

## Uso

```bash
python ajuste_et_gps.py \
    --gps gps.csv --control control.csv --et et.csv \
    --inicio PF1 --orientacion PF2 --cierre PF9 \
    --poligonal PF1,E1,E2,E3,E4,E5,E6,E7,PF9 \
    --faja 2 --salida resultados
```

Salida en la carpeta `resultados/`:

- `informe.txt`: el informe completo (también se imprime en pantalla).
- `et_ajustada_helmert.csv` y `et_ajustada_bowditch.csv`: coordenadas ajustadas,
  originales de la ET y corrección aplicada a cada punto. Con `--excel` se escriben
  con `;` y coma decimal.

Otras opciones: `--metodo helmert|bowditch|ambos`, `--usar-todos` (incluye en
Helmert todos los puntos comunes ET-GPS, con residuos), `--tol-h`, `--tol-v`,
`--sigma-gps`, `--sigma-ang`, `--sigma-dist-mm`, `--sigma-dist-ppm`, `--ondulacion`.

## Ejemplo

`ejemplo_sintetico/` contiene datos **inventados** (no son mediciones reales).
Simulan una poligonal de 1,6 km en la zona de Plaza Huincul, medida con distancias
de terreno, orientada a un punto GPS a 112 m y con errores aleatorios. Uno de los
puntos de partida tiene un error de 7 cm a propósito.

```bash
cd ejemplo_sintetico
python ../ajuste_et_gps.py --gps gps.csv --control control.csv --et et.csv \
    --inicio PF1 --orientacion PF2 --cierre PF9 \
    --poligonal PF1,E1,E2,E3,E4,E5,E6,E7,PF9
```

En ese caso la ET llega 26 cm desplazada. Después del ajuste, los targets quedan a
1,7 cm RMS de su posición verdadera con Helmert y a 2,0 cm con Bowditch; antes del
ajuste estaban a 13,5 cm.

## Notas técnicas

- **Factor de escala.** En la zona de Plaza Huincul (unos 17 km al oeste del
  meridiano central de la faja 2 y a unos 600 m de altura), el factor combinado es
  de aproximadamente −90 ppm, es decir unos 9 cm por kilómetro. Si la ET trabajó con
  factor 1 (distancias de terreno), las coordenadas de la ET quedan "estiradas" en
  esa proporción respecto de la cuadrícula GK. Helmert lo detecta y lo corrige: la
  escala que surge del ajuste debería ser parecida al factor teórico.
- **Orientación sobre GPS.** Con 1,5 cm de error por punto, orientar sobre un punto
  GPS a 100 m da una incertidumbre de unos ±40″. A 1,5 km de distancia, eso son
  ±0,3 m transversales. Suele ser la mayor causa del error de cierre, y es un giro
  puro alrededor del punto de arranque: también lo corrige Helmert.
- **Con solo 2 puntos** (inicio y cierre) el ajuste es exacto y no tiene redundancia.
  La calidad se evalúa por la coherencia de la escala y el giro con lo esperable,
  y por puntos de verificación independientes. Conviene que la ET mida algún otro
  punto GPS intermedio.
- Las fórmulas de proyección (GRS80, EPSG 5343-5349) se validaron contra PROJ con
  diferencias menores a 1 mm. Se usan solo para los factores de escala; el ajuste
  trabaja en coordenadas planas.
